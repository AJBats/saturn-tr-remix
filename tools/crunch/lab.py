"""The crunch lab: draws a camera move through captured rooms with each idea
(variants.py), exactly as VDP1 would (Mednafen's drawing code), beside a
filtered reference, and writes a page to compare them.

    lab.py [--capture ledge0] [--path dolly|creep|pan|strafe|still]
           [--frames 60] [--variants game,exact,...]

Output: build/crunch/runs/<capture>-<path>/index.html (open it in a browser),
with each variant's frames, flicker and error pictures as sprite sheets.

Setting up (once, or after the game's renderer changes):
    wsl -d Ubuntu -- sh tools/crunch/build_vdp1lib.sh   the VDP1 library
    capture.py NAME           a frame of the game in a private Mednafen
    check_vdp1.py NAME        vdp1lib draws its list as Mednafen did
    check_rooms.py NAME       rooms.py builds the game's room commands

Measures, per variant and frame:
  vdp1_ms   VDP1's drawing time (calibrated costs, the rooms alone)
  flicker   mean |change of the frame - change of the reference| (0..255
            luminance): what moves on screen that the scene doesn't
  error     mean |frame - reference|: everything that differs, still or not
  corner    99th percentile of the on-screen room corners' distance (px)
            from where an exact projection puts them (whole pixels alone:
            up to 1.41)
  wobble    95th percentile of the frame-to-frame change (px) of that
            distance: corners hopping (whole pixels alone: about 1 px)
"""
import argparse
import json
import os
import time

import numpy as np
from PIL import Image

from capdecode import Capture
from check_rooms import view_of
from gamedata import Level
from medclient import LAB
from ref import Reference
from rooms import Options, RoomDrawer, RoomJob, TextureStore, View, build_commands
from variants import VARIANTS
from vdp1 import Vdp1, rgb

VDP1_HZ = 28636364      # the 352-wide display's clock, VDP1's cycle counter's
FULL = (0, 0, 352, 224)


def forward(view):
    return view.rot[2].astype(np.float64) / 16384.0


def right(view):
    return view.rot[0].astype(np.float64) / 16384.0


PATHS = {
    'dolly': ('Forward 12 units a frame (about Lara\'s walk)',
              lambda v, i: View(np.round(np.array(v.pos) + forward(v) * 12 * i), v.angle_x, v.angle_y, FULL)),
    'creep': ('Forward 3 units a frame (a camera settling)',
              lambda v, i: View(np.round(np.array(v.pos) + forward(v) * 3 * i), v.angle_x, v.angle_y, FULL)),
    'pan': ('Turning 24 angle units (0.13 degrees) a frame',
            lambda v, i: View(v.pos, v.angle_x, (v.angle_y + 24 * i) & 0xFFFF, FULL)),
    'strafe': ('Sideways 8 units a frame',
               lambda v, i: View(np.round(np.array(v.pos) + right(v) * 8 * i), v.angle_x, v.angle_y, FULL)),
    'still': ('Not moving (flicker should read 0)', lambda v, i: View(v.pos, v.angle_x, v.angle_y, FULL)),
}


def luminance(img):
    return img.astype(np.float32) @ np.array([0.299, 0.587, 0.114], np.float32)


def heat(values, scale):
    """0.. -> black, magenta, yellow, white."""
    t = np.clip(values / scale, 0, 1)
    r = np.clip(t * 3, 0, 1)
    g = np.clip(t * 3 - 1, 0, 1)
    b = np.clip(np.where(t < 1 / 3, t * 3, 2 - t * 3), 0, 1) * 0.8 + np.clip(t * 3 - 2, 0, 1) * 0.2
    return (np.stack([r, g, b], axis=-1) * 255).astype(np.uint8)


def vertex_errors(drawer, jobs, verts):
    """(room, vertex) -> (dx, dy): each on-frame room vertex's screen
    position less its exact projection (pixel x covers x .. x + 1)."""
    x, y, z, g, clip = verts.arrays()
    out = {}
    for job in jobs:
        base = drawer.room_vertices.get(job.room.index)
        if base is None:
            continue
        v4 = job.room.vertices.astype(np.float64) * 4
        view = v4 @ job.R.T.astype(np.float64) + job.T.astype(np.float64)
        n = len(v4)
        ex = 176 + 256.0 * view[:, 0] / view[:, 2]
        ey = 112 + 256.0 * view[:, 1] / view[:, 2]
        ok = (clip[base:base + n] & 0x21) == 0
        for i in np.nonzero(ok)[0]:
            out[(job.room.index, int(i))] = (x[base + i] - ex[i], y[base + i] - ey[i])
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--capture', default='ledge0')
    ap.add_argument('--path', default='dolly', choices=sorted(PATHS))
    ap.add_argument('--frames', type=int, default=60)
    ap.add_argument('--variants', default=','.join(VARIANTS))
    args = ap.parse_args()

    cap = Capture(args.capture)
    base_level = Level('LEVEL1')
    view0, _ = view_of(cap)
    room_order = [r for r, *_ in cap.jobs()]
    path_title, path_fn = PATHS[args.path]
    views = [path_fn(view0, i) for i in range(args.frames)]
    names = args.variants.split(',')

    run = f'{args.capture}-{args.path}'
    out = os.path.join(LAB, 'runs', run)
    os.makedirs(out, exist_ok=True)

    def jobs_for(level, view):
        return [RoomJob.from_view(view, level.rooms[r], FULL) for r in room_order]

    t0 = time.time()
    view_list = [dict(pos=list(map(int, v.pos)), ax=v.angle_x, ay=v.angle_y) for v in views]
    refs = None
    try:
        with open(os.path.join(out, 'run.json')) as f:
            if json.load(f)['views'] == view_list:
                sheet = np.array(Image.open(os.path.join(out, 'ref.png')))
                refs = [sheet[i * 224:(i + 1) * 224] for i in range(len(views))]
    except (OSError, KeyError, ValueError):
        pass
    if refs is None:
        reference = Reference(base_level)
        refs = [reference.render(jobs_for(base_level, v)) for v in views]
    print(f'reference: {time.time() - t0:.1f} s')
    ref_l = [luminance(r) for r in refs]

    vdp1 = Vdp1()
    results = {}
    sheets = {'ref': refs}
    for name in names:
        title, about, kw, change = VARIANTS[name]
        level = change(base_level) if change else base_level
        opts = Options(**kw)
        drawer = RoomDrawer(level, opts)
        store = TextureStore(level, opts)
        frames, ms, flicker, error, wobble, corner = [], [], [], [], [], []
        last_err = None
        flick_imgs, err_imgs = [], []
        t1 = time.time()
        for i, view in enumerate(views):
            jobs = jobs_for(level, view)
            faces, verts = drawer.faces(jobs)
            errs = vertex_errors(drawer, jobs, verts)
            corner.append(float(np.percentile([np.hypot(*e) for e in errs.values()], 99)))
            both = [k for k in errs if last_err and k in last_err]
            wobble.append(float(np.percentile([np.hypot(errs[k][0] - last_err[k][0], errs[k][1] - last_err[k][1])
                                               for k in both], 95)) if both else None)
            last_err = errs
            vram = build_commands(level, faces, verts, opts, store).finish()
            vdp1.load_vram(vram)
            vdp1.fb[:] = 0
            totals, _ = vdp1.plot()
            img = rgb(vdp1.fb).copy()
            frames.append(img)
            ms.append(totals.cycles * 1000.0 / VDP1_HZ)
            lum = luminance(img)
            e = np.abs(lum - ref_l[i])
            error.append(float(e.mean()))
            err_imgs.append(heat(e, 96))
            if i == 0:
                f = np.zeros_like(lum)
                flicker.append(None)
            else:
                f = np.abs((lum - luminance(frames[i - 1])) - (ref_l[i] - ref_l[i - 1]))
                flicker.append(float(f.mean()))
            flick_imgs.append(heat(f, 64))
        sheets[name] = frames
        sheets[name + '.flicker'] = flick_imgs
        sheets[name + '.error'] = err_imgs
        results[name] = dict(title=title, about=about, vdp1_ms=ms, flicker=flicker, error=error, wobble=wobble, corner=corner)
        fl = [x for x in flicker if x is not None]
        wb = [x for x in wobble if x is not None]
        print(f'{name:10s} {time.time() - t1:5.1f} s  vdp1 {np.mean(ms):5.2f} ms (max {max(ms):5.2f})  '
              f'flicker {np.mean(fl) if fl else 0:5.2f}  error {np.mean(error):5.2f}  corner {np.mean(corner):4.2f} px  wobble {np.mean(wb) if wb else 0:5.3f} px')

    for key, frames in sheets.items():
        Image.fromarray(np.concatenate(frames, axis=0)).save(os.path.join(out, f'{key}.png'), optimize=True)

    meta = dict(run=run, capture=args.capture, path=args.path, path_title=path_title, frames=args.frames,
                width=352, height=224, variants=names, results=results,
                views=view_list)
    with open(os.path.join(out, 'run.json'), 'w') as f:
        json.dump(meta, f)
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'viewer.html'), encoding='utf-8') as f:
        page = f.read()
    with open(os.path.join(out, 'index.html'), 'w', encoding='utf-8') as f:
        f.write(page.replace('/*RUN_JSON*/null', json.dumps(meta)))
    print(os.path.join(out, 'index.html'))


if __name__ == '__main__':
    main()
