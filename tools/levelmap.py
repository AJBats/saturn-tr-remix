"""A level seen from above, for writing the playtest bot's routes: each sector a
square with its floor in clicks (256 units, the floor's y / 256), shaded by
height; solid sectors dark, water rooms blue, the portals sideways outlined
in white; items as their index at their spot. Where rooms stack, the floor
drawn is the highest one in the y range given.

    python -I tools/levelmap.py LEVEL X0 Z0 X1 Z1 [--y Y0 Y1] [--flipped] [--out FILE.png]

x runs left to right, z bottom to top (as the bot's floors command prints).
--flipped draws the alternate rooms in their bases' places (the level after
a flip map). The picture holds level geometry, so it goes in build/.
"""
import argparse
import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import trsat  # noqa: E402

CELL = 40   # pixels a sector
WALL = -127


def portal_side(lvl, fi):
    """The room a sector's floor data leads to sideways, or None."""
    if not fi:
        return None
    k = fi
    for _ in range(4):
        cmd = lvl.floors[k]
        t = cmd & 0x1F
        if t == 1:
            return lvl.floors[k + 1]
        if t in (2, 3) and not cmd >> 15:
            k += 2
            continue
        return None
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('level')
    ap.add_argument('box', type=int, nargs=4)
    ap.add_argument('--y', type=int, nargs=2, default=(-32768, 32767))
    ap.add_argument('--flipped', action='store_true')
    ap.add_argument('--out')
    args = ap.parse_args()
    lvl = trsat.load(os.path.join(ROOT, 'external_resources/disc/files/DATA'), args.level)
    x0, z0, x1, z1 = args.box
    sx0, sz0, sx1, sz1 = x0 >> 10, z0 >> 10, x1 >> 10, z1 >> 10
    w, h = (sx1 - sx0 + 1) * CELL, (sz1 - sz0 + 1) * CELL
    img = Image.new('RGB', (w + 60, h + 30), (24, 24, 28))
    d = ImageDraw.Draw(img)

    # which rooms count: bases, or their alternates when flipped
    alt_of = {r.alternate: r.index for r in lvl.rooms if r.alternate >= 0}
    rooms = []
    for r in lvl.rooms:
        if args.flipped:
            if r.alternate >= 0:
                continue            # its alternate stands in its place
        elif r.index in alt_of:
            continue                # an alternate: only after a flip
        rooms.append(r)

    best = {}   # (sx, sz) -> (floor, room, water, side)
    for r in rooms:
        if r.y_bottom < args.y[0] or r.y_top > args.y[1]:
            continue
        for i, sec in enumerate(r.sectors):
            fi, box, below, floor, above, ceiling = sec
            sx = (r.x >> 10) + i // r.z_sectors
            sz = (r.z >> 10) + i % r.z_sectors
            if not (sx0 <= sx <= sx1 and sz0 <= sz <= sz1):
                continue
            side = portal_side(lvl, fi)
            if floor == WALL or below != 255:
                if side is not None and (sx, sz) not in best:
                    best[(sx, sz)] = (None, r.index, False, side)
                continue
            y = floor * 256
            if not args.y[0] <= y <= args.y[1]:
                continue
            water = bool(r.flags & 1)
            cur = best.get((sx, sz))
            if cur is None or cur[0] is None or y < cur[0]:
                best[(sx, sz)] = (y, r.index, water, side)

    ys = [v[0] for v in best.values() if v[0] is not None]
    lo, hi = (min(ys), max(ys)) if ys else (0, 1)
    for (sx, sz), (y, room, water, side) in best.items():
        px = (sx - sx0) * CELL + 50
        py = (sz1 - sz) * CELL + 5
        if y is None:
            d.rectangle([px, py, px + CELL - 1, py + CELL - 1], outline=(220, 220, 220))
            d.text((px + 3, py + 13), f'>{side}', fill=(220, 220, 220))
            continue
        t = (y - lo) / max(1, hi - lo)
        shade = int(230 - 170 * t)
        col = (shade // 3, shade // 2, shade) if water else (shade, shade, int(shade * 0.85))
        d.rectangle([px, py, px + CELL - 1, py + CELL - 1], fill=col, outline=(60, 60, 60))
        d.text((px + 3, py + 3), f'{y // 256}', fill=(0, 0, 0))
        d.text((px + 3, py + 26), f'r{room}', fill=(70, 70, 70))
    for i, it in enumerate(lvl.items):
        sx, sz = it['x'] >> 10, it['z'] >> 10
        if not (sx0 <= sx <= sx1 and sz0 <= sz <= sz1) or not args.y[0] <= it['y'] <= args.y[1]:
            continue
        px = (it['x'] - (sx0 << 10)) * CELL // 1024 + 50
        py = ((sz1 + 1 << 10) - it['z']) * CELL // 1024 + 5
        d.ellipse([px - 3, py - 3, px + 3, py + 3], fill=(230, 40, 40))
        d.text((px + 4, py - 6), f'{i}:{it["type"]}', fill=(255, 90, 90))
    for sx in range(sx0, sx1 + 1, 2):
        d.text(((sx - sx0) * CELL + 50, h + 10), str(sx << 10), fill=(200, 200, 200))
    for sz in range(sz0, sz1 + 1, 2):
        d.text((2, (sz1 - sz) * CELL + 18), str(sz << 10), fill=(200, 200, 200))
    out = args.out or os.path.join(ROOT, 'build', f'levelmap_{args.level}.png')
    img.save(out)
    print(out, f'floors {lo // 256}..{hi // 256} clicks')


if __name__ == '__main__':
    main()
