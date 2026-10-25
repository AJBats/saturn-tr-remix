"""Where a frame's VDP1 time goes, from the Mednafen fork's cost log
(raw_command "vdp1_cost_log <path>" logs the next plot, a CSV row per command).

usage: vdp1_cost.py LOG.csv [cache_start]
       cache_start: VDP1 VRAM offset where the room texture cache begins
       (textures from there on are room textures; default 0x40000)

The cycles column is Mednafen's, with the calibrated drawing costs
(tools/mednafen_timing.py); a 30 fps frame is CYCLES_PER_FRAME of them.
"""
import csv
import sys
from collections import defaultdict

CYCLES_PER_FRAME = 28636360 // 30   # VDP1 counts the 28.6 MHz system clock


def kind(r, cache_start):
    t = int(r["ctrl"]) & 0xF
    if int(r["ctrl"]) & 0x4000:
        return "skipped"
    if t in (8, 9, 10):
        return "setup"
    if t == 1:
        return "clear/sprite"
    if t == 4:
        return "polygon"
    if t == 2:
        return "room texture" if int(r["srca"]) * 8 >= cache_start else "object texture"
    return f"type {t}"


def size_class(px):
    for limit, name in ((16, "<16 px"), (64, "16-63"), (256, "64-255"), (1024, "256-1023"), (4096, "1024-4095")):
        if px < limit:
            return name
    return "4096+"


def main():
    rows = list(csv.DictReader(open(sys.argv[1])))
    cache_start = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x40000
    total = sum(int(r["cycles"]) for r in rows)
    pct = lambda c: 100.0 * c / CYCLES_PER_FRAME

    print(f"{len(rows)} commands, {total} cycles = {pct(total):.0f}% of a 30 fps frame")
    sums = defaultdict(lambda: defaultdict(int))
    for r in rows:
        k = kind(r, cache_start)
        s = sums[k]
        s["n"] += 1
        for f in ("cycles", "lines", "pixels", "texels", "gouraud"):
            s[f] += int(r[f])

    print(f"\n{'kind':16s} {'n':>5s} {'%frame':>7s} {'lines':>7s} {'pixels':>8s} {'texels':>8s} {'px/line':>7s} {'tx/px':>6s} {'cyc/face':>8s}")
    for k, s in sorted(sums.items(), key=lambda kv: -kv[1]["cycles"]):
        print(f"{k:16s} {s['n']:5d} {pct(s['cycles']):7.1f} {s['lines']:7d} {s['pixels']:8d} {s['texels']:8d} "
              f"{s['pixels'] / max(1, s['lines']):7.1f} {s['texels'] / max(1, s['pixels']):6.2f} {s['cycles'] / s['n']:8.0f}")

    # the faces by how many pixels they step through
    print(f"\n{'face pixels':12s} {'n':>5s} {'%frame':>7s} {'cyc/px':>7s} {'lines':>7s} {'px/line':>7s}")
    by = defaultdict(lambda: defaultdict(int))
    for r in rows:
        if kind(r, cache_start) in ("setup", "skipped", "clear/sprite"):
            continue
        b = by[size_class(int(r["pixels"]))]
        b["n"] += 1
        b["cycles"] += int(r["cycles"])
        b["pixels"] += int(r["pixels"])
        b["lines"] += int(r["lines"])
    order = ["<16 px", "16-63", "64-255", "256-1023", "1024-4095", "4096+"]
    for name in order:
        b = by.get(name)
        if not b:
            continue
        print(f"{name:12s} {b['n']:5d} {pct(b['cycles']):7.1f} {b['cycles'] / max(1, b['pixels']):7.1f} "
              f"{b['lines']:7d} {b['pixels'] / max(1, b['lines']):7.1f}")

    # Lines run from edge AD to edge BC, as many as the longer of those is
    # long. Drawn the other way round (the quad's corners rotated, with a
    # transposed texture for a textured one) the longer of AB and DC sets the
    # count, and the texels each line steps through come with the lines.
    LINE_CYCLES, TEXEL_CYCLES = 26, 0.94
    saved_lines = saved_cycles = turned = 0
    for r in rows:
        if kind(r, cache_start) not in ("room texture", "object texture", "polygon"):
            continue
        span = lambda x0, y0, x1, y1: max(abs(int(r[x1]) - int(r[x0])), abs(int(r[y1]) - int(r[y0])))
        now = int(r["lines"])
        alt = max(span("xa", "ya", "xb", "yb"), span("xd", "yd", "xc", "yc")) + 1
        if alt < now:
            texels = int(r["texels"])
            saved_lines += now - alt
            saved_cycles += (now - alt) * LINE_CYCLES + texels * (1 - alt / now) * TEXEL_CYCLES
            turned += 1
    print(f"\nlines along the longer side: {turned} faces turned, {saved_lines} fewer lines, "
          f"~{pct(saved_cycles):.0f}% of a frame")

    # what the parts of the model come to (tools/mednafen_timing.py)
    lines = sum(int(r["lines"]) for r in rows)
    pixels = sum(int(r["pixels"]) for r in rows)
    texels = sum(int(r["texels"]) for r in rows)
    gour = sum(int(r["gouraud"]) for r in rows)
    print(f"\nlines {lines}  pixels {pixels} ({pixels / (352 * 240):.1f} screens)  texels {texels}  gouraud px {gour}")


if __name__ == "__main__":
    main()
