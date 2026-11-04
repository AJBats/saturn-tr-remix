"""What each big flat of a level asks of a plane: its rooms (dry, wet, flipped), and
what lies past it for a camera on its right side, which would paint over it.

    planesurvey.py LEVEL [top] [png_dir]
"""
import contextlib
import io
import os
import sys
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import trsat  # noqa: E402

name = sys.argv[1]
top = int(sys.argv[2]) if len(sys.argv) > 2 else 6
png_dir = sys.argv[3] if len(sys.argv) > 3 else None
with contextlib.redirect_stdout(io.StringIO()):
    lvl = trsat.load(os.path.join(ROOT, 'external_resources', 'disc', 'files', 'DATA'), name)
SECTOR = 1024
WATER = 1

alternates = {r.alternate for r in lvl.rooms if r.alternate not in (None, -1, 0xFFFF, 65535)}
flipping = {r.index for r in lvl.rooms if r.alternate not in (None, -1, 0xFFFF, 65535)}


def world(room, q):
    return [(room.x + room.vertices[i][0], room.vertices[i][1], room.z + room.vertices[i][2]) for i in q[:4]]


def flat_kind(c):
    ax, az = c[1][0] - c[0][0], c[1][2] - c[0][2]
    bx, bz = c[2][0] - c[0][0], c[2][2] - c[0][2]
    cross = ax * bz - az * bx
    if not cross:
        return None
    return 'floor' if cross < 0 else 'ceil'


planes = defaultdict(lambda: defaultdict(list))
total = 0
for room in lvl.rooms:
    total += len(room.quads) + len(room.triangles)
    for q in room.quads:
        c = world(room, q)
        if len({p[1] for p in c}) != 1 or q[5]:
            continue
        kind = flat_kind(c)
        if kind:
            planes[(kind, c[0][1])][room.index].append(c)


def describe(i):
    r = lvl.rooms[i]
    tags = []
    if r.flags & WATER:
        tags.append('wet')
    if i in flipping:
        tags.append('flips')
    if i in alternates:
        tags.append('alt')
    return tags


print(f'{name}: {len(lvl.rooms)} rooms, {total} faces, {len(alternates)} alternate rooms, '
      f'{sum(1 for r in lvl.rooms if r.flags & WATER)} wet rooms')
ranked = sorted(planes.items(), key=lambda kv: -sum(len(v) for v in kv[1].values()))
for (kind, y), rooms in ranked[:top]:
    faces = sum(len(v) for v in rooms.values())
    by = Counter()
    for i, v in rooms.items():
        by['+'.join(describe(i)) or 'dry'] += len(v)
    xs = [p[0] for v in rooms.values() for c in v for p in c]
    zs = [p[2] for v in rooms.values() for c in v for p in c]
    print(f'{kind} y {y}: {faces} faces in {len(rooms)} rooms, by state {dict(by)}, '
          f'x {min(xs)//SECTOR}..{max(xs)//SECTOR} z {min(zs)//SECTOR}..{max(zs)//SECTOR}')
    # past the plane: faces with a corner on the far side of it, in rooms that touch its span
    sign = 1 if kind == 'floor' else -1
    past_faces, past_rooms, touching = 0, Counter(), Counter()
    for room in lvl.rooms:
        n = 0
        for q in list(room.quads) + list(room.triangles):
            ys = [room.vertices[i][1] for i in q[:4 if len(q) > 5 else 3]]
            if any((v - y) * sign > 0 for v in ys):
                n += 1
        if n and room.x < max(xs) and room.x + room.x_sectors * SECTOR > min(xs) and \
                room.z < max(zs) and room.z + room.z_sectors * SECTOR > min(zs):
            past_rooms[room.index] = n
            past_faces += n
            # does a room of the plane have a portal into it?
            for i in rooms:
                if any(p[0] == room.index for p in lvl.rooms[i].portals):
                    touching[room.index] = n
    print(f'   past it, inside its span: {past_faces} faces in {len(past_rooms)} rooms; '
          f'of those through a portal from one of its rooms: {sum(touching.values())} faces in {len(touching)} rooms')
    print('   own rooms with faces past it:', {i: past_rooms[i] for i in rooms if i in past_rooms})
    if png_dir:
        from PIL import Image
        x0, z0 = min(xs) // SECTOR, min(zs) // SECTOR
        w, h = max(xs) // SECTOR - x0, max(zs) // SECTOR - z0
        scale = 8
        im = Image.new('RGB', (w * scale, h * scale), (20, 20, 20))
        px = im.load()
        for i, v in rooms.items():
            tags = describe(i)
            colour = (70, 110, 220) if 'wet' in tags else (200, 170, 90)
            if 'alt' in tags or 'flips' in tags:
                colour = (colour[0] // 2 + 110, colour[1] // 2, colour[2] // 2)
            for c in v:
                sx = min(p[0] for p in c) // SECTOR - x0
                sz = max(p[2] for p in c) // SECTOR - z0 - 1
                for a in range(scale - 1):
                    for b in range(scale - 1):
                        px[sx * scale + a, (h - 1 - sz) * scale + b] = colour
        im.save(os.path.join(png_dir, f'{name}_{kind}_{y}.png'))
