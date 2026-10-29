"""What a mix's settings cost the game at run time, at the ledge over the
three camera moves: the shaded colour table cache (rooms.ShadeCache: copies
built a frame, faces it leaves on gouraud, VDP1 ms with it against every
copy present) and, for contrast by room light, the seams where faces
sharing an edge take different steps (pixels a frame, and pictures).

    costs.py OUT_DIR

Rooms only, in one cache: the game's two CPUs each keep one, and meshes use
them too.
"""
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

import mixer
from lab import Lab, rgb, VDP1_HZ
from rooms import Options, RoomDrawer, TextureStore, ShadeCache, build_commands, shade_level

OUT = sys.argv[1]
CONFIGS = [
    ('game', {}),
    ('own palette', {'reduced': 'own'}),
    ('bands', {'grain': 'bandpass'}),
    ('contrast', {'shading': 'contrast'}),
    ('own + bands', {'reduced': 'own', 'grain': 'bandpass'}),
    ('mix', {'reduced': 'own', 'grain': 'bandpass', 'shading': 'contrast'}),
]


def step_of(s, corners):
    lit = sum(shade_level(int(s[i])) for i in corners) / 4.0
    return max(4, round(lit / 2)) if lit < 16 else 8


def face_corners(f):
    return list(f.corners) + ([f.corners[2]] if len(f.corners) == 3 else [])


def seams(faces, verts, w=352, h=224):
    """Face-id raster (draw order, PIL polygons) and the steps; pixels where
    two faces sharing an edge meet with different steps."""
    x, y, z, g, clip, s = verts.arrays()
    ids = Image.new('I', (w, h), -1)
    d = ImageDraw.Draw(ids)
    steps, edges = {}, {}
    buckets = {}
    for n, f in enumerate(faces):
        buckets.setdefault(f.bucket, []).append((n, f))
    order = [nf for b in sorted(buckets, reverse=True) for nf in reversed(buckets[b])]
    for n, f in order:
        if f.kind == 'void':
            continue
        cs = face_corners(f)
        steps[n] = step_of(s, cs)
        ring = list(f.corners)
        for a, b in zip(ring, ring[1:] + ring[:1]):
            edges.setdefault((min(a, b), max(a, b)), set()).add(n)
        pts = [(int(x[i]), int(y[i])) for i in f.corners]
        d.polygon(pts, fill=n)
    ids = np.array(ids)
    neighbours = {}
    for fs in edges.values():
        for a in fs:
            for b in fs:
                if a != b:
                    neighbours.setdefault(a, set()).add(b)
    seam = np.zeros((h, w), bool)
    for dy, dx in ((0, 1), (1, 0)):
        p, q = ids[:h - dy, :w - dx], ids[dy:, dx:]
        diff = (p != q) & (p >= 0) & (q >= 0)
        for yy, xx in zip(*np.nonzero(diff)):
            a, b = int(p[yy, xx]), int(q[yy, xx])
            if b in neighbours.get(a, ()) and steps.get(a, 8) != steps.get(b, 8):
                seam[yy, xx] = True
    return ids, steps, seam


def main():
    report = {}
    for path in ('dolly', 'pan', 'creep'):
        lab = Lab('ledge0', path, 60, f'ledge0-{path}-relief')
        report[path] = {}
        for label, mix in CONFIGS:
            name, title, kw, change, art = mixer.compile_mix(mix)
            level = change(lab.base) if change else lab.base
            opts = Options(**kw)
            drawer = RoomDrawer(level, opts)
            store = TextureStore(level, opts)
            cache = ShadeCache(level)
            rows = []
            for i, view in enumerate(lab.views):
                faces, verts = drawer.faces(lab.jobs_for(level, view))
                cl0 = build_commands(level, faces, verts, opts, store)
                lab.vdp1.load_vram(cl0.finish())
                lab.vdp1.fb[:] = 0
                ideal = lab.vdp1.plot()[0].cycles * 1000.0 / VDP1_HZ
                store.shades = cache
                cache.new_frame()
                cl = build_commands(level, faces, verts, opts, store)
                store.shades = None
                lab.vdp1.load_vram(cl.finish())
                lab.vdp1.fb[:] = 0
                real = lab.vdp1.plot()[0].cycles * 1000.0 / VDP1_HZ
                tables = len({c for c in cl.cluts})
                row = dict(hits=cache.hits, built=cache.built, failed=cache.failed, ideal=ideal, real=real,
                           tables=tables, keys=len({k for k in cache.keys if k is not None}))
                if path == 'dolly' and label in ('contrast', 'mix'):
                    ids, steps, seam = seams(faces, verts)
                    row['seam_px'] = int(seam.sum())
                    if i in (0, 30, 59):
                        img = rgb(lab.vdp1.fb).copy()
                        tint = {4: (220, 40, 40), 5: (240, 140, 30), 6: (230, 220, 40), 7: (60, 200, 60), 8: (0, 0, 0)}
                        sm = np.zeros_like(img)
                        for n, k in steps.items():
                            sm[ids == n] = tint[k]
                        over = img.copy()
                        over[seam] = (255, 255, 255)
                        Image.fromarray(np.hstack([img, over, sm])).resize((352 * 3 * 2, 224 * 2), Image.NEAREST) \
                            .save(os.path.join(OUT, f'seams_{label.replace(" ", "")}_{i}.png'))
                rows.append(row)
            m = lambda k, rs=rows[1:]: float(np.mean([r[k] for r in rs]))
            summary = {k: m(k) for k in ('hits', 'built', 'failed', 'ideal', 'real', 'tables', 'keys')}
            summary['first_built'] = rows[0]['built']
            summary['max_built'] = max(r['built'] for r in rows[1:])
            summary['max_failed'] = max(r['failed'] for r in rows[1:])
            if 'seam_px' in rows[0]:
                summary['seam_px'] = float(np.mean([r['seam_px'] for r in rows]))
            report[path][label] = summary
            print(path, label, json.dumps({k: round(v, 2) for k, v in summary.items()}), flush=True)
    with open(os.path.join(OUT, 'costcheck.json'), 'w') as f:
        json.dump(report, f, indent=1)


if __name__ == '__main__':
    main()
