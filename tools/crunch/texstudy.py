"""Texture study: what the textures of a game that reads cleanly on VDP1
(Sonic R) do that Tomb Raider's don't. Takes one drawn frame of each (VDP1
VRAM, and VDP2 colour RAM for colour-bank textures), finds every textured
sprite, and measures each texture and how it is drawn.

    texstudy.py     (build/crunch/texstudy/index.html; the inputs are
                     capture.py's ledge0 and Sonic R's race frame,
                     build/crunch/sonicr/race_vram.bin + race_cram.bin)

Per texture, over its texels (luminance 0..255 from its colours):
  colours     colours its texels use
  contrast    luminance standard deviation
  step        mean luminance difference between neighbouring texels
  grain       step / contrast: how much of the contrast changes texel to
              texel (noise) rather than across the texture (shapes, ramps)
  lonely      share of texels unlike all four neighbours
  run         mean length of same-colour runs along rows and columns
And per drawn sprite (from vdp1lib's per-command counts):
  texels/px   texels VDP1 read a pixel drawn (above 1: shrunk, rows or
              columns skipped)
  magnify     pixels a texel covers on screen (drawn pixels / texture area)
Everything is weighted by the pixels each sprite drew.
"""
import json
import os
import struct

import numpy as np
from PIL import Image

from capdecode import s13
from medclient import LAB
from vdp1 import Vdp1

LUM = np.array([0.299, 0.587, 0.114])


def sprites(vram, cram=None):
    """The textured sprites of a VDP1 list as drawn: (command address,
    texels h x w, colours 16 x RGB 0..31, transparent, corners)."""
    out = []
    addr, ret, seen = 0, -1, set()
    lx = ly = 0
    while addr not in seen and len(seen) < 8192:
        seen.add(addr)
        w = struct.unpack_from('>16H', vram, addr)
        if w[0] & 0x8000:
            break
        nxt = addr + 32
        if not (w[0] & 0x4000):
            kind = w[0] & 0xF
            if kind == 0xA:
                lx, ly = s13(w[6]), s13(w[7])
            elif kind in (0, 1, 2):
                pmod = w[2]
                cm = (pmod >> 3) & 7
                if cm in (0, 1) and (cm == 1 or cram is not None):
                    tw, th = ((w[5] >> 8) & 0x3F) * 8, w[5] & 0xFF
                    if tw and th:
                        raw = np.frombuffer(vram[w[4] * 8:w[4] * 8 + tw * th // 2], np.uint8)
                        if len(raw) == tw * th // 2:
                            tex = np.empty((th, tw), np.uint8)
                            tex[:, 0::2] = (raw >> 4).reshape(th, tw // 2)
                            tex[:, 1::2] = (raw & 15).reshape(th, tw // 2)
                            if cm == 1:
                                cols = struct.unpack_from('>16H', vram, w[3] * 8)
                            else:
                                bank = w[3] & ~0xF & 0x7FF
                                cols = [int(c) for c in cram[bank:bank + 16]]
                            rgb = np.array([[(c >> s) & 31 for s in (0, 5, 10)] for c in cols], np.float64)
                            corners = [(s13(w[6 + 2 * k]) + lx, s13(w[7 + 2 * k]) + ly) for k in range(4)] \
                                if kind == 2 else None
                            out.append(dict(addr=addr >> 3, texels=tex, rgb=rgb, transparent=not (pmod & 0x40),
                                            key=(w[4], w[5], tuple(cols)), corners=corners))
        jp = (w[0] >> 12) & 3
        if jp == 1:
            nxt = (w[1] * 8) & ~31
        elif jp == 2:
            if ret < 0:
                ret = nxt
            nxt = (w[1] * 8) & ~31
        elif jp == 3 and ret >= 0:
            nxt, ret = ret, -1
        addr = nxt
    return out


def texture_stats(texels, rgb, transparent):
    lum = (rgb @ LUM)[texels] * (255.0 / 31.0)
    solid = np.ones(texels.shape, bool) if not transparent else texels != 0
    if solid.sum() < 4:
        return None
    vals = lum[solid]
    contrast = float(vals.std())
    dx = np.abs(np.diff(lum, axis=1))[solid[:, 1:] & solid[:, :-1]]
    dy = np.abs(np.diff(lum, axis=0))[solid[1:, :] & solid[:-1, :]]
    step = float(np.concatenate([dx, dy]).mean()) if len(dx) + len(dy) else 0.0
    t = texels.astype(np.int16)
    p = np.pad(t, 1, mode='edge')
    n = np.stack([p[:-2, 1:-1], p[2:, 1:-1], p[1:-1, :-2], p[1:-1, 2:]])
    lonely = float((np.all(n != t, axis=0) & solid).sum() / solid.sum())

    def runs(a):
        lengths = []
        for row in a:
            r = 1
            for i in range(1, len(row)):
                if row[i] == row[i - 1]:
                    r += 1
                else:
                    lengths.append(r)
                    r = 1
            lengths.append(r)
        return lengths

    run = float(np.mean(runs(texels) + runs(texels.T)))
    return dict(colours=int(len(np.unique(texels[solid]))), contrast=contrast, step=step,
                grain=step / contrast if contrast > 0.5 else 0.0, lonely=lonely, run=run,
                size=f'{texels.shape[1]}x{texels.shape[0]}')


def study(name, vram, cram=None):
    v = Vdp1()
    v.load_vram(vram)
    v.fb[:] = 0
    _, log = v.plot(log_max=8192)
    cost = {c.addr: c for c in log}
    rows = []
    for s in sprites(vram, cram):
        c = cost.get(s['addr'])
        if not c or c.pixels == 0:
            continue
        st = texture_stats(s['texels'], s['rgb'], s['transparent'])
        if st is None:
            continue
        h, w = s['texels'].shape
        st.update(pixels=c.pixels, texels_px=c.texels / c.pixels, magnify=c.pixels / (w * h), key=s['key'],
                  sprite=s)
        rows.append(st)
    return rows


def weighted(rows, k):
    w = np.array([r['pixels'] for r in rows], np.float64)
    v = np.array([r[k] for r in rows], np.float64)
    order = np.argsort(v)
    cw = np.cumsum(w[order]) / w.sum()
    median = float(v[order][np.searchsorted(cw, 0.5)])
    return float((w * v).sum() / w.sum()), median


def sheet(rows, path, count=48, scale=2):
    """The textures drawing the most pixels, each at its size x scale."""
    by_key = {}
    for r in rows:
        k = r['key']
        if k not in by_key:
            by_key[k] = [r, 0]
        by_key[k][1] += r['pixels']
    top = sorted(by_key.values(), key=lambda e: -e[1])[:count]
    tiles = []
    for r, _ in top:
        s = r['sprite']
        img = (s['rgb'][s['texels']] * (255.0 / 31.0)).astype(np.uint8)
        if s['transparent']:
            img[s['texels'] == 0] = (40, 40, 40)
        tiles.append(np.kron(img, np.ones((scale, scale, 1), np.uint8)))
    cell = 64 * scale + 8
    cols = 8
    out = np.full((cell * ((len(tiles) + cols - 1) // cols), cell * cols, 3), 24, np.uint8)
    for i, t in enumerate(tiles):
        y, x = (i // cols) * cell + 4, (i % cols) * cell + 4
        t = t[:cell - 8, :cell - 8]
        out[y:y + t.shape[0], x:x + t.shape[1]] = t
    Image.fromarray(out).save(path)
    return len(by_key)


def main():
    out_dir = os.path.join(LAB, 'texstudy')
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(LAB, 'captures', 'ledge0_vram.bin'), 'rb') as f:
        tr = study('Tomb Raider', f.read())
    with open(os.path.join(LAB, 'sonicr', 'race_vram.bin'), 'rb') as f:
        vram = f.read()
    cram = np.frombuffer(open(os.path.join(LAB, 'sonicr', 'race_cram.bin'), 'rb').read(), '>u2')
    sr = study('Sonic R', vram, cram)

    keys = ['colours', 'contrast', 'step', 'grain', 'lonely', 'run', 'texels_px', 'magnify']
    summary = {}
    for name, rows in (('Tomb Raider', tr), ('Sonic R', sr)):
        summary[name] = {k: weighted(rows, k) for k in keys}
        summary[name]['sprites'] = len(rows)
        summary[name]['pixels'] = int(sum(r['pixels'] for r in rows))
        summary[name]['textures'] = sheet(rows, os.path.join(out_dir, f'{name.replace(" ", "_")}.png'))
        print(name, {k: (round(v[0], 3), round(v[1], 3)) if isinstance(v, tuple) else v
                     for k, v in summary[name].items()})
    with open(os.path.join(out_dir, 'summary.json'), 'w') as f:
        json.dump(summary, f, indent=1)


if __name__ == '__main__':
    main()
