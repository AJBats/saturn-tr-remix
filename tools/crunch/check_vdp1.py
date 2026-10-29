"""Checks vdp1lib against Mednafen on captured plots (capture.py): draws each
capture's command list onto its frame buffer from before the plot and
compares the result with Mednafen's frame buffer after it, every word of the
512 x 256 buffer.

    check_vdp1.py NAME...       (writes build/crunch/captures/NAME_check.png)
"""
import os
import sys

import numpy as np
from PIL import Image

from medclient import LAB
from vdp1 import Vdp1, rgb


def check(v, name):
    path = lambda kind: os.path.join(LAB, 'captures', f'{name}_{kind}.bin')
    with open(path('vram'), 'rb') as f:
        v.load_vram(f.read())
    with open(path('fb_pre'), 'rb') as f:
        v.load_fb(f.read())
    with open(path('fb_post'), 'rb') as f:
        want = np.frombuffer(f.read(), dtype='>u2').reshape(v.fb.shape).astype(np.uint16)
    totals, _ = v.plot()
    diff = v.fb != want
    print(f'{name}: {totals.commands} commands, {totals.lines} lines, {totals.pixels} pixels, '
          f'{totals.texels} texels, {totals.cycles} cycles; {int(diff.sum())} of {diff.size} words differ')
    Image.fromarray(np.concatenate([rgb(want), rgb(v.fb)], axis=1)).save(path('check').replace('.bin', '.png'))
    return not diff.any()


def main():
    v = Vdp1()
    ok = all([check(v, name) for name in sys.argv[1:]])
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
