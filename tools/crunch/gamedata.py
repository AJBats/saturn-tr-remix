"""Level data for the crunch lab, as the game packs it: room geometry
(tools/trsat.py read through pack_level.py's room packing) and room textures
(pack_level.TextureTable: the full-res set with the game's colour tables).
Also the game's sine/cosine and divide tables."""
import os
import re
import sys

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.append(os.path.join(ROOT, 'tools'))

import pack_level  # noqa: E402
import trsat  # noqa: E402
from texart import pack_texels, texels_of  # noqa: E402,F401

# pack_level puts tools/ first on the path: the lab's own folder goes back in
# front of it (tools/lab.py, the render lab's, would hide lab.py here)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

DATA = os.path.join(ROOT, 'external_resources', 'disc', 'files', 'DATA')
TEX_ATTR_AKILL = pack_level.TEX_ATTR_AKILL


def sincos_table():
    """openlara/common.cpp gSinCosTable: sin << 16 | cos (1.14) by angle >> 4."""
    with open(os.path.join(ROOT, 'openlara', 'common.cpp')) as f:
        text = f.read()
    body = text[text.index('gSinCosTable[4096]'):]
    body = body[body.index('{') + 1:body.index('};')]
    values = [int(v, 16) for v in re.findall(r'0x[0-9A-Fa-f]+', body)]
    assert len(values) == 4096
    return np.array(values, dtype=np.uint32)


SINCOS = sincos_table()


def sincos(angle):
    """common.h sincos: the table at bits 4..15 of the 16-bit angle."""
    sc = int(SINCOS[(angle & 0xFFFF) >> 4])
    s = sc >> 16
    s = s - 0x10000 if s & 0x8000 else s
    c = sc & 0xFFFF
    c = c - 0x10000 if c & 0x8000 else c
    return s, c


DIV_TABLE_SIZE = 1281
DIV_TABLE = np.array([0] + [min(0x7FFF, 65536 // i) for i in range(1, DIV_TABLE_SIZE)], dtype=np.int32)


class Texture:
    """A room texture whole: w x h 4-bit texels (a row of bytes per line, the
    left texel high), its 16 colours, see-through colour 0 or not."""

    def __init__(self, index, pixels, w, h, colors, transparent, mean, end_codes=False):
        self.index = index
        self.end_codes = end_codes  # colour 15 is VDP1's end code (a triangle's parallelogram)
        self.pixels = pixels
        self.w, self.h = w, h
        self.colors = colors
        self.transparent = transparent
        self.mean = mean

    def texels(self):
        """h x w array of colour indices."""
        return texels_of(self.pixels, self.w, self.h)


class Room:
    def __init__(self, index, origin, points, shades, quads, triangles, water):
        self.index = index
        self.origin = origin        # world x, y (its top), z of the points' 0
        self.points = points        # n x 3 int: x, y, z in 64-unit steps from the origin (xform.s's
                                    # packed vertex times 4), so a split's midpoints stay whole
        self.shades = shades        # n: 0 (lit) .. 31 (dark)
        self.quads = quads          # (v0, v1, v2, v3, texture, see-through), packed order
        self.triangles = triangles  # (v0, v1, v2, texture)
        self.water = water


class Level:
    def __init__(self, name='LEVEL1'):
        self.name = name
        lvl = trsat.load(DATA, name)
        self.lvl = lvl
        table = pack_level.TextureTable(lvl)
        for room in lvl.rooms:
            for q in room.quads:
                table.room(q[4], q[5])
        self.table = table

        self.textures = []
        for i, t in enumerate(lvl.room_textures):
            pixels, colors, _, _ = table.sources[i]
            transparent = bool(table.entries[i][3] & TEX_ATTR_AKILL)
            mean = pack_level.texture_average(pixels, colors, transparent)
            self.textures.append(Texture(i, pixels, t['w'] * 2, t['h'] * 2, list(colors), transparent, mean))

        self.rooms = []
        for room in lvl.rooms:
            ys = [v[1] for v in room.vertices]
            y_top = min(ys) if ys else room.y_top
            packed = [(x >> 8, (y - y_top) >> 8, z >> 8) for x, y, z, _ in room.vertices]
            quads, _ = pack_level.sort_room_quads(packed, room.quads)
            self.rooms.append(Room(room.index, (room.x, y_top, room.z),
                                   np.array(packed, dtype=np.int32).reshape(-1, 3) * 4,
                                   np.array([pack_level.shade_from_color16(v[3]) for v in room.vertices],
                                            dtype=np.int32),
                                   quads, list(room.triangles), bool(room.flags & 1)))
