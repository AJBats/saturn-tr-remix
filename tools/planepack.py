"""A level's flat floor and ceiling for VDP2's RBG0 (saturn/plane.h,
docs/vdp2-production.md): those faces out of its rooms for good, and their
tiles, cells and colours for bank B of VDP2's VRAM.

saturn/planes.txt names the levels, their heights and the stretch each plane is for.
saturn/plane_paint/LEVEL.txt, tools/floor_paint.py's, holds what's been changed by hand:

    tile SET SX SZ TEX TURNED FLIPH FLIPV   this sector shows this tile
    blank SET SX SZ                         no tile here, and its face gone
    keep SET SX SZ                          this sector's face stays a polygon
    flatten SET SX SZ ROOM QUAD             that face (room.quads index) goes on the plane here
    light SET SX SZ A B C D                 corner levels: -x-z, +x-z, +x+z, -x+z
    box X0 Y0 Z0 X1 Y1 Z1                   an obstruction box (not read here)

SET is floor or ceiling. PLANES=0 in the environment packs the level without a plane.

    plan = plan_for(level)                  None: no plane
    tiles = take_faces(lvl, plan)           out of the rooms, onto the plane
    write_art(base, lvl, textures, tiles)   base.PLC and base.PLN

.PLC, for VRAM at CELLS_AT: the cells (each texture whole, 64 x 64 texels, and turned
when a face lays it across, a blank tile first, in 2 x 2 characters of four 8 x 8 cells),
then a palette of 16 colours for each texture and light step, then PlaneInfo:
floor and ceiling heights, each set's 16 planes as page numbers from the names' base,
each set's map's first sector (x, z), or NO_ORIGIN where the map repeats across the level.

.PLN, for VRAM at NAMES_AT: the names' pages, 4 KB each (two-word names, 32 x 32 of
16 x 16 tiles), page 0 blank.

VDP1's gouraud adds a vertex's level - 16 to each channel, so a palette is the texture's
colours plus its step's, clamped. Light is held per 16 x 16 tile, bilinear across the face
from the corners as VDP1's is.

The map holds 32 x 32 sectors and repeats. Tiles of a level wider than that can want
one spot: identical tiles share it, and of two that differ the first takes it and the
other's face stays a polygon, which draws over the plane and so is right on screen. Who's
first, as tools/floor_paint.py has it: a painted or flattened tile before the level's own;
among those the lowest SX, then SZ; among the level's own, room and quad order.

Flooded copies: a room's alternate holds the same floor. Its face goes too if its room's
tile went and it has the same picture; the plane then serves both, in the dry room's light.
"""
import math
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLANES_FILE = os.path.join(ROOT, 'saturn', 'planes.txt')
PAINT_DIR = os.path.join(ROOT, 'saturn', 'plane_paint')

SETS = ('floor', 'ceiling')
NO_PLANE = 0x7FFFFFFF       # saturn/plane.cpp PLANE_NONE: a height the level hasn't
SECTOR = 1024
TILES_A_SECTOR = 4
PAGE_TILES = 32
PAGE_BYTES = PAGE_TILES * PAGE_TILES * 4
MAP_PLANES = 4              # a side
MAP_TILES = MAP_PLANES * PAGE_TILES
MAP_SECTORS = MAP_TILES // TILES_A_SECTOR
CELLS_AT = 0x40000          # saturn/plane.cpp
TILE_UNITS = 4              # 0x20-byte units a 16 x 16 16-colour character
TILE_BYTES = TILE_UNITS * 0x20
MAX_PAGES = 32              # bank B1 holds 128 KB
PALETTES = 112              # CRAM from 0x700 is the line colours, wet then dry
BLACK_PALETTE = PALETTES - 1
GOURAUD_NEUTRAL = 16
LEVELS = 32
FLIP_H = 0x4000
FLIP_V = 0x8000
STEEP = 6                   # corner levels apart: a sector this steep has its light painted in
TABLE_AT = 0x57D00          # saturn/plane.cpp: where bank B0's room for the .PLC ends
NAMES_AT = 0x60000          # saturn/plane.cpp
B1_END = 0x7FF00            # saturn/plane.cpp
NO_ORIGIN = -32768          # saturn/plane.cpp: a set's map repeats
INFO_BYTES = 12 + 2 * MAP_PLANES * MAP_PLANES + 8


class Plan:
    def __init__(self, level, heights, region, look=''):
        self.level = level
        self.heights = heights      # set -> y
        self.region = region        # 'x', 'z' -> first and last sector of the stretch
        self.look = look            # planes.txt's light: dither, paint, soft, skirt, comma-separated
        self.tile = {}              # (set, sx, sz) -> (tex, turned, flip_h, flip_v)
        self.blank = set()          # (set, sx, sz)
        self.keep = set()           # (set, sx, sz)
        self.flatten = {}           # (set, sx, sz) -> (room, quad)
        self.light = {}             # (set, sx, sz) -> levels


    def within(self, sx, sz):
        """A sector's in the stretch the plane is for."""
        x, z = self.region.get('x'), self.region.get('z')
        return (not x or x[0] <= sx <= x[1]) and (not z or z[0] <= sz <= z[1])


class Tile:
    def __init__(self, tex, turned, flip_h, flip_v, levels):
        self.tex, self.turned, self.flip_h, self.flip_v, self.levels = tex, turned, flip_h, flip_v, levels

    def same(self, other):
        return (self.tex, self.turned, self.flip_h, self.flip_v, self.levels) == \
               (other.tex, other.turned, other.flip_h, other.flip_v, other.levels)


def levels_planned():
    """Each level planes.txt names: its Plan, without what's been changed by hand."""
    plans = {}
    with open(PLANES_FILE) as f:
        for line in f:
            words = line.split('#')[0].split()
            if not words:
                continue
            heights, region, look, i = {}, {}, '', 1
            while i < len(words):
                if words[i] in ('x', 'z'):
                    region[words[i]] = (int(words[i + 1]), int(words[i + 2]))
                    i += 3
                elif words[i] == 'light':
                    look = words[i + 1]
                    i += 2
                else:
                    heights[words[i]] = int(words[i + 1])
                    i += 2
            plans[words[0]] = Plan(words[0], heights, region, look)
    return plans


def plan_for(level):
    """The level's plan, or None if planes.txt doesn't name it."""
    if os.environ.get('PLANES') == '0':
        return None
    plan = levels_planned().get(level)
    if plan is None:
        return None
    path = os.path.join(PAINT_DIR, level + '.txt')
    if os.path.exists(path):
        with open(path) as f:
            for line in f:
                words = line.split('#')[0].split()
                if not words or words[0] == 'box':
                    continue
                kind, key, args = words[0], (words[1], int(words[2]), int(words[3])), [int(w) for w in words[4:]]
                if kind == 'tile':
                    plan.tile[key] = (args[0], bool(args[1]), bool(args[2]), bool(args[3]))
                elif kind == 'blank':
                    plan.blank.add(key)
                elif kind == 'keep':
                    plan.keep.add(key)
                elif kind == 'flatten':
                    plan.flatten[key] = (args[0], args[1])
                elif kind == 'light':
                    plan.light[key] = tuple(args)
                else:
                    raise ValueError(f'{path}: {line.strip()}')
    return plan


def corners_of(room, q):
    """A quad's corners: world x, y, z, level."""
    out = []
    for i in q[:4]:
        v = room.vertices[i]
        out.append((room.x + v[0], v[1], room.z + v[2], v[3] & 31))
    return out


def flat_on(room, q, height, floor):
    """The quad's corners if it is flat at that height, faces the plane's way and is opaque."""
    if q[5]:
        return None
    c = corners_of(room, q)
    if any(p[1] != height for p in c):
        return None
    ux, uz = c[1][0] - c[0][0], c[1][2] - c[0][2]
    ax, az = c[2][0] - c[0][0], c[2][2] - c[0][2]
    winding = ux * az - uz * ax
    if (winding >= 0) if floor else (winding <= 0):
        return None
    return c


def tile_of(tex, c):
    """A face's sector and tile from its corners; None if it isn't a sector square to the axes."""
    a, b, d = c[0], c[1], c[3]
    ux, uz, vx, vz = b[0] - a[0], b[2] - a[2], d[0] - a[0], d[2] - a[2]
    if sorted((abs(ux), abs(uz))) != [0, SECTOR] or sorted((abs(vx), abs(vz))) != [0, SECTOR]:
        return None
    turned = uz != 0
    flip_h = (vx < 0) if turned else (ux < 0)
    flip_v = (uz < 0) if turned else (vz < 0)
    x0, z0 = min(p[0] for p in c), min(p[2] for p in c)
    at = {(p[0] - x0 != 0, p[2] - z0 != 0): p[3] for p in c}
    levels = (at[(False, False)], at[(True, False)], at[(True, True)], at[(False, True)])
    return (x0 // SECTOR, z0 // SECTOR), Tile(tex, turned, flip_h, flip_v, levels)


def map_origin(sectors):
    """The first sector (x, z) of a map that holds these without repeating: of those that do, the one on
    fewest pages of names (4 KB each), then with most room round them (the skirt's); None if none does."""
    xs, zs = [sx for sx, _ in sectors], [sz for _, sz in sectors]
    if max(xs) - min(xs) >= MAP_SECTORS or max(zs) - min(zs) >= MAP_SECTORS:
        return None
    side = PAGE_TILES // TILES_A_SECTOR
    best = None
    for ox in range(max(xs) - MAP_SECTORS + 1, min(xs) + 1):
        for oz in range(max(zs) - MAP_SECTORS + 1, min(zs) + 1):
            pages = len({((sx - ox) // side, (sz - oz) // side) for sx, sz in sectors})
            room = min(min(xs) - ox, ox + MAP_SECTORS - 1 - max(xs), min(zs) - oz, oz + MAP_SECTORS - 1 - max(zs))
            score = (pages, -min(room, 1), abs(ox % side) + abs(oz % side))
            if best is None or score < best[0]:
                best = (score, (ox, oz))
    return best[1]


def take_faces(lvl, plan):
    """The plane's faces out of lvl's rooms, with the vertices nothing else uses; each
    set's tiles, {(map x, map z) in sectors: Tile}, and the heights under 'heights'."""
    animated = set()
    for first, last in lvl.anim_ranges:
        animated.update(range(first, last + 1))
    rooms = lvl.rooms
    primary = {r.alternate: r.index for r in rooms if 0 <= r.alternate < len(rooms)}
    gone = {}           # room index -> quad indices
    tiled = {'heights': plan.heights, 'floods': False, 'origin': {}, 'look': plan.look}
    for kind in SETS:
        height = plan.heights.get(kind)
        if height is None:
            tiled[kind] = {}
            continue
        floor = kind == 'floor'
        wanted = {}     # (sx, sz) -> [Tile, room, quad], room None for a painted tile on no face
        doubled = 0
        for room in rooms:
            if room.index in primary or room.flags & 1:
                continue
            for q_i, q in enumerate(room.quads):
                if q[4] in animated:
                    continue
                c = flat_on(room, q, height, floor)
                made = c and tile_of(q[4], c)
                if not made or not plan.within(*made[0]):
                    continue
                if made[0] in wanted:
                    doubled += 1
                else:
                    wanted[made[0]] = [made[1], room.index, q_i]

        # the hand's changes
        for (s, sx, sz), (r, q_i) in plan.flatten.items():
            if s == kind:
                room = rooms[r]
                q = room.quads[q_i]
                c = corners_of(room, q)
                c = [(p[0], height, p[2], p[3]) for p in c]
                made = tile_of(q[4], c)
                if not made or made[0] != (sx, sz):
                    raise ValueError(f'flatten {s} {sx} {sz}: room {r} quad {q_i} is not that sector')
                wanted[(sx, sz)] = [made[1], r, q_i]
        taken = set()
        for (s, sx, sz) in plan.blank:
            if s == kind and (sx, sz) in wanted:
                _, r, q_i = wanted.pop((sx, sz))
                if r is not None:
                    gone.setdefault(r, set()).add(q_i)
                taken.add((sx, sz))
        for (s, sx, sz) in plan.keep:
            if s == kind:
                wanted.pop((sx, sz), None)
        for (s, sx, sz), (tex, turned, flip_h, flip_v) in plan.tile.items():
            if s != kind:
                continue
            old = wanted.get((sx, sz))
            levels = old[0].levels if old else (GOURAUD_NEUTRAL,) * 4
            wanted[(sx, sz)] = [Tile(tex, turned, flip_h, flip_v, levels), old[1] if old else None, old[2] if old else None]
        for (s, sx, sz), levels in plan.light.items():
            if s == kind and (sx, sz) in wanted:
                wanted[(sx, sz)][0].levels = levels

        # the map's spots: the first of two that differ takes one, the other stays a polygon
        def first(item):
            (sx, sz), (_, r, q_i) = item
            if (kind, sx, sz) in plan.tile or (kind, sx, sz) in plan.flatten:
                return (0, sx, sz)
            return (1, r, q_i)

        # a set whose sectors fit the map has it placed where they are, showing nothing past its
        # edges; else the map repeats across the level
        xs, zs = [sx for sx, _ in wanted] or [0], [sz for _, sz in wanted] or [0]
        ox = oz = 0
        placed = map_origin(wanted) if wanted else None
        if placed:
            ox, oz = tiled['origin'][kind] = placed

        map_tiles = {}
        lost = 0
        for (sx, sz), (tile, r, q_i) in sorted(wanted.items(), key=first):
            spot = ((sx - ox) % MAP_SECTORS, (sz - oz) % MAP_SECTORS)
            if spot in map_tiles and not map_tiles[spot].same(tile):
                lost += 1
                continue
            map_tiles[spot] = tile
            taken.add((sx, sz))
            if r is not None:
                gone.setdefault(r, set()).add(q_i)
        tiled[kind] = map_tiles

        # the flooded copies
        copies = left = 0
        for index, p in primary.items():
            room, dry = rooms[index], rooms[p]
            for q_i, q in enumerate(room.quads):
                c = flat_on(room, q, height, floor)
                made = c and tile_of(q[4], c)
                if not made:
                    continue
                other = wanted.get(made[0])
                if made[0] in taken and other and other[1] == p and other[0].tex == q[4] and \
                        (other[0].turned, other[0].flip_h, other[0].flip_v) == \
                        (made[1].turned, made[1].flip_h, made[1].flip_v):
                    gone.setdefault(index, set()).add(q_i)
                    copies += 1
                    if floor and room.flags & 1:
                        tiled['floods'] = True
                else:
                    left += 1
        span = (f'sectors x {min(xs)}-{max(xs)} z {min(zs)}-{max(zs)}' if wanted else 'no sectors') + \
            (f', the map placed at {ox}, {oz}' if kind in tiled['origin'] else ', the map repeating')
        print(f'{plan.level} {kind} {height}: {len(taken)} tiles, {lost} stay polygons in a spot taken, '
              f'{doubled} doubled, {copies} flooded copies out, {left} stay; {span}')

    vertices_gone = 0
    for index, quads in gone.items():
        room = rooms[index]
        # what was on the plane, for pack_level.py take_masks: each face's texture and corners
        room.plane_faces = [(room.quads[i][4], [room.vertices[v] for v in room.quads[i][:4]]) for i in sorted(quads)]
        room.quads = [q for i, q in enumerate(room.quads) if i not in quads]
        used = set()
        for q in room.quads:
            used.update(q[:4])
        for t in room.triangles:
            used.update(t[:3])
        for vertex, _ in room.sprites:
            used.add(vertex)
        order = [i for i in range(len(room.vertices)) if i in used]
        renumber = {old: new for new, old in enumerate(order)}
        vertices_gone += len(room.vertices) - len(order)
        room.vertices = [room.vertices[i] for i in order]
        room.quads = [(*[renumber[v] for v in q[:4]], q[4], q[5]) for q in room.quads]
        room.triangles = [(*[renumber[v] for v in t[:3]], t[3]) for t in room.triangles]
        room.sprites = [(renumber[vertex], tex) for vertex, tex in room.sprites]
    print(f'{plan.level}: {sum(len(g) for g in gone.values())} faces, {vertices_gone} vertices out of the rooms')
    return tiled


BAYER = ((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5))
SOFT_SIGMA = 0.6            # sectors: the corners' light blurred this wide (a gaussian's sigma)
SOFT_REACH = 2


def softened(tiles):
    """A set's tiles with their corners' levels blurred across the plane: each corner the weighted mean of the
    set's corners around it (a corner where sectors meet the mean of theirs), to a quarter level."""
    corner = {}
    for (sx, sz), tile in tiles.items():
        for (dx, dz), level in zip(((0, 0), (1, 0), (1, 1), (0, 1)), tile.levels):
            corner.setdefault((sx + dx, sz + dz), []).append(level)
    corner = {at: sum(v) / len(v) for at, v in corner.items()}
    weight = {(dx, dz): math.exp(-(dx * dx + dz * dz) / (2 * SOFT_SIGMA * SOFT_SIGMA))
              for dx in range(-SOFT_REACH, SOFT_REACH + 1) for dz in range(-SOFT_REACH, SOFT_REACH + 1)}
    soft = {}
    for (x, z) in corner:
        total = sum_w = 0.0
        for (dx, dz), w in weight.items():
            level = corner.get((x + dx, z + dz))
            if level is not None:
                total += w * level
                sum_w += w
        soft[(x, z)] = round(total / sum_w * 4) / 4
    return {(sx, sz): Tile(t.tex, t.turned, t.flip_h, t.flip_v,
                           tuple(soft[(sx + dx, sz + dz)] for dx, dz in ((0, 0), (1, 0), (1, 1), (0, 1))))
            for (sx, sz), t in tiles.items()}


def skirted(tiles):
    """A set's tiles with a ring of copies round them: each empty spot beside a tile, edgewise first, then at a
    corner, a copy of it, on the pages of names the tiles have already (a page more is 4 KB of bank B1). Where
    VDP1's edges and VDP2's miss each other by a pixel at the plane's edge, the plane goes on under the polygons
    there instead of showing black."""
    side = PAGE_TILES // TILES_A_SECTOR
    pages = {(sx // side, sz // side) for sx, sz in tiles}
    out = dict(tiles)
    for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)):
        for (sx, sz), tile in tiles.items():
            spot = ((sx + dx) % MAP_SECTORS, (sz + dz) % MAP_SECTORS)
            if (spot[0] // side, spot[1] // side) in pages:
                out.setdefault(spot, tile)
    return out


def rgb(c):
    return [c & 31, (c >> 5) & 31, (c >> 10) & 31]


def quantize(colours, count):
    """count colours for a list of [r, g, b]: median cut, then refined by k-means."""
    boxes = [colours]
    while len(boxes) < count:
        boxes.sort(key=lambda bx: max(max(c[i] for c in bx) - min(c[i] for c in bx) for i in range(3)) if len(bx) > 1 else -1)
        bx = boxes.pop()
        if len(bx) < 2:
            boxes.append(bx)
            break
        axis = max(range(3), key=lambda i: max(c[i] for c in bx) - min(c[i] for c in bx))
        bx = sorted(bx, key=lambda c: c[axis])
        boxes += [bx[:len(bx) // 2], bx[len(bx) // 2:]]
    centres = [[sum(c[i] for c in bx) / len(bx) for i in range(3)] for bx in boxes]
    for _ in range(4):
        sums = [[0.0, 0.0, 0.0, 0] for _ in centres]
        for c in colours:
            j = min(range(len(centres)), key=lambda j: sum((c[i] - centres[j][i]) ** 2 for i in range(3)))
            s = sums[j]
            s[0] += c[0]
            s[1] += c[1]
            s[2] += c[2]
            s[3] += 1
        centres = [[s[i] / s[3] for i in range(3)] if s[3] else centres[j] for j, s in enumerate(sums)]
    return [[max(0, min(31, round(v))) for v in c] for c in centres]


PAINT_GROUPS = 5            # a painted tile's colours: the texture's in 5 groups ...
PAINT_LADDER = (0, 1.5, 3)  # ... each at 3 levels from the tile's lowest


def level_at(levels, fx, fz):
    a, b, c, d = levels
    return (a * (1 - fx) + b * fx) * (1 - fz) + (d * (1 - fx) + c * fx) * fz


def tile_lows(levels):
    """Each tile's lowest level in a painted sector (its palette's first rung), tile rows by columns."""
    lows = []
    for tj in range(TILES_A_SECTOR):
        for ti in range(TILES_A_SECTOR):
            edge = [(ti * 16 + e) / 64 for e in (0.5, 15.5)], [(tj * 16 + e) / 64 for e in (0.5, 15.5)]
            lows.append(math.floor(min(level_at(levels, fx, fz) for fx in edge[0] for fz in edge[1])))
    return lows


def colour_groups(rows, colours):
    """The texture's colours (1 to 15) in PAINT_GROUPS groups by how much each is used:
    each colour's group and each group's colour."""
    count = [0] * 16
    for row in rows:
        for i in row:
            count[i] += 1
    used = [i for i in range(1, 16) if count[i]]
    samples = [rgb(colours[i]) for i in used for _ in range((count[i] + 7) // 8)]
    groups = quantize(samples, PAINT_GROUPS) if len(used) > PAINT_GROUPS else [rgb(colours[i]) for i in used]
    groups += [groups[-1]] * (PAINT_GROUPS - len(groups))
    group_of = [0] * 16
    for i in used:
        group_of[i] = min(range(PAINT_GROUPS), key=lambda j: sum((rgb(colours[i])[k] - groups[j][k]) ** 2 for k in range(3)))
    return group_of, groups


def painted_palette(colours, groups, low):
    """A painted tile's 16 colours: colour 0 at its middle rung, then each rung's groups."""
    def lit(c, level):
        return [max(0, min(31, round(v + level - GOURAUD_NEUTRAL))) for v in c]
    out = [lit(rgb(colours[0]), low + PAINT_LADDER[1])]
    for rung in PAINT_LADDER:
        out += [lit(g, low + rung) for g in groups]
    return [(c[2] << 10) | (c[1] << 5) | c[0] for c in out]


def painted(rows, group_of, tile):
    """A sector's picture as it lies (rows already turned; flipped here), each texel lit as
    VDP1's gouraud lights it (the corners' levels across the sector): its colour's group at
    its tile's rung below or above its level, picked by a 4 x 4 ordered pattern. Colour 0
    stays 0."""
    n = len(rows)
    lows = tile_lows(tile.levels)
    out = [[0] * n for _ in range(n)]
    for y in range(n):
        for x in range(n):
            i = rows[n - 1 - y if tile.flip_v else y][n - 1 - x if tile.flip_h else x]
            if not i:
                continue
            low = lows[(y // 16) * TILES_A_SECTOR + x // 16]
            v = min(max(level_at(tile.levels, (x + 0.5) / n, (y + 0.5) / n) - low, 0), PAINT_LADDER[-1])
            rung = 0
            while rung + 1 < len(PAINT_LADDER) and v >= PAINT_LADDER[rung + 1]:
                rung += 1
            if rung + 1 < len(PAINT_LADDER):
                f = (v - PAINT_LADDER[rung]) / (PAINT_LADDER[rung + 1] - PAINT_LADDER[rung])
                rung += f > (BAYER[y % 4][x % 4] + 0.5) / 16
            out[y][x] = 1 + rung * PAINT_GROUPS + group_of[i]
    return out, lows


def cells_of(rows):
    """A 64 x 64 picture as 2 x 2 characters of 8 x 8 16-colour cells, tile by tile."""
    out = bytearray()
    for tile_row in range(TILES_A_SECTOR):
        for tile_col in range(TILES_A_SECTOR):
            for cell in range(4):
                x0 = tile_col * 16 + (cell & 1) * 8
                y0 = tile_row * 16 + (cell >> 1) * 8
                for y in range(y0, y0 + 8):
                    row = rows[y]
                    for x in range(x0, x0 + 8, 2):
                        out.append((row[x] << 4) | row[x + 1])
    return bytes(out)


def write_art(base, lvl, textures, tiled):
    """base.PLC and base.PLN from the textures as the packer leaves them, the same art
    VDP1 draws. Both empty for a level with no plane."""
    if tiled is None:
        for ext in ('.PLC', '.PLN'):
            with open(base + ext, 'wb'):
                pass
        return

    def picture(tex):
        t = lvl.room_textures[tex]
        offset = t['sub'][0] * 8
        size = t['w'] * 2
        if size != 64:
            raise ValueError(f'room texture {tex} is {size} wide')
        rows = []
        for y in range(size):
            row = []
            for x in range(0, size, 2):
                b = textures.sub[offset + y * (size // 2) + x // 2]
                row += [b >> 4, b & 15]
            rows.append(row)
        return rows

    def turned_picture(tex, turned):
        rows = picture(tex)
        if turned:
            rows = [[rows[x][y] for x in range(len(rows))] for y in range(len(rows))]
        return rows

    mode = os.environ.get('PLANE_LIGHT', tiled['look'])     # the environment's, to try a look
    if 'soft' in mode:
        tiled = dict(tiled, **{kind: softened(tiled[kind]) for kind in SETS})
    if 'skirt' in mode:
        tiled = dict(tiled, **{kind: skirted(tiled[kind]) for kind in SETS})

    # the steep sectors, each picture with its light painted in, steepest first, as many as
    # bank B0 has room for: past the table's end, or the inventory's items when B1 can't
    # take them past the names
    page_count = 1 + sum(len({(sx // (PAGE_TILES // TILES_A_SECTOR), sz // (PAGE_TILES // TILES_A_SECTOR))
                              for sx, sz in tiled[kind]}) for kind in SETS)
    items = (len(lvl.item_tex_data) + 15) & ~15
    room = TABLE_AT - CELLS_AT - PALETTES * 32 - INFO_BYTES
    if NAMES_AT + page_count * PAGE_BYTES + items > B1_END:
        room -= items
    def key_of(tile):
        return tile.tex, tile.turned, tile.flip_h, tile.flip_v, tile.levels

    # each tile's light, at each 16 x 16 tile's centre
    lit = {}
    tex_of = {}
    sector_of = {}
    for s, kind in enumerate(SETS):
        for spot, tile in tiled[kind].items():
            for tj in range(TILES_A_SECTOR):
                for ti in range(TILES_A_SECTOR):
                    key = (s, spot, ti, tj)
                    lit[key] = level_at(tile.levels, (ti + 0.5) / TILES_A_SECTOR, (tj + 0.5) / TILES_A_SECTOR)
                    tex_of[key] = tile.tex
                    sector_of[key] = key_of(tile)

    def level_of(key, step):
        """A tile's palette: its texture and light step, the step rounded, or with
        dither chosen by a 4 x 4 ordered pattern over the map's tiles."""
        v = lit[key] / step
        if 'dither' in mode:
            _, spot, ti, tj = key
            mx, mz = spot[0] * TILES_A_SECTOR + ti, spot[1] * TILES_A_SECTOR + tj
            return tex_of[key], int(v + 1 - (BAYER[mz % 4][mx % 4] + 0.5) / 16)
        return tex_of[key], int(v + 0.5)

    # the light's steps: as fine as the palettes allow with every sector plain
    for step in range(1, LEVELS):
        used = {level_of(key, step) for key in lit}
        if len(used) <= BLACK_PALETTE:
            break
    else:
        raise ValueError('too many palettes')

    # the steep sectors, each picture with its light painted in, steepest first, while the
    # palettes the plain tiles leave and bank B0 have room: past the table's end, or the
    # inventory's items when B1 can't take them past the names
    page_count = 1 + sum(len({(sx // (PAGE_TILES // TILES_A_SECTOR), sz // (PAGE_TILES // TILES_A_SECTOR))
                              for sx, sz in tiled[kind]}) for kind in SETS)
    items = (len(lvl.item_tex_data) + 15) & ~15
    room = TABLE_AT - CELLS_AT - PALETTES * 32 - INFO_BYTES
    if NAMES_AT + page_count * PAGE_BYTES + items > B1_END:
        room -= items
    steep = {}
    for s, kind in enumerate(SETS):
        for spot, tile in tiled[kind].items():
            if 'paint' in mode and max(tile.levels) - min(tile.levels) >= STEEP:
                steep.setdefault(key_of(tile), []).append((s, spot))
    order = sorted(steep, key=lambda k: (-(max(k[4]) - min(k[4])), -len(steep[k]), k))

    def plain_images(painting):
        return {(tile.tex, tile.turned) for kind in SETS for tile in tiled[kind].values() if key_of(tile) not in painting}

    painting, rungs = [], set()
    for key in order:
        more = rungs | {(key[0], low) for low in tile_lows(key[4])}
        trial = set(painting) | {key}
        if len(more) <= BLACK_PALETTE - len(used) and                 TILE_BYTES + (len(plain_images(trial)) + len(trial)) * TILE_BYTES * 16 <= room:
            painting.append(key)
            rungs = more
    left = len(order) - len(painting)
    painting_set = set(painting)
    images = {}
    for kind in SETS:
        for tile in tiled[kind].values():
            if key_of(tile) not in painting_set:
                images.setdefault((tile.tex, tile.turned), len(images))
    used = {level_of(key, step) for key in lit if sector_of[key] not in painting_set}
    palette_of = {key: i for i, key in enumerate(sorted(used))}
    rung_of = {key: len(palette_of) + i for i, key in enumerate(sorted(rungs))}

    cram = [0] * (PALETTES * 16)
    for (tex, kq), p in palette_of.items():
        k = kq * step - GOURAUD_NEUTRAL
        for i, c in enumerate(textures.sources[tex][1]):
            r, g, bl = c & 31, (c >> 5) & 31, (c >> 10) & 31
            r, g, bl = [max(0, min(31, v + k)) for v in (r, g, bl)]
            cram[p * 16 + i] = (bl << 10) | (g << 5) | r
    groups_of = {tex: colour_groups(picture(tex), textures.sources[tex][1]) for tex in {k[0] for k in painting}}
    for (tex, low), p in rung_of.items():
        cram[p * 16:p * 16 + 16] = painted_palette(textures.sources[tex][1], groups_of[tex][1], low)

    # the cells: a blank tile first
    cells = bytearray(TILE_BYTES)
    image_at = {}
    for (tex, turned), _ in sorted(images.items(), key=lambda kv: kv[1]):
        image_at[(tex, turned)] = len(cells) // 0x20
        cells += cells_of(turned_picture(tex, turned))
    painted_at = {}
    for key in painting:
        tex, turned = key[:2]
        rows, lows = painted(turned_picture(tex, turned), groups_of[tex][0], Tile(*key))
        painted_at[key] = (len(cells) // 0x20, [rung_of[(tex, low)] for low in lows])
        cells += cells_of(rows)

    # the pages: page 0 all blank
    first = CELLS_AT // 0x20
    blank_name = struct.pack('>HH', BLACK_PALETTE, first)
    names = bytearray(blank_name * (PAGE_TILES * PAGE_TILES))
    maps = bytearray()
    pages = 1
    for s, kind in enumerate(SETS):
        by_plane = {}
        for spot, tile in tiled[kind].items():
            for tj in range(TILES_A_SECTOR):
                for ti in range(TILES_A_SECTOR):
                    mx, mz = spot[0] * TILES_A_SECTOR + ti, spot[1] * TILES_A_SECTOR + tj
                    by_plane.setdefault((mx // PAGE_TILES, mz // PAGE_TILES), {})[(mx % PAGE_TILES, mz % PAGE_TILES)] = \
                        (tile, spot, ti, tj)
        for pz in range(MAP_PLANES):
            for px in range(MAP_PLANES):
                tiles = by_plane.get((px, pz))
                if not tiles:
                    maps.append(0)
                    continue
                maps.append(pages)
                pages += 1
                for ty in range(PAGE_TILES):
                    for tx in range(PAGE_TILES):
                        entry = tiles.get((tx, ty))
                        if not entry:
                            names += blank_name
                            continue
                        tile, spot, ti, tj = entry
                        at = painted_at.get(key_of(tile))
                        if at:
                            names += struct.pack('>HH', at[1][tj * TILES_A_SECTOR + ti],
                                                 first + at[0] + (tj * TILES_A_SECTOR + ti) * TILE_UNITS)
                            continue
                        col = TILES_A_SECTOR - 1 - ti if tile.flip_h else ti
                        row = TILES_A_SECTOR - 1 - tj if tile.flip_v else tj
                        word0 = palette_of[level_of((s, spot, ti, tj), step)] | \
                            (FLIP_H if tile.flip_h else 0) | (FLIP_V if tile.flip_v else 0)
                        word1 = first + image_at[(tile.tex, tile.turned)] + (row * TILES_A_SECTOR + col) * TILE_UNITS
                        names += struct.pack('>HH', word0, word1)
    if pages > MAX_PAGES:
        raise ValueError(f'{pages} pages of names')
    if pages != page_count:
        raise ValueError(f'{pages} pages of names, {page_count} counted')
    if len(cells) > room:
        raise ValueError(f'{len(cells)} bytes of cells, room for {room}')

    info = struct.pack('>iii', *(tiled['heights'].get(kind, NO_PLANE) for kind in SETS), int(tiled['floods']))
    origins = [v for kind in SETS for v in tiled['origin'].get(kind, (NO_ORIGIN, NO_ORIGIN))]
    with open(base + '.PLC', 'wb') as f:
        f.write(bytes(cells))
        f.write(struct.pack(f'>{len(cram)}H', *cram))
        f.write(info)
        f.write(bytes(maps))
        f.write(struct.pack('>4h', *origins))
    with open(base + '.PLN', 'wb') as f:
        f.write(bytes(names))
    print(f'plane art: {len(images)} pictures, {len(cells) // 1024} KB cells, {pages} pages of names '
          f'({len(names) // 1024} KB), {len(palette_of)} palettes at light steps of {step}, '
          f'{len(painting)} painted for {sum(len(steep[k]) for k in painting)} steep sectors in {len(rung_of)} palettes'
          + (f', {left} left plain for room' if left else ''))
