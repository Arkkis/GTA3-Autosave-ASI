#!/usr/bin/env python3
"""Compare two GTA exe builds and report how their virtual addresses relate.

Answers one question: can addresses for build A be mechanically translated into
build B (a constant shift, or a small number of shifted regions), or are the two
builds unrelated enough that every address has to be found by hand?

Usage:
    python tools/compare_exe.py <exe-a> <exe-b>
"""

import struct
import sys
from collections import Counter


def sections(data):
    pe_off = struct.unpack_from("<I", data, 0x3C)[0]
    num = struct.unpack_from("<H", data, pe_off + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe_off + 20)[0]
    opt_off = pe_off + 24
    base = struct.unpack_from("<I", data, opt_off + 28)[0]
    out = []
    sec_off = opt_off + opt_size
    for i in range(num):
        b = sec_off + i * 40
        name = data[b:b + 8].rstrip(b"\0").decode("latin-1")
        vsize, vaddr, rsize, rptr = struct.unpack_from("<IIII", data, b + 8)
        out.append(dict(name=name, vaddr=vaddr, vsize=vsize, rptr=rptr, rsize=rsize))
    return base, out


def find_section(secs, name):
    for s in secs:
        if s["name"] == name:
            return s
    return None


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2

    with open(argv[1], "rb") as f:
        a = f.read()
    with open(argv[2], "rb") as f:
        b = f.read()

    base_a, secs_a = sections(a)
    base_b, secs_b = sections(b)

    print(f"A {argv[1]}  {len(a):,} bytes  base {base_a:#x}")
    for s in secs_a:
        print(f"    {s['name']:<8} va {base_a + s['vaddr']:#010x} "
              f"vsize {s['vsize']:#010x} raw {s['rptr']:#010x} {s['rsize']:#010x}")
    print(f"B {argv[2]}  {len(b):,} bytes  base {base_b:#x}")
    for s in secs_b:
        print(f"    {s['name']:<8} va {base_b + s['vaddr']:#010x} "
              f"vsize {s['vsize']:#010x} raw {s['rptr']:#010x} {s['rsize']:#010x}")
    print()

    ta, tb = find_section(secs_a, ".text"), find_section(secs_b, ".text")
    if not ta or not tb:
        print("no .text in one of the images; nothing to compare")
        return 1

    code_a = a[ta["rptr"]:ta["rptr"] + ta["rsize"]]
    code_b = b[tb["rptr"]:tb["rptr"] + tb["rsize"]]

    # Walk the two code sections in lockstep and report where they diverge. If the
    # builds are the same compile with inserts, divergence is rare and localised.
    limit = min(len(code_a), len(code_b))
    same = sum(1 for i in range(0, limit, 64) if code_a[i:i + 64] == code_b[i:i + 64])
    total = len(range(0, limit, 64))
    print(f".text: {len(code_a):#x} vs {len(code_b):#x} bytes, "
          f"{same}/{total} aligned 64-byte blocks identical "
          f"({100.0 * same / total:.1f}%) at zero shift")

    # Probe: take distinctive 32-byte code samples from A, locate them in B, and
    # tally the resulting shifts. A single dominant shift means a mechanical
    # translation exists; a scatter means it does not.
    print("\nshift histogram (32-byte samples from A located in B):")
    shifts = Counter()
    unfound = 0
    step = max(len(code_a) // 400, 32)
    for off in range(0, len(code_a) - 32, step):
        sample = code_a[off:off + 32]
        if len(set(sample)) < 8:   # skip padding / repetitive filler
            continue
        pos = code_b.find(sample)
        if pos < 0:
            unfound += 1
            continue
        if code_b.find(sample, pos + 1) >= 0:   # ambiguous, skip
            continue
        shifts[(tb["vaddr"] + pos) - (ta["vaddr"] + off)] += 1

    for shift, count in shifts.most_common(12):
        print(f"    {shift:+#x}  {count} samples")
    print(f"    (not found in B: {unfound})")

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
