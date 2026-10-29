"""Checks the lab's bridge meshes (meshes.py) against the frame capture
they come from (build/captures/ledge): for its view, the lab's mesh
commands, decoded (capdecode.decode), each found among the game's.

    check_meshes.py
"""
import os
import sys
from collections import Counter

from capdecode import decode
from gamedata import Level
from meshes import FRAME, Bridge
from rooms import Options, TextureStore, Vertices, build_commands


def main():
    bridge = Bridge()
    view = bridge.view
    level = Level('LEVEL1')
    opts = Options()
    faces, verts = [], Vertices()
    bridge.add(view, faces, verts)
    lab = decode(build_commands(level, faces, verts, opts, TextureStore(level, opts)).finish())
    with open(os.path.join(FRAME, 'vdp1.bin'), 'rb') as f:
        game = decode(f.read())
    have = Counter(game)
    missing = []
    for r in lab:
        if have[r] > 0:
            have[r] -= 1
        else:
            missing.append(r)
    print(f'{len(bridge.draws)} draws, {len(faces)} faces: {len(lab) - len(missing)} of the lab\'s {len(lab)} '
          f'commands found among the game\'s {len(game)}')
    for r in missing[:8]:
        print('   lab only:', r[:6], r[7:])
    return 0 if not missing else 1


if __name__ == '__main__':
    sys.exit(main())
