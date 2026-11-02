"""A level's rooms for occmodel.c: geometry, portals, sectors, and each room's big solid faces
merged into rectangles (the occluders a packer could bake), largest first.

usage: python -I tools/occmodel/export.py LEVEL OUT.bin
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, ROOT + '/tools')
import trsat  # noqa: E402
from pack_level import shade_from_color16  # noqa: E402

MIN_AREA = 512 * 512    # smaller rectangles hide nothing worth a test
MAX_OCCLUDERS = 32      # a room's, kept


def plane_rect(p):
    """A quad's corners as (axis, coord, u0, u1, v0, v1) if it is a rectangle square to an axis."""
    for axis in (0, 1, 2):
        if len({c[axis] for c in p}) != 1:
            continue
        a, b = [k for k in (0, 1, 2) if k != axis]
        us, vs = sorted({c[a] for c in p}), sorted({c[b] for c in p})
        if len(us) == 2 and len(vs) == 2 and len({(c[a], c[b]) for c in p}) == 4:
            return axis, p[0][axis], us[0], us[1], vs[0], vs[1]
    return None


def normal(p):
    e1 = [p[1][k] - p[0][k] for k in range(3)]
    e2 = [p[2][k] - p[0][k] for k in range(3)]
    return (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])


def merged(rects):
    """The rectangles of a plane merged: the largest in the union, again and again."""
    us = sorted({u for r in rects for u in r[:2]})
    vs = sorted({v for r in rects for v in r[2:]})
    iu = {u: i for i, u in enumerate(us)}
    iv = {v: i for i, v in enumerate(vs)}
    w, h = len(us) - 1, len(vs) - 1
    grid = [[False] * w for _ in range(h)]
    for u0, u1, v0, v1 in rects:
        for j in range(iv[v0], iv[v1]):
            for i in range(iu[u0], iu[u1]):
                grid[j][i] = True
    out = []
    while True:
        best = None
        heights = [0] * w
        for j in range(h):
            for i in range(w):
                heights[i] = heights[i] + 1 if grid[j][i] else 0
            stack = []
            for i in range(w + 1):
                cur = heights[i] if i < w else 0
                start = i
                while stack and stack[-1][1] >= cur:
                    s, height = stack.pop()
                    if height:
                        area = (us[i] - us[s]) * (vs[j + 1] - vs[j + 1 - height])
                        if best is None or area > best[0]:
                            best = (area, s, i, j + 1 - height, j + 1)
                    start = s
                stack.append((start, cur))
        if best is None or best[0] < MIN_AREA:
            return out
        area, i0, i1, j0, j1 = best
        out.append((area, us[i0], us[i1], vs[j0], vs[j1]))
        for j in range(j0, j1):
            for i in range(i0, i1):
                grid[j][i] = False


def occluders(room, world):
    planes = {}
    for q in room.quads:
        if q[5]:
            continue
        p = [world[i] for i in q[:4]]
        r = plane_rect(p)
        if r is None:
            continue
        n = normal(p)
        axis, coord, u0, u1, v0, v1 = r
        planes.setdefault((axis, coord, n[axis] > 0), []).append((u0, u1, v0, v1))
    out = []
    for (axis, coord, positive), rects in planes.items():
        a, b = [k for k in (0, 1, 2) if k != axis]
        n = [0, 0, 0]
        n[axis] = 1 if positive else -1
        for area, u0, u1, v0, v1 in merged(rects):
            corners = []
            for u, v in ((u0, v0), (u1, v0), (u1, v1), (u0, v1)):
                c = [0, 0, 0]
                c[axis], c[a], c[b] = coord, u, v
                corners.extend(c)
            out.append((area, corners, n))
    out.sort(key=lambda o: -o[0])
    return out[:MAX_OCCLUDERS]


def main():
    level, path = sys.argv[1], sys.argv[2]
    lvl = trsat.Level()
    lvl.name = level
    trsat.read_sat(lvl, open(f'{ROOT}/external_resources/disc/files/DATA/{level}.SAT', 'rb').read())
    alternates = {r.alternate for r in lvl.rooms if 0 <= r.alternate < len(lvl.rooms)}
    b = bytearray(struct.pack('<i', len(lvl.rooms)))
    total = 0
    for i, r in enumerate(lvl.rooms):
        world = [(r.x + v[0], v[1], r.z + v[2]) for v in r.vertices]
        occ = occluders(r, world)
        total += len(occ)
        b += struct.pack('<14i', r.x, r.z, r.y_top, r.y_bottom, r.x_sectors, r.z_sectors, int(i in alternates),
                         len(world), len(r.quads), len(r.triangles), len(r.portals), len(r.sectors), len(occ),
                         len(r.static_meshes))
        for v, raw in zip(world, r.vertices):
            b += struct.pack('<4i', *v, shade_from_color16(raw[3]))
        for q in r.quads:
            b += struct.pack('<5i', q[0], q[1], q[2], q[3], int(bool(q[5])))
        for t in r.triangles:
            b += struct.pack('<3i', t[0], t[1], t[2])
        for target, n, corners in r.portals:
            b += struct.pack('<4i', target, *n)
            for c in corners:
                b += struct.pack('<3i', r.x + c[0], c[1], r.z + c[2])
        for s in r.sectors:
            b += struct.pack('<2i', s[3] * 256, s[5] * 256)
        for area, corners, n in occ:
            b += struct.pack('<15i', *corners, *n)
    open(path, 'wb').write(b)
    print(f'{level}: {len(lvl.rooms)} rooms, {total} occluders, {len(b)} bytes')


main()
