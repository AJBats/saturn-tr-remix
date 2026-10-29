"""The meshes the crunch lab draws beside the rooms at the ledge: the rope
bridges' planks (items) and the rooms' static meshes (the rope and post
assemblies, icicles), replayed for any view from a frame capture
(tools/capture.py; build/captures/ledge holds the lab's ledge0 view).

The capture holds each draw's matrix for its own view. A plank's and a
static's is the view's rotation turned by whole quarter turns about y and
its origin's position in view, both exact in the game's arithmetic: so the
turn and the world origin come out exactly, and another view's matrix is
the one the game would make (matrixTranslateAbs/Set, matrixRotateY/YQ,
matrixFrame without rotation). Then meshxform.s's vertices
(capture.transform), faceAddMeshQuads/Triangles' faces into the master's
ordering table, flush's commands (rooms.face_command, kind 'mesh'), with
the object textures as they lie in the captured VDP1 VRAM; and the same
faces for the reference (ref.py).

Lara, the wolves and the door are left out: their poses aren't recorded.
Statics are drawn as the master draws them (the capture keeps every draw on
it); in play the slave draws some, in the same buckets.
"""
import importlib.util
import json
import os
import struct

import numpy as np

from gamedata import DATA
import pack_level
import trsat
from medclient import ROOT
from check_rooms import angle_for
from rooms import (CLIP_DISCARD, CLIP_PLANE, FACE_PARALLELOGRAM, FACE_TYPE_F, FACE_TYPE_SHIFT, FACE_V0_AT_D,
                   FOG_MIN, OT_SHIFT, Face, View, shade_level, wrap32)

FRAME = os.path.join(ROOT, 'build', 'captures', 'ledge')
BRIDGE_ITEMS = ('BRIDGE_FLAT', 'BRIDGE_TILT_1', 'BRIDGE_TILT_2')

FACE_TRIANGLE = 1 << 31
FACE_TYPE_AVG = 10
FACE_TEXTURE = 0x3FFF
TEX_ATTR_AKILL = 1
MESH_BYTES = 20                         # common.h Mesh
TEXTURE = struct.Struct('>8H')          # Texture: srca, colr, size, attr, color, reserved, altColor, contrast
LEVEL_TEXTURES = 92                     # Level.textures' offset
LEVEL_PALETTE = 32                      # Level.palette's


def _framecap():
    """tools/capture.py (the lab has a capture.py of its own)."""
    spec = importlib.util.spec_from_file_location('framecap', os.path.join(ROOT, 'tools', 'capture.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


framecap = _framecap()


def turned(rot, q):
    """matrixRotateY by q quarter turns (q = angle >> 14): exact, as the
    sines and cosines are 0 and +-1 << 14."""
    s, c = ((0, 16384), (16384, 0), (0, -16384), (-16384, 0))[q]
    out = np.array(rot, np.int64)
    for r in range(3):
        x, y = int(rot[r][0]), int(rot[r][2])
        out[r][0] = (x * c - y * s) >> 14
        out[r][2] = (y * c + x * s) >> 14
    return out


class ObjectTexture:
    """An object texture as it lies in VDP1 VRAM: its texels as drawn (the
    turned copies included), 16 colours, see-through colour 0, VDP1's flip
    bits (TEX_ATTR_DIR, the command's direction)."""

    def __init__(self, vram, entry):
        srca, colr, size, attr = entry[:4]
        w, h = (size >> 8) * 8, size & 0xFF
        pixels = vram[srca * 8:srca * 8 + w * h // 2]
        b = np.frombuffer(pixels, np.uint8).reshape(h, w // 2)
        self.texels = np.empty((h, w), np.uint8)
        self.texels[:, 0::2] = b >> 4
        self.texels[:, 1::2] = b & 15
        self.colors = list(struct.unpack_from('>16H', vram, colr * 8))
        self.transparent = bool(attr & TEX_ATTR_AKILL)
        self.direction = attr & 0x30
        self.key = ('obj', srca, colr, size)


class Colour:
    """A face's palette colour, as a one-texel texture for the reference."""

    def __init__(self, color):
        self.color = color
        self.texels = np.zeros((1, 1), np.uint8)
        self.colors = [color]
        self.transparent = False
        self.direction = 0
        self.key = ('colour', color)


def view_z(view, point):
    """A world point's depth in view (matrixTranslateAbs's e23 >> 14)."""
    return wrap32(sum(int(view.rot[2][c]) * (point[c] - view.pos[c]) for c in range(3))) >> 14


def fog(z):
    return max(0, z - FOG_MIN)


class Draw:
    """One mesh drawn: its parts, where it stands (a quarter turn and a
    world origin), and its lighting (calcLightingDynamic or Static) less
    what the captured view gave it: the fog, by the depth of the item's or
    static's own position, and the brightest light's direction in view."""

    def __init__(self, name, mesh, turn, origin, record, view, lit_at, item):
        self.name = name
        self.vertices, self.intensity, self.normals, self.shades, self.quads, self.triangles = mesh
        self.turn = turn
        self.origin = origin
        self.lit_at = lit_at
        self.item = item
        self.range = record['range']
        cap = 8191 + self.range if item else 8191
        if record['ambient'] >= cap:
            raise ValueError(f'{name}: its light is at the fog\'s cap, which hides what it was')
        self.ambient = record['ambient'] - fog(view_z(view, lit_at))
        # the brightest light's direction in the world (1.14): gLightView is
        # the view's rotation of it, >> 14
        light = np.array(record['light'], np.float64)
        self.light_dir = np.round(view.rot.astype(np.float64).T @ light / 16384.0).astype(np.int64)

    def matrix(self, view):
        """The draw's matrix for view, as the game makes it."""
        R = turned(view.rot, self.turn)
        t = [wrap32(sum(int(view.rot[r][c]) * (self.origin[c] - view.pos[c]) for c in range(3))) for r in range(3)]
        return [int(R[0][0]), int(R[0][1]), int(R[0][2]), t[0], int(R[1][0]), int(R[1][1]), int(R[1][2]), t[1],
                int(R[2][0]), int(R[2][1]), int(R[2][2]), t[2]]

    def lighting(self, view):
        """capture.transform's lighting record for view."""
        cap = 8191 + self.range if self.item else 8191
        ambient = min(self.ambient + fog(view_z(view, self.lit_at)), cap)
        light = [int(sum(int(view.rot[r][c]) * int(self.light_dir[c]) for c in range(3))) >> 14 for r in range(3)]
        return dict(ambient=ambient, range=self.range, light=light)


class Bridge:
    """The ledge's bridge planks and static meshes, for any view."""

    def __init__(self, folder=FRAME):
        with open(os.path.join(folder, 'frame.json')) as f:
            frame = json.load(f)
        with open(os.path.join(folder, 'lwram.bin'), 'rb') as f:
            lwram = f.read()
        with open(os.path.join(folder, 'hwram.bin'), 'rb') as f:
            hwram = f.read()
        with open(os.path.join(folder, 'vdp1.bin'), 'rb') as f:
            vram = f.read()
        mem = framecap.Memory(lwram, hwram)
        syms = framecap.symbols(os.path.join(folder, '0MAIN.elf'))
        table = framecap.symbol(syms, '_divTable')
        self.div_table = [mem.unpack('>H', table + 2 * i)[0] for i in range(1281)]
        level = framecap.symbol(syms, '_level')
        textures = mem.unpack('>I', level + LEVEL_TEXTURES)[0]
        self.palette_at = mem.unpack('>I', level + LEVEL_PALETTE)[0]
        self.mem, self.vram = mem, vram
        self.entries = lambda i: mem.unpack(TEXTURE.format, textures + TEXTURE.size * i)
        self.textures = {}

        header = frame['header']
        view_rot = np.array(header['rotation'], np.int64).reshape(3, 3)
        camera = np.array(header['camera'], np.int64)
        self.view = captured_view(header)
        items = trsat.load(DATA, 'LEVEL1').items
        names = {v: k for k, v in pack_level.ITEM.items()}
        self.draws = []
        for r in frame['records']:
            if r['type'] == 'mesh' and names.get(items[r['index']]['type']) in BRIDGE_ITEMS:
                it = items[r['index']]
                name = f'item {r["index"]} ({names[it["type"]]})'
            elif r['type'] == 'static':
                it = None
                name = f'static {r["index"]}'
            else:
                continue
            m = r['matrix']
            rot = np.array([[s16(m[k]) for k in row] for row in ((0, 1, 2), (4, 5, 6), (8, 9, 10))], np.int64)
            t = np.array([m[3], m[7], m[11]], np.int64)
            turn = [q for q in range(4) if np.array_equal(turned(view_rot, q), rot)]
            d = np.round(np.linalg.solve(view_rot.astype(np.float64), t.astype(np.float64))).astype(np.int64)
            if len(turn) != 1 or not np.array_equal(view_rot @ d, t):
                raise ValueError(f'{name}: its matrix is not the view turned by quarter turns')
            origin = (camera + d).tolist()
            lit_at = [it['x'], it['y'], it['z']] if it else origin
            self.draws.append(Draw(name, self.mesh(r['ref']), turn[0], origin, r, self.view, lit_at, it is not None))

    def mesh(self, address):
        """render.cpp meshParts: vertices, intensity, normals or shades,
        quads and triangles (flags, 4 indices; a triangle's fourth its
        arrangement)."""
        mem = self.mem
        vertices, intensity, normals, shades = framecap.mesh_parts(mem, address)
        (_, _, _, _, _, count, lighting, n_quads, n_triangles, _, _) = mem.unpack(framecap.MESH.format, address)
        at = address + MESH_BYTES + 6 * count
        if lighting == framecap.MESH_LIGHT_NORMALS:
            at += 4 * count
        elif lighting == framecap.MESH_LIGHT_SHADES:
            at += (count + 1) & ~1
        quads = [mem.unpack('>H4B', at + 6 * i) for i in range(n_quads)]
        at += 6 * n_quads
        triangles = [mem.unpack('>H4B', at + 6 * i) for i in range(n_triangles)]
        return vertices, intensity, normals, shades, quads, triangles

    def texture(self, index):
        if index not in self.textures:
            self.textures[index] = ObjectTexture(self.vram, self.entries(index))
        return self.textures[index]

    def colour(self, index):
        return self.mem.unpack('>H', self.palette_at + 2 * index)[0]

    def vertices(self, view, draw):
        """meshxform.s's vertices (x, y, z, g, clip) for view."""
        record = dict(draw.lighting(view), matrix=draw.matrix(view))
        return framecap.transform(record, draw.vertices, self.div_table, draw.intensity, draw.normals, draw.shades)

    def add(self, view, faces, verts):
        """The draws' faces for view after the rooms' (faces, verts as
        rooms.RoomDrawer.faces makes them): faceAddMeshQuads/Triangles."""
        for draw in self.draws:
            vs = self.vertices(view, draw)
            a = np.array(vs, np.int64).reshape(-1, 5)
            base = verts.add(a[:, 0], a[:, 1], a[:, 2], a[:, 3], a[:, 4], a[:, 3])
            for flags, *idx in draw.quads:
                self.face(faces, base, vs, flags, idx, False)
            for flags, *idx in draw.triangles:
                self.face(faces, base, vs, flags, idx[:3], True, idx[3])

    def face(self, faces, base, vs, flags, idx, triangle, arrangement=0):
        v = [vs[i] for i in idx]
        if (v[1][0] - v[0][0]) * (v[2][1] - v[0][1]) <= (v[2][0] - v[0][0]) * (v[1][1] - v[0][1]):
            return      # checkBackface
        clip_and = clip_or = v[0][4]
        for p in v[1:]:
            clip_and &= p[4]
            clip_or |= p[4]
        if (clip_and & CLIP_DISCARD) or (clip_or & CLIP_PLANE):
            return      # cullFace
        corners = [base + i for i in idx]
        if triangle:
            flags |= FACE_TRIANGLE
            kind = (flags >> FACE_TYPE_SHIFT) & 15
            if kind not in (FACE_TYPE_F, FACE_TYPE_AVG) and not arrangement & 0x80:
                flags |= FACE_PARALLELOGRAM
                if arrangement & 1:
                    flags |= FACE_V0_AT_D
                if arrangement & 2:
                    corners[1], corners[2] = corners[2], corners[1]
                    v[1], v[2] = v[2], v[1]
            corners.append(corners[2])
            v.append(v[2])
        depth = (sum(p[2] for p in v) & 0xFFFFFFFF) >> (2 + OT_SHIFT)
        kind = (flags >> FACE_TYPE_SHIFT) & 15
        tex = Colour(self.colour(flags & FACE_TEXTURE)) if kind == FACE_TYPE_F else self.texture(flags & FACE_TEXTURE)
        faces.append(Face('mesh', corners, depth, tex, flags=flags))

    def reference_faces(self, view):
        """Each face facing the camera as the reference takes it: triangles
        of view-space corners (floats), texture coordinates, lights (level
        - 16) and the texture."""
        out = []
        for draw in self.draws:
            m = draw.matrix(view)
            R = np.array([m[0:3], m[4:7], m[8:11]], np.float64) / 16384.0
            t = np.array([m[3], m[7], m[11]], np.float64) / 16384.0
            pts = np.array(draw.vertices, np.float64) @ R.T + t
            levels = [shade_level(v[3]) for v in self.vertices(view, draw)]
            for flags, *idx in draw.quads:
                out += self.reference_face(pts, levels, flags, idx, None)
            for flags, *idx in draw.triangles:
                out += self.reference_face(pts, levels, flags, idx[:3], idx[3])
        return out

    def reference_face(self, pts, levels, flags, idx, arrangement):
        """(corners, texture coordinates, lights, texture) triangles: the
        texture mapped as VDP1 maps it on the game's command."""
        p = [pts[i] for i in idx]
        if np.dot(np.cross(p[1] - p[0], p[2] - p[0]), p[0]) <= 0:
            return []       # facing away, as checkBackface sees it
        light = [levels[i] - 16 for i in idx]
        kind = (flags >> FACE_TYPE_SHIFT) & 15
        t = Colour(self.colour(flags & FACE_TEXTURE)) if kind == FACE_TYPE_F else self.texture(flags & FACE_TEXTURE)
        h, w = t.texels.shape
        corners = [(0, 0), (w, 0), (w, h), (0, h)]
        if t.direction & 0x10:
            corners = [(w - u, v) for u, v in corners]
        if t.direction & 0x20:
            corners = [(u, h - v) for u, v in corners]
        if arrangement is None:
            return [([p[0], p[1], p[2]], corners[0:3], light[0:3], t),
                    ([p[0], p[2], p[3]], [corners[0], corners[2], corners[3]], [light[0], light[2], light[3]], t)]
        if kind == FACE_TYPE_F or arrangement & 0x80:
            # a quad with v2 at C and D: the texture's bottom edge pinched to v2
            mid = ((corners[2][0] + corners[3][0]) / 2, (corners[2][1] + corners[3][1]) / 2)
            return [(p, [corners[0], corners[1], mid], light, t)]
        if arrangement & 2:
            p = [p[0], p[2], p[1]]
            light = [light[0], light[2], light[1]]
        # the parallelogram: v0, v1, v2 at A, B, D (or v0 at D: v1, v2 at A, C)
        uv = [corners[3], corners[0], corners[2]] if arrangement & 1 else [corners[0], corners[1], corners[3]]
        return [(p, uv, light, t)]


def captured_view(header):
    """A frame capture's view (rooms.View), its angles found from the
    recorded rotation."""
    rot = np.array(header['rotation'], np.int64).reshape(3, 3)
    view = View(header['camera'], angle_for(-int(rot[2][1]), int(rot[1][1])),
                angle_for(-int(rot[0][2]), int(rot[0][0])))
    if not np.array_equal(view.rot, rot):
        raise ValueError('no angles give the captured rotation')
    return view


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v
