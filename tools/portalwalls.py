"""Portals with walls standing on them: how much of each portal is really open from the side it's looked through.
python -I tools/portalwalls.py [LEVEL ...]    each level's total; with levels named, a row per portal at least a quarter covered"""
import os
import sys

os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, 'tools')
import trsat  # noqa: E402

LEVELS = sys.argv[1:] or ['LEVEL1', 'LEVEL2', 'LEVEL3A', 'LEVEL3B', 'LEVEL4', 'LEVEL5', 'LEVEL6', 'LEVEL7A', 'LEVEL7B', 'LEVEL8A', 'LEVEL8B', 'LEVEL8C', 'LEVEL10A', 'LEVEL10B', 'LEVEL10C']
VERBOSE = len(sys.argv) > 1
CELL = 64   # in the portal's plane, fine enough for slopes' stair edges


def plane_faces(room):
    """Faces flat on an axis plane: (axis, coordinate, seen from + side?, corners in world)."""
    out = []
    for q, count in [(q, 4) for q in room.quads] + [(t, 3) for t in room.triangles]:
        p = [(room.x + room.vertices[i][0], room.vertices[i][1], room.z + room.vertices[i][2]) for i in q[:count]]
        for axis in range(3):
            if len({a[axis] for a in p}) == 1:
                ax = [p[1][k] - p[0][k] for k in range(3)]
                bx = [p[2][k] - p[1][k] for k in range(3)]
                n = (ax[1] * bx[2] - ax[2] * bx[1], ax[2] * bx[0] - ax[0] * bx[2], ax[0] * bx[1] - ax[1] * bx[0])
                # floors' cross product points down (+y) though they're seen from above: seen from the side opposite it
                out.append((axis, p[0][axis], n[axis] < 0, p, q[count]))
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


def survey(name):
    lvl = trsat.load('external_resources/disc/files/DATA', name)
    cache = {}

    def faces_of(rn):
        if rn not in cache:
            cache[rn] = plane_faces(lvl.rooms[rn])
        return cache[rn]

    total = closed = 0
    rows = []
    for room in lvl.rooms:
        for to, normal, verts in room.portals:
            w = [(room.x + v[0], v[1], room.z + v[2]) for v in verts]
            axis = next(a for a in range(3) if normal[a])
            if len({p[axis] for p in w}) != 1:
                continue
            plane = w[0][axis]
            ua, va = [a for a in range(3) if a != axis]
            u0, u1 = min(p[ua] for p in w), max(p[ua] for p in w)
            v0, v1 = min(p[va] for p in w), max(p[va] for p in w)
            # looked through from this room: the normal points into this room
            from_plus = normal[axis] > 0
            walls = []
            for rn in (room.index, to):
                for fa, coord, seen_plus, p, tex in faces_of(rn):
                    # room data sits a unit off the plane either side
                    if fa == axis and abs(coord - plane) <= 1 and seen_plus == from_plus:
                        walls.append([(a[ua], a[va]) for a in p])
            nu, nv = max(1, (u1 - u0) // CELL), max(1, (v1 - v0) // CELL)
            cov = 0
            openu = []
            openv = []
            for iu in range(nu):
                uu = u0 + (iu + 0.5) * (u1 - u0) / nu
                for iv in range(nv):
                    vv = v0 + (iv + 0.5) * (v1 - v0) / nv
                    if any(inside(poly, uu, vv) for poly in walls):
                        cov += 1
                    else:
                        openu.append(uu)
                        openv.append(vv)
            area = (u1 - u0) * (v1 - v0)
            share = cov / (nu * nv)
            total += area
            closed += area * share
            if share >= 0.25:
                rows.append((share, area, room.index, to, 'xyz'[axis], plane, u0, u1, v0, v1, len(walls),
                             (min(openu), max(openu), min(openv), max(openv)) if openu else None))
    print(f'{name}: {len(rows)} portals at least a quarter covered, {closed * 100 / max(total, 1):.1f}% of all portal area covered')
    if VERBOSE:
        for r in sorted(rows, key=lambda r: -r[1] * r[0])[:30]:
            print(f'   room {r[2]:3d} -> {r[3]:3d} {r[4]}={r[5]:6d} {"xyz"} u {r[6]}..{r[7]} v {r[8]}..{r[9]}  covered {r[0] * 100:3.0f}% by {r[10]} walls; open part {r[11]}')


for lv in LEVELS:
    survey(lv)
