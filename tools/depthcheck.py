"""How wrong a frame's draw order is: depthcheck.py CASE [--png OUT]

CASE names two high work RAM dumps of one frame from Mednafen (see
capture_steps below): CASE_faces.bin, taken in flush_c just after roomsFinish
returns, with both CPUs' ordering tables full, and CASE_cmds.bin, taken at the
next flush_c's entry, with that frame's VDP1 commands still in gCmds. The ELF
beside them (build/depth_cases/0MAIN.elf, copied with the save states) gives
the addresses; a save state runs the binary it was made with, so the ELF must
be that one.

The faces are put in flush's order (far buckets to near, in each the slave's
list before the master's) and checked one for one against the commands built
from them. Then the frame is drawn twice from the faces' corners, the same
coverage both times: in that order, as VDP1 paints it, and with a depth
buffer, nearest wins. A pixel where the two differ is a sorting error; the
report counts them, and the face pairs behind them, by where the faces come
from:

  across rooms      a room's face against another room's, or an object
                    against a room not its own (seen through a portal)
  object on room    a mesh or sprite against a face of the room it stands in
  room in room      two faces of one room
  object on object  two meshes or sprites
  inside a wall     an object with no corner in any room's open space

Shadows (decals on the floor), void fills (behind everything on purpose) and
thin-face lines are left out of both drawings; see-through texels count as
solid.
"""
import argparse
import os
import struct
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import trsat

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NM = os.path.join(ROOT, 'toolchain/gcc-14.2.0/sh2eb-elf/bin/sh2eb-elf-nm')
HWRAM = 0x06000000

FRAME_W, FRAME_H = 352, 224
MASTER_VERTICES = 2048
VERTEX_SIZE = 8
FACE_SIZE = 16
CMD_SIZE = 32
VRAM_FRAME_CMDS = 0x60
ROOM_SIZE = 64
JOB_SIZE = 72
WORK_SIZE = 12

FACE_TRIANGLE = 1 << 31
FACE_PARALLELOGRAM = 1 << 28
FACE_TYPE_SHIFT, FACE_TYPE_MASK = 14, 15
(T_SHADOW, T_F, T_FT, T_FTA, T_GT, T_GTA, T_SPRITE, T_FILL_S,
 T_LINE_H, T_LINE_V, T_AVG, T_VOID) = range(12)
TYPE_NAMES = ['shadow', 'F', 'FT', 'FTA', 'GT', 'GTA', 'sprite', 'fill', 'lineH', 'lineV', 'avg', 'void']
CLIP_FRAME = 1 << 0
CLIP_PLANE = 1 << 5
CMD_SKIP = 0x4000
CMD_END = 0x8000

# An object reaches into the rooms within this of its corners (its box, as
# the game would test it, is never tight).
REACH = 64

# A sorting error: the face painted last is farther than the nearest face by
# more than this fraction of its depth (faces meeting at an edge tie).
DEPTH_EPS = 0.01


def capture_steps(case):
    return f"""Capture with the Mednafen MCP, the build's ELF copied to build/depth_cases:
  load_state build/depth_cases/{case}.mcs
  breakpoint_set <flush_c's return from roomsFinish>;  run_free
  dump_region wram_high build/depth_cases/{case}_faces.bin
  breakpoint_clear;  breakpoint_set <flush_c>;  run_free
  dump_region wram_high build/depth_cases/{case}_cmds.bin;  breakpoint_clear"""


class Symbols:
    def __init__(self, elf):
        out = subprocess.run([NM, '-C', '-S', elf], capture_output=True, text=True, check=True).stdout
        self.addr, self.size = {}, {}
        for line in out.splitlines():
            parts = line.split()
            if len(parts) == 4:
                a, s, _, name = parts
                self.size.setdefault(name, int(s, 16))
            elif len(parts) == 3:
                a, _, name = parts
            else:
                continue
            self.addr.setdefault(name, []).append(int(a, 16))

    def __getitem__(self, name):
        a = self.addr[name]
        if len(a) != 1:
            raise KeyError(f'{name}: {len(a)} symbols')
        return a[0]

    def bss(self, name):
        # statics sharing a name across files: the one in .bss (high work RAM's top)
        return max(self.addr[name])


class Ram:
    def __init__(self, path):
        self.d = open(path, 'rb').read()

    def u32(self, a): return struct.unpack_from('>I', self.d, a - HWRAM)[0]
    def s32(self, a): return struct.unpack_from('>i', self.d, a - HWRAM)[0]
    def u16(self, a): return struct.unpack_from('>H', self.d, a - HWRAM)[0]
    def s16(self, a): return struct.unpack_from('>h', self.d, a - HWRAM)[0]
    def u8(self, a): return self.d[a - HWRAM]


class Face:
    __slots__ = ('order', 'addr', 'room_list', 'flags', 'type', 'corners', 'kind', 'owner', 'room', 'touches', 'face_room', 'bucket', 'label',
                 'skip', 'cmd', 'drawn')

    def __init__(self):
        self.drawn = False
        self.face_room = None


def vertex(ram, a):
    return (ram.s16(a), ram.s16(a + 2), ram.s16(a + 4), ram.u8(a + 7))   # x, y, z, clip


def faces_in_order(ram, sym):
    ot_slave, ot_master = sym['gSlaveOT'], sym['gOT']
    buckets = sym.size['gOT'] // 4
    verts = sym['gVertices']
    faces = []
    for i in range(buckets - 1, -1, -1):
        for room_list, ot in ((True, ot_slave), (False, ot_master)):
            f = ram.u32(ot + 4 * i)
            while f:
                face = Face()
                face.order = len(faces)
                face.bucket = i
                face.addr = f
                face.room_list = room_list
                face.flags = ram.u32(f)
                face.type = (face.flags >> FACE_TYPE_SHIFT) & FACE_TYPE_MASK
                half = verts + (MASTER_VERTICES * VERTEX_SIZE if room_list else 0)
                face.corners = [(half + ram.s16(f + 8 + 2 * k) - verts) // VERTEX_SIZE for k in range(4)]
                faces.append(face)
                f = ram.u32(f + 4)
    return faces


def commands(ram, sym):
    """The frame's commands in VDP1's order: (ctrl, corners) per face slot."""
    base = sym['gCmds']
    out = []
    i = 1   # gCmds[0] clears the frame
    for _ in range(4096):
        a = base + i * CMD_SIZE
        ctrl = ram.u16(a)
        if ctrl & CMD_END:
            return out
        jp = (ctrl >> 12) & 7
        if jp == 5 and (ctrl & 15) == 0 and ram.u32(a + 12) == 0:
            # the link from the slave's commands to the master's
            i = (ram.u16(a + 2) * 8 - VRAM_FRAME_CMDS) // CMD_SIZE
            continue
        xy = [(ram.s16(a + 12 + 4 * k), ram.s16(a + 14 + 4 * k)) for k in range(4)]
        out.append((ctrl, xy))
        i += 1
    raise RuntimeError('no end command')


def walk_order(ram, sym):
    """The frame's rooms as queued: the portal walk's list, deepest first, the camera's room last."""
    rooms = sym['rooms']
    return [(ram.u32(sym['gJobs'] + j * JOB_SIZE) - rooms) // ROOM_SIZE for j in range(ram.s32(sym['gPlaced']))]


def room_owners(ram, sym, lvl):
    """The slave's vertex ranges by room: [(first, last, room index)]."""
    rooms = sym['rooms']
    out = []
    for j in range(ram.s32(sym['gPlaced'])):
        room = (ram.u32(sym['gJobs'] + j * JOB_SIZE) - rooms) // ROOM_SIZE
        base = ram.s32(sym.bss('gWork') + j * WORK_SIZE)
        if base >= 0:
            out.append((base, base + len(lvl.rooms[room].vertices), room))
    return out


def label_faces(faces, ram, sym, lvl):
    owners = room_owners(ram, sym, lvl)
    quad_index = {}
    for r in lvl.rooms:
        for qi, q in enumerate(r.quads):
            quad_index[(r.index, frozenset(q[:4]))] = f'q{qi}'
        for ti, t in enumerate(r.triangles):
            quad_index[(r.index, frozenset(t[:3]))] = f't{ti}'

    for f in faces:
        n = 3 if f.flags & FACE_TRIANGLE else 4
        f.kind, f.owner, f.room, f.touches, f.label = 'object', None, None, set(), ''
        if f.room_list:
            for lo, hi, room in owners:
                if lo <= f.corners[0] < hi:
                    f.kind, f.owner, f.room, f.touches = 'room', room, room, {room}
                    local = frozenset(c - lo for c in f.corners[:n])
                    f.label = f'room {room} ' + quad_index.get((room, local), 'split')
                    break
            else:
                f.label = f'slave mesh v{f.corners[0]}'
        else:
            f.label = f'{TYPE_NAMES[f.type]} v{f.corners[0]}'
    return owners


def objects_by_vertices(faces):
    """Each object face's mesh: faces sharing vertices, joined."""
    parent = {}

    def find(x):
        while parent.setdefault(x, x) != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    for f in faces:
        if f.kind == 'object':
            cs = [('s' if f.room_list else 'm', c) for c in f.corners]
            for c in cs[1:]:
                parent[find(cs[0])] = find(c)
    for f in faces:
        if f.kind == 'object':
            f.owner = find(('s' if f.room_list else 'm', f.corners[0]))


def screen_to_world(ram, sym, lvl, owners):
    """A function taking a vertex (screen x, y, depth z) to world x, y, z.

    The view's rotation is the room jobs' matrix; what's left is linear in a
    room vertex's world position p: z = s (R2 p) + b, x z = a (R0 p) + cx z + c,
    and y likewise. Each room's vertices on the frame (none clamped) are fitted
    alone, as some rooms' don't fit (their far corners); the room with the
    most vertices among those fitting to a pixel gives the map, and solving
    for p undoes it."""
    verts = sym['gVertices']
    job = sym['gJobs']
    m = np.array([ram.s16(job + 4 + 2 * k) for k in range(9)], float).reshape(3, 3)
    rot = m / np.linalg.norm(m, axis=1, keepdims=True)
    best = None
    for lo, hi, room in owners:
        r = lvl.rooms[room]
        pts, scr = [], []
        for k, (x, y, z) in enumerate(v[:3] for v in r.vertices):
            sx, sy, sz, clip = vertex(ram, verts + (lo + k) * VERTEX_SIZE)
            if clip & ~CLIP_FRAME or sz <= 0:
                continue    # clamped: past the guard band or the near plane
            pts.append((x + r.x, y, z + r.z))
            scr.append((sx, sy, sz))
        if len(pts) < 8 or (best and len(pts) <= best[0]):
            continue
        q = np.array(pts, float) @ rot.T     # rotated world positions
        sx, sy, sz = np.array(scr, float).T
        ones = np.ones(len(q))
        (s_, b), *_ = np.linalg.lstsq(np.c_[q[:, 2], ones], sz, rcond=None)
        (ax, cx, c0), *_ = np.linalg.lstsq(np.c_[q[:, 0], sz, ones], sx * sz, rcond=None)
        (ay, cy, c1), *_ = np.linalg.lstsq(np.c_[q[:, 1], sz, ones], sy * sz, rcond=None)
        err = max(np.abs(s_ * q[:, 2] + b - sz).max(),
                  (np.abs(ax * q[:, 0] + cx * sz + c0 - sx * sz) / sz).max(),
                  (np.abs(ay * q[:, 1] + cy * sz + c1 - sy * sz) / sz).max())
        if err <= 2.0:
            best = (len(pts), (s_, b, ax, cx, c0, ay, cy, c1))
    if best is None:
        return None
    s_, b, ax, cx, c0, ay, cy, c1 = best[1]

    def to_world(px, py, pz):
        return rot.T @ np.array(((px * pz - cx * pz - c0) / ax, (py * pz - cy * pz - c1) / ay, (pz - b) / s_))
    return to_world


def door(lvl, floor_index):
    """The room a sector's floor data passes into sideways (a portal's sector), else None."""
    i = floor_index
    while 0 < i < len(lvl.floors):
        w = lvl.floors[i]
        func = w & 0x1F
        if func == 1:
            return lvl.floors[i + 1]
        if func not in (2, 3) or w & 0x8000:
            return None
        i += 2
    return None


def room_at(lvl, rooms, p):
    """The first of rooms holding world point p in one of its open sectors, else None."""
    x, y, z = p
    for i in rooms:
        r = lvl.rooms[i]
        sx, sz = int(x - r.x) // 1024, int(z - r.z) // 1024
        if not (0 <= sx < r.x_sectors and 0 <= sz < r.z_sectors and r.y_top <= y <= r.y_bottom):
            continue
        floor_index, _, _, floor, _, ceiling = r.sectors[sx * r.z_sectors + sz]
        if floor == -127 or door(lvl, floor_index) is not None:
            continue
        return i
    return None


def object_rooms(faces, ram, sym, lvl, owners):
    """Each object face's room: the one holding most of its object's corners."""
    to_world = screen_to_world(ram, sym, lvl, owners)
    if to_world is None:
        return
    verts = sym['gVertices']
    visible = [room for _, _, room in owners]
    others = [r.index for r in lvl.rooms if r.index not in visible]
    def room_of(p):
        room = room_at(lvl, visible, p)
        return room if room is not None else room_at(lvl, others, p)

    votes, reach = {}, {}
    nudges = [np.array(d) for d in ((0, 0, 0), (REACH, 0, 0), (-REACH, 0, 0), (0, REACH, 0),
                                    (0, -REACH, 0), (0, 0, REACH), (0, 0, -REACH))]
    for f in faces:
        f.face_room = f.room
        if f.kind != 'object':
            continue
        points = []
        for c in f.corners:
            sx, sy, sz, clip = vertex(ram, verts + c * VERTEX_SIZE)
            if sz <= 0 or clip & CLIP_PLANE:
                continue
            points.append(to_world(sx, sy, sz))
            v = votes.setdefault(f.owner, {})
            room = room_of(points[-1])
            v[room] = v.get(room, 0) + 1
            r = reach.setdefault(f.owner, set())
            r.update(room_of(points[-1] + d) for d in nudges)
        if points:
            f.face_room = room_of(sum(points) / len(points))
    for f in faces:
        if f.kind == 'object':
            v = votes.get(f.owner, {})
            known = {r: n for r, n in v.items() if r is not None}
            f.room = max(known, key=known.get) if known else None
            f.touches = set(known) | (reach.get(f.owner, set()) - {None})
            if v:
                others = [('in a wall' if r is None else str(r)) for r in v if r != f.room]
                f.label += ((f' (in room {f.room}' if known else ' (in a wall') +
                            (', straddles ' + '/'.join(others) if known and others else '') + ')')


def check_commands(faces, cmds, ram, sym):
    """None if each face's command has the face's corners, else what differs."""
    if len(faces) != len(cmds):
        return f'{len(faces)} faces but {len(cmds)} commands'
    verts = sym['gVertices']
    bad = []
    for f, (ctrl, xy) in zip(faces, cmds):
        f.skip = bool(ctrl & CMD_SKIP)
        f.cmd = xy
        if f.skip or f.type in (T_SPRITE, T_VOID, T_FILL_S, T_LINE_H, T_LINE_V) or f.flags & FACE_PARALLELOGRAM:
            continue
        corners = {vertex(ram, verts + c * VERTEX_SIZE)[:2] for c in f.corners}
        if corners != set(xy):
            bad.append(f.order)
    return None if not bad else f'{len(bad)} faces differ from their commands, first #{bad[0]}'


def polygon(f, ram, sym):
    """The face's screen triangles, each corner (x, y, 1/z), or None and why not."""
    verts = sym['gVertices']
    if f.type in (T_SHADOW, T_VOID, T_FILL_S, T_LINE_H, T_LINE_V):
        return None, TYPE_NAMES[f.type]
    if f.skip:
        return None, 'skipped'
    vs = [vertex(ram, verts + c * VERTEX_SIZE) for c in f.corners]
    if f.type == T_SPRITE:
        (x0, y0, z, _), (x1, y1, _, _) = vs[0], vertex(ram, verts + (f.corners[0] + 1) * VERTEX_SIZE)
        if z <= 0:
            return None, 'behind'
        w = 1.0 / z
        a, b, c, d = (x0, y0, w), (x1, y0, w), (x1, y1, w), (x0, y1, w)
        return [(a, b, c), (a, c, d)], None
    n = 3 if f.flags & (FACE_TRIANGLE | FACE_PARALLELOGRAM) and f.kind == 'object' or f.flags & FACE_TRIANGLE else 4
    vs = vs[:n]
    if any(v[2] <= 0 or v[3] & CLIP_PLANE for v in vs):
        return None, 'near plane'
    p = [(v[0], v[1], 1.0 / v[2]) for v in vs]
    return ([(p[0], p[1], p[2])] if n == 3 else [(p[0], p[1], p[2]), (p[0], p[2], p[3])]), None


def rasterize(tri, face_id, paint, paint_w, best, best_w):
    (x0, y0, w0), (x1, y1, w1), (x2, y2, w2) = tri
    area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
    if area == 0:
        return
    xa, xb = max(min(x0, x1, x2), 0), min(max(x0, x1, x2), FRAME_W - 1)
    ya, yb = max(min(y0, y1, y2), 0), min(max(y0, y1, y2), FRAME_H - 1)
    if xa > xb or ya > yb:
        return
    xs, ys = np.meshgrid(np.arange(xa, xb + 1), np.arange(ya, yb + 1))
    b0 = ((x1 - xs) * (y2 - ys) - (x2 - xs) * (y1 - ys)) / area
    b1 = ((x2 - xs) * (y0 - ys) - (x0 - xs) * (y2 - ys)) / area
    b2 = 1.0 - b0 - b1
    inside = (b0 >= 0) & (b1 >= 0) & (b2 >= 0)
    if not inside.any():
        return
    w = b0 * w0 + b1 * w1 + b2 * w2
    sl = (slice(ya, yb + 1), slice(xa, xb + 1))
    paint[sl][inside] = face_id
    paint_w[sl][inside] = w[inside]
    nearer = inside & (w > best_w[sl])
    best[sl][nearer] = face_id
    best_w[sl][nearer] = w[nearer]


def classify(p, t):
    objs = [f for f in (p, t) if f.kind == 'object']
    if len(objs) == 2:
        return 'object on object'
    if p.room is None or t.room is None:
        return 'inside a wall'
    if p.room != t.room:
        return 'across rooms'
    return 'room in room' if not objs else 'object on room'


ORDERS = {
    'captured': 'as the frame drew them',
    'portal': "rooms in walk order, each with the objects standing in it; by depth within",
    'portal-nearest': "... objects with the latest-drawn room they reach into",
    'portal-face': "... each object face with the room holding its centre",
    'levels': "rooms by portal steps from the camera's room (--levels deep at most), farthest "
              "first, objects with the nearest room they reach into; by depth within",
    'camera': "the camera's room and what reaches into it last, the rest first; by depth within",
    'camera-back': "... but what reaches across rooms with the rest",
    'camera-face': "... but each face of what reaches across rooms with the room holding its centre",
}


def bucket_key(spec):
    """The function taking a frame's bucket to the bucket of the ordering table --coarse describes."""
    if ':' not in spec:
        n = int(spec)
        return lambda b: b >> n
    ranges = []
    for part in spec.split(','):
        end, shift = part.split(':')
        ranges.append((int(end) if end else 1 << 30, int(shift)))

    def key(b):
        start, base = 0, 0
        for end, shift in ranges:
            if b < end:
                return base + ((b - start) >> shift)
            base += (end - start) >> shift
            start = end
        return base
    return key


def portal_levels(lvl, walk):
    """Each queued room's portal steps from the camera's room (the walk's last)."""
    queued = set(walk)
    level = {walk[-1]: 0}
    frontier = [walk[-1]]
    while frontier:
        nxt = []
        for room in frontier:
            for p in lvl.rooms[room].portals:
                if p[0] in queued and p[0] not in level:
                    level[p[0]] = level[room] + 1
                    nxt.append(p[0])
        frontier = nxt
    return level


def draw_order(faces, walk, args, lvl):
    """The faces in the order to paint them.

    Within a layer they go as an ordering table of buckets --coarse bits
    wider than the frame's would hold them: far to near, in a bucket the
    slave's room faces, then its meshes', then the master's, each list
    newest first (faceAdd pushes; pool addresses rise as faces are added)."""
    order = args.order
    if order == 'captured' and args.coarse == '0':
        return faces
    if order in ('captured', 'levels', 'camera', 'camera-back', 'camera-face'):
        level = portal_levels(lvl, walk) if walk else {}
        deepest = max(level.values(), default=0)
        if order == 'captured':
            rank = {}
        elif order.startswith('camera'):
            rank = {room: int(room == walk[-1]) for room in walk}
        else:
            cap = args.levels
            rank = {room: cap - min(n, cap) for room, n in level.items()}

        top = max(rank.values(), default=0)

        def layer(f):
            if f.kind == 'room':
                return rank.get(f.room, 0)
            if order == 'camera-back' and len(f.touches) > 1:
                return min(rank.get(r, 0) for r in f.touches)
            if order == 'camera-face' and len(f.touches) > 1 and f.face_room is not None:
                return rank.get(f.face_room, 0)
            return max((rank.get(r, 0) for r in f.touches), default=top)

        def sub_list(f):
            return 0 if f.kind == 'room' else 1 if f.room_list else 2

        key = bucket_key(args.coarse)
        return sorted(faces, key=lambda f: (layer(f), -key(f.bucket), sub_list(f), -f.addr))

    pos = {room: i for i, room in enumerate(walk)}
    last = len(walk)

    def layer(f):
        if f.kind == 'room' or order == 'portal':
            return pos.get(f.room, last)
        if order == 'portal-face' and f.face_room is not None:
            return pos.get(f.face_room, last)
        return max((pos.get(r, last) for r in f.touches), default=last)

    return sorted(faces, key=lambda f: (layer(f), f.order))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cases', nargs='+', help='names in build/depth_cases (CASE_faces.bin, CASE_cmds.bin)')
    ap.add_argument('--order', choices=ORDERS, default='captured',
                    help='paint order to score: ' + '; '.join(f'{k}: {v}' for k, v in ORDERS.items()))
    ap.add_argument('--levels', type=int, default=1, help='portal steps kept apart (--order levels)')
    ap.add_argument('--coarse', default='0',
                    help="the ordering table's buckets from the frame's: N bits fewer, or by ranges, "
                         "e.g. 256:0,512:1,:2 (below 256 as they are, below 512 halved, the rest quartered)")
    ap.add_argument('--dir', default=os.path.join(ROOT, 'build/depth_cases'))
    ap.add_argument('--elf', help="the cases' build (default: 0MAIN.elf in --dir)")
    ap.add_argument('--level', default='LEVEL1')
    ap.add_argument('--png', help='write the frame (CASE in the name is replaced): faces grey (rooms) '
                                  'and blue (objects), errors red')
    ap.add_argument('--pairs', type=int, default=12, help='face pairs to list')
    ap.add_argument('--check-model', action='store_true',
                    help="check that the ordering table model (draw_order) gives the captured order")
    args = ap.parse_args()
    lvl = trsat.load(os.path.join(ROOT, 'external_resources/disc/files/DATA'), args.level)
    total = 0
    for case in args.cases:
        total += score(args, case, lvl)
    if len(args.cases) > 1:
        print(f'all {len(args.cases)} cases, {args.order}: {total} wrong pixels')


def score(args, case, lvl):
    """Prints case's report; returns its wrong pixels."""
    faces_bin = os.path.join(args.dir, case + '_faces.bin')
    cmds_bin = os.path.join(args.dir, case + '_cmds.bin')
    if not (os.path.exists(faces_bin) and os.path.exists(cmds_bin)):
        sys.exit(capture_steps(case))

    sym = Symbols(args.elf or os.path.join(args.dir, '0MAIN.elf'))
    ram, ram_cmds = Ram(faces_bin), Ram(cmds_bin)

    faces = faces_in_order(ram, sym)
    owners = label_faces(faces, ram, sym, lvl)
    objects_by_vertices(faces)
    object_rooms(faces, ram, sym, lvl, owners)
    err = check_commands(faces, commands(ram_cmds, sym), ram, sym)
    if err:
        sys.exit(f'face order does not match the commands: {err}')

    paint = np.full((FRAME_H, FRAME_W), -1, np.int32)
    best = np.full((FRAME_H, FRAME_W), -1, np.int32)
    paint_w = np.zeros((FRAME_H, FRAME_W))
    best_w = np.zeros((FRAME_H, FRAME_W))
    left_out = {}
    order = draw_order(faces, walk_order(ram, sym), args, lvl)
    if args.order == 'captured' and args.coarse == '0' and args.check_model:
        model = sorted(faces, key=lambda f: (-f.bucket, 0 if f.kind == 'room' else 1 if f.room_list else 2, -f.addr))
        moved = sum(a is not b for a, b in zip(model, faces))
        print(f'ordering table model: {moved} faces out of place')
    for f in order:
        tris, why = polygon(f, ram, sym)
        if tris is None:
            left_out[why] = left_out.get(why, 0) + 1
            continue
        f.drawn = True
        for tri in tris:
            rasterize(tri, f.order, paint, paint_w, best, best_w)

    wrong = (paint != best) & (best >= 0) & (best_w > paint_w * (1 + DEPTH_EPS))
    covered = int((paint >= 0).sum())
    print(f'{case} ({args.order}): {len(faces)} faces, {sum(f.drawn for f in faces)} drawn '
          f'(left out: {", ".join(f"{n} {k}" for k, n in sorted(left_out.items()))})')
    print(f'wrong pixels: {int(wrong.sum())} of {covered} covered ({100.0 * wrong.sum() / max(covered, 1):.2f}%)')

    pairs = {}
    for y, x in zip(*np.nonzero(wrong)):
        key = (int(paint[y, x]), int(best[y, x]))
        pairs[key] = pairs.get(key, 0) + 1
    classes = {}
    for (pi, ti), n in pairs.items():
        c = classify(faces[pi], faces[ti])
        classes[c] = classes.get(c, 0) + n
    for c, n in sorted(classes.items(), key=lambda kv: -kv[1]):
        print(f'  {c:17s} {n:6d}')
    print(f'face pairs (painted over -> should show), worst {args.pairs}:')
    for (pi, ti), n in sorted(pairs.items(), key=lambda kv: -kv[1])[:args.pairs]:
        p, t = faces[pi], faces[ti]
        print(f'  {n:5d} px  {classify(p, t):17s} #{p.order} {p.label}  over  #{t.order} {t.label}')

    if args.png:
        from PIL import Image
        img = np.zeros((FRAME_H, FRAME_W, 3), np.uint8)
        kinds = np.array([0 if f.kind == 'room' else 1 for f in faces] + [2])
        k = kinds[paint]
        near = np.percentile(paint_w[paint >= 0], 99) if covered else 1.0
        shade = (np.sqrt(np.clip(paint_w / near, 0, 1)) * 170 + 50).astype(np.uint8)
        img[..., 0] = np.where(k == 0, shade, np.where(k == 1, shade // 3, 0))
        img[..., 1] = np.where(k == 0, shade, np.where(k == 1, shade // 2, 0))
        img[..., 2] = np.where(k == 0, shade, np.where(k == 1, shade, 0))
        img[wrong] = (255, 40, 40)
        png = args.png.replace('CASE', case)
        Image.fromarray(img).resize((FRAME_W * 3, FRAME_H * 3), Image.NEAREST).save(png)
        print('wrote', png)
    print()
    return int(wrong.sum())


if __name__ == '__main__':
    main()
