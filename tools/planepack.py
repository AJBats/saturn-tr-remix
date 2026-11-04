"""A level's flat floor and ceiling for VDP2's RBG0 (saturn/plane.h,
docs/vdp2-production.md): those faces out of its rooms for good, and their
tiles, cells and colours for bank B of VDP2's VRAM.

saturn/planes.txt names the levels and their heights. saturn/plane_paint/LEVEL.txt,
tools/floor_paint.py's, holds what's been changed by hand:

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
floor and ceiling heights, each set's 16 planes as page numbers from the names' base.

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
PALETTES = 120              # CRAM from 0x780 is the line colours
BLACK_PALETTE = PALETTES - 1
GOURAUD_NEUTRAL = 16
LEVELS = 32
FLIP_H = 0x4000
FLIP_V = 0x8000


class Plan:
    def __init__(self, level, heights):
        self.level = level
        self.heights = heights      # set -> y
        self.tile = {}              # (set, sx, sz) -> (tex, turned, flip_h, flip_v)
        self.blank = set()          # (set, sx, sz)
        self.keep = set()           # (set, sx, sz)
        self.flatten = {}           # (set, sx, sz) -> (room, quad)
        self.light = {}             # (set, sx, sz) -> levels


class Tile:
    def __init__(self, tex, turned, flip_h, flip_v, levels):
        self.tex, self.turned, self.flip_h, self.flip_v, self.levels = tex, turned, flip_h, flip_v, levels

    def same(self, other):
        return (self.tex, self.turned, self.flip_h, self.flip_v, self.levels) == \
               (other.tex, other.turned, other.flip_h, other.flip_v, other.levels)


def plan_for(level):
    """The level's plan, or None if planes.txt doesn't name it."""
    if os.environ.get('PLANES') == '0':
        return None
    plan = None
    with open(PLANES_FILE) as f:
        for line in f:
            words = line.split('#')[0].split()
            if words and words[0] == level:
                plan = Plan(level, {words[i]: int(words[i + 1]) for i in range(1, len(words), 2)})
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


def take_faces(lvl, plan):
    """The plane's faces out of lvl's rooms, with the vertices nothing else uses; each
    set's tiles, {(map x, map z) in sectors: Tile}, and the heights under 'heights'."""
    animated = set()
    for first, last in lvl.anim_ranges:
        animated.update(range(first, last + 1))
    rooms = lvl.rooms
    primary = {r.alternate: r.index for r in rooms if 0 <= r.alternate < len(rooms)}
    gone = {}           # room index -> quad indices
    tiled = {'heights': plan.heights}
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
                if not made:
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

        map_tiles = {}
        lost = 0
        for (sx, sz), (tile, r, q_i) in sorted(wanted.items(), key=first):
            spot = (sx % MAP_SECTORS, sz % MAP_SECTORS)
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
                else:
                    left += 1
        print(f'{plan.level} {kind} {height}: {len(taken)} tiles, {lost} stay polygons in a spot taken, '
              f'{doubled} doubled, {copies} flooded copies out, {left} stay')

    vertices_gone = 0
    for index, quads in gone.items():
        room = rooms[index]
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

    # each tile's light, at each 16 x 16 tile's centre
    images = {}
    levels = set()
    lit = {}
    for s, kind in enumerate(SETS):
        for spot, tile in tiled[kind].items():
            images.setdefault((tile.tex, tile.turned), len(images))
            a, b, c, d = tile.levels
            for tj in range(TILES_A_SECTOR):
                for ti in range(TILES_A_SECTOR):
                    fx, fz = (ti + 0.5) / TILES_A_SECTOR, (tj + 0.5) / TILES_A_SECTOR
                    k = (a * (1 - fx) + b * fx) * (1 - fz) + (d * (1 - fx) + c * fx) * fz
                    lit[(s, spot, ti, tj)] = k
                    levels.add((tile.tex, k))

    # the light's steps: as fine as the palettes allow
    for step in range(1, LEVELS):
        used = {(tex, int(k / step + 0.5)) for tex, k in levels}
        if len(used) <= BLACK_PALETTE:
            break
    else:
        raise ValueError('too many palettes')
    palette_of = {key: i for i, key in enumerate(sorted(used))}

    cram = [0] * (PALETTES * 16)
    for (tex, kq), p in palette_of.items():
        k = kq * step - GOURAUD_NEUTRAL
        for i, c in enumerate(textures.sources[tex][1]):
            r, g, bl = c & 31, (c >> 5) & 31, (c >> 10) & 31
            r, g, bl = [max(0, min(31, v + k)) for v in (r, g, bl)]
            cram[p * 16 + i] = (bl << 10) | (g << 5) | r

    # the cells: a blank tile first
    cells = bytearray(TILE_BYTES)
    image_at = {}
    for (tex, turned), _ in sorted(images.items(), key=lambda kv: kv[1]):
        rows = picture(tex)
        if turned:
            rows = [[rows[x][y] for x in range(len(rows))] for y in range(len(rows))]
        image_at[(tex, turned)] = len(cells) // 0x20
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
                        col = TILES_A_SECTOR - 1 - ti if tile.flip_h else ti
                        row = TILES_A_SECTOR - 1 - tj if tile.flip_v else tj
                        k = lit[(s, spot, ti, tj)]
                        word0 = palette_of[(tile.tex, int(k / step + 0.5))] | \
                            (FLIP_H if tile.flip_h else 0) | (FLIP_V if tile.flip_v else 0)
                        word1 = first + image_at[(tile.tex, tile.turned)] + (row * TILES_A_SECTOR + col) * TILE_UNITS
                        names += struct.pack('>HH', word0, word1)
    if pages > MAX_PAGES:
        raise ValueError(f'{pages} pages of names')

    info = struct.pack('>ii', *(tiled['heights'].get(kind, NO_PLANE) for kind in SETS))
    with open(base + '.PLC', 'wb') as f:
        f.write(bytes(cells))
        f.write(struct.pack(f'>{len(cram)}H', *cram))
        f.write(info)
        f.write(bytes(maps))
    with open(base + '.PLN', 'wb') as f:
        f.write(bytes(names))
    print(f'plane art: {len(images)} pictures, {len(cells) // 1024} KB cells, {pages} pages of names '
          f'({len(names) // 1024} KB), {len(palette_of)} palettes at light steps of {step}')
