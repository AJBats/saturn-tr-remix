"""Sets the Mednafen fork's VDP1 drawing costs (ss.vdp1.cost.*, sixteenths
of a cycle) in the MCP server's config, so that Mednafen times VDP1 as a real
Saturn does. Run with Mednafen closed: it writes its settings back on exit.

usage: mednafen_timing.py [texel tex_pixel gouraud pixel line]
       (no arguments: the calibrated values below; "0 0 0 0 0": stock timing)

Calibration (2026-10-25, LEVEL1 bridge view, six ways of building the frame
measured on a Saturn): a texel read costs 15/16 of a cycle more, including
the texels a shrunken texture skips; a gouraud-shaded pixel 18/16; a line
224/16. The fit matches all six within 3% of a frame.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CFG = os.path.join(ROOT, "build", "mednafen_home", "mednafen.cfg")
NAMES = ["texel", "tex_pixel", "gouraud", "pixel", "line"]
CALIBRATED = ["15", "0", "18", "0", "224"]


def main():
    values = dict(zip(NAMES, sys.argv[1:6] if len(sys.argv) > 1 else CALIBRATED))
    lines = open(CFG, encoding="utf-8", errors="replace").read().splitlines() if os.path.exists(CFG) else []
    for name, value in values.items():
        key = f"ss.vdp1.cost.{name}"
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
