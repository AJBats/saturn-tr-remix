"""Texture arithmetic the lab's variants share: 15-bit colours to RGB,
reducing a 4-bit texture by blocks, choosing the nearest table colour,
fitting a table of 16 colours."""
import numpy as np

WEIGHTS = np.array([3.0, 4.0, 2.0])     # pack_level.mip_texture's: red, green, blue


def to_rgb(colors):
    """15-bit BGR colours to an n x 3 array of 0..31 channels (r, g, b)."""
    return np.array([[(c >> s) & 31 for s in (0, 5, 10)] for c in colors], np.float64)


def from_rgb(rgb, msb=0x8000):
    c = np.clip(np.round(rgb), 0, 31).astype(int)
    return msb | (int(c[2]) << 10) | (int(c[1]) << 5) | int(c[0])


def block_mean(texels, colors, clear, fx, fy):
    """Each fx x fy block's mean RGB over the texels that aren't index
    'clear' (None: every texel counts), and whether the block is mostly
    clear (then it stays clear, as the game's mips decide)."""
    pal = to_rgb(colors)
    h, w = texels.shape
    solid = np.ones((h, w), bool) if clear is None else texels != clear
    rgb = (pal[texels] * solid[..., None]).reshape(h // fy, fy, w // fx, fx, 3).sum(axis=(1, 3))
    n = solid.reshape(h // fy, fy, w // fx, fx).sum(axis=(1, 3))
    return rgb / np.maximum(n, 1)[..., None], n * 2 < fx * fy


def nearest_colors(rgb, colors, choices):
    """Each RGB to the nearest of colors[choices], as indices into colors."""
    choices = np.asarray(choices)
    pal = to_rgb(colors)[choices]
    d = (((rgb[..., None, :] - pal) ** 2) * WEIGHTS).sum(axis=-1)
    return choices[np.argmin(d, axis=-1)].astype(np.uint8)


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


def kmeans_colors(rgb, slots, iters=12):
    """'slots' colours (RGB 0..31 rows) fitted to the samples by k-means."""
    pts = rgb.reshape(-1, 3)
    if len(pts) == 0:
        return np.zeros((slots, 3))
    uniq = np.unique(np.round(pts), axis=0)
    n = min(slots, len(uniq))
    order = np.argsort((uniq * WEIGHTS).sum(axis=1))
    centres = uniq[order][np.linspace(0, len(uniq) - 1, n).astype(int)].astype(np.float64)
    for _ in range(iters):
        d = (((pts[:, None, :] - centres[None, :, :]) ** 2) * WEIGHTS).sum(axis=-1)
        lab = np.argmin(d, axis=1)
        for j in range(n):
            m = lab == j
            if m.any():
                centres[j] = pts[m].mean(axis=0)
    if n < slots:
        centres = np.concatenate([centres, np.repeat(centres[-1:], slots - n, axis=0)])
    return centres


def edge_energy(texels, colors):
    """(across columns, across rows): the luminance steps between
    neighbouring texels along x (vertical edges) and along y (horizontal
    edges)."""
    lum = to_rgb(colors)[texels] @ np.array([0.299, 0.587, 0.114])
    return float(np.abs(np.diff(lum, axis=1)).sum()), float(np.abs(np.diff(lum, axis=0)).sum())
