"""Build the game and the CPU benchmark with every toolchain/flag combination
and report code sizes. Benchmark discs land in build/bakeoff/<config>/bench/disc/.

usage: python tools/bakeoff.py [--results results.json]

With --results (a JSON map of config name -> list of benchmark timer ticks,
read from 0x00200008 after running each bench disc), also prints the speed
table.
"""
import argparse
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

TOOLCHAINS = [
    ("gcc8.4", "C:/Users/albat/saturndev/saturn-sdk-8-4/toolchain/bin", "sh-elf-"),
    ("gcc9.3", "C:/Users/albat/saturndev/saturn-sdk/toolchain/bin", "sh-elf-"),
    ("gcc14.2", os.path.join(ROOT, "toolchain/gcc-14.2.0/sh2eb-elf/bin").replace("\\", "/"),
     "sh2eb-elf-"),
]
OPTS = ["-O2", "-Os"]
KERNELS = ["matrix", "frameLerp", "transform", "room+flush", "math"]


def configs():
    for name, sh_bin, prefix in TOOLCHAINS:
        for opt in OPTS:
            yield f"{name}{opt}", sh_bin, prefix, opt


def size(sh_bin, prefix, elf):
    out = subprocess.run([os.path.join(sh_bin, prefix + "size"), elf],
                         capture_output=True, text=True, check=True).stdout
    text, data, bss = out.splitlines()[1].split()[:3]
    return int(text), int(data), int(bss)


def build():
    rows = {}
    for name, sh_bin, prefix, opt in configs():
        build_dir = f"build/bakeoff/{name}"
        cmd = ["make", "disc", "bench", f"SH_BIN={sh_bin}", f"SH_PREFIX={prefix}",
               f"OPT={opt}", f"BUILD={build_dir}"]
        res = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        log = res.stdout + res.stderr
        with open(os.path.join(ROOT, build_dir + ".log"), "w") as f:
            f.write(log)
        if res.returncode != 0:
            sys.exit(f"{name}: build failed, see {build_dir}.log")
        warnings = log.count("warning:")
        game = size(sh_bin, prefix, os.path.join(ROOT, build_dir, "0MAIN.elf"))
        rows[name] = {"game": game, "warnings": warnings}
        print(f"built {name}: text {game[0]}, {warnings} warnings")
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results")
    args = ap.parse_args()

    rows = build()

    print()
    print(f"{'config':14s} {'text':>8s} {'data':>6s} {'bss':>7s} {'warn':>5s}")
    for name, r in rows.items():
        t, d, b = r["game"]
        print(f"{name:14s} {t:8d} {d:6d} {b:7d} {r['warnings']:5d}")

    if args.results:
        results = json.load(open(args.results))
        base = results.get("gcc9.3-O2")
        print()
        print(f"{'config':14s} " + " ".join(f"{k:>12s}" for k in KERNELS) + f" {'total':>12s}")
        for name in rows:
            ticks = results.get(name)
            if not ticks:
                continue
            cycles = [t * 128 for t in ticks]
            cells = []
            for i, c in enumerate(cycles + [sum(cycles)]):
                ref = (base[i] * 128) if (base and i < len(base)) else (sum(base) * 128 if base else None)
                pct = f"{100.0 * c / ref:5.1f}%" if ref else ""
                cells.append(f"{c / 1e6:5.2f}M {pct}")
            print(f"{name:14s} " + " ".join(f"{c:>12s}" for c in cells))


if __name__ == "__main__":
    main()
