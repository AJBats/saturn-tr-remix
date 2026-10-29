"""Texture art for the remaster, shared by the packer (pack_level.py) and the
crunch lab (tools/crunch), so the game draws what the lab measured.

A texture here is its 4-bit texels (an h x w array of colour indices) with
its 16 colours (15-bit, VDP1's), and whether colour 0 is see-through.

    bands_reweighted    the texel-to-texel grain cut, the 2-6 texel detail
                        (cracks, lumps) lifted, the contrast kept: the noise
                        VDP1's skipped rows and columns turn to sparkle goes
                        down while the surface still reads
    own_palette         a texture reduced by blocks, in 16 colours fitted to
                        the reduced picture instead of dithered into the full
                        texture's: mip levels without dither's speckle

Filters see past a texture's edge as its mirror image: most of TR's room
textures don't tile seamlessly, so wrapping would draw each seam as a ridge
along the tile's edge. Changing what any function here draws changes the
game's textures and the lab's (bump the lab's art.ART_VERSION with it).
"""
import numpy as np
from scipy import ndimage

WEIGHTS = np.array([3.0, 4.0, 2.0])     # colour distance: red, green, blue (mip_texture's)
LUM = np.array([0.299, 0.587, 0.114])

# bands: the blurs splitting a texture into octaves of detail, finest first
BAND_SIGMAS = (0.7, 1.4, 2.8, 5.6)
# bands_reweighted's gains for them and for what is left below the last
BAND_GAINS = (0.35, 1.25, 1.25, 1, 1)


# --- 4-bit texels -------------------------------------------------------

def texels_of(pixels, w, h):
    """w x h 4-bit pixels (a row of bytes per line, the left texel high) as
    an h x w array of colour indices."""
    b = np.frombuffer(pixels, dtype=np.uint8).reshape(h, w // 2)
    out = np.empty((h, w), dtype=np.uint8)
    out[:, 0::2] = b >> 4
    out[:, 1::2] = b & 15
    return out


def pack_texels(t):
    """h x w colour indices back to 4-bit rows."""
    return (((t[:, 0::2] & 15) << 4) | (t[:, 1::2] & 15)).astype(np.uint8).tobytes()


# --- colours --------------------------------------------------------------

def to_rgb(colors):
    """15-bit BGR colours to an n x 3 array of 0..31 channels (r, g, b)."""
    return np.array([[(c >> s) & 31 for s in (0, 5, 10)] for c in colors], np.float64)


def from_rgb(rgb, msb=0x8000):
    c = np.clip(np.round(rgb), 0, 31).astype(int)
    return msb | (int(c[2]) << 10) | (int(c[1]) << 5) | int(c[0])


def lum(rgb):
    return rgb @ LUM


def image(texels, colors, transparent):
    """A texture's RGB (0..31 floats) and which texels are solid."""
    rgb = to_rgb(colors)[texels]
    solid = np.ones(texels.shape, bool) if not transparent else texels != 0
    return rgb, solid


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


def kmeans_colors(rgb, slots, iters=12):
    """'slots' colours (RGB 0..31 rows) fitted to the samples by k-means,
    started from the samples spread by brightness (the same every run)."""
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


def kmeans_fit(rgb, solid, transparent, color0):
    """Indices and a 16-colour table fitted to rgb (k-means); colour 0
    stays see-through for transparent textures."""
    first = 1 if transparent else 0
    fit = kmeans_colors(rgb[solid], 16 - first)
    colors = ([color0] if first else []) + [from_rgb(c) for c in fit]
    idx = nearest_colors(rgb, colors, list(range(first, 16)))
    if transparent:
        idx[~solid] = 0
    return idx, colors


def match_contrast(out, ref, solid, k=1.0):
    """out's deviation from its mean scaled so its luminance spread is k of ref's."""
    s0, s1 = lum(ref[solid]).std(), lum(out[solid]).std()
    m = out[solid].mean(axis=0)
    return np.clip(m + (out - m) * (k * s0 / max(s1, 1e-3)), 0, 31)


def tidy(fit, passes=2):
    """A pixel artist's clean-up of a fitted (texels, colours): texels unlike
    all four neighbours take the colour three or four of them share."""
    idx, colors = fit
    for _ in range(passes):
        t = idx.astype(np.int16)
        p = np.pad(t, 1, mode='edge')
        n = np.stack([p[:-2, 1:-1], p[2:, 1:-1], p[1:-1, :-2], p[1:-1, 2:]])
        lonely = np.all(n != t, axis=0)
        out = idx.copy()
        for y, x in zip(*np.nonzero(lonely)):
            vals, counts = np.unique(n[:, y, x], return_counts=True)
            if counts.max() >= 3 and not (colors[0] & 0x8000 == 0 and (vals[counts.argmax()] == 0) != (t[y, x] == 0)):
                out[y, x] = vals[counts.argmax()]
        idx = out
    return idx, colors


# --- filters ----------------------------------------------------------------

def gauss(a, sigma):
    """Gaussian blur, mirrored at the edges."""
    if a.ndim == 3:
        return np.stack([ndimage.gaussian_filter(a[..., c], sigma, mode='reflect') for c in range(a.shape[2])], -1)
    return ndimage.gaussian_filter(a, sigma, mode='reflect')


def shifted(a, dy, dx):
    """a moved by (dy, dx), mirrored at the edges (np.roll without the wrap)."""
    h, w = a.shape[:2]
    pad = ((abs(dy), abs(dy)), (abs(dx), abs(dx))) + ((0, 0),) * (a.ndim - 2)
    p = np.pad(a, pad, mode='reflect')
    return p[abs(dy) - dy:abs(dy) - dy + h, abs(dx) - dx:abs(dx) - dx + w]


def bands(a, sigmas=BAND_SIGMAS):
    """a (h x w, or h x w x 3) as octave bands, finest first, then what is
    left below the last."""
    levels = [a] + [gauss(a, s) for s in sigmas]
    return [levels[i] - levels[i + 1] for i in range(len(sigmas))] + [levels[-1]]


def reweight(rgb, gains):
    """The texture with each band scaled by its gain (finest first)."""
    return sum(g * b for g, b in zip(gains, bands(rgb)))


# --- the game's -------------------------------------------------------------

def bands_reweighted(texels, colors, transparent):
    """(texels, colours): the finest band cut to 35%, the two middle bands
    lifted by a quarter, the texture's contrast restored, a 16-colour table
    fitted, lonely texels tidied."""
    rgb, solid = image(texels, colors, transparent)
    out = match_contrast(reweight(rgb, BAND_GAINS), rgb, solid)
    return tidy(kmeans_fit(out, solid, transparent, colors[0]))


def own_palette(texels, colors, transparent, fx, fy):
    """(texels, colours): the texture reduced fx across and fy down, each
    texel its block's mean, in 16 colours fitted to the reduced picture
    (colour 0 kept see-through: a texel is when most of its block is)."""
    clear = 0 if transparent else None
    rgb, empty = block_mean(texels, colors, clear, fx, fy)
    first = 1 if transparent else 0
    fit = kmeans_colors(rgb[~empty], 16 - first)
    table = ([colors[0]] if first else []) + [from_rgb(c) for c in fit]
    out = nearest_colors(rgb, table, list(range(first, 16)))
    if transparent:
        out[empty] = 0
    return out, table
