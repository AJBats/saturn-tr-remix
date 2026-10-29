"""Texture arithmetic the lab's variants share: 15-bit colours to RGB,
reducing a 4-bit texture by blocks, choosing the nearest table colour,
fitting a table of 16 colours (the game's own, tools/texart.py, and the
lab's)."""
import os
import sys

import numpy as np

# after the lab's own folder: tools/lab.py (the render lab) must not hide ours
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from texart import WEIGHTS, block_mean, from_rgb, kmeans_colors, nearest_colors, to_rgb  # noqa: E402,F401


def reduce(texels, colors, clear, fx, fy, choices=None):
    """A texture reduced fx across and fy down: block means, nearest colours
    (no dither); clear blocks keep index 'clear'."""
    rgb, empty = block_mean(texels, colors, clear, fx, fy)
    if choices is None:
        choices = [i for i in range(16) if i != clear]
    out = nearest_colors(rgb, colors, choices)
    if clear is not None:
        out[empty] = clear
    return out


def edge_energy(texels, colors):
    """(across columns, across rows): the luminance steps between
    neighbouring texels along x (vertical edges) and along y (horizontal
    edges)."""
    lum = to_rgb(colors)[texels] @ np.array([0.299, 0.587, 0.114])
    return float(np.abs(np.diff(lum, axis=1)).sum()), float(np.abs(np.diff(lum, axis=0)).sum())
