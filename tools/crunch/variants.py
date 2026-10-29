"""The ideas the crunch lab tries. Each is an ingredient: a change to one
step of the room drawing (rooms.Options) and/or to the level's textures or
geometry before drawing. Ingredients combine: 'ripmap+exact' draws with
both (compose)."""
import copy

import numpy as np

import gamedata  # noqa: F401 (puts tools/ on the path for pack_level)
import art
import pack_level
import textures
from gamedata import Room, Texture, pack_texels


def clear_index(t):
    return 0 if t.transparent else None


# --- texture reductions -------------------------------------------------

def mip_nearest(t, factor):
    """A mip level without dither: each block's mean, the nearest colour."""
    return textures.reduce(t.texels(), t.colors, clear_index(t), factor, factor), t.colors


def own_palette(t, fx, fy):
    """t reduced fx x fy with its own 16 colours fitted to the reduced
    picture, so it reaches its tones without dither."""
    clear = clear_index(t)
    rgb, empty = textures.block_mean(t.texels(), t.colors, clear, fx, fy)
    first = 1 if clear is not None else 0
    fit = textures.kmeans_colors(rgb[~empty], 16 - first)
    colors = ([t.colors[0]] if first else []) + [textures.from_rgb(c) for c in fit]
    out = textures.nearest_colors(rgb, colors, list(range(first, 16)))
    if clear is not None:
        out[empty] = 0
    return out, colors


def mip_own_palette(t, factor):
    return own_palette(t, factor, factor)


def reduce_xy(t, fx, fy):
    """A ripmap level: fx across, fy down, means and nearest colours."""
    return textures.reduce(t.texels(), t.colors, clear_index(t), fx, fy), t.colors


# --- art changes: texels, colour tables ---------------------------------

def despeckle(t):
    """Texels unlike all four neighbours, whose neighbours mostly agree,
    take the neighbours' colour: the 1-texel spots shrinking drops."""
    texels = t.texels()
    v = texels.astype(np.int16)
    p = np.pad(v, 1, mode='edge')
    n = np.stack([p[:-2, 1:-1], p[2:, 1:-1], p[1:-1, :-2], p[1:-1, 2:]])
    out = texels.copy()
    for y, x in zip(*np.nonzero(np.all(n != v, axis=0))):
        vals, counts = np.unique(n[:, y, x], return_counts=True)
        best = vals[counts.argmax()]
        if counts.max() >= 3 and not (t.transparent and (best == 0) != (v[y, x] == 0)):
            out[y, x] = best
    return out, t.colors


def soften_thin(t):
    """1-texel lines (a texel standing out from two neighbours on opposite
    sides that are alike) drawn halfway to those neighbours: kept, at half
    their contrast, so they blink less when rows or columns are skipped."""
    texels = t.texels()
    rgb = textures.to_rgb(t.colors)[texels]
    lum = rgb @ np.array([0.299, 0.587, 0.114])
    p = np.pad(lum, 1, mode='edge')
    pr = np.pad(rgb, ((1, 1), (1, 1), (0, 0)), mode='edge')
    out_rgb = rgb.copy()
    for (a, b) in (((1, 0), (1, 2)), ((0, 1), (2, 1))):     # left/right, then up/down
        la = p[a[0]:a[0] + lum.shape[0], a[1]:a[1] + lum.shape[1]]
        lb = p[b[0]:b[0] + lum.shape[0], b[1]:b[1] + lum.shape[1]]
        stand = np.abs(lum - (la + lb) / 2)
        thin = (stand > 2.0) & (np.abs(la - lb) < stand * 0.5) & (np.sign(lum - la) == np.sign(lum - lb))
        ra = pr[a[0]:a[0] + lum.shape[0], a[1]:a[1] + lum.shape[1]]
        rb = pr[b[0]:b[0] + lum.shape[0], b[1]:b[1] + lum.shape[1]]
        out_rgb[thin] = (rgb[thin] + (ra[thin] + rb[thin]) / 2) / 2
    choices = [i for i in range(16) if not (t.transparent and i == 0)]
    out = textures.nearest_colors(out_rgb, t.colors, choices)
    if t.transparent:
        out[texels == 0] = 0
    return out, t.colors


def clump_noise(t):
    """The texture's fine detail (it less its 3 x 3 blur) gathered into 2 x 2
    clumps: the same grain up close, half the frequency to alias."""
    texels = t.texels()
    rgb = textures.to_rgb(t.colors)[texels]
    p = np.pad(rgb, ((1, 1), (1, 1), (0, 0)), mode='wrap')
    h, w = texels.shape
    low = sum(p[dy:dy + h, dx:dx + w] for dy in range(3) for dx in range(3)) / 9.0
    detail = rgb - low
    if h % 2 == 0 and w % 2 == 0:
        detail = detail.reshape(h // 2, 2, w // 2, 2, 3).mean(axis=(1, 3)).repeat(2, axis=0).repeat(2, axis=1)
    choices = [i for i in range(16) if not (t.transparent and i == 0)]
    out = textures.nearest_colors(low + detail * 1.3, t.colors, choices)
    if t.transparent:
        out[texels == 0] = 0
    return out, t.colors


def flatter(t):
    """The colour table at 3/4 of its contrast about the texture's mean."""
    m = textures.to_rgb([t.mean])[0]
    return t.texels(), [textures.from_rgb(m + (c - m) * 0.75, t.colors[i] & 0x8000)
                        for i, c in enumerate(textures.to_rgb(t.colors))]


def degrain(t, keep=0.3):
    """Sonic R's kind of texture from TR's: the texel-to-texel detail (above
    a 3 x 3 blur) cut to 'keep' of itself, the broad shapes stretched back to
    the texture's contrast, then a 16-colour table fitted to the result (the
    old table's in-between tones are missing, and nearest picks from it would
    put the grain back)."""
    texels = t.texels()
    rgb = textures.to_rgb(t.colors)[texels]
    solid = np.ones(texels.shape, bool) if not t.transparent else texels != 0
    p = np.pad(rgb, ((1, 1), (1, 1), (0, 0)), mode='wrap')
    h, w = texels.shape
    low = sum(p[dy:dy + h, dx:dx + w] for dy in range(3) for dx in range(3)) / 9.0
    mean = rgb[solid].mean(axis=0)
    out = low + keep * (rgb - low)
    lum = lambda a: a @ np.array([0.299, 0.587, 0.114])
    s0, s1 = lum(rgb[solid]).std(), lum(out[solid]).std()
    if s1 > 0.1:
        out = mean + (out - mean) * (s0 / s1)
    out = np.clip(out, 0, 31)
    first = 1 if t.transparent else 0
    fit = textures.kmeans_colors(out[solid], 16 - first)
    colors = ([t.colors[0]] if first else []) + [textures.from_rgb(c) for c in fit]
    idx = textures.nearest_colors(out, colors, list(range(first, 16)))
    if t.transparent:
        idx[~solid] = 0
    return idx, colors


def with_textures(level, fn):
    """A copy of level whose textures fn(Texture) -> (texels, colors) made (at
    any size: a face draws its texture whole, whatever its size)."""
    new = copy.copy(level)
    new.textures = []
    for t in level.textures:
        texels, colors = fn(t)
        data = pack_texels(texels)
        h, w = texels.shape
        new.textures.append(Texture(t.index, data, w, h, list(colors), t.transparent,
                                    pack_level.texture_average(data, colors, t.transparent), t.end_codes))
    return new


def level_remastered(level):
    """The game's own remastered colour tables (debug menu Palette)."""
    table = level.table
    new = dict(table.remastered_palettes())
    return with_textures(level, lambda t: (t.texels(), new.get(table.sources[t.index][2], t.colors)))


# --- geometry -----------------------------------------------------------

def level_tessellated(level, min_edge=1024):
    """Every room quad with an edge of min_edge world units or more split
    2 x 2, once, offline: each quarter drawn with its quadrant of the texture
    (as the game's near split), midpoints shared by neighbouring quads, their
    shades their corners' means."""
    new = copy.copy(level)
    new.textures = list(level.textures)
    quadrant_index = {}

    def quadrant(tex, q):
        if (tex, q) not in quadrant_index:
            t = level.textures[tex]
            hw, hh = t.w // 2, t.h // 2
            oy, ox = ((0, 0), (0, hw), (hh, hw), (hh, 0))[q]
            data = pack_texels(t.texels()[oy:oy + hh, ox:ox + hw])
            quadrant_index[(tex, q)] = len(new.textures)
            new.textures.append(Texture(len(new.textures), data, hw, hh, t.colors, t.transparent,
                                        pack_level.texture_average(data, t.colors, t.transparent)))
        return quadrant_index[(tex, q)]

    new.rooms = []
    for room in level.rooms:
        pts = [tuple(int(c) for c in p) for p in room.points]
        shades = [int(v) for v in room.shades]
        at = {p: i for i, p in enumerate(pts)}

        def vertex(p, shade):
            if p not in at:
                at[p] = len(pts)
                pts.append(p)
                shades.append(shade)
            return at[p]

        quads = []
        for q in room.quads:
            a, b, c, d, tex, transp = q
            t = level.textures[tex]
            P = [np.array(pts[i]) for i in (a, b, c, d)]
            longest = max(np.abs(P[i] - P[(i + 1) % 4]).max() for i in range(4)) * 64
            if longest < min_edge or (t.w // 2) % 8:
                quads.append(q)
                continue
            sh = [shades[i] for i in (a, b, c, d)]
            mid = lambda i, j: vertex(tuple((P[i] + P[j]) // 2), (sh[i] + sh[j]) >> 1)
            m01, m12, m23, m30 = mid(0, 1), mid(1, 2), mid(2, 3), mid(3, 0)
            mc = vertex(tuple(sum(P) // 4), sum(sh) >> 2)
            quads += [(a, m01, mc, m30, quadrant(tex, 0), transp), (m01, b, m12, mc, quadrant(tex, 1), transp),
                      (mc, m12, c, m23, quadrant(tex, 2), transp), (m30, mc, m23, d, quadrant(tex, 3), transp)]
        quads, _ = pack_level.sort_room_quads(pts, quads)
        new.rooms.append(Room(room.index, room.origin, np.array(pts, dtype=np.int32), np.array(shades, dtype=np.int32),
                              quads, room.triangles, room.water))
    return new


# --- the ingredients ----------------------------------------------------
#
# stage orders the level changes when ingredients combine: art first, then
# colour tables, then geometry (whose quadrant textures are cut from the art).

INGREDIENTS = {
    'game': dict(title='The game today', group='baseline',
                 about='Room drawing exactly as the game does it (checked against Mednafen).'),
    'exact': dict(title='Exact divide', group='projection', set=dict(divide='exact'),
                  about='Projection with a full divide (the SH-2 divider, as the 1996 game) instead of divTable[z >> 4].'),
    'fine': dict(title='Finer divide table', group='projection', set=dict(divide='fine'),
                 about='Two 16-bit tables, no divider: z under 4096 by z >> 2, beyond it 16 times the '
                       'precision of divTable (products shifted 4 more). 4.5 KB more.'),
    'fullres': dict(title='No mips', group='textures', set=dict(lod=0),
                    about='Every face at full resolution: what the mip levels already do.'),
    'mipnear': dict(title='Mips, no dither', group='textures', set=dict(mip=mip_nearest),
                    about='Mip levels without Floyd-Steinberg dither: each block\'s mean, the nearest colour.'),
    'mippal': dict(title='Own palettes', group='textures', set=dict(mip=mip_own_palette, rip=own_palette),
                   about='Each reduced level (mip or ripmap) with its own 16 colours, so no dither is needed.'),
    'ripmap': dict(title='Ripmaps', group='textures', set=dict(ripmap=True), default=dict(rip=reduce_xy),
                   about='Texture reduced separately along and across VDP1\'s lines, so skipped rows are filtered too.'),
    'despeckle': dict(title='Despeckled art', group='art', stage=0, level=lambda lv: with_textures(lv, despeckle),
                      about='Lonely single texels in every room texture take their neighbours\' colour.'),
    'softthin': dict(title='Softened thin lines', group='art', stage=0, level=lambda lv: with_textures(lv, soften_thin),
                     about='1-texel lines and spots drawn halfway to their surroundings: kept, at half contrast.'),
    'clump': dict(title='Clumped noise', group='art', stage=0, level=lambda lv: with_textures(lv, clump_noise),
                  about='Each texture\'s fine grain regathered in 2 x 2 clumps (detail above a 3 x 3 blur).'),
    'degrain': dict(title='Degrained art (Sonic R grain)', group='art', stage=0,
                    level=lambda lv: with_textures(lv, degrain),
                    about='Texel-to-texel detail cut to 30%, the broad shapes stretched back to the same contrast, '
                          'a new 16-colour table fitted: Sonic R\'s textures have TR\'s contrast but half its grain.'),
    'flatter': dict(title='Flatter art', group='art', stage=1, level=lambda lv: with_textures(lv, flatter),
                    about='Every colour table at 3/4 of its contrast about the texture\'s mean.'),
    'remaster': dict(title='Remastered palettes', group='shading', stage=1, level=level_remastered,
                     about='The game\'s own lifted, desaturated colour tables (debug menu option).'),
    'contrast': dict(title='Contrast by room light', group='shading', set=dict(contrast=True),
                     about='Each face\'s table at less contrast as its (static) room light falls, 8/8 to 4/8, '
                           'chosen offline: the multiply\'s look in the dark at no CPU cost.'),
    'multiply': dict(title='Multiply per face', group='shading', set=dict(flat_multiply=True),
                     about='Each face one table of its colours times its light, no gouraud: what VDP2 colour '
                           'banks could do (the exact multiply, but flat across the face).'),
    'tess': dict(title='Fixed tessellation', group='geometry', stage=2, level=level_tessellated,
                 about='Every quad with a 1024-unit edge split 2 x 2 once, offline, each quarter its own quadrant '
                       'texture: smaller warps, never popping.'),
    'para': dict(title='Triangles as parallelograms', group='geometry', set=dict(para_triangles=True),
                 about='Room triangles drawn v0 v1 (v1+v2-v0) v2 with the far half end codes, as the game\'s mesh '
                       'triangles, instead of a quad pinched to a point.'),
    'texturn': dict(title='Lines by texture', group='geometry', set=dict(turn_by_texture=True),
                    about='Lines run along the texture\'s strong edges (they draw straight; edges across lines '
                          'step), unless that takes 1.5 x the lines.'),
}

INGREDIENTS.update({
    'half': dict(title='32 x 32 rebake', group='sonic r', stage=1, level=lambda lv: with_textures(lv, art.half),
                 about='Every room texture filtered down to half size with a table fitted to it, drawn as large: '
                       'the texture size of Sonic R, texels twice as big on screen, half the memory.'),
    'shapes': dict(title='Shapes, not noise', group='sonic r', stage=0, level=lambda lv: with_textures(lv, art.shapes),
                   about='Edge-preserving smoothing (Kuwahara): the grain inside each region flattened, the edges '
                         'between regions kept, a table fitted to the result.'),
    'ramps': dict(title='Colour ramps', group='sonic r', stage=2, level=lambda lv: with_textures(lv, art.ramps),
                  about='Each table refitted as one or two shade ramps of a hue, so neighbouring texels differ in '
                        'shade rather than colour.'),
    'relief': dict(title='Baked relief', group='sonic r', stage=0, level=lambda lv: with_textures(lv, art.relief),
                   about='Brightness as a height field, smoothed and lit from the upper left: broad highlights and '
                         'shadows (the rock of Sonic R), the colours laid on at low detail, then ramps.'),
    'lightnear': dict(title='Light nearer neutral', group='sonic r', set=dict(light_scale=0.7),
                      about='Additive light kept, every corner pulled 30% of the way to neutral: Sonic R lights '
                            '3.5 levels from neutral on average, the ledge 5.2.'),
    'repaint': dict(title='Repaint samples', group='sonic r', stage=0, level=lambda lv: with_textures(lv, art.repaint),
                    about='The 12 textures covering 88% of the rooms at the ledge repainted by recipe (ground as soft '
                          'sand, snow as drifts, rock flattened and relit, cut stone as domed blocks), 32 x 32 with '
                          'ramps; the rest as they are.'),
})

INGREDIENTS.update({
    'relief_lite': dict(title='Relief, light', group='relief', stage=0,
                        level=lambda lv: with_textures(lv, art.relief_lite),
                        about='Relief without its blur: the texture\'s own colours, its finest (texel-to-texel) band '
                              'cut to 35%, the slope of its brightness lit from the upper left added, its own table.'),
    'relief_soft': dict(title='Relief, light, gentler', group='relief', stage=0,
                        level=lambda lv: with_textures(lv, lambda t: art.relief_lite(t, 0.4, 0.5)),
                        about='Relief, light at a gentler setting: finest band to 50%, the lighting at 4/7.'),
    'relief_strong': dict(title='Relief, light, stronger', group='relief', stage=0,
                          level=lambda lv: with_textures(lv, lambda t: art.relief_lite(t, 1.0, 0.2)),
                          about='Relief, light at a stronger setting: finest band to 20%, the lighting at 10/7.'),
    'relief_front': dict(title='Relief, lit from the front', group='relief', stage=0,
                         level=lambda lv: with_textures(lv, art.relief_front),
                         about='Relief, light lit from straight in front: slopes darken whichever way they face, so '
                               'the baked light has no direction to disagree with the room or a turned neighbour.'),
    'bandpass': dict(title='Bands reweighted, no light', group='relief', stage=0,
                     level=lambda lv: with_textures(lv, art.bandpass),
                     about='No lighting: the finest band cut to 35%, the 2-6 texel bands lifted a quarter, the '
                           'contrast restored: what relief does to the detail sizes, without its shading.'),
})

COMBOS = {
    # name: what the combination tests
    'ripmap+mippal': 'Ripmaps whose levels each have their own palette.',
    'ripmap+exact': 'The two biggest single ideas for texture and geometry.',
    'ripmap+tess': 'Ripmaps on tessellated rooms: each quarter its own level.',
    'ripmap+texturn': 'Ripmaps with lines along the texture\'s edges.',
    'tess+exact': 'Smaller warps with exact corners.',
}


def compose(name):
    """A variant from ingredients joined by '+': (title, about, group,
    Options keywords, level change or None)."""
    parts = name.split('+')
    for p in parts:
        if p not in INGREDIENTS:
            raise KeyError(f'no ingredient {p}')
    if len(parts) == 1:
        ing = INGREDIENTS[name]
        kw = dict(ing.get('default', {}), **ing.get('set', {}))
        return ing['title'], ing['about'], ing['group'], kw, ing.get('level')
    kw, defaults, levels = {}, {}, []
    for p in parts:
        ing = INGREDIENTS[p]
        for k, v in ing.get('set', {}).items():
            if k in kw and kw[k] is not v:
                raise ValueError(f'{name}: two ingredients set {k}')
            kw[k] = v
        defaults.update(ing.get('default', {}))
        if 'level' in ing:
            levels.append((ing.get('stage', 0), ing['level']))
    for k, v in defaults.items():
        kw.setdefault(k, v)
    levels.sort(key=lambda e: e[0])

    def change(level):
        for _, fn in levels:
            level = fn(level)
        return level

    title = ' + '.join(INGREDIENTS[p]['title'] for p in parts)
    about = COMBOS.get(name, 'A combination: ' + '; '.join(INGREDIENTS[p]['about'].rstrip('.') for p in parts) + '.')
    return title, about, 'combination', kw, change if levels else None
