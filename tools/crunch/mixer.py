"""The lab's mixer: every technique as a row of settings, so any mix can be
drawn on demand (labserver.py) instead of every combination ahead of time.

A mix is {row id: option id}; rows left out take their first option, the
game's. compile_mix turns a mix into what lab.Lab.draw takes.
"""
import hashlib

import art
import variants
from variants import with_textures

# Each option: (id, label, effect). An effect sets rooms.Options keywords
# ('set', or 'default' when no other row sets them), or changes the level
# ('level': a function of the level and the whole mix, at 'stage').
ROWS = [
    dict(id='projection', title='Projection', group='projection', options=[
        ('table', 'divTable (game)', {}),
        ('fine', 'Finer tables', dict(set=dict(divide='fine'))),
        ('exact', 'Exact divide', dict(set=dict(divide='exact'))),
    ]),
    dict(id='reduce', title='Texture reduction', group='textures', options=[
        ('mips', 'Mips (game)', {}),
        ('ripmap', 'Ripmaps', dict(set=dict(ripmap=True), default=dict(rip=variants.reduce_xy))),
        ('none', 'None', dict(set=dict(lod=0))),
    ]),
    dict(id='reduced', title='Reduced levels\' colours', group='textures', options=[
        ('dither', 'Dithered (game)', {}),
        ('nearest', 'Nearest, no dither', dict(set=dict(mip=variants.mip_nearest))),
        ('own', 'Own palette', dict(set=dict(mip=variants.mip_own_palette, rip=variants.own_palette))),
    ]),
    dict(id='repaint', title='Repaint samples', group='art', options=[
        ('off', 'Off', {}),
        ('on', '12 ledge textures', dict(stage=0, level=lambda lv, mix: with_textures(lv, art.repaint))),
    ]),
    dict(id='speckle', title='Lonely texels', group='art', options=[
        ('off', 'Kept', {}),
        ('despeckle', 'Despeckled', dict(stage=1, level=lambda lv, mix: with_textures(lv, variants.despeckle))),
    ]),
    dict(id='thin', title='Thin lines', group='art', options=[
        ('off', 'Kept', {}),
        ('soften', 'Softened', dict(stage=2, level=lambda lv, mix: with_textures(lv, variants.soften_thin))),
    ]),
    dict(id='grain', title='Grain', group='art', options=[
        ('off', 'Kept', {}),
        ('clump', 'Clumped', dict(stage=3, level=lambda lv, mix: with_textures(lv, variants.clump_noise))),
        ('degrain', 'Degrained', dict(stage=3, level=lambda lv, mix: with_textures(lv, variants.degrain))),
        ('bandpass', 'Bands reweighted', dict(stage=3, level=lambda lv, mix: with_textures(lv, art.bandpass))),
        ('shapes', 'Shapes (Kuwahara)', dict(stage=3, level=lambda lv, mix: with_textures(lv, art.shapes))),
    ]),
    dict(id='size', title='Texture size', group='art', options=[
        ('64', '64 (game)', {}),
        ('32', '32 x 32 rebake', dict(stage=5, level=lambda lv, mix: with_textures(lv, art.half))),
    ]),
    dict(id='palette', title='Colour tables', group='art', options=[
        ('original', 'Original (game)', {}),
        ('remaster', 'Remastered', dict(stage=6, level=lambda lv, mix: variants.level_remastered(lv))),
        ('ramps', 'Ramps', dict(stage=6, level=lambda lv, mix: with_textures(lv, art.ramps))),
        ('flatter', 'Flatter (3/4)', dict(stage=6, level=lambda lv, mix: with_textures(lv, variants.flatter))),
    ]),
    dict(id='relief', title='Baked relief', group='relief', options=[
        ('off', 'Off', {}),
        ('gentle', 'Gentle', dict(stage=4, level=lambda lv, mix: relief_level(lv, mix, 0.5))),
        ('medium', 'Medium (first try)', dict(stage=4, level=lambda lv, mix: relief_level(lv, mix, 1.0))),
        ('strong', 'Strong', dict(stage=4, level=lambda lv, mix: relief_level(lv, mix, 1.6))),
    ]),
    dict(id='relief_light', title='Relief light', group='relief', options=[
        ('upper left', 'Upper left', {}),
        ('front', 'Front (no direction)', {}),
    ]),
    dict(id='relief_colours', title='Relief colours', group='relief', options=[
        ('blurred', 'Blurred (first try)', {}),
        ('kept', 'Kept', {}),
    ]),
    dict(id='shading', title='Shading', group='shading', options=[
        ('add', 'Add (game)', {}),
        ('contrast', 'Contrast by room light', dict(set=dict(contrast=True))),
        ('multiply', 'Multiply per face', dict(set=dict(flat_multiply=True))),
    ]),
    dict(id='light', title='Light toward neutral', group='shading', options=[
        ('0', 'As lit (game)', {}),
        ('15', '15%', dict(set=dict(light_scale=0.85))),
        ('30', '30%', dict(set=dict(light_scale=0.7))),
    ]),
    dict(id='tess', title='Tessellation', group='geometry', options=[
        ('off', 'Off (game)', {}),
        ('on', '2 x 2, fixed', dict(stage=7, geometry=True, level=lambda lv, mix: variants.level_tessellated(lv))),
    ]),
    dict(id='para', title='Room triangles', group='geometry', options=[
        ('quad', 'Pinched quads (game)', {}),
        ('para', 'Parallelograms', dict(set=dict(para_triangles=True))),
    ]),
    dict(id='turn', title='Line direction', group='geometry', options=[
        ('lines', 'Fewer lines (game)', {}),
        ('texture', 'By texture edges', dict(set=dict(turn_by_texture=True))),
    ]),
]

BY_ID = {r['id']: r for r in ROWS}


def relief_level(lv, mix, amount):
    light, colours = mix.get('relief_light', 'upper left'), mix.get('relief_colours', 'blurred')
    return with_textures(lv, lambda t: art.relief_mix(t, amount, light, colours))


def normal(mix):
    """The mix with every row's choice, unknown rows and options dropped;
    the relief's light and colours only while relief is on."""
    out = {}
    for row in ROWS:
        ids = [o[0] for o in row['options']]
        v = mix.get(row['id'], ids[0])
        out[row['id']] = v if v in ids else ids[0]
    if out['relief'] == 'off':
        out['relief_light'], out['relief_colours'] = 'upper left', 'blurred'
    return out


def key_of(mix, rows=None):
    """A short name for (part of) a mix: its choices other than the game's."""
    mix = normal(mix)
    parts = [f'{r["id"]}={mix[r["id"]]}' for r in ROWS if (rows is None or r['id'] in rows)
             and mix[r['id']] != r['options'][0][0]]
    return ','.join(parts)


def describe(key):
    """A key_of string in words: 'Baked relief: Medium (first try); ...'."""
    out = []
    for part in key.split(','):
        if '=' in part:
            rid, oid = part.split('=', 1)
            row = BY_ID[rid]
            out.append(f'{row["title"]}: ' + next(o[1] for o in row['options'] if o[0] == oid))
    return '; '.join(out)


def digest(text):
    return hashlib.md5(text.encode()).hexdigest()[:10]


def compile_mix(mix):
    """(name, title, Options keywords, level change or None, art key): the
    art key names what the mix does to the textures (its ideal picture),
    '' for nothing."""
    mix = normal(mix)
    kw, defaults, levels = {}, {}, []
    titles, art_rows = [], []
    for row in ROWS:
        oid = mix[row['id']]
        label, effect = next((o[1], o[2]) for o in row['options'] if o[0] == oid)
        if oid != row['options'][0][0]:
            titles.append(f'{row["title"]}: {label}')
        kw.update(effect.get('set', {}))
        defaults.update(effect.get('default', {}))
        if 'level' in effect:
            levels.append((effect['stage'], effect['level']))
            if not effect.get('geometry'):
                art_rows.append(row['id'])
    if mix['relief'] != 'off':
        art_rows += ['relief_light', 'relief_colours']
    for k, v in defaults.items():
        kw.setdefault(k, v)
    levels.sort(key=lambda e: e[0])

    def change(level):
        for _, fn in levels:
            level = fn(level, mix)
        return level

    name = 'mix-' + digest(key_of(mix) or 'game')
    title = '; '.join(titles) or 'The game today'
    return name, title, kw, change if levels else None, key_of(mix, set(art_rows))


def rows_for_page():
    return [dict(id=r['id'], title=r['title'], group=r['group'], options=[[o[0], o[1]] for o in r['options']])
            for r in ROWS]
