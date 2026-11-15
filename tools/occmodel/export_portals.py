"""Each portal cut to what's open (issue 298), for occmodel's portals mode: faces standing flat on a
portal's plane, seen from the side it's looked through (this room's or the room behind's, as
tools/portalwalls.py finds them), close part of it. A 64-unit cell is closed only if its middle and
all four corners are under a wall, so nothing open is cut.
  bound  each portal the one rectangle round its open cells (none: the portal dropped)
  split  each portal as rectangles covering exactly its open cells, largest first

usage: python -I tools/occmodel/export_portals.py LEVEL OUT.bin bound|split
Format: int rooms; per room int portals; per portal int target, normal xyz, four corners xyz (world).
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, ROOT + '/tools')
import trsat  # noqa: E402

CELL = 64


def plane_faces(room):
    """Faces flat on an axis plane: (axis, coordinate, seen from + side?, corners in world)."""
    out = []
    for q, count in [(q, 4) for q in room.quads] + [(t, 3) for t in room.triangles]:
        p = [(room.x + room.vertices[i][0], room.vertices[i][1], room.z + room.vertices[i][2]) for i in q[:count]]
        if count == 4 and q[5]:
            continue        # see-through: doesn't close anything
        for axis in range(3):
            if len({a[axis] for a in p}) == 1:
                ax = [p[1][k] - p[0][k] for k in range(3)]
                bx = [p[2][k] - p[1][k] for k in range(3)]
                n = (ax[1] * bx[2] - ax[2] * bx[1], ax[2] * bx[0] - ax[0] * bx[2], ax[0] * bx[1] - ax[1] * bx[0])
                out.append((axis, p[0][axis], n[axis] < 0, p))
                break
    return out


def inside(poly, u, v):
    hit = False
    n = len(poly)
    for i in range(n):
        u0, v0 = poly[i]
        u1, v1 = poly[(i + 1) % n]
        if (v0 > v) != (v1 > v) and u < u0 + (v - v0) * (u1 - u0) / (v1 - v0):
            hit = not hit
    return hit


def under(walls, u, v):
    """On or inside a wall (edges count, so walls meeting edge to edge close the line between them)."""
    for poly in walls:
        us = [a[0] for a in poly]
        vs = [a[1] for a in poly]
        if not (min(us) - 0.01 <= u <= max(us) + 0.01 and min(vs) - 0.01 <= v <= max(vs) + 0.01):
            continue
        if inside(poly, u, v):
            return True
        for du, dv in ((0.02, 0.02), (-0.02, 0.02), (0.02, -0.02), (-0.02, -0.02)):
            if inside(poly, u + du, v + dv):
                return True
    return False


def rects_of(grid, w, h):
    """Rectangles covering exactly the open cells: the largest each time."""
    out = []
    grid = [row[:] for row in grid]
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
                    area = (i - s) * height
                    if height and (best is None or area > best[0]):
                        best = (area, s, i, j + 1 - height, j + 1)
                    start = s
                stack.append((start, cur))
        if best is None:
            return out
        area, i0, i1, j0, j1 = best
        out.append((i0, i1, j0, j1))
        for j in range(j0, j1):
            for i in range(i0, i1):
                grid[j][i] = False


def main():
    level, path, mode = sys.argv[1], sys.argv[2], sys.argv[3]
    lvl = trsat.Level()
    lvl.name = level
    trsat.read_sat(lvl, open(f'{ROOT}/external_resources/disc/files/DATA/{level}.SAT', 'rb').read())
    cache = {}

    def faces_of(rn):
        if rn not in cache:
            cache[rn] = plane_faces(lvl.rooms[rn])
        return cache[rn]

    b = bytearray(struct.pack('<i', len(lvl.rooms)))
    before = after = 0
    total_portals = dropped = changed = 0
    for room in lvl.rooms:
        out = []
        for to, normal, verts in room.portals:
            w = [(room.x + v[0], v[1], room.z + v[2]) for v in verts]
            total_portals += 1
            axis = next((a for a in range(3) if normal[a]), None)
            if axis is None or len({p[axis] for p in w}) != 1:
                out.append((to, normal, w))
                continue
            plane = w[0][axis]
            ua, va = [a for a in range(3) if a != axis]
            u0, u1 = min(p[ua] for p in w), max(p[ua] for p in w)
            v0, v1 = min(p[va] for p in w), max(p[va] for p in w)
            from_plus = normal[axis] > 0
            walls = []
            for rn in (room.index, to):
                for fa, coord, seen_plus, p in faces_of(rn):
                    if fa == axis and abs(coord - plane) <= 1 and seen_plus == from_plus:
                        walls.append([(a[ua], a[va]) for a in p])
            nu, nv = max(1, (u1 - u0) // CELL), max(1, (v1 - v0) // CELL)
            du, dv = (u1 - u0) / nu, (v1 - v0) / nv
            grid = []
            for iv in range(nv):
                row = []
                for iu in range(nu):
                    pts = [(u0 + (iu + 0.5) * du, v0 + (iv + 0.5) * dv)] + \
                          [(u0 + (iu + a) * du, v0 + (iv + c) * dv) for a in (0, 1) for c in (0, 1)]
                    row.append(not (walls and all(under(walls, pu, pv) for pu, pv in pts)))
                grid.append(row)
            area = (u1 - u0) * (v1 - v0)
            before += area
            if all(all(r) for r in grid):
                out.append((to, normal, w))
                after += area
                continue
            changed += 1
            if mode == 'bound':
                cells = [(iu, iv) for iv in range(nv) for iu in range(nu) if grid[iv][iu]]
                rects = [(min(c[0] for c in cells), max(c[0] for c in cells) + 1, min(c[1] for c in cells), max(c[1] for c in cells) + 1)] if cells else []
            else:
                rects = rects_of(grid, nu, nv)
            if not rects:
                dropped += 1
            for i0, i1, j0, j1 in rects:
                corners = []
                for uu, vv in ((u0 + i0 * du, v0 + j0 * dv), (u0 + i1 * du, v0 + j0 * dv), (u0 + i1 * du, v0 + j1 * dv), (u0 + i0 * du, v0 + j1 * dv)):
                    c = [0, 0, 0]
                    c[axis], c[ua], c[va] = plane, round(uu), round(vv)
                    corners.append(tuple(c))
                out.append((to, normal, corners))
                after += (i1 - i0) * du * (j1 - j0) * dv
        b += struct.pack('<i', len(out))
        for to, normal, corners in out:
            b += struct.pack('<4i', to, *normal)
            for c in corners:
                b += struct.pack('<3i', *c)
    open(path, 'wb').write(b)
    print(f'{level} {mode}: {total_portals} portals, {changed} cut, {dropped} dropped, area {100 * after / max(1, before):.1f}% of before')


main()
