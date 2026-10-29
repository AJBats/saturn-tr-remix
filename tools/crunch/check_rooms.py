"""Checks the lab's room drawing (rooms.py) against a captured frame of the
game (capture.py):

1. the view: angles found for the captured gViewRotation give the same
   rotation through matrixSetView, and the same room matrices from
   gCameraViewPos;
2. the commands: the lab's commands for the captured room jobs, decoded
   (capdecode.decode), each found among the game's;
3. the picture: the game's frame with only the commands the lab also made,
   against the lab's frame, drawn by vdp1lib.

    check_rooms.py NAME      (writes build/crunch/captures/NAME_rooms.png)
"""
import os
import struct
import sys
from collections import Counter

import numpy as np
from PIL import Image

from capdecode import Capture, decode, record
from gamedata import SINCOS, Level
from medclient import LAB
from rooms import Options, RoomDrawer, RoomJob, TextureStore, View, build_commands
from vdp1 import Vdp1, rgb


def angle_for(s, c):
    """A 16-bit angle whose table entry is sin s, cos c."""
    for i, sc in enumerate(SINCOS):
        si = int(sc) >> 16
        si = si - 0x10000 if si & 0x8000 else si
        ci = int(sc) & 0xFFFF
        ci = ci - 0x10000 if ci & 0x8000 else ci
        if si == s and ci == c:
            return i << 4
    raise ValueError(f'no angle with sin {s} cos {c}')


def view_of(cap):
    pos, rot = cap.view()
    ax = angle_for(-int(rot[2][1]), int(rot[1][1]))
    ay = angle_for(-int(rot[0][2]), int(rot[0][0]))
    return View(pos, ax, ay, (0, 0, 352, 224)), rot


def main():
    name = sys.argv[1]
    cap = Capture(name)
    level = Level('LEVEL1')
    jobs = cap.jobs()

    view, rot = view_of(cap)
    ok = np.array_equal(view.rot, rot)
    for room_index, R, T, camera, clip in jobs:
        R2, T2 = view.room_matrix(level.rooms[room_index].origin)
        ok &= np.array_equal(R2, R) and np.array_equal(T2, T)
    print(f'view: angles {view.angle_x:#06x} {view.angle_y:#06x}, rotation and room matrices '
          f'{"match" if ok else "DIFFER"}')

    opts = Options()
    drawer = RoomDrawer(level, opts)
    lab_jobs = [RoomJob(level.rooms[r], R, T, camera, clip) for r, R, T, camera, clip in jobs]
    faces, verts = drawer.faces(lab_jobs)
    cl = build_commands(level, faces, verts, opts, TextureStore(level, opts))
    lab_vram = cl.finish()

    game = decode(cap.vram)
    lab = decode(lab_vram)
    have = Counter(game)
    missing = []
    for r in lab:
        if have[r] > 0:
            have[r] -= 1
        else:
            missing.append(r)
    print(f'commands: lab {len(lab)} ({Counter(r[0] for r in lab)}), game {len(game)}; '
          f'{len(lab) - len(missing)} of the lab\'s found in the game\'s, {len(missing)} not')
    for r in missing[:12]:
        print('   lab only:', r[:6], r[7:])

    # the game's list with what the lab doesn't make skipped, drawn on black
    # (the game's list runs straight through, without jumps)
    keep = Counter(lab)
    out = bytearray(cap.vram)
    addr = 0
    while addr < 0x80000:
        w = struct.unpack_from('>16H', out, addr)
        if w[0] & 0x8000:
            break
        if not (w[0] & 0x4000) and (w[0] & 0xF) < 8:
            r = record(out, w, addr)
            if keep[r] > 0:
                keep[r] -= 1
            else:
                out[addr:addr + 2] = (w[0] | 0x4000).to_bytes(2, 'big')
        addr += 32
    v = Vdp1()
    v.load_vram(bytes(out))
    v.fb[:] = 0
    v.plot()
    game_img = rgb(v.fb).copy()
    v.load_vram(lab_vram)
    v.fb[:] = 0
    totals, _ = v.plot()
    lab_img = rgb(v.fb).copy()
    diff = np.any(game_img != lab_img, axis=2)
    print(f'picture: {int(diff.sum())} of {diff.size} pixels differ; lab VDP1 {totals.cycles} cycles')
    img = np.concatenate([game_img, lab_img, np.where(diff[..., None], [255, 0, 255], lab_img // 3).astype(np.uint8)],
                         axis=1)
    Image.fromarray(img).save(os.path.join(LAB, 'captures', f'{name}_rooms.png'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
