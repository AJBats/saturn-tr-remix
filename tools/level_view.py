"""A level with its open places closed by flat quads, for reviewing before any of it goes in the game: what a camera sees
through the portal walk, with whatever nothing covers in magenta (tools/level_view.html).

No frame clear means what no face covers shows the last frame (issue 267). The user's idea: make every area airtight in the
level data, with big flat quads behind everything over the sky, hand-placed where the rules below miss. These are the rules'
first proposal:

    ceiling     a room with no top: one quad over all of it at its ceiling
    side        the same room's outer sides above the rock's rim, where no portal leads on
    other       a room mostly open but not all: a quad over each open patch
    gap         a hole in the rock, listed in GAPS

    python -I tools/level_view.py [LEVEL3A] [build/levelview/lost_valley.html]

The page holds level geometry, so it stays in build/.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import levelgaps  # noqa: E402
import trsat  # noqa: E402
from diet import rgb  # noqa: E402
from pack_level import TextureTable, texture_average  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WALL = -127
NO_ROOM = 255
ALL_SKY = 0.9       # of a room's sectors with air: open this much, it has no top at all

# Where to look from: the user's reports, as their HUD gave them (x y z pitch yaw), and two places near Lara's spots.
# The side of a listed gap a camera sees it from.
GAP_INWARD = {('LEVEL3A', 27): (0, 0, -1)}

SPOTS = {
    'LEVEL3A': [
        ('Looking up, room 56', 41756, 3581, 28336, 0x17E9, 0x8FFA),
        ('Plants, room 51', 41490, 3017, 36766, 0x0047, 0x80FA),
        ('Cave, room 27', 35083, -477, 46347, 0xFFA8, 0x3A45),
        ('Walking in (about)', 38400, 2700, 41500, 0x0000, 0x6000),
        ('T-rex (about)', 44100, 2700, 15900, 0x0000, 0x4000),
        ('Above the valley', 45000, -16000, 28000, 0xC800, 0x0000),
    ],
}


def snap(v):
    return (v + 512) // 1024 * 1024


def orient(points, inward):
    """Corners in the order a face seen from the inward side has: (b - a) x (c - a) points away from that side."""
    a, b, c = points[0], points[1], points[2]
    u = [b[i] - a[i] for i in range(3)]
    v = [c[i] - a[i] for i in range(3)]
    n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    return points if sum(n[i] * inward[i] for i in range(3)) < 0 else points[::-1]


def highest_faces(room):
    """The highest face at each corner of the room's grid, from vertices faces use."""
    highest = {}
    used = {i for f in [q[:4] for q in room.quads] + [t[:3] for t in room.triangles] for i in f}
    for i in used:
        v = room.vertices[i]
        c = (v[0], v[2])
        highest[c] = min(highest.get(c, v[1]), v[1])
    return highest


def side_quads(room, ceiling, highest):
    """A room's sides above the rock, where no portal leads on: from its ceiling down to the lowest rim along each run of cells."""
    out = []
    xs, zs = room.x_sectors, room.z_sectors
    for axis, border, inner, plane, inward in (('x', 0, 1, 1024, (1, 0, 0)), ('x', xs - 1, xs - 2, (xs - 1) * 1024, (-1, 0, 0)),
                                               ('z', 0, 1, 1024, (0, 0, 1)), ('z', zs - 1, zs - 2, (zs - 1) * 1024, (0, 0, -1))):
        led_on = set()
        for _, normal, verts in room.portals:
            along = 0 if axis == 'x' else 2
            other = 2 if axis == 'x' else 0
            if normal[along] and all(snap(v[along]) == plane for v in verts):
                lo, hi = min(snap(v[other]) for v in verts), max(snap(v[other]) for v in verts)
                led_on.update(range(lo // 1024, hi // 1024))
        run = None
        count = zs if axis == 'x' else xs

        def flush():
            nonlocal run
            if run:
                a, b, low = run
                if axis == 'x':
                    points = [(plane, ceiling, a), (plane, ceiling, b), (plane, low, b), (plane, low, a)]
                else:
                    points = [(a, ceiling, plane), (b, ceiling, plane), (b, low, plane), (a, low, plane)]
                out.append((orient(points, inward), inward))
            run = None

        for i in range(1, count - 1):
            if i in led_on:
                flush()
                continue
            s = room.sectors[inner * zs + i] if axis == 'x' else room.sectors[i * zs + inner]
            floor = s[3] * 256 if s[3] != WALL else room.y_bottom
            a, b = i * 1024, (i + 1) * 1024
            keys = ((plane, a), (plane, b)) if axis == 'x' else ((a, plane), (b, plane))
            rim = max(highest.get(keys[0], floor), highest.get(keys[1], floor))
            if rim <= ceiling:
                flush()
                continue
            run = (run[0], b, max(run[2], rim)) if run else (a, b, rim)
        flush()
    return out


def proposals(lvl, name):
    quads = []

    def add(room, kind, points, inward, label):
        world = [[p[0] + room.x, p[1], p[2] + room.z] for p in points]
        quads.append({'room': room.index, 'kind': kind, 'v': world, 'n': list(inward), 'label': label})

    alternates = {r.alternate for r in lvl.rooms if r.alternate >= 0}
    for room in lvl.rooms:
        if room.index in alternates:
            continue
        boxes = levelgaps.openings(room)
        if not boxes:
            continue
        air = open_ = 0
        for sx in range(1, room.x_sectors - 1):
            for sz in range(1, room.z_sectors - 1):
                s = room.sectors[sx * room.z_sectors + sz]
                if s[3] == WALL or s[5] == WALL or s[5] >= s[3]:
                    continue
                air += 1
                open_ += s[4] == NO_ROOM
        if open_ >= air * ALL_SKY:
            ceiling = min(b[1] for b in boxes)
            x1, z1 = (room.x_sectors - 1) * 1024, (room.z_sectors - 1) * 1024
            points = [(1024, ceiling, 1024), (x1, ceiling, 1024), (x1, ceiling, z1), (1024, ceiling, z1)]
            add(room, 'ceiling', orient(points, (0, 1, 0)), (0, 1, 0), f'{room.x_sectors - 2} x {room.z_sectors - 2} sectors at y {ceiling}')
            for points, inward in side_quads(room, ceiling, highest_faces(room)):
                span = max(abs(points[0][0] - points[2][0]), abs(points[0][2] - points[2][2])) // 1024
                add(room, 'side', points, inward, f'{span} sectors, y {points[0][1]} to {points[2][1]}')
        else:
            for b in boxes:
                y = b[1]
                points = [(b[0], y, b[2]), (b[3], y, b[2]), (b[3], y, b[5]), (b[0], y, b[5])]
                add(room, 'other', orient(points, (0, 1, 0)), (0, 1, 0), f'{(b[3] - b[0]) // 1024} x {(b[5] - b[2]) // 1024} sectors at y {y}')
    for index, boxes in levelgaps.GAPS.get(name, {}).items():
        room = lvl.rooms[index]
        for x0, y0, z0, x1, y1, z1 in boxes:
            plane = (z0 + z1) // 2
            inward = GAP_INWARD[(name, index)]
            points = [(x0, y0, plane), (x1, y0, plane), (x1, y1, plane), (x0, y1, plane)]
            add(room, 'gap', orient(points, inward), inward, f'{(x1 - x0) // 1024} sector wide, y {y0} to {y1}')
    return quads


def room_json(room, alternates):
    return {
        'i': room.index, 'x': room.x, 'z': room.z, 'xs': room.x_sectors, 'zs': room.z_sectors, 'alt': room.index in alternates,
        'v': [c for v in room.vertices for c in (room.x + v[0], v[1], room.z + v[2])],
        's': [round((v[3] & 31) / 16, 3) for v in room.vertices],
        'q': [[q[0], q[1], q[2], q[3], q[4], 1 if q[5] else 0] for q in room.quads],
        't': [[t[0], t[1], t[2], t[3]] for t in room.triangles],
        'p': [[to, *normal, *[c for v in verts for c in (room.x + v[0], v[1], room.z + v[2])]] for to, normal, verts in room.portals],
        'f': [s[3] for s in room.sectors], 'c': [s[5] for s in room.sectors], 'b': [s[2] for s in room.sectors],
    }


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else 'LEVEL3A'
    out_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'build', 'levelview', 'lost_valley.html')
    lvl = trsat.load(levelgaps.DATA, name)
    table = TextureTable(lvl)
    means = []
    for i in range(len(lvl.room_textures)):
        pixels, colors, _, _ = table.sources[i]
        means.append(rgb(texture_average(pixels, colors, False)))
    quads = proposals(lvl, name)
    alternates = {r.alternate for r in lvl.rooms if r.alternate >= 0}
    data = {'name': name, 'means': means, 'rooms': [room_json(r, alternates) for r in lvl.rooms], 'quads': quads,
            'spots': [{'name': s[0], 'pos': list(s[1:4]), 'pitch': s[4], 'yaw': s[5]} for s in SPOTS.get(name, [])]}
    html = open(os.path.join(ROOT, 'tools', 'level_view.html'), encoding='utf-8').read()
    html = html.replace('/*LEVEL*/null', json.dumps(data, separators=(',', ':')))
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, 'w', encoding='utf-8', newline='\n') as o:
        o.write(html)
    kinds = {}
    for q in quads:
        kinds[q['kind']] = kinds.get(q['kind'], 0) + 1
    print(out_path, os.path.getsize(out_path) >> 10, 'KB', kinds)


if __name__ == '__main__':
    main()
