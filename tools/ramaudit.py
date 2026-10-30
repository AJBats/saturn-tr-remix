"""The RAM the new renderer (lab/) takes in TRS against what TRS's own
renderer frees: step 8's switch, measured (OPEN_ISSUES 239).

    ramaudit.py [--trs build/ship] [--lab build/lab] [--data build/data]

TRS as it ships, without HW_LOG's tools (make BUILD=build/ship
build/ship/0MAIN.elf builds it; an HW_LOG build is said to be one). Reads
TRS's map (its ELF, link map and objects: only those linked), the lab's
(likewise, and its structures' sizes from the cross-compiler) and every
level's packed file (build/data/*.PKD), and prints:

  1. TRS's high work RAM: its sections, what the master's stack leaves free,
     and the old renderer's buffers, which go at the switch.
  2. Each level's data: its meshes as TRS keeps them (gMeshData) and in the
     new formats (meshrot.dsp's groups, the face sets, the materials, each
     mesh's parts); its room textures (a RoomMaterial each); its rooms'
     vertices (which roomrot.dsp reads by DMA, so high work RAM only) and
     faces against gRoomData; its low work RAM, and how much of that is a
     copy TRS has also placed in high work RAM.
  3. The new renderer's buffers (renderer/, as the lab builds it): as they
     are, and at the game's limits where the lab sizes for itself.
  4. Code: the old renderer's against the new's (its checks and C models
     left out), and 0MAIN.BIN's size before and after.
  5. High work RAM after the switch, at each level.
  6. The same with three of TRS's buffers that no frame reads moved to low
     work RAM (the level's texture entries, the palettes' other versions,
     the inventory's item textures), and low work RAM with each level's
     meshes and placed rooms loaded straight to high work RAM instead of
     copied there.

The lab's buffers and functions are sorted into the renderer's and the
lab's own (checks, the game's way, pages) by the tables below: whatever the
renderer's objects hold counts as the renderer's unless a table names it
the lab's, and the audit lists names in those tables the lab no longer has.
"""
import argparse
import glob
import os
import re
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import facepack     # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, 'toolchain', 'gcc-14.2.0', 'sh2eb-elf', 'bin')
EXE = '.exe' if os.name == 'nt' else ''
HWRAM_TOP = 0x06100000
LWRAM_BYTES = 1024 * 1024
KB = 1024

# --- the game's limits ------------------------------------------------------

LIMITS = {
    'rooms': 139,           # openlara MAX_ROOMS: LEVEL7A
    'room_list': 32,        # MAX_ROOM_LIST: the rooms a frame's walk lists
    'room_jobs': 64,        # rooms.cpp MAX_ROOM_JOBS
    'room_fills': 64,
    'draws': 256,           # lab MAX_DRAWS: the master's 2048 vertices hold about that many of the smallest meshes
    'items': 64,            # lab MAX_POSE_ITEMS
    'statics': 128,         # rooms.cpp MAX_STATIC_SKIPS
    'shadows': 32,
    'faces': 256,           # sprites and the HUD's rectangles
    'room_textures': 288,   # GYM has 279 (this audit)
}
MAX_VERTICES = 5 * 1024
MASTER_VERTICES = 2048
MAX_ROOMS_LAB = 64          # lab/vertices.cpp MAX_ROOMS: rooms with vertices a frame
MAX_ROOM_VERTICES = MAX_VERTICES - MASTER_VERTICES + 3 * MAX_ROOMS_LAB

# --- TRS: the old renderer's buffers (they go at the switch) -----------------

OLD_BUFFERS = {
    'render.cpp': ['gCmds', 'gGouraud', 'gVertices', 'gFaces', 'gOT', 'gTurnedFrom',
                   'turnObjectQuads(unsigned int)::turnedIndex', 'gHeld', 'gActiveHints', 'gFarXform'],
    'rooms.cpp': ['gSlaveFaces', 'gSlaveOT', 'gSlaveMeshOT', 'gDspDivide', 'gJobs', 'gWork', 'gStaticSkipped',
                  'gItemClaims', 'gStaticTaken', 'gMeshList', 'gFaceList', 'gXform'],
}
# its code: these objects whole (render.cpp's HUD, text and sprite calls stay
# as calls into the new, so a little less goes), and the game's drawing that
# the new renderer's pose, light, walk and draw list replace
OLD_CODE = ['render.cpp', 'rooms.cpp', 'faces.s', 'flush.s', 'meshxform.s', 'xform.s']
OLD_FUNCTIONS = {
    'main.cpp': ['drawLaraNodesLerp', 'drawLaraNodes', 'drawNodesLerp', 'drawNodes', 'calcLightingDynamic',
                 'calcLightingStatic', 'drawModel', 'drawRoomObjects', 'drawRoomStatic', 'drawShadow',
                 'Room::checkPortal', 'Room::addVisibleRoom', 'Room::getVisibleRooms', 'drawRooms'],
}
# the debug menu's tools that work on the old renderer's faces, and go with it
OLD_TOOLS = ['hunt.cpp']

# --- the lab: the new renderer's buffers -------------------------------------

# the renderer's objects (renderer/, built for the lab: the lab's halves of
# vertices and faces included in them); every buffer in them is the
# renderer's but those named in LAB_ONLY
NEW_OBJECTS = ['frame.cpp', 'vertices.cpp', 'faces.cpp', 'pose.cpp', 'draws.cpp', 'rooms.cpp', 'hints.cpp']
LAB_ONLY = {
    'vertices.cpp': ['gDspJobs3', 'meshlight3_dsp', 'verticesRoomsSeldomCheck()::kCaustics', 'gSceneVertices',
                     'gMeshReady', 'gReady', 'gSeldomRotated', 'gSeldomPoints', 'gSeldomAsm', 'gSeldomModel', 'gFeedC',
                     'gFeedResults', 'gFeedXforms', 'gFeedParts', 'gClaims'],
    'faces.cpp': ['facesOtherCheck(unsigned short const*)::sides', 'facesOtherCheck(unsigned short const*)::kBuckets'],
}
# the renderer's buffers in the game's objects: the vertices it projects the
# rooms into (the lab keeps the game's way's master vertices below them, for
# the scene's copied faces; the switch doesn't)
NEW_ELSEWHERE = {('game.cpp', 'gVertices'): (MAX_ROOM_VERTICES * 8, 'rooms\' from 0, not MASTER_VERTICES')}
# the lab's sizes against the game's (names without their functions'
# parameters)
MOST_MESH_FACES = 256       # LEVEL3B has a mesh of 163 (this audit)
NEW_RESIZED = {
    'render::facesMeshHinted::made': (MOST_MESH_FACES * 16, "a mesh's faces held: 512 in the lab"),
}
# code: the renderer's assembly whole, and its C but for the lab's own
NEW_ASM = ['pose.s', 'facesroom.s', 'facesmesh.s', 'draws.s', 'rooms.s', 'roomproject.s', 'meshfeed.s',
           'facesother.s', 'meshproject.s', 'facesjoin.s']
LAB_FUNCTIONS = ('Check', 'Seldom', 'Poison', 'facesRoomC', 'facesMeshC', 'facesOther(', 'quadCommand',
                 'facesJoin(', 'Dsp3', 'meshlight3', 'feedCompare', 'meshExact', 'meshAsMade', 'readyVertices',
                 'rndFrame', 'listsOff', 'jobsOff', 'verticesTogether', 'verticesMeshesDsp()',
                 'verticesRoomsDsp()', 'verticesRooms()', 'verticesMeshes()', 'verticesFeed()', 'roomsAsTurned',
                 'main', 'stageNew', 'stagePose', 'stageRun', 'stageThree', 'timeRow', 'put', 'show(', 'workRow',
                 'Page::', 'pad(', 'togetherMeasured', 'newMeshes', 'newPrepare', 'newRooms(', 'roomQuadFlags', 'shadeTables', 'upload(', 'labTrace',
                 'hintsSceneDraws', 'CSWTCH', 'LERPS', 'kRates', '::table', '::kBuckets', '::kCaustics',
                 '::LERP', '::RADII', 'newJoin', 'newAsm', 'newSettle', 'newCheck', 'meshFlush', 'roomFlush')

# TRS's buffers no frame reads, which could go to low work RAM: the bytes
# each takes there by level (None: none, it reads them where they are)
MOVES = [
    ('render.cpp', 'gTextures', None, "the level's texture entries: its file in low work RAM holds them"),
    ('palettes.cpp', 'gPalettes', '.PAL', "swapped into VRAM by the CPU at load and at the player's pick"),
    ('detail.cpp', 'gItemData', '.ITX', 'copied to VRAM once as the inventory opens (by the CPU from low)'),
]
STACK = 4 * KB              # saturn.ld: what bss leaves for the master's stack
SECTION = re.compile(r'^ (\.\S+|COMMON)(?:\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+))?$')
CONTINUED = re.compile(r'^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)$')


def linked(path):
    """Each linked object's code and read-only data from a GNU ld map (its
    merged strings left out: the map gives them all to the first object),
    and whether the build is an HW_LOG one (trace.cpp has code)."""
    lines = open(path).read().splitlines()
    sizes = {os.path.basename(l[5:].replace('\\', '/')): 0 for l in lines if l.startswith('LOAD ')}
    k = next(i for i, l in enumerate(lines) if l.startswith('Linker script and memory map'))
    while k < len(lines):
        m = SECTION.match(lines[k])
        k += 1
        if not m:
            continue
        if m.group(2) is None:
            c = CONTINUED.match(lines[k]) if k < len(lines) else None
            if not c:
                continue
            addr, size, obj = int(c.group(1), 16), int(c.group(2), 16), c.group(3)
            k += 1
        else:
            addr, size, obj = int(m.group(2), 16), int(m.group(3), 16), m.group(4)
        sec = m.group(1)
        code_section = sec.startswith('.text') or (sec.startswith('.rodata') and '.str' not in sec)
        if addr and code_section:
            name = os.path.basename(obj.replace('\\', '/'))
            sizes[name] = sizes.get(name, 0) + size
    return sizes, sizes.get('trace.cpp.o', 0) > 0


def plain(name):
    """A symbol's name without its functions' parameter lists."""
    while True:
        bare = re.sub(r'\([^()]*\)', '', name)
        if bare == name:
            return bare.replace(' const', '')
        name = bare


def tool(name):
    return os.path.join(BIN, f'sh2eb-elf-{name}{EXE}')


def symbols(obj, kinds):
    """An object's defined symbols of the given nm kinds: {name: size}."""
    out = subprocess.run([tool('nm'), '-S', '-C', '--defined-only', obj], capture_output=True, text=True).stdout
    found = {}
    for line in out.splitlines():
        p = line.split(None, 3)
        if len(p) == 4 and p[2] in kinds:
            found[p[3].strip()] = found.get(p[3].strip(), 0) + int(p[1], 16)
    return found


def data(obj):
    return symbols(obj, 'bBdDrR')


def code(obj):
    return symbols(obj, 'tTrR')


def sections(elf):
    out = subprocess.run([tool('objdump'), '-h', elf], capture_output=True, text=True).stdout
    found = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 4 and p[1].startswith('.') and not p[1].startswith('.debug'):
            found[p[1]] = (int(p[2], 16), int(p[3], 16))
    return found


def kb(n):
    return f'{n / KB:6.1f}'


# --- levels -------------------------------------------------------------------

ROOM_INFO = struct.Struct('>4h4H4B4B6H8I')     # pack_level.py write_rooms
PORTAL_BYTES = 32                              # Portal: room, normal, 4 corners


def level(path):
    d = open(path, 'rb').read()
    counts = struct.unpack_from('>I14H', d, 0)[1:]
    offs = struct.unpack_from('>35I', d, 32)
    rooms, meshes, textures = counts[1], counts[3], counts[8]
    palette = struct.unpack_from('>256H', d, offs[0])
    mesh_base, mesh_offsets = offs[5], offs[6]
    entries = [struct.unpack_from('>5H', d, offs[15] + 16 * i) for i in range(textures)]
    materials = facepack.Materials(entries, palette, facepack.PMOD_LUT16 | facepack.PMOD_GOURAUD)
    lv = {'name': os.path.basename(path)[:-4], 'pkd': len(d), 'meshes': 0, 'old': 0, 'groups': 0, 'sets': 0,
          'heads': 0, 'shades': 0, 'faces': 0}
    for at in sorted(set(struct.unpack_from(f'>{meshes}i', d, mesh_offsets))):
        m = d[mesh_base + at:]
        n, light = m[10], m[11]
        quads, triangles = struct.unpack_from('>hh', m, 12)
        shades = (n + 1) & ~1 if light == facepack.MESH_LIGHT_SHADES else 0
        size = facepack.MESH_HEADER + 6 * n + (4 * n if light == facepack.MESH_LIGHT_NORMALS else shades) + \
            6 * (quads + triangles)
        lv['meshes'] += 1
        lv['old'] += (size + 3) & ~3
        lv['groups'] += ((n + 3) >> 2) * (40 if light == facepack.MESH_LIGHT_NORMALS else 24)
        lv['shades'] += (shades + 3) & ~3
        lv['heads'] += facepack.MESH_HEADER
        lv['sets'] += len(facepack.mesh_faces(m, materials))
        lv['faces'] += quads + triangles
    if lv['old'] != mesh_offsets - mesh_base:
        sys.exit(f'{path}: meshes read as {lv["old"]} bytes, the file has {mesh_offsets - mesh_base}')
    lv['materials'] = len(materials.data)
    lv['new'] = lv['heads'] + lv['groups'] + lv['shades'] + lv['sets']
    store = path[:-4] + '.MSH'
    lv['store'] = os.path.getsize(store) if os.path.exists(store) else lv['new'] + lv['materials']
    room_textures, vertices, faces, portals, placed, at = 0, 0, 0, 0, 0, 0
    lv_faces = []
    for r in range(rooms):
        info = ROOM_INFO.unpack_from(d, offs[3] + ROOM_INFO.size * r)
        q, t, v = info[4], info[5], info[6]
        for k in range(q):
            room_textures = max(room_textures, (struct.unpack_from('>I', d, info[22] + 12 * k)[0] & 0x3FFF) + 1)
        for k in range(t):
            room_textures = max(room_textures, (struct.unpack_from('>I', d, info[23] + 12 * k)[0] & 0x3FFF) + 1)
        vertices += 4 * v
        faces += 12 * (q + t)
        portals += info[8]
        if at + 4 * v + 12 * (q + t) <= 128 * KB:      # os.cpp osPlaceRooms
            at += 4 * v + 12 * (q + t)
            placed += 1
        lv_faces.append(12 * (q + t))
    # the new renderer's gRoomData: every room's vertices (roomrot.dsp reads
    # them by DMA), then rooms' faces as they fit
    new_placed = vertices
    for f in lv_faces:
        if new_placed + f <= 128 * KB:
            new_placed += f
    lv.update(rooms=rooms, room_textures=room_textures, room_vertices=vertices, room_faces=faces,
              placed_rooms=placed, placed=at, portals=portals, new_placed=new_placed)
    for ext in ('PAL', 'ITX'):
        other = path[:-3] + ext
        lv[ext] = os.path.getsize(other) if os.path.exists(other) else 0
    sub = path[:-4] + '.SUB'
    lv['sub'] = os.path.getsize(sub) if os.path.exists(sub) else 0
    lv['low'] = ((lv['pkd'] + 15) & ~15) + ((lv['sub'] + 15) & ~15)
    lv['parts'] = 40 * (lv['meshes'] + LIMITS['shadows'])     # MeshParts
    lv['walk'] = 16 * rooms + PORTAL_BYTES * portals              # RoomLevel each, the portals gathered
    return lv


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--trs', default=os.path.join(ROOT, 'build', 'ship'))
    ap.add_argument('--lab', default=os.path.join(ROOT, 'build', 'lab'))
    ap.add_argument('--data', default=os.path.join(ROOT, 'build', 'data'))
    args = ap.parse_args()
    if not os.path.exists(os.path.join(args.trs, '0MAIN.map')):
        sys.exit(f'no TRS build in {args.trs}: make BUILD=build/ship build/ship/0MAIN.elf')
    trs_code, hw_log = linked(os.path.join(args.trs, '0MAIN.map'))
    lab_code, _ = linked(os.path.join(args.lab, '0MAIN.map'))
    for name in list(OLD_BUFFERS) + OLD_CODE + list(OLD_FUNCTIONS) + OLD_TOOLS + [m[0] for m in MOVES]:
        if name + '.o' not in trs_code:
            sys.exit(f'{name} is not linked in {args.trs}: name a linked object')
    for name in NEW_OBJECTS + NEW_ASM:
        if name + '.o' not in lab_code:
            sys.exit(f'{name} is not linked in {args.lab}: name a linked object')
    trs_obj = lambda name: os.path.join(args.trs, 'obj', 'saturn', name + '.o')
    lab_obj = lambda name: next(p for p in (os.path.join(args.lab, 'obj', d, name + '.o') for d in ('renderer', 'lab'))
                                if os.path.exists(p))

    # 1. TRS today
    sec = sections(os.path.join(args.trs, '0MAIN.elf'))
    bss_vma, bss_size = sec['.bss'][1], sec['.bss'][0]
    free = HWRAM_TOP - (bss_vma + bss_size)
    print('1. TRS TODAY, HIGH WORK RAM (KB)' + ('  AN HW_LOG BUILD: its tools take more than the game ships with'
                                               if hw_log else ', as it ships'))
    for name in ('.text', '.rodata', '.data', '.bss'):
        print(f'  {name:10s} {kb(sec[name][0])}')
    print(f'  free       {kb(free)}  (the master\'s stack grows down into it)')
    old_buffers = 0
    print('  the old renderer\'s buffers:')
    for obj, names in OLD_BUFFERS.items():
        have = data(trs_obj(obj))
        size = sum(have[n] for n in names)
        old_buffers += size
        big = ', '.join(f'{n} {have[n] // KB}' for n in names if have[n] >= 4 * KB)
        print(f'    {obj:14s} {kb(size)}  ({big})')
    print(f'    {"all":14s} {kb(old_buffers)}')
    gmesh = data(trs_obj('os.cpp'))['gMeshData']
    groom = data(trs_obj('os.cpp'))['gRoomData']
    print(f'  level data placed here: gMeshData {kb(gmesh)}, gRoomData {kb(groom)}')

    # 2. levels
    levels = [level(p) for p in sorted(glob.glob(os.path.join(args.data, '*.PKD')))]
    print()
    print('2. EACH LEVEL\'S DATA (KB)')
    print('  meshes: TRS\'s (gMeshData) against the new formats: header + meshrot.dsp\'s groups + shades + '
          'face sets, the materials; the level\'s mesh store (DATA/<level>.MSH, its tables in it), the parts '
          '(MeshParts, shadows\' too) and all the new')
    print(f'  {"level":9s} {"meshes":>6s} {"faces":>5s} {"TRS":>6s} {"new":>6s} = {"head":>5s} {"groups":>6s} '
          f'{"shades":>6s} {"sets":>6s} {"mater":>6s}   {"store":>6s} {"parts":>6s}   {"all new":>7s} {"+":>6s}')
    for lv in levels:
        lv['mesh_store'] = lv['store'] + lv['parts']
        print(f'  {lv["name"]:9s} {lv["meshes"]:6d} {lv["faces"]:5d} {kb(lv["old"])} {kb(lv["new"])} = '
              f'{kb(lv["heads"])[1:]} {kb(lv["groups"])} {kb(lv["shades"])} {kb(lv["sets"])} '
              f'{kb(lv["materials"])}   {kb(lv["store"])} {kb(lv["parts"])}   {kb(lv["mesh_store"])} '
              f'{kb(lv["mesh_store"] - lv["old"])}')
    print('  rooms: room textures (a RoomMaterial, 64 bytes, each), the walk\'s tables (RoomLevel + portals), '
          'vertices (roomrot.dsp reads them by DMA: high work RAM only) and faces, against gRoomData\'s 128')
    print(f'  {"level":9s} {"rooms":>5s} {"tex":>4s} {"mater":>6s} {"walk":>6s} {"verts":>6s} {"faces":>6s} '
          f'{"TRS places":>14s}')
    for lv in levels:
        print(f'  {lv["name"]:9s} {lv["rooms"]:5d} {lv["room_textures"]:4d} {kb(64 * lv["room_textures"])} '
              f'{kb(lv["walk"])} {kb(lv["room_vertices"])} {kb(lv["room_faces"])} '
              f'{lv["placed_rooms"]:4d} {kb(lv["placed"])}' +
              ('' if lv['placed_rooms'] == lv['rooms'] else '  (the rest in low work RAM)'))
    print('  low work RAM: the level (.PKD) and its room texture set (.SUB); of those, the meshes and the placed '
          'rooms are copies TRS has also put in high work RAM')
    print(f'  {"level":9s} {"PKD":>6s} {"SUB":>6s} {"free":>6s}   {"copies":>6s}')
    for lv in levels:
        print(f'  {lv["name"]:9s} {kb(lv["pkd"])} {kb(lv["sub"])} {kb(LWRAM_BYTES - lv["low"])}   '
              f'{kb(lv["old"] + lv["placed"])}')

    # 3. the new renderer's buffers
    print()
    print('3. THE NEW RENDERER\'S BUFFERS (KB): as the lab builds them, and at the game\'s limits')
    new_static = new_lab = 0
    unnamed = []
    rows = []
    for obj in NEW_OBJECTS:
        have = data(lab_obj(obj))
        lab_only = LAB_ONLY.get(obj, [])
        mine = {n: s for n, s in have.items() if n not in lab_only and not n.endswith('_dsp')}
        for n, s in sorted(mine.items(), key=lambda x: -x[1]):
            port, note = NEW_RESIZED.get(plain(n), (s, ''))
            new_lab += s
            new_static += port
            if s >= 2 * KB:
                rows.append((obj, plain(n), s, port, note))
        small = sum(s for n, s in mine.items() if s < 2 * KB)
        rows.append((obj, 'the rest', small, small, ''))
        unnamed += [n for n in lab_only if n not in have]
    for (obj, n), (port, note) in NEW_ELSEWHERE.items():
        s = data(lab_obj(obj))[n]
        new_lab += s
        new_static += port
        rows.append((obj, n, s, port, note))
    for obj, n, s, port, note in rows:
        print(f'  {obj:13s} {n[:28]:28s} {kb(s)} {kb(port)}  {note}')
    resized = {plain(n) for obj in NEW_OBJECTS for n in data(lab_obj(obj))}
    unnamed += [n for n in NEW_RESIZED if n not in resized]
    print(f'  {"static":42s} {kb(new_lab)} {kb(new_static)}')
    if unnamed:
        print(f'  (LAB_ONLY names not in the lab any more: {", ".join(unnamed)})')

    # 4. code
    print()
    print('4. CODE (KB)')
    old_objects = sum(trs_code[o + '.o'] for o in OLD_CODE)
    old_drawing = 0
    for obj, names in OLD_FUNCTIONS.items():
        have = {plain(n): s for n, s in code(trs_obj(obj)).items()}
        old_drawing += sum(have[n] for n in names)
    old_code = old_objects + old_drawing
    print(f'  old renderer: its objects whole {kb(old_objects)}, the game\'s drawing {kb(old_drawing)}  '
          f'{kb(old_code)}')
    old_tools = sum(trs_code[o + '.o'] for o in OLD_TOOLS)
    new_asm = sum(lab_code[o + '.o'] for o in NEW_ASM)
    new_c = 0
    for obj in NEW_OBJECTS:
        for n, s in code(lab_obj(obj)).items():
            if not any(k in n for k in LAB_FUNCTIONS):
                new_c += s
    new_code = new_asm + new_c
    print(f'  new renderer: assembly {kb(new_asm)}, C {kb(new_c)} (DSP programs in it)  {kb(new_code)}')
    binary = sec['.text'][0] + sec['.rodata'][0] + sec['.data'][0]
    print(f'  0MAIN.BIN {kb(binary)} today; after the switch {kb(binary - old_code + new_code)} '
          f'({kb(new_code - old_code).strip()}); the polygon hunt, which reads the old renderer\'s faces, '
          f'{kb(old_tools).strip()} more if it goes')

    # 5. after the switch
    print()
    print('5. HIGH WORK RAM AFTER THE SWITCH (KB), by level: today\'s free + the old renderer (buffers, code, '
          'its polygon hunt, gMeshData) - the new (buffers, code, mesh store, walk tables)')
    freed = old_buffers + old_code + old_tools + gmesh
    worst = None
    for lv in levels:
        need = new_static + new_code + lv['mesh_store'] + lv['walk']
        left = free + freed - need
        if worst is None or left < worst[1]:
            worst = (lv['name'], left)
        print(f'  {lv["name"]:9s} frees {kb(freed)}, needs {kb(need)}: left {kb(left)}')
    print(f'  worst: {worst[0]}, {kb(worst[1]).strip()} KB (and the master\'s stack wants {STACK // KB} of it)')

    # 6. with TRS's frame-idle buffers in low work RAM
    print()
    print("6. WITH TRS'S BUFFERS NO FRAME READS IN LOW WORK RAM (KB)")
    moved = 0
    for obj, name, ext, note in MOVES:
        size = data(trs_obj(obj))[name]
        moved += size
        print(f'  {name:10s} {kb(size)}  {note}')
    print("  high work RAM left (the stack's 4 out), and low work RAM free once each level's meshes and the "
          "rooms placed in high work RAM load there straight (the new placing: every room's vertices, then "
          "faces as they fit) and the moved buffers take their level's bytes:")
    print(f'  {"level":9s} {"high left":>9s} {"low free":>9s}  {"(today":>7s} {"+ out":>6s} {"- in)":>6s}')
    worst_high = worst_low = None
    for lv in levels:
        need = new_static + new_code + lv['mesh_store'] + lv['walk']
        high = free + freed - need + moved - STACK
        out = lv['old'] + lv['new_placed']
        into = sum(lv[ext[1:]] for _, _, ext, _ in MOVES if ext)
        low = LWRAM_BYTES - lv['low'] + out - into
        worst_high = min(worst_high or (high, ''), (high, lv['name']))
        worst_low = min(worst_low or (low, ''), (low, lv['name']))
        print(f'  {lv["name"]:9s} {kb(high)}    {kb(low)}   {kb(LWRAM_BYTES - lv["low"])} {kb(out)} {kb(into)}')
    print(f'  least: high {kb(worst_high[0]).strip()} KB ({worst_high[1]}), low {kb(worst_low[0]).strip()} KB '
          f'({worst_low[1]})')


if __name__ == '__main__':
    main()
