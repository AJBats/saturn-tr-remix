"""The ideas the crunch lab tries, each a change to one step of the room
drawing (rooms.Options) or to the textures the game packs."""
import copy

import numpy as np

import pack_level
from gamedata import Texture, pack_texels


# --- texture reductions -------------------------------------------------

def block_mean(texels, colors, transparent, fx, fy):
    """Each fx x fy block's mean RGB (0..31) and whether it is see-through
    (most of it see-through, as the game's mips decide)."""
    pal = np.array([[(c >> s) & 31 for s in (0, 5, 10)] for c in colors], np.float64)
    h, w = texels.shape
    rgb = pal[texels]
    opaque = np.ones((h, w), bool) if not transparent else texels != 0
    rgb = (rgb * opaque[..., None]).reshape(h // fy, fy, w // fx, fx, 3).sum(axis=(1, 3))
    n = opaque.reshape(h // fy, fy, w // fx, fx).sum(axis=(1, 3))
    clear = n * 2 < fx * fy
    rgb = rgb / np.maximum(n, 1)[..., None]
    return rgb, clear


def nearest_colors(rgb, colors, clear, transparent):
    """Each RGB to the nearest of colors (the game's weights, colour 0 kept
    for see-through when transparent)."""
    pal = np.array([[(c >> s) & 31 for s in (0, 5, 10)] for c in colors], np.float64)
    first = 1 if transparent else 0
    wgt = np.array([3.0, 4.0, 2.0])
    d = (((rgb[..., None, :] - pal[None, None, first:, :]) ** 2) * wgt).sum(axis=-1)
    out = (np.argmin(d, axis=-1) + first).astype(np.uint8)
    out[clear] = 0
    return out


def mip_nearest(t, factor):
    """A mip level without dither: each block's mean, the nearest colour."""
    rgb, clear = block_mean(t.texels(), t.colors, t.transparent, factor, factor)
    return nearest_colors(rgb, t.colors, clear, t.transparent), t.colors


def kmeans_colors(rgb, k, first, iters=12, seed_colors=None):
    """k colours (15-bit) fitted to the RGB samples, with colour 0 left for
    see-through when first is 1."""
    pts = rgb.reshape(-1, 3)
    if len(pts) == 0:
        return [0x8000] * 16
    uniq = np.unique(np.round(pts), axis=0)
    n = min(k - first, len(uniq))
    if seed_colors is not None and len(seed_colors) >= n:
        centres = np.array(seed_colors[:n], np.float64)
    else:
        idx = np.linspace(0, len(uniq) - 1, n).astype(int)
        centres = uniq[np.argsort(uniq.sum(axis=1))][idx].astype(np.float64)
    wgt = np.array([3.0, 4.0, 2.0])
    for _ in range(iters):
        d = (((pts[:, None, :] - centres[None, :, :]) ** 2) * wgt).sum(axis=-1)
        lab = np.argmin(d, axis=1)
        for j in range(n):
            m = lab == j
            if m.any():
                centres[j] = pts[m].mean(axis=0)
    out = [0x8000] * first
    for c in np.clip(np.round(centres), 0, 31).astype(int):
        out.append(0x8000 | (c[2] << 10) | (c[1] << 5) | c[0])
    while len(out) < 16:
        out.append(out[-1])
    return out


def mip_own_palette(t, factor):
    """A mip level with its own 16 colours fitted to the reduced picture, so
    it reaches its tones without dither."""
    rgb, clear = block_mean(t.texels(), t.colors, t.transparent, factor, factor)
    first = 1 if t.transparent else 0
    colors = kmeans_colors(rgb[~clear], 16, first)
    return nearest_colors(rgb, colors, clear, t.transparent), colors


def reduce_xy(t, fx, fy):
    """A ripmap level: fx across, fy down, means and nearest colours."""
    rgb, clear = block_mean(t.texels(), t.colors, t.transparent, fx, fy)
    return nearest_colors(rgb, t.colors, clear, t.transparent), t.colors


# --- texture art changes ------------------------------------------------

def despeckle(texels, transparent):
    """Texels unlike all four neighbours, whose neighbours mostly agree,
    take the neighbours' colour: the 1-texel features shrinking drops."""
    t = texels.astype(np.int16)
    p = np.pad(t, 1, mode='edge')
    n = np.stack([p[:-2, 1:-1], p[2:, 1:-1], p[1:-1, :-2], p[1:-1, 2:]])
    lonely = np.all(n != t, axis=0)
    out = texels.copy()
    ys, xs = np.nonzero(lonely)
    for y, x in zip(ys, xs):
        vals, counts = np.unique(n[:, y, x], return_counts=True)
        if counts.max() >= 3 and not (transparent and (vals[counts.argmax()] == 0) != (t[y, x] == 0)):
            out[y, x] = vals[counts.argmax()]
    return out


def with_textures(level, fn):
    """A copy of level whose textures fn(Texture) -> (texels, colors) made."""
    new = copy.copy(level)
    new.textures = []
    for t in level.textures:
        texels, colors = fn(t)
        new.textures.append(Texture(t.index, pack_texels(texels), t.w, t.h, list(colors), t.transparent,
                                    pack_level.texture_average(pack_texels(texels), colors, t.transparent)))
    return new


def level_despeckled(level):
    return with_textures(level, lambda t: (despeckle(t.texels(), t.transparent), t.colors))


def level_remastered(level):
    """The game's own remastered colour tables (debug menu Palette)."""
    table = level.table
    new = dict(table.remastered_palettes())
    return with_textures(level, lambda t: (t.texels(), new.get(table.sources[t.index][2], t.colors)))


VARIANTS = {
    # name: (title, what it shows, Options keywords, level change)
    'game': ('The game today', 'Room drawing exactly as the game does it (checked against Mednafen).', {}, None),
    'exact': ('Exact divide', 'Projection with a full-precision divide (the SH-2 divider) instead of divTable[z >> 4].',
              dict(exact_divide=True), None),
    'fullres': ('No mips', 'Every face at full resolution: what the mip levels already do.', dict(lod=0), None),
    'mipnear': ('Mips, no dither', 'Mip levels made without Floyd-Steinberg dither: each block\'s mean, the nearest colour.',
                dict(mip=mip_nearest), None),
    'mippal': ('Mips, own palette', 'Mip levels each with their own 16 colours, so no dither is needed.',
               dict(mip=mip_own_palette), None),
    'ripmap': ('Ripmaps', 'Texture reduced separately along and across VDP1\'s lines, so skipped rows are filtered too.',
               dict(ripmap=True, rip=reduce_xy), None),
    'despeckle': ('Despeckled art', 'Lonely single texels in every room texture replaced by their neighbours\' colour.',
                  {}, level_despeckled),
    'remaster': ('Remastered palettes', 'The game\'s own lifted, desaturated colour tables (debug menu option).',
                 {}, level_remastered),
}
