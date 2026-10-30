"""Packs a captured frame (tools/capture.py, build/captures/NAME) into a scene
for the clean-room renderer's lab (lab/scene.h): what the game drew that
frame with and what it made of it, from the dumps, as the lab reads it.

    labscene.py NAME OUT.SCN

The capture's own program (its 0MAIN.elf) names what the dumps hold. Faces
become indices into their pool; mesh and room data are copied as the game
keeps them in memory; the game's command list and gouraud tables come along
as the reference the lab checks against. The new stages' data is packed
from the same level data by their packers (tools/facepack.py). The capture must use the look the
lab builds (the game's defaults): anything else is refused.
"""
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import capture as cap   # noqa: E402
import facepack         # noqa: E402
import posepack         # noqa: E402
import roompack         # noqa: E402
import drawpack         # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SCENE_MAGIC = 0x53434E31        # "SCN1"
MAX_VERTICES = 5 * 1024
MASTER_VERTICES = 2048
MAX_FACES = 1920
OT_SIZE, OT_GROUPS = cap.OT_SIZE, cap.OT_GROUPS
OT_SHIFT = 4
VDP1_BYTES = 0x80000
VRAM_FRAME_CMDS = 0x60          # saturn/vram.h
VRAM_GOURAUD_BASE = VRAM_FRAME_CMDS + 1792 * 32
DIV_TABLE_SIZE = 1281
DETAIL_KEYS_BYTES = 7 * 1088
DETAIL_SLOTS_BYTES = 256 * 8
DETAIL_MAX_UPLOADS = 32
SECTOR = 2048
SHADE_CACHE_BYTES = 2064
PALETTE_COLOURS = 256
TEXTURE_BYTES = 16
SPRITE_BYTES = 16
MESH_HEADER = 20                # Mesh (common.h)
MESH_LIGHT_NORMALS, MESH_LIGHT_SHADES = 1, 2
VIEW_DIST = 20480

# Level (common.h): fields read
LEVEL_TEXTURES_COUNT, LEVEL_SPRITES_COUNT = 20, 22
LEVEL_PALETTE, LEVEL_TEXTURES, LEVEL_SPRITES = 32, 92, 96
# Room and RoomInfo (common.h), RoomJob and RoomWork (rooms.cpp)
ROOM_INFO, ROOM_DATA = 4, 12
INFO_COUNTS, INFO_FACING = 8, 24
JOB_BYTES, WORK_BYTES = 72, 12

# the look the lab builds: the game's defaults (render.cpp, debug menu)
DEFAULTS = {'_gBenchMode': 0, '_gDarkFaces': 0, '_gShadeTables': 1, '_gFlatDist': 0, '_gNearSplit': 0,
            '_gShadowMode': 2, '_gTexelView': 0, '_gTurnLines': 1, '_gTextureLod': 4}

DRAW_MESH, DRAW_SHADOW, DRAW_COPY = 1, 2, 3
FACE_TYPE_SHIFT, FACE_TYPE_MASK = 14, 15     # saturn/render.h
HUD_TYPES = (7, 8, 9)       # FaceType FILL_S, LINE_H, LINE_V: renderRect's, the HUD's bars and frames
WHERE_HIGH, WHERE_LOW, WHERE_VDP1 = 0, 1, 2

INFO = struct.Struct('>9I3I4I3i2I16s')      # SceneInfo
ROOM = struct.Struct('>9hH3i4i3iiBBBBi4H6H3I')  # RoomScene
ROOM_GEOMETRY = struct.Struct('>3I2H6H4x')      # RoomGeometry
NO_GEOMETRY = 0xFFFFFFFF
DRAW = struct.Struct('>BBHIiiii')           # DrawScene
XFORM = struct.Struct('>12i2i3h4hh')        # DrawXform
FLOOR = struct.Struct('>3I4HI')             # FloorFace
MOVE = struct.Struct('>HH')

FLOOR_EXACT = 1


def u32(mem, address):
    return mem.unpack('>I', address)[0]


def chain_faces(mem, chains, pool):
    """The chains' faces as indices into the pool at 'pool', bucket by bucket."""
    out = []
    for chain in chains:
        idx = []
        for face in chain:
            i, rest = divmod((face & 0x0FFFFFFF) - pool, 16)
            if rest or not 0 <= i < MAX_FACES:
                sys.exit(f'face {face:#x} is not in the pool at {pool:#x}')
            idx.append(i)
        out.append(idx)
    return out


def pool_bytes(mem, pool, count, index_of):
    """count faces of the pool, their next pointers as index + 1 (0: none)."""
    out = bytearray()
    for i in range(count):
        flags, nxt, a, b, c, d = mem.unpack('>II4h', pool + 16 * i)
        n = index_of(nxt) if nxt else 0
        out += struct.pack('>II4h', flags, n, a, b, c, d)
    return bytes(out)


def mesh_size(mem, address):
    """The bytes of a mesh and the arrays after it (render.cpp meshParts)."""
    vcount = mem.unpack('>B', address + 10)[0]
    lighting = mem.unpack('>B', address + 11)[0]
    quads, triangles = mem.unpack('>hh', address + 12)
    size = MESH_HEADER + 6 * vcount
    if lighting == MESH_LIGHT_NORMALS:
        size += 4 * vcount
    elif lighting == MESH_LIGHT_SHADES:
        size += (vcount + 1) & ~1
    return size + 6 * (quads + triangles)


def sar(v, n):
    return v >> n       # Python's >> is arithmetic


def mesh_inside(mem, r):
    """meshxform.s renderMeshAsm's test: the bounding sphere wholly in the
    frame and between the planes."""
    m = r['matrix']
    rows = [cap.s16(m[k]) for k in (0, 1, 2, 4, 5, 6, 8, 9, 10)]
    cx0, cy0, cz0 = mem.unpack('>3h', r['ref'])
    bound = mem.unpack('>h', r['ref'] + 16)[0]
    cx = sar(cap.s32(m[3] + rows[0] * cx0 + rows[1] * cy0 + rows[2] * cz0), 14)
    cy = sar(cap.s32(m[7] + rows[3] * cx0 + rows[4] * cy0 + rows[5] * cz0), 14)
    cz = sar(cap.s32(m[11] + rows[6] * cx0 + rows[7] * cy0 + rows[8] * cz0), 14)
    near = cz - bound
    return (near > 64 and cz + bound < VIEW_DIST and
            cap.s32((abs(cx) + bound) << 8) < 172 * near and cap.s32((abs(cy) + bound) << 8) < 108 * near)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    folder = os.path.join(ROOT, 'build/captures', sys.argv[1])
    out_path = sys.argv[2]
    syms = cap.symbols(os.path.join(folder, '0MAIN.elf'))

    def sym(name, size=None):
        found = [a for a, s in syms.get(name, []) if size is None or s == size]
        if len(found) != 1:
            sys.exit(f'{name}: {len(found)} symbols')
        return found[0]

    def read(name):
        with open(os.path.join(folder, name), 'rb') as f:
            return f.read()

    mem = cap.Memory(read('lwram.bin'), read('hwram.bin'))
    vram = read('vdp1.bin')
    with open(os.path.join(folder, 'frame.json')) as f:
        frame = json.load(f)
    header, recs = frame['header'], frame['records']
    if 'order' not in header:
        sys.exit('the capture has no ordering tables: capture it again')

    for name, want in DEFAULTS.items():
        got = cap.s32(u32(mem, sym(name)))
        if got != want:
            sys.exit(f'{name} is {got}: the lab builds the defaults ({want})')

    master_pool = sym('__ZL6gFaces')
    slave_pool = sym('__ZL11gSlaveFaces')
    vertices = sym('_gVertices')
    room_ot, mesh_ot = cap.order_tables(mem, header)
    room_ot = chain_faces(mem, room_ot, slave_pool)
    mesh_ot = chain_faces(mem, mesh_ot, master_pool)
    order = mem.unpack(cap.ORDER.format, header['order'])
    room_groups = order[2 * OT_SIZE:2 * OT_SIZE + OT_GROUPS]
    mesh_groups = order[2 * OT_SIZE + OT_GROUPS:2 * OT_SIZE + 2 * OT_GROUPS]

    def bucket_of(table):
        where = {}
        for b, chain in enumerate(table):
            for i in chain:
                if i in where:
                    sys.exit(f'face {i} twice in the ordering tables')
                where[i] = b
        return where

    room_bucket, mesh_bucket = bucket_of(room_ot), bucket_of(mesh_ot)
    slave_faces = max(room_bucket) + 1 if room_bucket else 0
    master_faces = max(mesh_bucket) + 1 if mesh_bucket else 0
    if len(room_bucket) != slave_faces or len(mesh_bucket) != master_faces:
        sys.exit('a pool holds faces the ordering tables leave out')

    def indexer(pool):
        def index_of(address):
            return ((address & 0x0FFFFFFF) - pool) // 16 + 1
        return index_of

    chunks = {}
    chunks['VRAM'] = vram[:VDP1_BYTES]
    chunks['VERT'] = mem.bytes(vertices, MAX_VERTICES * 8)
    chunks['MPOL'] = pool_bytes(mem, master_pool, master_faces, indexer(master_pool))
    chunks['SPOL'] = pool_bytes(mem, slave_pool, slave_faces, indexer(slave_pool))
    heads = [chain[0] + 1 if chain else 0 for chain in room_ot] + [chain[0] + 1 if chain else 0 for chain in mesh_ot]
    chunks['ORDR'] = struct.pack(f'>{2 * OT_SIZE}H{2 * OT_GROUPS}H', *heads, *room_groups, *mesh_groups)

    commands = header['faces']
    cmds = mem.bytes(sym('__ZL5gCmds', 0xE000), (commands + 1) * 32)
    grds = mem.bytes(sym('__ZL8gGouraud'), commands * 8)
    if (vram[VRAM_FRAME_CMDS:VRAM_FRAME_CMDS + len(cmds)] != cmds or
            vram[VRAM_GOURAUD_BASE:VRAM_GOURAUD_BASE + len(grds)] != grds):
        sys.exit('VDP1 VRAM does not hold the list as built: capture it again')
    chunks['CMDS'] = cmds
    chunks['GRDS'] = grds

    level = sym('_level')
    textures = mem.unpack('>H', level + LEVEL_TEXTURES_COUNT)[0]
    sprites = mem.unpack('>H', level + LEVEL_SPRITES_COUNT)[0]
    chunks['TEXS'] = mem.bytes(u32(mem, level + LEVEL_TEXTURES), textures * TEXTURE_BYTES)
    chunks['PALT'] = mem.bytes(u32(mem, level + LEVEL_PALETTE), PALETTE_COLOURS * 2)
    chunks['SPRS'] = mem.bytes(u32(mem, level + LEVEL_SPRITES), sprites * SPRITE_BYTES)
    chunks['SHAD'] = (mem.bytes(sym('__ZL11gShadeLevel'), 32) + mem.bytes(sym('__ZL10gHalfLevel'), 96) +
                      mem.bytes(sym('__ZL13gShadeGouraud'), 64))
    chunks['SHCA'] = mem.bytes(sym('__ZL12gShadeCaches'), 2 * SHADE_CACHE_BYTES)
    chunks['DKEY'] = mem.bytes(sym('_gDetailKeys'), DETAIL_KEYS_BYTES)
    chunks['DSLT'] = mem.bytes(sym('_gDetailSlots'), DETAIL_SLOTS_BYTES)

    # the room texture cache as the frame found it (a capture without that:
    # as it left it, which serves a frame that missed nothing), the uploads
    # the frame's faces queued, and the level's room texture set the misses
    # take textures from: its levels (full res, half, quarter), the full-res
    # set's size, each level's pool's slots
    if 'cache' in header:
        chunks['DKY0'] = mem.bytes(header['cache'], cap.CACHE_KEYS)
        chunks['DSL0'] = mem.bytes(header['cache'] + cap.CACHE_KEYS, cap.CACHE_SLOTS)
        uploads = header['uploads']
    else:
        chunks['DKY0'], chunks['DSL0'], uploads = chunks['DKEY'], chunks['DSLT'], []
    data = mem.unpack('>3I', sym('__ZL5gData'))
    data_size = u32(mem, sym('__ZL9gDataSize'))
    pools = mem.unpack('>3i', sym('__ZL10gPoolCount'))
    levels = [a - data[0] for a in data]
    set_bytes = levels[1] + levels[1] // 4 + levels[1] // 16 if pools[1] else data_size
    chunks['RTEX'] = mem.bytes(data[0], set_bytes)
    chunks['RTXI'] = struct.pack('>3II3i', *levels, data_size, *pools)
    queue = b''.join(struct.pack('>3I', u['src'] - data[0], u['dst'] | u['variant'] << 24, u['w'] << 16 | u['h'])
                     for u in uploads)
    if any(not 0 <= u['src'] - data[0] < set_bytes for u in uploads):
        sys.exit('an upload comes from outside the room texture set')
    chunks['DUPL'] = struct.pack('>i', len(uploads)) + queue + bytes(12 * (DETAIL_MAX_UPLOADS - len(uploads)))
    chunks['DIVT'] = mem.bytes(sym('_divTable'), DIV_TABLE_SIZE * 2)

    # the rooms, in the order the slave added their faces: a face is the
    # room's whose vertices hold one of its corners (a split quad's quarters
    # each keep one of the quad's); rooms that added none go last
    jobs, work = sym('__ZL5gJobs'), sym('__ZL5gWork', 0x300)
    placed = u32(mem, sym('__ZL7gPlaced'))
    spans = []
    for j in range(placed):
        room = u32(mem, jobs + JOB_BYTES * j)
        info = u32(mem, room + ROOM_INFO)
        count = mem.unpack('>H', info + INFO_COUNTS + 4)[0]
        base = mem.unpack('>i', work + WORK_BYTES * j)[0]
        spans.append((base, count))
    faced = []
    for i in range(slave_faces):
        corners = mem.unpack('>4h', slave_pool + 16 * i + 8)
        owner = None
        for c in corners:
            v = c // 8 + MASTER_VERTICES
            for j, (base, count) in enumerate(spans):
                if base >= 0 and base <= v < base + count:
                    owner = j
        if owner is None:
            sys.exit(f'slave face {i} has no room')
        if owner not in faced:
            faced.append(owner)
    faced += [j for j in range(placed) if j not in faced and spans[j][0] >= 0]

    # caustics under water: each vertex's shade moves along the wave from its
    # own random start (rooms.cpp roomBegin)
    wave = mem.unpack('>32i', sym('_gCaustics'))
    starts = mem.unpack('>32i', sym('_gRandTable'))
    room_recs = bytearray()
    geometry = bytearray()
    caustics_tables = bytearray()
    rooms_base, room_size = sym('_rooms'), roompack.room_size()
    geometry_of = {}
    phases = set()
    for j in faced:
        at = jobs + JOB_BYTES * j
        room = u32(mem, at)
        m = mem.unpack('>9h', at + 4)
        t = mem.unpack('>3i', at + 24)
        clip = mem.unpack('>4i', at + 36)
        caustics = mem.unpack('>i', at + 52)[0]
        camera = mem.unpack('>3i', at + 56)
        wet, ripple = mem.unpack('>BB', at + 68)
        dsp = mem.unpack('>B', work + WORK_BYTES * j + 8)[0]
        info = u32(mem, room + ROOM_INFO)
        quads, triangles, count = mem.unpack('>3H', info + INFO_COUNTS)
        facing = mem.unpack('>6H', info + INFO_FACING)
        data_quads, data_triangles, data_vertices = mem.unpack('>3I', room + ROOM_DATA)
        offsets = []
        for address, size in ((data_quads, 12 * quads), (data_triangles, 12 * triangles), (data_vertices, 4 * count)):
            offsets.append(len(geometry))
            geometry += mem.bytes(address, size)
            geometry += bytes(-len(geometry) % 4)
        index = (room - rooms_base) // room_size
        room_recs += ROOM.pack(*m, index, *t, *clip, *camera, caustics, wet, ripple, dsp, 0, spans[j][0],
                               quads, triangles, count, 0, *facing, *offsets)
        geometry_of[index] = (*offsets, quads, triangles, *facing)
        phases.add(caustics)
        caustics_tables += struct.pack('>32h', *(cap.s16(wave[(starts[i] + caustics) & 31]) for i in range(32)))
    chunks['ROOM'] = bytes(room_recs)
    chunks['RGEO'] = bytes(geometry)
    chunks['CAUS'] = bytes(caustics_tables)
    # the frame's phase of the caustics' wave, the wave, each vertex's start
    if len(phases) > 1:
        sys.exit('rooms queued with different caustics phases')
    chunks['CAUW'] = struct.pack('>i32h32B', phases.pop() if phases else 0, *(cap.s16(w) for w in wave),
                                 *(s & 31 for s in starts))

    # the master's draws in order, the faces they added; before the first,
    # the portals' fills past the draw distance (renderVoid). Sprites' and
    # fills' faces are copied as made, into the buckets they went in.
    mesh_index, mesh_offsets, mesh_data = {}, [], bytearray()
    draws = bytearray()
    xforms = bytearray()

    def xform(r=None):
        """A draw's transform as its record has it (zeros for a copied face)."""
        if r is None:
            return bytes(XFORM.size)
        m = r['matrix']
        rows = [cap.s16(m[k]) if k % 4 != 3 else cap.s32(m[k]) for k in range(12)]
        light = [cap.s16(v) for v in r['light']]
        return XFORM.pack(*rows, cap.s32(r['ambient']), cap.s32(r['range']), *light,
                          *(r.get('shadow') or (0, 0, 0, 0)), 0)
    draw_count = 0
    natural = {}
    copied = 0

    def copy_face(i):
        nonlocal draws, draw_count, copied
        draws += DRAW.pack(DRAW_COPY, 0, 0, 0, 0, i, 1, mesh_bucket[i])
        xforms.extend(xform())
        draw_count += 1
        natural[i] = mesh_bucket[i]
        copied += 1

    def zsum_bucket(i):
        corners = mem.unpack('>4h', master_pool + 16 * i + 8)
        z = sum(mem.unpack('>h', vertices + c + 4)[0] for c in corners)
        return (z & 0xFFFFFFFF) >> (2 + OT_SHIFT)

    draw_recs = [r for r in recs if r['type'] in ('mesh', 'static', 'shadow', 'sprite')]
    firsts = [(r['first'] - master_pool) // 16 for r in draw_recs] + [master_faces]
    for i in range(firsts[0]):
        copy_face(i)
    hinted = set()
    owner = None
    first_draws = {}    # a drawn item's first mesh's draw, by item index
    statics, shadows = [], []   # (draw, object record); (draw, item index, record)
    k = 0
    for r in recs:
        if r['type'] == 'object':
            owner = r
            continue
        if r['type'] not in ('mesh', 'static', 'shadow', 'sprite'):
            continue
        first, end = firsts[k], firsts[k + 1]
        k += 1
        # the HUD's faces, drawn between two draws, after the first's: copied as made
        hud = first
        while hud < end and (u32(mem, master_pool + 16 * hud) >> FACE_TYPE_SHIFT & FACE_TYPE_MASK) not in HUD_TYPES:
            hud += 1
        hud, end = range(hud, end), hud
        base = (r['base'] - vertices) // 8
        if r['type'] == 'sprite':
            for i in range(first, end):
                copy_face(i)
            for i in hud:
                copy_face(i)
            continue
        for i in range(first, end):
            natural[i] = zsum_bucket(i)
            if natural[i] != mesh_bucket[i] and owner:
                hinted.add((owner['kind'], owner['owner'], owner['index']))
        xforms.extend(xform(r))
        if r['type'] == 'shadow':
            draws += DRAW.pack(DRAW_SHADOW, 0, 0, r['flags'], base, first, end - first, 0)
        else:
            address = r['ref'] & 0x0FFFFFFF
            if address not in mesh_index:
                mesh_index[address] = len(mesh_offsets)
                mesh_offsets.append(len(mesh_data))
                mesh_data += mem.bytes(address, mesh_size(mem, address))
                mesh_data += bytes(-len(mesh_data) % 4)
            draws += DRAW.pack(DRAW_MESH, int(mesh_inside(mem, r)), mesh_index[address], r['flags'], base,
                               first, end - first, 0)
            if owner and owner['kind'] == 0 and r['type'] == 'mesh':
                first_draws.setdefault(owner['index'], draw_count)
            if owner and owner['kind'] == 1 and r['type'] == 'static':
                statics.append((draw_count, owner))
        if owner and owner['kind'] == 0 and r['type'] == 'shadow':
            shadows.append((draw_count, owner['index'], r))
        draw_count += 1
        for i in hud:
            copy_face(i)
    chunks['DRAW'] = bytes(draws)
    chunks['XFRM'] = bytes(xforms)
    # a shadow's item: its place in POSE, items in the order first drawn
    order = sorted(first_draws, key=first_draws.get)
    shadows = [(d, order.index(i), r) for d, i, r in shadows if i in first_draws]
    maps = {}
    chunks.update(posepack.pack(mem, sym, recs, first_draws, header, statics, shadows, maps))
    chunks.update(drawpack.pack(mem, sym, recs, mesh_index, maps))
    chunks.update(roompack.pack(mem, sym, recs, header))
    # each level room's geometry in RGEO: the rooms this frame drew, the
    # others none
    chunks['RLGE'] = b''.join(ROOM_GEOMETRY.pack(*geometry_of[r]) if r in geometry_of else
                              ROOM_GEOMETRY.pack(NO_GEOMETRY, NO_GEOMETRY, NO_GEOMETRY, 0, 0, 0, 0, 0, 0, 0, 0)
                              for r in range(len(chunks['RLVL']) // roompack.ROOM_LEVEL.size))
    chunks['CAMR'] = struct.pack('>3i9h2x', *header['camera'], *header['rotation'])
    chunks['MESH'] = struct.pack(f'>{len(mesh_offsets)}I', *mesh_offsets)
    chunks['MGEO'] = bytes(mesh_data)

    # the new face stage's: the materials, and each mesh's faces (MESH's
    # order) after a table of their offsets
    texture_entries = [struct.unpack_from('>5H', chunks['TEXS'], 16 * i) for i in range(textures)]
    materials = facepack.Materials(texture_entries, struct.unpack('>256H', chunks['PALT']),
                                   u32(mem, sym('_gTexturePmod')))
    sets = [facepack.mesh_faces(mesh_data[at:], materials) for at in mesh_offsets]
    offsets, at = [], 4 * len(sets)
    for face_set in sets:
        offsets.append(at)
        at += len(face_set)
    chunks['NMSH'] = struct.pack(f'>{len(sets)}I', *offsets) + b''.join(sets)
    chunks['NMAT'] = materials.bytes()
    moved = [(i, natural[i]) for i in range(master_faces) if natural[i] != mesh_bucket[i]]
    chunks['MOVE'] = b''.join(MOVE.pack(*m) for m in moved)

    # the floor: each command's face, in the order flush made them, with the
    # command's first three words and the corners whose positions its four
    # corner words hold (a corner left as the first when none does)
    flush_order = []
    for b in range(OT_SIZE - 1, -1, -1):
        flush_order += [(True, i) for i in room_ot[b]] + [(False, i) for i in mesh_ot[b]]
    shade_gouraud = struct.unpack('>32H', mem.bytes(sym('__ZL13gShadeGouraud'), 64))
    floor = bytearray()
    exact = 0
    for n, (room, i) in enumerate(flush_order):
        slot = n + 1
        w = struct.unpack('>8I', cmds[32 * slot:32 * slot + 32])
        g = struct.unpack('>2I', grds[8 * slot:8 * slot + 8])
        pool = slave_pool if room else master_pool
        corners = [c + (MASTER_VERTICES * 8 if room else 0) for c in mem.unpack('>4h', pool + 16 * i + 8)]
        chosen, found = [], True
        for word in w[3:7]:
            match = [c for c in corners if u32(mem, vertices + c) == word]
            found = found and bool(match)
            chosen.append(match[0] if match else corners[0])
        shades = [mem.unpack('>B', vertices + c + 6)[0] for c in chosen]
        gouraud = (shade_gouraud[shades[0]] << 16 | shade_gouraud[shades[1]],
                   shade_gouraud[shades[2]] << 16 | shade_gouraud[shades[3]]) if max(shades) < 32 else None
        ok = found and (not (w[1] >> 16) & 4 or gouraud == g)
        exact += ok
        floor += FLOOR.pack(w[0], w[1], w[2], *chosen, FLOOR_EXACT if ok else 0)
    chunks['FLOR'] = bytes(floor)

    pool_count = mem.unpack('>3i', sym('__ZL10gPoolCount'))
    chunks['INFO'] = INFO.pack(commands, master_faces, slave_faces, len(faced), draw_count, len(mesh_offsets),
                               len(moved), len(flush_order), exact,
                               *mem.unpack('>3I', sym('_gDetailPoolVram')),
                               header['shadeFrame'], header['detailFrame'], u32(mem, sym('_gDetailVramStart')),
                               u32(mem, sym('_gTexturePmod')),
                               cap.s32(u32(mem, sym('_gTextureLod'))) if pool_count[1] else 0,
                               cap.s32(u32(mem, sym('__ZL7gBottom'))), header['farShare'], textures, sprites,
                               sys.argv[1].upper().encode()[:15])

    # where each lives in the lab, as in the game: high work RAM, but for the
    # level's palette, sprites and room texture set in low; and VDP1 VRAM.
    # What no timed run reads lies in low work RAM: the game's list, the
    # vertices as it drew them, the ordering tables flush was given, the
    # texture cache as the frame found it and the uploads it queued (the
    # checks' and an untimed preparation's). The low chunks come first in the file: the lab takes
    # low work RAM past them back once the rest is where it belongs.
    order_ids = ['INFO', 'ORDR', 'MPOL', 'SPOL', 'VERT', 'CMDS', 'GRDS', 'TEXS', 'SHAD', 'SHCA', 'DKEY', 'DSLT',
                 'DIVT', 'ROOM', 'RGEO', 'CAUS', 'DRAW', 'XFRM', 'MESH', 'MGEO', 'MOVE', 'FLOR', 'NMSH', 'NMAT',
                 'POSE', 'PNOD', 'PFRM', 'SINC', 'CAMR', 'PLIT', 'LGHT', 'STAT', 'SHDW', 'RLVL', 'RPRT',
                 'RVIS', 'RLGE', 'CAUW', 'CALL', 'OITM', 'ANIM', 'AFRM', 'MODL', 'MIDX', 'MNOD', 'SMSH', 'RLIT',
                 'DKY0', 'DSL0', 'DUPL', 'RTXI', 'RTEX', 'PALT', 'SPRS', 'VRAM']
    where = {'PALT': WHERE_LOW, 'SPRS': WHERE_LOW, 'VRAM': WHERE_VDP1, 'CMDS': WHERE_LOW, 'GRDS': WHERE_LOW,
             'VERT': WHERE_LOW, 'ORDR': WHERE_LOW, 'DKY0': WHERE_LOW, 'DSL0': WHERE_LOW, 'DUPL': WHERE_LOW,
             'RTXI': WHERE_LOW, 'RTEX': WHERE_LOW}
    table = 8 + 16 * len(order_ids)
    offset = (table + 15) & ~15
    body = bytearray()
    at = {}
    rank = {WHERE_LOW: 0, WHERE_HIGH: 1, WHERE_VDP1: 2}
    run = WHERE_LOW
    for cid in sorted(order_ids, key=lambda c: rank[where.get(c, WHERE_HIGH)]):
        if where.get(cid, WHERE_HIGH) != run:
            # the next run (high, then VDP1) from a sector's start: the lab
            # reads each straight to where it lives
            run = where.get(cid, WHERE_HIGH)
            body += bytes(-(offset + len(body)) % SECTOR)
        at[cid] = offset + len(body)
        body += chunks[cid]
        body += bytes(-len(body) % 16)
    entries = [struct.pack('>4sIII', cid.encode(), at[cid], len(chunks[cid]), where.get(cid, WHERE_HIGH))
               for cid in order_ids]
    out = struct.pack('>II', SCENE_MAGIC, len(order_ids)) + b''.join(entries)
    out += bytes(offset - len(out)) + body
    with open(out_path, 'wb') as f:
        f.write(out)

    print(f'{sys.argv[1]}: {commands} commands; faces {master_faces} master, {slave_faces} slave; '
          f'{len(faced)} rooms, {draw_count} draws ({len(mesh_offsets)} meshes, {copied} faces copied); '
          f'{len(moved)} faces moved by draw order hints'
          + (f' (things {sorted(hinted)})' if hinted else '')
          + f'; floor exact on {exact} of {len(flush_order)}; {len(materials.data) // facepack.MATERIAL.size} '
          f'materials; {len(out) // 1024} KB')


if __name__ == '__main__':
    main()
