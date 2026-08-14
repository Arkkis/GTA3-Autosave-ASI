#!/usr/bin/env python3
"""Identify a GTA exe the same way the Plugin SDK does.

plugin-sdk's detect_game_id() (shared/GameVersion.cpp) fingerprints the running
game by reading a handful of virtual addresses and comparing them against known
constants. If none match it returns GAME_UNKNOWN and the mod refuses to install
its hooks. This script performs exactly those reads against an exe on disk, so a
user can be told which build they have without launching anything.

Usage:
    python tools/identify_exe.py <path-to-exe> [...]
"""

import hashlib
import struct
import sys

IMAGE_BASE_FALLBACK = 0x400000

# (virtual address, width in bytes, expected value, version name) per game.
# Mirrors detect_game_id() one-for-one; order matters for the same reason it
# does there -- the first match wins.
SIGNATURES = {
    "San Andreas": [
        (0x401000, 4, 0x53EC8B55, "GTA SA 1.0 US 'Compact'"),
        (0x401000, 4, 0x16197BE9, "GTA SA 1.0 US 'HoodLum'"),
        (0x8245BC, 2, 0x94BF, "GTA SA 1.0 EU"),
        (0x8252FC, 2, 0x94BF, "GTA SA 1.01 US"),
        (0x82533C, 2, 0x94BF, "GTA SA 1.01 EU"),
        (0x858D51, 4, 0x3539F633, "GTA SA 'NewSteam R2'"),
        (0x858C61, 4, 0x3539F633, "GTA SA 'NewSteam R2-LV'"),
    ],
    "Vice City": [
        (0x667BF0, 4, 0x53E58955, "GTA VC 1.0 EN"),
        (0x667C40, 4, 0x53E58955, "GTA VC 1.1 EN"),
        (0x666BA0, 4, 0x53E58955, "GTA VC Steam"),
    ],
    "GTA III": [
        (0x5C1E70, 4, 0x53E58955, "GTA 3 1.0 EN"),
        (0x5C2130, 4, 0x53E58955, "GTA 3 1.1 EN"),
        (0x5C6FD0, 4, 0x53E58955, "GTA 3 Steam"),
    ],
}

# Versions this mod actually supports, keyed by the names above.
SUPPORTED = {
    "GTA SA 1.0 US 'Compact'",
    "GTA SA 1.0 US 'HoodLum'",
    "GTA VC 1.0 EN",
    "GTA 3 1.0 EN",
}


class PE:
    """Just enough PE parsing to translate a virtual address to a file offset."""

    def __init__(self, data):
        self.data = data
        if data[:2] != b"MZ":
            raise ValueError("not a DOS/PE image (no MZ header)")
        pe_off = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe_off:pe_off + 4] != b"PE\0\0":
            raise ValueError("not a PE image (no PE signature)")
        num_sections = struct.unpack_from("<H", data, pe_off + 6)[0]
        opt_size = struct.unpack_from("<H", data, pe_off + 20)[0]
        opt_off = pe_off + 24
        magic = struct.unpack_from("<H", data, opt_off)[0]
        if magic != 0x10B:
            raise ValueError("not a 32-bit PE (PE32+ images are not GTA exes)")
        self.image_base = struct.unpack_from("<I", data, opt_off + 28)[0]
        self.sections = []
        sec_off = opt_off + opt_size
        for i in range(num_sections):
            base = sec_off + i * 40
            name = data[base:base + 8].rstrip(b"\0").decode("latin-1")
            virt_size, virt_addr, raw_size, raw_ptr = struct.unpack_from(
                "<IIII", data, base + 8)
            self.sections.append((name, virt_addr, virt_size, raw_ptr, raw_size))

    def read(self, va, width):
        """Read `width` bytes at virtual address `va`, or None if unmapped."""
        rva = va - self.image_base
        for _name, virt_addr, virt_size, raw_ptr, raw_size in self.sections:
            if virt_addr <= rva < virt_addr + max(virt_size, raw_size):
                offset = raw_ptr + (rva - virt_addr)
                # Tail of a section can be virtual-only (uninitialised data).
                if offset + width > raw_ptr + raw_size:
                    return None
                if offset + width > len(self.data):
                    return None
                chunk = self.data[offset:offset + width]
                return int.from_bytes(chunk, "little")
        return None


def identify(path):
    with open(path, "rb") as f:
        data = f.read()

    print(f"{path}")
    print(f"  size    {len(data):,} bytes")
    print(f"  md5     {hashlib.md5(data).hexdigest()}")
    print(f"  sha1    {hashlib.sha1(data).hexdigest()}")

    try:
        pe = PE(data)
    except ValueError as e:
        print(f"  ERROR   {e}")
        return
    if pe.image_base != IMAGE_BASE_FALLBACK:
        print(f"  note    image base is {pe.image_base:#x}, not the usual "
              f"{IMAGE_BASE_FALLBACK:#x} -- the exe has been rebased, so the "
              f"SDK's absolute addresses will not line up in-game either")

    match = None
    for game, sigs in SIGNATURES.items():
        for va, width, expected, name in sigs:
            actual = pe.read(va, width)
            if actual is not None and actual == expected:
                match = (game, name)
                break
        if match:
            break

    if match:
        game, name = match
        verdict = "supported" if name in SUPPORTED else "NOT SUPPORTED by this mod"
        print(f"  version {name}  ({game}) -- {verdict}")
    else:
        print("  version UNKNOWN -- matches no build plugin-sdk knows about, so "
              "the mod will refuse to load even if it is nominally 1.0")

    print("  probe results (what detect_game_id() would see):")
    for game, sigs in SIGNATURES.items():
        for va, width, expected, name in sigs:
            actual = pe.read(va, width)
            shown = "unmapped" if actual is None else f"{actual:#0{width * 2 + 2}x}"
            hit = "MATCH" if actual == expected else "     "
            print(f"    {hit} [{va:#08x}] want {expected:#0{width * 2 + 2}x}  "
                  f"got {shown:>12}  -> {name}")


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    for path in argv[1:]:
        identify(path)
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
