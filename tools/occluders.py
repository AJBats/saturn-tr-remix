"""saturn/occluders.txt to a C header, and a check of its boxes against the rooms.

occluders.py IN OUT           the header: gOccluders
occluders.py check LEVEL      a level's boxes (LEVEL7A), against every room of it,
                              alternates too: room vertices strictly inside a box (the
                              box isn't solid there), and how much of each side real
                              faces cover. A side no face covers is fine in rock, where
                              what covers it is further out; on a pillar it means the
                              box is bigger than the pillar.
"""
import os
import re
import sys

LINE = re.compile(r'(LVL_\w+)' + r'\s+(-?\d+)' * 6 + r'(?:\s+([+-][xyz]))?$')
AXES = 'xyz'
DEFAULT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'saturn', 'occluders.txt')


def parse(src):
    """(level, x0, x1, y0, y1, z0, z1) each."""
    boxes = []
    for n, line in enumerate(open(src), 1):
        line = line.split('#')[0].strip()
        if not line:
            continue
        m = LINE.match(line)
        if not m:
            sys.exit(f'{src}:{n}: not "level x0 x1 y0 y1 z0 z1": {line}')
        v = [int(a) for a in m.groups()[1:7]]
        if v[0] > v[1] or v[2] > v[3] or v[4] > v[5]:
            sys.exit(f'{src}:{n}: a low above its high: {line}')
        if any(a % 256 for a in v):
            sys.exit(f'{src}:{n}: not on the 256 grid room vertices lie on: {line}')
        side = 0
        if m.group(8):
            axis = AXES.index(m.group(8)[1])
            if v[2 * axis] != v[2 * axis + 1]:
                sys.exit(f'{src}:{n}: a side given for a box not flat on that axis: {line}')
            side = axis + 1 if m.group(8)[0] == '+' else -(axis + 1)
        boxes.append((m.group(1), *v, side))
    return boxes


def overlap(a0, a1, b0, b1):
    return max(0, min(a1, b1) - max(a0, b0))


def check(name):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import levelgaps
    import trsat
    ids = {'LEVEL7A': 'LVL_TR1_7A', 'LEVEL3A': 'LVL_TR1_3A', 'LEVEL1': 'LVL_TR1_1', 'LEVEL2': 'LVL_TR1_2'}
    level = trsat.load(levelgaps.DATA, name)
    bad = 0
    for box in parse(DEFAULT):
        if box[0] != ids.get(name):
            continue
        box = box[:7]
        lo = (box[1], box[3], box[5])
        hi = (box[2], box[4], box[6])
        inside = []
        covered = {}
        for r in level.rooms:
            world = [(r.x + v[0], v[1], r.z + v[2]) for v in r.vertices]
            for p in world:
                if all(lo[k] < p[k] < hi[k] for k in range(3)):
                    inside.append(r.index)
                    break
            for q in r.quads:
                c = [world[i] for i in q[:4]]
                for k in range(3):
                    vals = {p[k] for p in c}
                    if len(vals) != 1:
                        continue
                    val = vals.pop()
                    if val != lo[k] and val != hi[k]:
                        continue
                    a, b = [j for j in range(3) if j != k]
                    a0, a1 = min(p[a] for p in c), max(p[a] for p in c)
                    b0, b1 = min(p[b] for p in c), max(p[b] for p in c)
                    area = overlap(a0, a1, lo[a], hi[a]) * overlap(b0, b1, lo[b], hi[b])
                    if area:
                        key = (k, val, r.alternate >= 0)
                        covered[key] = covered.get(key, 0) + area
        print(f'box x {lo[0]}..{hi[0]} y {lo[1]}..{hi[1]} z {lo[2]}..{hi[2]}')
        if inside:
            bad += 1
            print(f'  rooms with a vertex strictly inside: {sorted(set(inside))}')
        for k, axis in enumerate('xyz'):
            a, b = [j for j in range(3) if j != k]
            side = (hi[a] - lo[a]) * (hi[b] - lo[b])
            for val in sorted({lo[k], hi[k]}):
                rows = [f'{"has an alternate " if alt else ""}{covered[(k, val, alt)] * 100 // side}%'
                        for alt in (False, True) if (k, val, alt) in covered and side]
                print(f'  side {axis} = {val}: {", ".join(rows) if rows else "no face"}')
    return bad


def main():
    if sys.argv[1] == 'check':
        sys.exit(1 if check(sys.argv[2]) else 0)
    src, out = sys.argv[1], sys.argv[2]
    rows = [f'    {{ {b[0]}, {{ {{ {b[1]}, {b[3]}, {b[5]} }}, {{ {b[2]}, {b[4]}, {b[6]} }}, {b[7]} }} }},' for b in parse(src)]
    with open(out, 'w', newline='\n') as f:
        f.write(f'// Made from {src} by tools/occluders.py\n')
        f.write('static const Occluder gOccluders[] = {\n')
        # with none, one for no level, as gDepthHints has
        f.write('\n'.join(rows) + '\n' if rows else '    { 0xFF, { { 0, 0, 0 }, { 0, 0, 0 }, 0 } },\n')
        f.write('};\n')


if __name__ == '__main__':
    main()
