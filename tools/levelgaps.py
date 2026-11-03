"""Where a level's rooms stand open to the sky, which retail's clear hid and which, with no clear, shows the last frame (issue 267).

A sector with air in it, no room above and no face over it has nothing for a camera looking up. A room with most of its sectors so
has no top: the box of those is kept in room space. A closed room's odd such sector doesn't count: it is rock no camera gets
under. Places these rules can't see, a gap in the rock between two faces, are listed by hand in GAPS. Nothing in the game reads
this now: a fill built on it was taken out (issue 267), and it stays as the list of where a level stands open.

    python -I tools/levelgaps.py [LEVEL ...]    what each level's rooms have
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trsat  # noqa: E402

DATA = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'external_resources', 'disc', 'files', 'DATA')
LEVELS = 'GYM LEVEL1 LEVEL2 LEVEL3A LEVEL3B LEVEL4 LEVEL5 LEVEL6 LEVEL7A LEVEL7B LEVEL8A LEVEL8B LEVEL8C LEVEL10A LEVEL10B LEVEL10C'.split()
WALL = -127
NO_ROOM = 255
MOSTLY_OPEN = 0.5   # of a room's sectors with air

# Gaps in a room's own rock, found in play: level, room, a box in room space.
GAPS = {
    'LEVEL3A': {27: [(7168, -768, 3840, 8192, 0, 4352)]},   # the triangle beside the way in
}


def _inside(px, pz, poly):
    c = False
    n = len(poly)
    for i in range(n):
        x0, z0 = poly[i]
        x1, z1 = poly[(i + 1) % n]
        if (z0 > pz) != (z1 > pz) and px < (x1 - x0) * (pz - z0) / (z1 - z0) + x0:
            c = not c
    return c


def _covered(room):
    """The sectors with a face over them, seen from below."""
    top = set()
    for f in [q[:4] for q in room.quads] + [t[:3] for t in room.triangles]:
        a, b, c = room.vertices[f[0]], room.vertices[f[1]], room.vertices[f[2]]
        ny = (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2])
        if ny >= 0:
            continue
        poly = [(room.vertices[i][0], room.vertices[i][2]) for i in f]
        xs, zs = [p[0] for p in poly], [p[1] for p in poly]
        for sx in range(int(min(xs)) // 1024, int(max(xs)) // 1024 + 1):
            for sz in range(int(min(zs)) // 1024, int(max(zs)) // 1024 + 1):
                if _inside(sx * 1024 + 512, sz * 1024 + 512, poly):
                    top.add((sx, sz))
    return top


def openings(room):
    """The room's open sectors, joined: (x0, y0, z0, x1, y1, z1), a box in room space round each set."""
    top = _covered(room)
    cells = {}
    air = 0
    for sx in range(1, room.x_sectors - 1):
        for sz in range(1, room.z_sectors - 1):
            s = room.sectors[sx * room.z_sectors + sz]
            floor, above, ceiling = s[3], s[4], s[5]
            if floor == WALL or ceiling == WALL or ceiling >= floor:
                continue
            air += 1
            if above == NO_ROOM and (sx, sz) not in top:
                cells[(sx, sz)] = (ceiling * 256, floor * 256)
    if len(cells) < air * MOSTLY_OPEN:
        return []
    # the highest face at each corner of the grid, where the sky begins at that corner;
    # with none there (a room of nothing but air, between other rooms) it is open to the floor
    highest = {}
    for i in {i for f in [q[:4] for q in room.quads] + [t[:3] for t in room.triangles] for i in f}:
        v = room.vertices[i]
        c = (v[0], v[2])
        highest[c] = min(highest.get(c, v[1]), v[1])
    boxes = []
    while cells:
        stack = [next(iter(cells))]
        group = []
        while stack:
            c = stack.pop()
            if c not in cells:
                continue
            group.append((c, cells.pop(c)))
            for d in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                stack.append((c[0] + d[0], c[1] + d[1]))
        rim = [highest.get(k, height[1]) for (sx, sz), height in group for k in ((sx * 1024, sz * 1024), ((sx + 1) * 1024, sz * 1024),
                                                                                 (sx * 1024, (sz + 1) * 1024), ((sx + 1) * 1024, (sz + 1) * 1024))]
        boxes.append((min(c[0][0] for c in group) * 1024, min(c[1][0] for c in group), min(c[0][1] for c in group) * 1024,
                      (max(c[0][0] for c in group) + 1) * 1024, max([c[1][0] for c in group] + rim), (max(c[0][1] for c in group) + 1) * 1024))
    return boxes


def level_gaps(lvl, name):
    """By room: the boxes to fill behind everything."""
    out = {}
    for r in lvl.rooms:
        b = openings(r) + GAPS.get(name, {}).get(r.index, [])
        if b:
            out[r.index] = b
    return out


def main():
    for name in sys.argv[1:] or LEVELS:
        lvl = trsat.load(DATA, name)
        gaps = level_gaps(lvl, name)
        cells = sum((b[3] - b[0]) * (b[5] - b[2]) // (1024 * 1024) for bs in gaps.values() for b in bs)
        print(f'{name:9s} rooms {len(lvl.rooms):3d}, with openings {len(gaps):3d}, boxes {sum(len(b) for b in gaps.values()):4d}, box sectors {cells:5d}')
        if len(sys.argv) > 1:
            for index, bs in sorted(gaps.items()):
                print(f'  room {index:3d}: ' + ' '.join(f'[{b[0]},{b[1]},{b[2]} .. {b[3]},{b[4]},{b[5]}]' for b in bs[:6]) + (f' +{len(bs) - 6}' if len(bs) > 6 else ''))


if __name__ == '__main__':
    main()
