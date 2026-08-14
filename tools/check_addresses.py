#!/usr/bin/env python3
"""Check whether the addresses this mod relies on are the same in two exe builds.

Plugin SDK bakes San Andreas addresses in for 1.0 US. This asks the narrower,
answerable question: at each address the SA build actually depends on, does the
other exe hold the same code? Matching code at the same VA means the address
carries over; differing code means it does not, and no amount of version-check
loosening will make the mod safe there.

Only code addresses are conclusive. Globals live in .data, which is mostly
uninitialised on disk, so those are reported separately as "not comparable".

Usage:
    python tools/check_addresses.py <reference-exe> <candidate-exe>
"""

import struct
import sys

# Everything the San Andreas build touches, gathered from source/Main.cpp and
# resolved against plugin-sdk's plugin_sa/game_sa/*.cpp and shared/Events.h.
TARGETS = [
    # (address, label, kind)  kind: "hook" = we patch a call here, "call" = we
    # call into it, "data" = we read/write a global.
    (0x53E4FF, "Events::drawHudEvent hook site", "hook"),
    (0x53E981, "Events::gameProcessEvent hook site", "hook"),
    (0x748CFB, "Events::initGameEvent hook site", "hook"),

    (0x464D50, "CTheScripts::IsPlayerOnAMission", "call"),
    (0x5D13E0, "CGenericGameStorage::GenericSave", "call"),
    (0x5D1380, "CGenericGameStorage::CheckSlotDataValid", "call"),
    (0x5D0E90, "CGenericGameStorage::MakeValidSaveName", "call"),
    (0x558E40, "CStats::GetStatValue", "call"),
    (0x69F0B0, "CMessages::AddMessage", "call"),
    (0x71A700, "CFont::PrintString", "call"),
    (0x719380, "CFont::SetScale", "call"),
    (0x719490, "CFont::SetFontStyle", "call"),
    (0x719610, "CFont::SetOrientation", "call"),

    (0xC1A970, "CMessages::BIGMessages", "data"),
    (0xB7CD98, "CWorld::Players", "data"),
    (0xB5F851, "CCutsceneMgr::ms_running", "data"),
    (0xBA86F0, "CRadar::ms_RadarTrace", "data"),
    (0xB6F028, "TheCamera", "data"),
    (0xB7CB84, "CTimer::m_snTimeInMilliseconds", "data"),
    (0xB70152, "CClock::ms_nGameClockMinutes", "data"),
    (0xB70153, "CClock::ms_nGameClockHours", "data"),
]

WINDOW = 32   # bytes of context compared at each address


class PE:
    def __init__(self, data):
        self.data = data
        pe_off = struct.unpack_from("<I", data, 0x3C)[0]
        num = struct.unpack_from("<H", data, pe_off + 6)[0]
        opt_size = struct.unpack_from("<H", data, pe_off + 20)[0]
        opt_off = pe_off + 24
        self.base = struct.unpack_from("<I", data, opt_off + 28)[0]
        self.sections = []
        sec_off = opt_off + opt_size
        for i in range(num):
            b = sec_off + i * 40
            name = data[b:b + 8].rstrip(b"\0").decode("latin-1")
            vsize, vaddr, rsize, rptr = struct.unpack_from("<IIII", data, b + 8)
            self.sections.append((name, vaddr, vsize, rptr, rsize))

    def at(self, va, n):
        rva = va - self.base
        for name, vaddr, vsize, rptr, rsize in self.sections:
            if vaddr <= rva < vaddr + max(vsize, rsize):
                off = rptr + (rva - vaddr)
                if off + n > rptr + rsize or off + n > len(self.data):
                    return name, None      # virtual-only tail: no bytes on disk
                return name, self.data[off:off + n]
        return None, None


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2

    with open(argv[1], "rb") as f:
        ref = PE(f.read())
    with open(argv[2], "rb") as f:
        cand = PE(f.read())

    print(f"reference {argv[1]}")
    print(f"candidate {argv[2]}\n")

    verdicts = {"same": 0, "differs": 0, "n/a": 0}
    for va, label, kind in TARGETS:
        sec_r, br = ref.at(va, WINDOW)
        sec_c, bc = cand.at(va, WINDOW)
        if br is None or bc is None:
            state, note = "n/a", f"not initialised on disk ({sec_r or '?'})"
        elif br == bc:
            state, note = "same", f"{sec_r}  {br[:8].hex(' ')}"
        else:
            state, note = "differs", (f"{sec_r}\n{'':>14}ref  {br[:16].hex(' ')}"
                                      f"\n{'':>14}cand {bc[:16].hex(' ')}")
        verdicts[state] += 1
        print(f"  {state:<8} {va:#08x} {kind:<5} {label}\n{'':>14}{note}")

    print(f"\n  same {verdicts['same']}   differs {verdicts['differs']}   "
          f"not comparable {verdicts['n/a']}")
    if verdicts["differs"]:
        print("\n  At least one address the mod depends on holds different code in\n"
              "  the candidate build. Loosening the version check would make the mod\n"
              "  call into the wrong function.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
