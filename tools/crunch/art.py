"""Texture changes toward Sonic R's design (texstudy.py: its textures have
TR's colours and contrast, but at 32 x 32, with the contrast in broad shapes
instead of texel-to-texel grain), and art-directed repaint samples.

Each change takes a gamedata.Texture and gives (texels, 16 colours) at any
size; variants.with_textures builds the level from them.

    half      32 x 32: filtered down, a table fitted to the smaller picture
    shapes    edge-preserving smoothing (Kuwahara): the grain inside each
              region flattened, the edges between regions kept
    ramps     the table refitted as one or two ramps of a hue, so
              neighbouring texels differ in shade, not in colour
    relief    the texture's brightness as a height field, smoothed and lit
              from above: broad highlights and shadows
    repaint   per-material recipes for the textures that cover most of the
              ledge, using the same tools by hand: ground and snow repainted
              from their low-frequency layout, rock and stone through
              shapes and relief with their own strengths
"""
import numpy as np
from scipy import ndimage

import textures

LUM = np.array([0.299, 0.587, 0.114])


def image(t):
    """A texture's RGB (0..31 floats) and which texels are solid."""
    texels = t.texels()
    rgb = textures.to_rgb(t.colors)[texels]
    solid = np.ones(texels.shape, bool) if not t.transparent else texels != 0
    return rgb, solid


def gauss(a, sigma):
    """Gaussian blur, wrapping (room textures tile)."""
    if a.ndim == 3:
        return np.stack([ndimage.gaussian_filter(a[..., c], sigma, mode='wrap') for c in range(a.shape[2])], -1)
    return ndimage.gaussian_filter(a, sigma, mode='wrap')


def lum(rgb):
    return rgb @ LUM


def match_contrast(out, ref, solid, k=1.0):
    """out's deviation from its mean scaled so its luminance spread is k of ref's."""
    s0, s1 = lum(ref[solid]).std(), lum(out[solid]).std()
    m = out[solid].mean(axis=0)
    return np.clip(m + (out - m) * (k * s0 / max(s1, 1e-3)), 0, 31)


def kmeans_fit(rgb, solid, transparent, color0):
    """Indices and a 16-colour table fitted to rgb (k-means); colour 0
    stays see-through for transparent textures."""
    first = 1 if transparent else 0
    fit = textures.kmeans_colors(rgb[solid], 16 - first)
    colors = ([color0] if first else []) + [textures.from_rgb(c) for c in fit]
    idx = textures.nearest_colors(rgb, colors, list(range(first, 16)))
    if transparent:
        idx[~solid] = 0
    return idx, colors


def ramp_fit(rgb, solid, transparent, color0, hues=2):
    """Indices and a table of one or two hue ramps fitted to rgb: texels
    split by hue (chroma) into at most 'hues' groups, each group's shades
    spaced over its brightness, each shade's hue its texels' mean, smoothed
    along the ramp."""
    first = 1 if transparent else 0
    n = 16 - first
    lu = lum(rgb)
    chroma = rgb - lu[..., None]
    P, L = chroma[solid], lu[solid]
    centres = P.mean(axis=0, keepdims=True)
    if hues > 1 and len(P) > 16:
        # two hues: start from the halves along the chroma's main axis
        axis = np.linalg.svd(P - P.mean(axis=0), full_matrices=False)[2][0]
        side = (P - P.mean(axis=0)) @ axis > 0
        c = np.array([P[~side].mean(axis=0), P[side].mean(axis=0)]) if side.any() and (~side).any() else \
            np.repeat(centres, 2, axis=0)
        for _ in range(10):
            lab = np.argmin(((P[:, None, :] - c[None]) ** 2).sum(-1), axis=1)
            c = np.array([P[lab == j].mean(axis=0) if (lab == j).any() else c[j] for j in range(2)])
        share = np.bincount(lab, minlength=2) / len(P)
        if np.linalg.norm(c[0] - c[1]) >= 1.2 and share.min() >= 0.12:
            centres = c
    groups = np.argmin(((P[:, None, :] - centres[None]) ** 2).sum(-1), axis=1)
    counts = np.bincount(groups, minlength=len(centres))
    alloc = np.maximum(4, np.round(n * counts / counts.sum()).astype(int)) if len(centres) > 1 else np.array([n])
    while alloc.sum() > n:
        alloc[np.argmax(alloc)] -= 1
    while alloc.sum() < n:
        alloc[np.argmax(counts / alloc)] += 1
    colors = [color0] if first else []
    ramps = []
    for g, k in enumerate(alloc):
        Lg, Pg = L[groups == g], P[groups == g]
        # shades where the texels are, but reaching the rare extremes too
        # (ice, prints): quantiles blended with an even spread
        levels = 0.6 * np.quantile(Lg, (np.arange(k) + 0.5) / k) + 0.4 * np.linspace(Lg.min(), Lg.max(), k)
        edges = (levels[1:] + levels[:-1]) / 2
        bins = np.searchsorted(edges, Lg)
        ch = np.array([Pg[bins == i].mean(axis=0) if (bins == i).any() else Pg.mean(axis=0) for i in range(k)])
        if k > 2:
            ch = np.vstack([ch[:1], (ch[:-2] + 2 * ch[1:-1] + ch[2:]) / 4, ch[-1:]])
        start = len(colors)
        colors += [textures.from_rgb(np.clip(levels[i] + ch[i], 0, 31)) for i in range(k)]
        ramps.append((start, levels))
    # each texel: its hue group by its neighbourhood's hue (texel by texel
    # the groups would alternate in mixed areas: speckle), the nearest shade
    # of that group's ramp
    allP = gauss(chroma, 1.0).reshape(-1, 3)
    allg = np.argmin(((allP[:, None, :] - centres[None]) ** 2).sum(-1), axis=1).reshape(lu.shape)
    idx = np.zeros(lu.shape, np.uint8)
    for g, (start, levels) in enumerate(ramps):
        m = allg == g
        idx[m] = start + np.argmin(np.abs(lu[m][:, None] - levels[None]), axis=1)
    if transparent:
        idx[~solid] = 0
    return idx, colors


def tidy(fit, passes=2):
    """A pixel artist's clean-up of a fitted (texels, colours): texels unlike
    all four neighbours take the colour three or four of them share."""
    idx, colors = fit
    for _ in range(passes):
        t = idx.astype(np.int16)
        p = np.pad(t, 1, mode='wrap')
        n = np.stack([p[:-2, 1:-1], p[2:, 1:-1], p[1:-1, :-2], p[1:-1, 2:]])
        lonely = np.all(n != t, axis=0)
        out = idx.copy()
        for y, x in zip(*np.nonzero(lonely)):
            vals, counts = np.unique(n[:, y, x], return_counts=True)
            if counts.max() >= 3 and not (colors[0] & 0x8000 == 0 and (vals[counts.argmax()] == 0) != (t[y, x] == 0)):
                out[y, x] = vals[counts.argmax()]
        idx = out
    return idx, colors


def downsample(rgb, solid):
    """2 x 2 means over solid texels; a texel solid where most of its block is."""
    h, w = solid.shape
    s = solid.reshape(h // 2, 2, w // 2, 2).sum(axis=(1, 3))
    m = (rgb * solid[..., None]).reshape(h // 2, 2, w // 2, 2, 3).sum(axis=(1, 3)) / np.maximum(s, 1)[..., None]
    return m, s >= 2


def kuwahara(rgb, r=2, passes=2):
    """Each texel the mean of whichever of its four (r+1) x (r+1) corner
    windows varies least: flattens grain inside regions, keeps edges."""
    for _ in range(passes):
        lu = lum(rgb)
        best_v = np.full(lu.shape, np.inf)
        out = rgb.copy()
        for sy in (0, -r):
            for sx in (0, -r):
                shifts = [(sy + dy, sx + dx) for dy in range(r + 1) for dx in range(r + 1)]
                stack = np.stack([np.roll(rgb, (-y, -x), axis=(0, 1)) for y, x in shifts])
                lstack = stack @ LUM
                v = lstack.var(axis=0)
                m = stack.mean(axis=0)
                better = v < best_v
                best_v = np.where(better, v, best_v)
                out = np.where(better[..., None], m, out)
        rgb = out
    return rgb


def shade(height, light=(-0.55, -0.75, 0.65), slope=1.0):
    """Lambert shading of a height field (texel units), centred on 0."""
    gx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) / 2 * slope
    gy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) / 2 * slope
    n = np.stack([-gx, -gy, np.ones_like(gx)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    l = np.array(light) / np.linalg.norm(light)
    s = n @ l
    return s - s.mean()


# --- ideas 1 to 4 -------------------------------------------------------

def half(t):
    rgb, solid = image(t)
    if t.w < 16 or t.h < 2:
        return t.texels(), t.colors
    small, ssolid = downsample(rgb, solid)
    return kmeans_fit(small, ssolid, t.transparent, t.colors[0])


def shapes(t):
    rgb, solid = image(t)
    out = match_contrast(kuwahara(rgb), rgb, solid, 0.9)
    return kmeans_fit(out, solid, t.transparent, t.colors[0])


def ramps(t):
    rgb, solid = image(t)
    return ramp_fit(rgb, solid, t.transparent, t.colors[0])


def relief_rgb(rgb, solid, sigma=1.6, keep_detail=0.2, amount=1.0):
    lu = lum(rgb)
    height = gauss(lu, sigma)
    s = shade(height, slope=1.2)
    albedo = gauss(rgb, sigma * 1.4)
    detail = rgb - gauss(rgb, 0.8)
    out = albedo + amount * 6.0 * s[..., None] + keep_detail * detail
    return match_contrast(out, rgb, solid)


def relief(t):
    rgb, solid = image(t)
    return tidy(ramp_fit(relief_rgb(rgb, solid), solid, t.transparent, t.colors[0]))


# --- repaint samples ----------------------------------------------------
#
# The ledge's textures by screen share (ledge0): 18 ground 35%, 14 snow 10%,
# 10 rock and ice 6%, 38 cut stone 5%, 24 cut stone 5%, 11 rock and icicles
# 5%, 8 icy rock 3%, 12 ground and ice 3%, 9 rock 3%, 13 ground and ice 3%,
# 48 snow and rock 2%, 3 snow and paw prints 2%: 88% of the rooms' pixels.

def soft_noise(shape, seed, sigma):
    rng = np.random.default_rng(seed)
    n = gauss(rng.standard_normal(shape), sigma)
    return n / (n.std() + 1e-9)


def blobs(mask, min_size):
    """mask without its connected parts smaller than min_size texels."""
    lab, n = ndimage.label(mask)
    if n == 0:
        return mask
    sizes = ndimage.sum(mask, lab, range(1, n + 1))
    keep = np.zeros(n + 1, bool)
    keep[1:] = sizes >= min_size
    return keep[lab]


def keep_features(out, rgb, mask, soften=0.6):
    """Large features of the original (ice, prints, cracks) laid back over
    out, flattened inside and soft at their edges."""
    if not mask.any():
        return out
    w = np.clip(gauss(mask.astype(float), soften) * 1.6, 0, 1)[..., None]
    return out * (1 - w) + kuwahara(rgb, 1, 1) * w


def ground(t, seed=1):
    """Sonic R's sand for TR's speckled ground: its colour as broad soft
    patches (its own low-frequency layout, deepened), a gentle relief, the
    blue-grey flecks gathered into a few soft stones; big bright shapes
    (ice) kept."""
    rgb, solid = image(t)
    lu = lum(rgb)
    base = gauss(rgb, 2.5)
    base = match_contrast(base + 1.8 * soft_noise(lu.shape, seed, 3.5)[..., None], rgb, solid, 0.5)
    out = base + 4.5 * shade(gauss(lum(base), 1.5) + 1.2 * soft_noise(lu.shape, seed + 1, 2.5), slope=1.0)[..., None]
    chroma = gauss(rgb, 1.0) - lum(gauss(rgb, 1.0))[..., None]
    blue = chroma[..., 2] - chroma[..., 0]
    stones = blobs(blue > np.percentile(blue, 92), 4)
    if stones.any():
        w = gauss(stones.astype(float), 0.7)[..., None]
        out = out * (1 - 0.8 * w) + (rgb[stones].mean(axis=0) + 1.5) * 0.8 * w
    ice = blobs(gauss(lu, 0.8) > lu.mean() + 2.2 * lu.std(), 10)
    out = keep_features(out, rgb, ice)
    small, ssolid = downsample(np.clip(out, 0, 31), solid)
    return tidy(ramp_fit(small, ssolid, t.transparent, t.colors[0]))


def snow(t, seed=2):
    """Snow as soft drifts: the texture's mean and broad layout, a slow
    relief, no speckle; marks in it (paw prints) and rock kept as soft
    shapes."""
    rgb, solid = image(t)
    lu = lum(rgb)
    marks = blobs(ndimage.binary_closing(gauss(lu, 0.6) < lu.mean() - 2.0 * lu.std()), 5)
    base = gauss(rgb, 3.0)
    base[...] = match_contrast(base, rgb, solid, 0.25)
    h = gauss(lu, 3.0) + 1.5 * soft_noise(lu.shape, seed, 4.0)
    out = base + 2.2 * shade(h, slope=1.5)[..., None]
    out = keep_features(out, rgb, marks, 0.5)
    small, ssolid = downsample(np.clip(out, 0, 31), solid)
    return tidy(ramp_fit(small, ssolid, t.transparent, t.colors[0], hues=1))


def rock(t):
    """Rock and ice: regions flattened (Kuwahara), relit as broad forms
    from their own shapes, bright ice kept crisp, 32 x 32 with ramps."""
    rgb, solid = image(t)
    lu = lum(rgb)
    flat = kuwahara(rgb, r=2, passes=2)
    out = relief_rgb(flat, solid, sigma=0.9, keep_detail=0.1, amount=1.1)
    ice = blobs(gauss(lu, 0.6) > lu.mean() + 1.8 * lu.std(), 6)
    out = keep_features(out, rgb, ice, 0.4)
    small, ssolid = downsample(out, solid)
    return tidy(ramp_fit(small, ssolid, t.transparent, t.colors[0]))


def cut_stone(t):
    """Cut stone: the mortar (long dark lines, locally darker than their
    surroundings) widened into soft seams, each block's face flattened and
    lit as a gently domed slab."""
    rgb, solid = image(t)
    lu = lum(rgb)
    local = lu - gauss(lu, 2.5)
    seams = blobs((local < -1.5) & (gauss(lu, 0.5) < np.percentile(lu, 40)), 12)
    seams = ndimage.binary_dilation(seams, iterations=1)
    tiled = np.tile(seams, (3, 3))
    h, w = lu.shape
    dist = ndimage.distance_transform_edt(~tiled)[h:2 * h, w:2 * w]
    dome = np.minimum(dist, 5.0) / 5.0
    flat = kuwahara(rgb, r=2, passes=3)
    lab, n = ndimage.label(~seams)
    face = flat.copy()
    for i in range(1, n + 1):
        m = lab == i
        if m.sum() >= 6:
            face[m] = 0.5 * flat[m] + 0.5 * flat[m].mean(axis=0)
    mortar = rgb[seams].mean(axis=0) if seams.any() else face.mean(axis=(0, 1))
    out = face + 5.0 * shade(gauss(dome, 0.7), slope=2.0)[..., None]
    sw = gauss(seams.astype(float), 0.5)[..., None]
    out = out * (1 - sw) + (mortar - 1.0) * sw
    out = match_contrast(out, rgb, solid, 0.9)
    small, ssolid = downsample(out, solid)
    return tidy(ramp_fit(small, ssolid, t.transparent, t.colors[0]))


REPAINT = {18: ground, 12: ground, 13: ground, 14: snow, 3: snow, 48: rock,
           10: rock, 11: rock, 8: rock, 9: rock, 38: cut_stone, 24: cut_stone}


def repaint(t):
    fn = REPAINT.get(t.index)
    return fn(t) if fn else (t.texels(), t.colors)
