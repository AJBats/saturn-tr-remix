"""The measurement table's sampling profile (HW_LOG builds, debug.cpp) by
function.

usage: profile_map.py ELF --dump HWRAM.bin LWRAM.bin
           every block, from dumps of high and low work RAM (the blocks are
           kept with the trace, below its rings: trace.py capture writes
           both dumps)
       profile_map.py ELF ADDR SHARE [ADDR SHARE ...]
           the blocks the table shows (a photo of a Saturn's): each the low 5
           hex digits of a block's address and its share in percent

A block is 64 bytes of the program; each counts towards the function it
starts in.
"""
import bisect
import os
import struct
import subprocess
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NM = os.path.join(ROOT, "toolchain", "gcc-14.2.0", "sh2eb-elf", "bin", "sh2eb-elf-nm")
START = 0x06004000
SHIFT = 6
BLOCKS = 152 * 1024 >> SHIFT    # debug.cpp's PROFILE_BLOCKS
TRACE_HEADER = 0x002FFF00
TRACE_LANES = 4                 # trace.h: the header's profile pointer follows the lanes


def symbols(elf):
    out = subprocess.run([NM, "-n", "-C", elf], capture_output=True, text=True, check=True).stdout
    funcs, data = [], {}
    for line in out.splitlines():
        parts = line.split(" ", 2)
        if len(parts) != 3:
            continue
        addr, kind, name = int(parts[0], 16), parts[1], parts[2]
        if kind in "TtWw":
            funcs.append((addr, name))
        data[name] = addr
    return funcs, data


def main():
    elf = sys.argv[1]
    funcs, data = symbols(elf)
    starts = [a for a, _ in funcs]

    def func(addr):
        i = bisect.bisect_right(starts, addr) - 1
        return funcs[i][1] if i >= 0 else "?"

    shares = defaultdict(float)
    if sys.argv[2] == "--dump":
        ram = open(sys.argv[3], "rb").read()
        low = open(sys.argv[4], "rb").read()
        hist, = struct.unpack_from(">I", low, TRACE_HEADER - 0x00200000 + 8 + TRACE_LANES * 16)
        hist -= 0x00200000
        counts = struct.unpack_from(f">{BLOCKS}H", low, hist)
        total = struct.unpack_from(">I", ram, data["gProfileSamples"] - 0x06000000)[0]
        other = struct.unpack_from(">I", ram, data["gProfileOther"] - 0x06000000)[0]
        print(f"{total} samples, {100 * other / max(1, total):.1f}% outside the program")
        if abs(sum(counts) + other - total) > total // 100:
            print(f"the blocks hold {sum(counts)} samples: misread")
        for b, n in enumerate(counts):
            if n:
                shares[func(START + (b << SHIFT))] += 100 * n / total
    else:
        pairs = sys.argv[2:]
        for addr, share in zip(pairs[0::2], pairs[1::2]):
            shares[func(0x06000000 + int(addr, 16))] += float(share)

    shown = 0.0
    for name, share in sorted(shares.items(), key=lambda kv: -kv[1]):
        if share < 0.3:
            break
        shown += share
        print(f"{share:6.1f}%  {name[:90]}")
    print(f"{shown:6.1f}%  in all")


if __name__ == "__main__":
    main()
