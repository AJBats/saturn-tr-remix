"""A level's floor as it would look on a VDP2 rotation plane, beside the original polygons, with
a tile painter and hand-made obstruction boxes (tools/floor_paint.html). Local only: the page
holds the game's textures, so the server listens on this machine alone and the page fetches
nothing from anywhere else.

    python -I tools/floor_paint.py [LEVEL3A] [--port 8766] [--no-open]
    python -I tools/floor_paint.py [LEVEL3A] --budget       the saved paint's budget, as text

then http://127.0.0.1:8766/

What goes on a plane (as tools/planepack.py in the lab, and docs/vdp2-production.md): a face flat
at the level's height, facing the right way, opaque, a sector square, not animated, in a dry room
that isn't an alternate. Everything else stays polygons, always drawn in front of the plane.

The map holds 32 x 32 sectors and repeats, so a world sector (sx, sz) lands on spot
(sx % 32, sz % 32). Sectors that want one spot share it if their tile is identical (texture,
orientation, light step). If not, one wins and the rest stay polygons. Who wins: a painted or
flattened tile beats the level's own; among painted ones the lowest SX, then SZ; among the
level's own the first in room and quad order, as the lab's pack does.

The page and this script each work out the budget (the page live, this for --budget and the
text button), and the text button says so if they ever differ.

Saved as saturn/plane_paint/<LEVEL>.txt, only what differs from the level, a line each:

    tile KIND SX SZ TEX TURNED FLIPH FLIPV      this sector shows this tile
    blank KIND SX SZ                            no tile here, and its face goes too
    keep KIND SX SZ                             this sector's face stays a polygon
    flatten KIND SX SZ ROOM QUAD                that face (raw room.quads index) goes on the plane here
    light KIND SX SZ A B C D                    the four corner levels, world (x,z) corners -,- +,- +,+ -,+
    box X0 Y0 Z0 X1 Y1 Z1                       an obstruction box

KIND is floor, or ceiling on a level that has one.
"""
import argparse
import contextlib
import io
import json
import os
import struct
import sys
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trsat  # noqa: E402
from pack_level import whole_textures  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, 'external_resources', 'disc', 'files', 'DATA')
PAGE = os.path.join(ROOT, 'tools', 'floor_paint.html')
SAVE_DIR = os.path.join(ROOT, 'saturn', 'plane_paint')

# The levels this opens, and their plane heights (world y grows down).
PLANES = {
    'LEVEL3A': {'floor': 3584},
    'LEVEL7A': {'floor': 0, 'ceiling': -7168},
}

# Inventory item textures in bank B, in bytes, when build/data has no .ITX to measure.
INVENTORY_BYTES = {'LEVEL3A': 58688, 'LEVEL7A': 65888}

# Places to look from: name, x, y of Lara's feet, z, yaw.
SPOTS = {
    'LEVEL3A': [
        ('Mouth', 39424, 3584, 42496, 0x6000),
        ('Plants', 41453, 3584, 35241, 0x8143),
        ('T-rex floor', 45568, 3584, 15872, 0x4000),
        ('Looking at the sky', 41167, 3584, 27288, 0xA136),
    ],
}

SECTOR = 1024
MAP_SECTORS = 32
BLOCK_SECTORS = 8
TILES_A_SECTOR = 4
TEXTURE_TEXELS = 64
ATLAS_COLUMNS = 16
MAX_PALETTES = 119
LEVELS = 32
NEUTRAL = 16

BANK_BYTES = 256 * 1024
PICTURE_BYTES = 2048
BLANK_TILE_BYTES = 128
NAME_BLOCK_BYTES = 4096
DEPTH_TABLE_BYTES = 32 * 1024
PARAMETER_BYTES = 1024

HEADER = [
    '# Tile paint for {level}: only what differs from the level. Written by tools/floor_paint.py.',
    '# A sector is 1024 units. A map spot is (SX % 32, SZ % 32). Where different tiles want one spot, a painted or',
    '# flattened tile beats the level\'s own, then the lowest SX, then SZ; a level\'s own tile, the first in room and',
    '# quad order. A sector whose tile lost the spot stays a polygon if it has a face.',
]


def tile_of(room, q):
    """A quad as a tile on the map, if its footprint is a sector square: its world sector, how
    the texture lies (see the page's orient()), and its corner levels in world order -,- +,- +,+ -,+."""
    corners = [room.vertices[i] for i in q[:4]]
    at = [(room.x + c[0], room.z + c[2]) for c in corners]
    a, b, d = at[0], at[1], at[3]
    ux, uz, vx, vz = b[0] - a[0], b[1] - a[1], d[0] - a[0], d[1] - a[1]
    plain = abs(ux) == SECTOR and uz == 0 and abs(vz) == SECTOR and vx == 0
    turned = abs(uz) == SECTOR and ux == 0 and abs(vx) == SECTOR and vz == 0
    if not (plain or turned) or at[2] != (a[0] + ux + vx, a[1] + uz + vz):
        return None
    x0, z0 = min(p[0] for p in at), min(p[1] for p in at)
    if x0 % SECTOR or z0 % SECTOR:
        return None
    light = [0, 0, 0, 0]
    order = {(0, 0): 0, (1, 0): 1, (1, 1): 2, (0, 1): 3}
    for p, c in zip(at, corners):
        light[order[((p[0] - x0) // SECTOR, (p[1] - z0) // SECTOR)]] = c[3] & 31
    return {
        'sx': x0 // SECTOR, 'sz': z0 // SECTOR, 'turned': int(turned),
        'fliph': int((vx < 0) if turned else (ux < 0)), 'flipv': int((uz < 0) if turned else (vz < 0)),
        'light': light,
    }


def winding(room, verts):
    at = [(room.x + room.vertices[i][0], room.z + room.vertices[i][2]) for i in verts[:3]]
    ux, uz = at[1][0] - at[0][0], at[1][1] - at[0][1]
    ax, az = at[2][0] - at[0][0], at[2][1] - at[0][1]
    return ux * az - uz * ax


def tile_levels(light):
    """The 16 x 16 texel tiles' light in a sector: bilinear from the corners at each tile's
    centre, four tiles a side, in world x then z."""
    out = []
    for tj in range(TILES_A_SECTOR):
        fz = (tj + 0.5) / TILES_A_SECTOR
        for ti in range(TILES_A_SECTOR):
            fx = (ti + 0.5) / TILES_A_SECTOR
            out.append((light[0] * (1 - fx) + light[1] * fx) * (1 - fz) + (light[3] * (1 - fx) + light[2] * fx) * fz)
    return out


class Model:
    """A level, its plane heights, and each plane's faces as the level has them."""

    def __init__(self, name, save_dir=SAVE_DIR):
        self.name = name
        self.planes = PLANES[name]
        with contextlib.redirect_stdout(io.StringIO()):
            self.lvl = trsat.load(DATA, name)
        self.animated = {t for first, last in self.lvl.anim_ranges for t in range(first, last + 1)}
        rooms = self.lvl.rooms
        self.alternates = {r.alternate for r in rooms if 0 <= r.alternate < len(rooms)}
        self.base = {kind: self.plane_faces(height, kind == 'floor') for kind, height in self.planes.items()}
        itx = os.path.join(ROOT, 'build', 'data', name + '.ITX')
        self.inventory = os.path.getsize(itx) if os.path.exists(itx) else INVENTORY_BYTES.get(name, 0)
        self.save_path = os.path.join(save_dir, name + '.txt')

    def paintable(self, tex):
        """A texture that can be a whole tile: 64 x 64, and not animated."""
        if not 0 <= tex < len(self.lvl.room_textures):
            return False
        t = self.lvl.room_textures[tex]
        return t['w'] * 2 == TEXTURE_TEXELS and t['h'] * 2 == TEXTURE_TEXELS and tex not in self.animated

    def plane_faces(self, height, floor):
        """(sx, sz) -> the face on the plane there, in room and quad order."""
        out = {}
        for room in self.lvl.rooms:
            if room.flags & 1 or room.index in self.alternates:
                continue
            for index, q in enumerate(room.quads):
                ys = {room.vertices[i][1] for i in q[:4]}
                if ys != {height} or q[5] or not self.paintable(q[4]):
                    continue
                w = winding(room, q)
                if (w >= 0) if floor else (w <= 0):
                    continue
                tile = tile_of(room, q)
                if tile:
                    out.setdefault((tile['sx'], tile['sz']), dict(tile, room=room.index, quad=index, tex=q[4]))
        return out

    def page_json(self):
        rooms = []
        for r in self.lvl.rooms:
            alt = r.index in self.alternates
            rooms.append({
                'i': r.index, 'wet': r.flags & 1, 'alt': int(alt),
                'v': [] if alt else [c for v in r.vertices for c in (r.x + v[0], v[1], r.z + v[2], v[3] & 31)],
                'q': [] if alt else [[q[0], q[1], q[2], q[3], q[4], int(q[5])] for q in r.quads],
                't': [] if alt else [list(t) for t in r.triangles],
            })
        textures = [{'w': t['w'] * 2, 'h': t['h'] * 2, 'anim': int(i in self.animated), 'paint': int(self.paintable(i))}
                    for i, t in enumerate(self.lvl.room_textures)]
        lara = next((i for i in self.lvl.items if i['type'] == 0), None)
        spots = [list(s) for s in SPOTS.get(self.name, [])]
        if lara:
            spots.append(["Lara's start", lara['x'], lara['y'], lara['z'], lara['rotation'] & 0xFFFF])
        columns, width, height = self.atlas_size()
        return {
            'name': self.name, 'planes': self.planes, 'rooms': rooms, 'textures': textures, 'spots': spots,
            'atlas': {'columns': columns, 'width': width, 'height': height, 'cell': TEXTURE_TEXELS},
            'base': {kind: [[k[0], k[1], f['room'], f['quad'], f['tex'], f['turned'], f['fliph'], f['flipv'], *f['light']]
                            for k, f in faces.items()] for kind, faces in self.base.items()},
            'bank': {'bank': BANK_BYTES, 'picture': PICTURE_BYTES, 'blankTile': BLANK_TILE_BYTES, 'block': NAME_BLOCK_BYTES,
                     'depth': DEPTH_TABLE_BYTES, 'parameters': PARAMETER_BYTES, 'inventory': self.inventory,
                     'palettes': MAX_PALETTES},
        }

    def atlas_size(self):
        rows = (len(self.lvl.room_textures) + ATLAS_COLUMNS - 1) // ATLAS_COLUMNS
        return ATLAS_COLUMNS, ATLAS_COLUMNS * TEXTURE_TEXELS, rows * TEXTURE_TEXELS

    def atlas(self):
        """Every room texture as RGBA, top left in its 64 x 64 cell: the 15-bit colours times 8,
        alpha 0 on colour 0, which a transparent face doesn't draw."""
        lvl = self.lvl
        whole = whole_textures(lvl)
        _, width, height = self.atlas_size()
        out = bytearray(width * height * 4)
        for tex, t in enumerate(lvl.room_textures):
            lut = ((t['clut'] + t['tile']) & 0xFFFF) * 8
            colours = struct.unpack_from('>16H', lvl.room_tex_qtr, lut)
            rgba = [bytes(((c & 31) * 8, ((c >> 5) & 31) * 8, ((c >> 10) & 31) * 8, 255 if i else 0))
                    for i, c in enumerate(colours)]
            pair = [rgba[b >> 4] + rgba[b & 15] for b in range(256)]
            stride = t['w']
            at = t['sub'][0] * 8
            cx, cy = (tex % ATLAS_COLUMNS) * TEXTURE_TEXELS, (tex // ATLAS_COLUMNS) * TEXTURE_TEXELS
            for y in range(t['h'] * 2):
                row = b''.join(pair[b] for b in whole[at + y * stride:at + (y + 1) * stride])
                start = (cy + y) * width * 4 + cx * 4
                out[start:start + len(row)] = row
        return bytes(out)


# ---- the paint: reading, checking and writing it

def parse_paint(model, text):
    """edits {kind: {(sx, sz): {'tile': (...), 'blank': 1, 'keep': 1, 'flatten': (room, quad), 'light': (...)}}},
    boxes, the file's own comment lines, and the lines it couldn't take with why."""
    edits = {kind: {} for kind in model.planes}
    boxes = []
    comments = []
    problems = []
    header = {h.format(level=model.name) for h in HEADER}

    def ints(words):
        return [int(w) for w in words]

    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.split('#', 1)[0].strip()
        if not line:
            if raw.strip() and raw.strip() not in header:
                comments.append(raw.strip())
            continue
        words = line.split()
        try:
            what = words[0]
            if what == 'box':
                x0, y0, z0, x1, y1, z1 = ints(words[1:])
                if x0 > x1 or y0 > y1 or z0 > z1:
                    raise ValueError('a box runs from its low corner to its high one')
                boxes.append((x0, y0, z0, x1, y1, z1))
                continue
            if what not in ('tile', 'blank', 'keep', 'flatten', 'light'):
                raise ValueError(f'no such entry as {what}')
            kind = words[1]
            if kind not in edits:
                raise ValueError(f'this level has no {kind}')
            sx, sz = ints(words[2:4])
            rest = ints(words[4:])
            entry = edits[kind].setdefault((sx, sz), {})
            if what == 'light':
                a, b, c, d = rest
                if not all(0 <= v < LEVELS for v in rest):
                    raise ValueError('a level is 0 to 31')
                entry['light'] = (a, b, c, d)
                continue
            if what == 'tile':
                tex, turned, fliph, flipv = rest
                if not model.paintable(tex):
                    raise ValueError(f'texture {tex} can\'t be a tile: it is animated or not 64 x 64')
                if not all(v in (0, 1) for v in (turned, fliph, flipv)):
                    raise ValueError('turned, flip H and flip V are 0 or 1')
                new = {'tile': (tex, turned, fliph, flipv)}
            elif what == 'flatten':
                room, quad = rest
                check_flatten(model, room, quad, sx, sz)
                new = {'flatten': (room, quad)}
            else:
                if rest:
                    raise ValueError('nothing follows the sector')
                new = {what: 1}
            # one of these a sector: the later line stands, and any light with it
            for other in ('tile', 'flatten', 'blank', 'keep'):
                entry.pop(other, None)
            entry.update(new)
        except (ValueError, IndexError) as e:
            problems.append(f'line {number}: {raw.strip()}: {e}')
            comments.append(f'# could not read: {raw.strip()}')
    return edits, boxes, comments, problems


def check_flatten(model, room, quad, sx, sz):
    rooms = model.lvl.rooms
    if not 0 <= room < len(rooms) or not 0 <= quad < len(rooms[room].quads):
        raise ValueError(f'no quad {quad} in room {room}')
    q = rooms[room].quads[quad]
    tile = tile_of(rooms[room], q)
    if not tile or (tile['sx'], tile['sz']) != (sx, sz):
        raise ValueError(f'room {room} quad {quad} doesn\'t cover sector {sx} {sz}')
    if q[5] or not model.paintable(q[4]):
        raise ValueError(f'room {room} quad {quad} is see-through, animated or not 64 x 64')


def write_paint(model, edits, boxes, comments=()):
    lines = [h.format(level=model.name) for h in HEADER]
    lines += comments
    for kind in model.planes:
        for (sx, sz), e in sorted(edits.get(kind, {}).items()):
            if 'tile' in e:
                lines.append('tile {} {} {} {} {} {} {}'.format(kind, sx, sz, *e['tile']))
            if 'blank' in e:
                lines.append(f'blank {kind} {sx} {sz}')
            if 'keep' in e:
                lines.append(f'keep {kind} {sx} {sz}')
            if 'flatten' in e:
                lines.append('flatten {} {} {} {} {}'.format(kind, sx, sz, *e['flatten']))
            if 'light' in e:
                lines.append('light {} {} {} {} {} {} {}'.format(kind, sx, sz, *e['light']))
    for b in boxes:
        lines.append('box {} {} {} {} {} {}'.format(*b))
    return '\n'.join(lines) + '\n'


def paint_from_page(body):
    """The page's edits and boxes as the file's lines, so they pass the same checks as the file."""
    lines = []
    for kind, sectors in body.get('edits', {}).items():
        for key, e in sectors.items():
            sx, sz = key.split(',')
            if 'tile' in e:
                lines.append(f'tile {kind} {sx} {sz} ' + ' '.join(str(int(v)) for v in e['tile']))
            if e.get('blank'):
                lines.append(f'blank {kind} {sx} {sz}')
            if e.get('keep'):
                lines.append(f'keep {kind} {sx} {sz}')
            if 'flatten' in e:
                lines.append(f'flatten {kind} {sx} {sz} ' + ' '.join(str(int(v)) for v in e['flatten']))
            if 'light' in e:
                lines.append(f'light {kind} {sx} {sz} ' + ' '.join(str(int(v)) for v in e['light']))
    for b in body.get('boxes', []):
        lines.append('box ' + ' '.join(str(int(v)) for v in b))
    return '\n'.join(lines)


def paint_to_page(edits, boxes):
    return {
        'edits': {kind: {f'{k[0]},{k[1]}': {n: (list(v) if isinstance(v, tuple) else v) for n, v in e.items()}
                         for k, e in sectors.items() if e} for kind, sectors in edits.items()},
        'boxes': [list(b) for b in boxes],
    }


def read_saved(model):
    if not os.path.exists(model.save_path):
        return parse_paint(model, '')
    with open(model.save_path, encoding='utf-8') as f:
        return parse_paint(model, f.read())


# ---- the map and its budget

def fold(model, edits):
    """Who holds each spot of each plane, the light step, and the budget. The page's computeFold."""
    rooms = model.lvl.rooms
    planes = {}
    for kind in model.planes:
        base = model.base[kind]
        sectors = edits.get(kind, {})
        claims = []
        for (sx, sz), e in sorted(sectors.items()):
            if 'flatten' in e:
                room, quad = e['flatten']
                q = rooms[room].quads[quad]
                t = tile_of(rooms[room], q)
                claims.append({'at': (sx, sz), 'tex': q[4], 't': t['turned'], 'h': t['fliph'], 'v': t['flipv'],
                               'light': e.get('light', t['light']), 'painted': True})
            elif 'tile' in e:
                tex, turned, fliph, flipv = e['tile']
                own = base[(sx, sz)]['light'] if (sx, sz) in base else [NEUTRAL] * 4
                claims.append({'at': (sx, sz), 'tex': tex, 't': turned, 'h': fliph, 'v': flipv,
                               'light': e.get('light', own), 'painted': True})
        for at, f in base.items():
            e = sectors.get(at, {})
            if not any(n in e for n in ('tile', 'flatten', 'blank', 'keep')):
                claims.append({'at': at, 'tex': f['tex'], 't': f['turned'], 'h': f['fliph'], 'v': f['flipv'],
                               'light': e.get('light', f['light']), 'painted': False})
        spots = {}
        for c in claims:
            c['levels'] = tile_levels(c['light'])
            spots.setdefault((c['at'][0] % MAP_SECTORS, c['at'][1] % MAP_SECTORS), []).append(c)
        planes[kind] = spots

    levels = set()
    pictures = set()
    for spots in planes.values():
        for claims in spots.values():
            w = claims[0]
            pictures.add((w['tex'], w['t']))
            levels.update((w['tex'], k) for k in w['levels'])
    step = None
    used = set()
    for s in range(1, LEVELS):
        used = {(tex, int(k / s + 0.5)) for tex, k in levels}
        if len(used) <= MAX_PALETTES:
            step = s
            break

    conflicts = losers = sharing = pages = 0
    for spots in planes.values():
        pages += len({(m[0] // BLOCK_SECTORS, m[1] // BLOCK_SECTORS) for m in spots})

    def signature(c):
        s = step or LEVELS - 1
        return (c['tex'], c['t'], c['h'], c['v'], tuple(int(k / s + 0.5) for k in c['levels']))

    for spots in planes.values():
        for claims in spots.values():
            if len(claims) < 2:
                continue
            w = signature(claims[0])
            lost = sum(signature(c) != w for c in claims[1:])
            if lost:
                conflicts += 1
                losers += lost
            sharing += len(claims) - 1 - lost
    cells = len(pictures) * PICTURE_BYTES + BLANK_TILE_BYTES
    names = (pages + 1) * NAME_BLOCK_BYTES
    total = cells + names + DEPTH_TABLE_BYTES + PARAMETER_BYTES + model.inventory
    return {
        'pictures': len(pictures), 'cells': cells, 'palettes': len(used), 'step': step, 'blocks': pages, 'names': names,
        'conflicts': conflicts, 'losers': losers, 'sharing': sharing, 'depth': DEPTH_TABLE_BYTES,
        'parameters': PARAMETER_BYTES, 'inventory': model.inventory, 'total': total, 'bank': BANK_BYTES,
    }


def budget_text(b):
    def kb(v):
        return f'{v / 1024:.1f} KB'

    step = b['step'] if b['step'] else 'none fits'
    return '\n'.join([
        f"Pictures   {b['pictures']} ({kb(b['cells'])} of cells, with the blank tile)",
        f"Palettes   {b['palettes']} of {MAX_PALETTES}, light step {step}",
        f"Blocks     {b['blocks']} + 1 blank ({kb(b['names'])} of names)",
        f"Conflicts  {b['conflicts']} spots, {b['losers']} tiles lost, {b['sharing']} tiles sharing a spot with an identical one",
        f"Depth      {kb(b['depth'])}",
        f"Parameters {kb(b['parameters'])}",
        f"Inventory  {kb(b['inventory'])}",
        f"Total      {kb(b['total'])} of {b['bank'] // 1024} KB",
    ])


# ---- the server

class Server:
    def __init__(self, model):
        self.model = model
        self.lock = threading.Lock()
        self.level = json.dumps(model.page_json(), separators=(',', ':')).encode()
        self.atlas = model.atlas()


class Handler(BaseHTTPRequestHandler):
    server_version = 'floor_paint'
    sys_version = ''

    def log_message(self, *args):
        pass

    def send(self, code, kind, body):
        self.send_response(code)
        self.send_header('Content-Type', kind)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(body)

    def reply(self, value, code=200):
        self.send(code, 'application/json', json.dumps(value).encode())

    def local(self):
        """Only this machine's address, so no page elsewhere can reach the game's art by DNS."""
        host = self.headers.get('Host', '').rsplit(':', 1)[0]
        if host in ('127.0.0.1', 'localhost'):
            return True
        self.send_error(403)
        return False

    def do_GET(self):
        if not self.local():
            return
        model = self.server.state.model
        path = self.path.split('?', 1)[0]
        if path == '/':
            with open(PAGE, 'rb') as f:
                self.send(200, 'text/html; charset=utf-8', f.read())
        elif path == '/level.json':
            self.send(200, 'application/json', self.server.state.level)
        elif path == '/atlas.bin':
            self.send(200, 'application/octet-stream', self.server.state.atlas)
        elif path == '/api/paint':
            with self.server.state.lock:
                edits, boxes, comments, problems = read_saved(model)
            self.reply(dict(paint_to_page(edits, boxes), problems=problems, comments=comments,
                            path=os.path.relpath(model.save_path, ROOT).replace('\\', '/')))
        elif path == '/api/budget':
            with self.server.state.lock:
                edits = read_saved(model)[0]
            self.reply(fold(model, edits))
        else:
            self.send_error(404)

    def do_POST(self):
        if not self.local():
            return
        # a header no form or plain cross-site request can send
        if self.headers.get('X-Floor-Paint') != '1':
            self.send_error(403)
            return
        path = self.path.split('?', 1)[0]
        if path not in ('/api/save', '/api/text'):
            self.send_error(404)
            return
        model = self.server.state.model
        try:
            body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            edits, boxes, _, problems = parse_paint(model, paint_from_page(body))
        except (ValueError, TypeError, AttributeError, KeyError) as e:
            self.reply({'ok': False, 'problems': [f'not readable: {e}']}, 400)
            return
        if problems:
            self.reply({'ok': False, 'problems': problems}, 400)
            return
        with self.server.state.lock:
            comments = read_saved(model)[2]
            text = write_paint(model, edits, boxes, comments)
            if path == '/api/text':
                b = fold(model, edits)
                self.reply({'ok': True, 'text': text, 'budget': b, 'budgetText': budget_text(b)})
                return
            os.makedirs(os.path.dirname(model.save_path), exist_ok=True)
            partial = model.save_path + '.partial'
            with open(partial, 'w', encoding='utf-8', newline='\n') as f:
                f.write(text)
            os.replace(partial, model.save_path)
        self.reply({'ok': True, 'path': os.path.relpath(model.save_path, ROOT).replace('\\', '/'), 'text': text})


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('level', nargs='?', default='LEVEL3A', help=f'one of {", ".join(sorted(PLANES))}')
    ap.add_argument('--port', type=int, default=8766)
    ap.add_argument('--no-open', action='store_true', help="don't open a browser")
    ap.add_argument('--budget', action='store_true', help="print the saved paint's budget and leave")
    ap.add_argument('--save-dir', default=SAVE_DIR, help='read and save the paint here instead: a trial, or a test')
    args = ap.parse_args()
    if args.level not in PLANES:
        sys.exit(f'{args.level}: no plane heights for it in PLANES, in {os.path.basename(__file__)}')
    model = Model(args.level, os.path.abspath(args.save_dir))
    if args.budget:
        edits, boxes, _, problems = read_saved(model)
        for p in problems:
            print(p)
        faces = ', '.join(f'{kind} {len(base)} faces' for kind, base in model.base.items())
        print(f'{args.level}: {faces}, {sum(len(e) for e in edits.values())} sectors changed, {len(boxes)} boxes')
        print(budget_text(fold(model, edits)))
        return
    server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    server.state = Server(model)
    url = f'http://127.0.0.1:{args.port}/'
    print(f'floor paint, {args.level}: {url}   (Ctrl+C to stop)', flush=True)
    if not args.no_open:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
