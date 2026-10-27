"""Draw order hints for a level, found from captured cases: depthhints.py CASE... [--out FILE]

Captured frames (tools/depthcheck.py's cases, one ordering table layer) are
drawn once each with a depth buffer, which gives every pixel's nearest face,
and each face's pixels are kept. Repainting a case in another order is then
cheap, so hints can be tried: a hint moves every face of one thing (an item,
a room's static mesh, or one room face) that many ordering table buckets
farther (positive) or nearer. Greedily, worst offender first, the bias that
removes the most wrong pixels over every case the thing appears in is kept,
if it removes enough; the hints found are written as JSON for the packer.

Hints only help what a bias can fix: a thing wrongly in front from most
views. Check them on cases they weren't found from (--check).
"""
import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import depthcheck as dc
import trsat

BIASES = (-64, -32, -16, -8, -4, -2, -1, 1, 2, 4, 8, 16, 32, 64)
MIN_GAIN = 20       # wrong pixels a hint must remove over its cases


class Case:
    def __init__(self, name, args, lvl):
        d = args.dir
        sym = dc.Symbols(args.elf or os.path.join(d, '0MAIN.elf'))
        ram = dc.Ram(os.path.join(d, name + '_faces.bin'))
        faces = dc.faces_in_order(ram, sym)
        owners = dc.label_faces(faces, ram, sym, lvl)
        dc.objects_by_vertices(faces)
        dc.object_rooms(faces, ram, sym, lvl, owners)
        err = dc.check_commands(faces, dc.commands(dc.Ram(os.path.join(d, name + '_cmds.bin')), sym), ram, sym)
        if err:
            sys.exit(f'{name}: face order does not match the commands: {err}')
        walk = dc.walk_order(ram, sym)
        camera = walk[-1] if walk else -1
        for f in faces:
            if f.kind == 'room':
                f.ident = f.label
            if args.by_camera:
                f.ident = f'{f.ident} @{camera}'

        self.name = name
        self.faces = faces
        self.pix, self.w = [], []
        best = np.full(dc.FRAME_H * dc.FRAME_W, -1, np.int32)
        best_w = np.zeros(dc.FRAME_H * dc.FRAME_W)
        for f in faces:
            tris, _ = dc.polygon(f, ram, sym)
            pix, w = [], []
            for tri in tris or ():
                p, ww = coverage(tri)
                pix.append(p)
                w.append(ww)
            p = np.concatenate(pix) if pix else np.zeros(0, np.int64)
            ww = np.concatenate(w) if w else np.zeros(0)
            self.pix.append(p)
            self.w.append(ww)
            nearer = ww > best_w[p]
            best[p[nearer]] = f.order
            best_w[p[nearer]] = ww[nearer]
        self.best, self.best_w = best, best_w
        self.sprite = np.array([f.type == dc.T_SPRITE for f in faces] + [False])
        self.idents = {f.ident for f in faces}

    def wrong(self, hints):
        """Wrong pixels painted in the ordering table's order, buckets moved by hints."""
        def key(f):
            b = min(max(f.bucket + hints.get(f.ident, 0), 0), 1 << 16)
            return (-b, 0 if f.kind == 'room' else 1 if f.room_list else 2, -f.addr)
        paint = np.full(self.best.shape, -1, np.int32)
        paint_w = np.zeros(self.best.shape)
        for f in sorted(self.faces, key=key):
            paint[self.pix[f.order]] = f.order
            paint_w[self.pix[f.order]] = self.w[f.order]
        wrong = (paint != self.best) & (self.best >= 0) & (self.best_w > paint_w * (1 + dc.DEPTH_EPS))
        wrong &= ~(self.sprite[paint] | self.sprite[self.best])
        return wrong, paint

    def offenders(self, hints):
        wrong, paint = self.wrong(hints)
        out = {}
        for pi, ti in zip(paint[wrong], self.best[wrong]):
            for who in (self.faces[pi].ident, self.faces[ti].ident):
                out[who] = out.get(who, 0) + 1
        return int(wrong.sum()), out


def coverage(tri):
    """A triangle's pixels (flat indices) and its 1/z at each."""
    (x0, y0, w0), (x1, y1, w1), (x2, y2, w2) = tri
    area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
    xa, xb = max(min(x0, x1, x2), 0), min(max(x0, x1, x2), dc.FRAME_W - 1)
    ya, yb = max(min(y0, y1, y2), 0), min(max(y0, y1, y2), dc.FRAME_H - 1)
    if area == 0 or xa > xb or ya > yb:
        return np.zeros(0, np.int64), np.zeros(0)
    xs, ys = np.meshgrid(np.arange(xa, xb + 1), np.arange(ya, yb + 1))
    b0 = ((x1 - xs) * (y2 - ys) - (x2 - xs) * (y1 - ys)) / area
    b1 = ((x2 - xs) * (y0 - ys) - (x0 - xs) * (y2 - ys)) / area
    b2 = 1.0 - b0 - b1
    inside = (b0 >= 0) & (b1 >= 0) & (b2 >= 0)
    w = b0 * w0 + b1 * w1 + b2 * w2
    return (ys[inside] * dc.FRAME_W + xs[inside]).astype(np.int64), w[inside]


def total(cases, hints):
    return sum(int(c.wrong(hints)[0].sum()) for c in cases)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cases', nargs='+')
    ap.add_argument('--dir', default=os.path.join(dc.ROOT, 'build/depth_cases'))
    ap.add_argument('--elf')
    ap.add_argument('--level', default='LEVEL1')
    ap.add_argument('--candidates', type=int, default=40, help='offenders to try hints on')
    ap.add_argument('--check', nargs='*', default=[], help='cases to check the hints on, not found from')
    ap.add_argument('--out', help='write the hints (JSON)')
    ap.add_argument('--by-camera', action='store_true', help="hints apply only with the camera in a room")
    args = ap.parse_args()

    lvl = trsat.load(os.path.join(dc.ROOT, 'external_resources/disc/files/DATA'), args.level)
    cases = [Case(n, args, lvl) for n in args.cases]
    checks = [Case(n, args, lvl) for n in args.check]

    hints = {}
    before = total(cases, hints)
    counts = {}
    for c in cases:
        for who, n in c.offenders(hints)[1].items():
            counts[who] = counts.get(who, 0) + n
    candidates = [w for w, _ in sorted(counts.items(), key=lambda kv: -kv[1])
                  if w.split(' @')[0] not in ('Lara', '?')][:args.candidates]
    print(f'{len(cases)} cases, {before} wrong pixels; trying {len(candidates)} offenders')

    for who in candidates:
        mine = [c for c in cases if who in c.idents]
        base = total(mine, hints)
        best_bias, best_n = 0, base
        for bias in BIASES:
            n = total(mine, {**hints, who: bias})
            if n < best_n:
                best_bias, best_n = bias, n
        if base - best_n >= MIN_GAIN:
            hints[who] = best_bias
            print(f'  {who:32s} bias {best_bias:+4d}: {base:6d} -> {best_n:6d} over {len(mine)} cases')

    after = total(cases, hints)
    print(f'found from: {before} -> {after} wrong pixels with {len(hints)} hints')
    if checks:
        print(f'checked on {len(checks)} others: {total(checks, {})} -> {total(checks, hints)}')
    if args.out:
        with open(args.out, 'w') as f:
            json.dump({args.level: hints}, f, indent=2, sort_keys=True)
        print('wrote', args.out)


if __name__ == '__main__':
    main()
