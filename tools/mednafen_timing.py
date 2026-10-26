"""Sets the Mednafen fork's timing calibration in the MCP server's config, so
that Mednafen times VDP1 and the SH-2s as a real Saturn does. Run with
Mednafen closed: it writes its settings back on exit.

usage: mednafen_timing.py [stock] [name=value ...]
       (no arguments: the calibrated values below; "stock": Mednafen's own
       timing; name=value overrides one, e.g. vdp1.cost.texel=18)

VDP1 (ss.vdp1.cost.*, sixteenths of a cycle; 2026-10-25, the measurement
spot, five ways of building the frame measured on a Saturn with no text
drawn by VDP1, f6876d8): a texel read costs 20/16 of a cycle more, including
the texels a shrunken texture skips; a gouraud-shaded pixel 14/16; a line
262/16. The fit matches the four ways the game draws within 3% of a frame,
and puts textures halved to a texel a pixel (the LOD x1 bench) 6% high: on a
Saturn, texel reads beyond one a pixel cost more than the rest (about 25/16
against 15/16), which a single texel cost can't follow. The earlier fit
(15/16, 18/16, 224/16) was made with the measurement table drawn by VDP1,
which Mednafen overcharges.

SH-2 (ss.sh2.cost.*; 2026-10-25, instruction probes timed on a Saturn, 6ee8df7):
a MAC.W takes half a cycle more (8 sixteenths); MACH/MACL can be read 2
cycles after the last MAC.W (a dot product of three MAC.Ws costs 3.2 more);
the multiplier is busy 4 cycles longer after MUL.L and 3 after DMULS.L/
DMULU.L. MULS.W, loads, stores and memory streams already match.

The SH-2 caches (ss.dbg_cem; 2026-10-26, 898c9ec): Mednafen emulates the
instruction cache only for the games in its database, and runs any other
disc as if every instruction fetch hit. In the master's sampling profile the
Saturn matched Mednafen in the hot loops but took about 1.45x as long over
the rest of the code. With "full", both caches emulated, the measurement
spot's CPU times come within 4% of the Saturn's (Base: master 112 against
116, meshes 51 against 53, slave 66 against 67).
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CFG = os.path.join(ROOT, "build", "mednafen_home", "mednafen.cfg")
CALIBRATED = {
    "vdp1.cost.texel": "20", "vdp1.cost.tex_pixel": "0", "vdp1.cost.gouraud": "14",
    "vdp1.cost.pixel": "0", "vdp1.cost.line": "262",
    "sh2.cost.mac_w": "8", "sh2.cost.mac_wait": "2", "sh2.cost.mul_l": "4", "sh2.cost.dmul": "3",
    "dbg_cem": "full",
}
STOCK = {"dbg_cem": "auto"}     # the rest: "0"


def main():
    args = sys.argv[1:]
    values = dict(CALIBRATED)
    if args and args[0] == "stock":
        values = {k: STOCK.get(k, "0") for k in values}
        args = args[1:]
    for a in args:
        name, _, value = a.partition("=")
        if name not in values:
            sys.exit(f"unknown setting {name}; one of {', '.join(values)}")
        values[name] = value

    lines = open(CFG, encoding="utf-8", errors="replace").read().splitlines() if os.path.exists(CFG) else []
    for name, value in values.items():
        key = f"ss.{name}"
        for i, line in enumerate(lines):
            if re.match(re.escape(key) + r"\s", line):
                lines[i] = f"{key} {value}"
                break
        else:
            lines.append(f"{key} {value}")
    with open(CFG, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(" ".join(f"{k}={v}" for k, v in values.items()))


if __name__ == "__main__":
    main()
