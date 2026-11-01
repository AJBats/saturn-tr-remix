"""The order in which a level's rooms take high work RAM for their faces
(saturn/os.cpp osPlaceRoomFaces places them in it until it is full; the rest
stay in low work RAM, which the CPUs read about 3 times slower and which
holds the bus longer).

The views that see the most faces cost a frame the most, so their rooms go
first. Random views about the level (a place in a room, a heading, a pitch)
are walked much as the renderer walks them (renderer/rooms.s: portals facing
the camera, each room's window the union of those it is seen through, a
portal cut by the near plane opening to the frame's edges); the fifth with
the most faces are the heavy views. A room scores the faces of every heavy
view it is seen in and goes before rooms that score less; rooms no heavy
view sees follow, those seen most often first.

Portals lead to a room, never to its alternate (flip maps swap the two's
faces in place), so alternates are left out: a room with one takes room for
the larger faces of the two, for whichever is in place.

Where the Cistern (LEVEL7A) has room for under half its faces, held-out
heavy views read 5% of theirs from low work RAM in this order, 72% in the
rooms' own order.
"""
import math
import random

W, H = 352, 224
F = 220.0               # focal length, pixels: about the game's
VIEW_MAX = 1024 * 20    # the draw distance
VIEWS = 2000
HEAVY = 0.8             # views past this fraction of them by faces are heavy


class _Walk:
    """The rooms one view sees: each room entered again only when its window
    grew (rooms.s)."""

    def __init__(self, rooms, cam, rot):
        self.rooms, self.cam, self.rot = rooms, cam, rot
        self.clip = {}
        self.listed = set()
        self.depth = 0

    def view(self, p):
        d = [p[k] - self.cam[k] for k in range(3)]
        r = self.rot
        return [r[i][0] * d[0] + r[i][1] * d[1] + r[i][2] * d[2] for i in range(3)]

    def check(self, index, portal):
        room = self.rooms[index]
        target, n, corners = portal
        ox, oz = room.x, room.z
        c0 = (corners[0][0] + ox, corners[0][1], corners[0][2] + oz)
        if n[0] * (c0[0] - self.cam[0]) + n[1] * (c0[1] - self.cam[1]) + n[2] * (c0[2] - self.cam[2]) >= 0:
            return False
        x0, y0, x1, y1 = math.inf, math.inf, -math.inf, -math.inf
        behind = far = 0
        for c in corners:
            v = self.view((c[0] + ox, c[1], c[2] + oz))
            z = v[2]
            if z <= 0:
                behind += 1
                continue
            if z > VIEW_MAX:
                far += 1
                z = VIEW_MAX
            sx, sy = v[0] / z * F + W / 2, v[1] / z * F + H / 2
            x0, x1, y0, y1 = min(x0, sx), max(x1, sx), min(y0, sy), max(y1, sy)
        if behind == 4 or far == 4:
            return False
        if behind:
            x0, y0, x1, y1 = min(x0, 0), min(y0, 0), max(x1, W), max(y1, H)
        cx0, cy0, cx1, cy1 = self.clip[index]
        x0, y0, x1, y1 = max(x0, cx0), max(y0, cy0), min(x1, cx1), min(y1, cy1)
        if not (x0 < x1 and y0 < y1):
            return False
        nx0, ny0, nx1, ny1 = self.clip.get(target, (math.inf, math.inf, -math.inf, -math.inf))
        self.clip[target] = (min(nx0, x0), min(ny0, y0), max(nx1, x1), max(ny1, y1))
        return x0 < nx0 or y0 < ny0 or x1 > nx1 or y1 > ny1

    def visit(self, index):
        self.depth += 1
        if self.depth > 300:
            raise RecursionError    # a walk that loops: the view is dropped
        for portal in self.rooms[index].portals:
            if self.check(index, portal):
                self.visit(portal[0])
                self.listed.add(portal[0])
        self.depth -= 1

    def run(self, camera_room):
        self.clip[camera_room] = (0, 0, W, H)
        self.listed.add(camera_room)
        self.visit(camera_room)
        return self.listed


def _random_view(rng, rooms, places):
    index = rng.choice(places)
    room = rooms[index]
    x = room.x + rng.uniform(1024, (room.x_sectors - 1) * 1024)
    z = room.z + rng.uniform(1024, (room.z_sectors - 1) * 1024)
    if room.y_bottom - room.y_top > 600:
        y = rng.uniform(room.y_top + 256, room.y_bottom - 256)
    else:
        y = (room.y_top + room.y_bottom) / 2
    yaw = rng.uniform(0, 2 * math.pi)
    pitch = rng.uniform(-0.5, 0.5)
    cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    fwd = (sy * cp, sp, cy * cp)
    right = (cy, 0, -sy)
    down = (fwd[1] * right[2] - fwd[2] * right[1], fwd[2] * right[0] - fwd[0] * right[2],
            fwd[0] * right[1] - fwd[1] * right[0])
    if down[1] < 0:
        down = tuple(-c for c in down)
    return index, (x, y, z), (right, down, fwd)


def room_order(lvl):
    """The level's rooms but alternates, the first to take high work RAM
    first."""
    rooms = lvl.rooms
    size = [len(r.quads) + len(r.triangles) for r in rooms]
    places = [i for i, r in enumerate(rooms) if r.x_sectors >= 3 and r.z_sectors >= 3 and r.portals]
    rng = random.Random(1)
    views = []
    while places and len(views) < VIEWS:
        camera_room, cam, rot = _random_view(rng, rooms, places)
        try:
            views.append(_Walk(rooms, cam, rot).run(camera_room))
        except RecursionError:
            continue
    costs = sorted(sum(size[r] for r in v) for v in views)
    heavy = costs[int(len(costs) * HEAVY)] if costs else 0
    score = [0] * len(rooms)
    seen = [0] * len(rooms)
    for v in views:
        cost = sum(size[r] for r in v)
        for r in v:
            seen[r] += 1
            if cost >= heavy:
                score[r] += cost
    alternates = {r.alternate for r in rooms if 0 <= r.alternate < len(rooms)}
    return sorted((r for r in range(len(rooms)) if r not in alternates), key=lambda r: (-score[r], -seen[r], r))
