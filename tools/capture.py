"""Captures one frame's render inputs for the clean-room renderer's lab, from
a build of the old renderer (saturn/capture.h, which step 8 removed: a
worktree of d10edc4 or before).

    capture.py CUE [--frame N] [--keys F:K,K,... | --trace FILE] [--out NAME]

Boots CUE in a Mednafen of its own (as trace.py capture: its own automation
and home directories), taps the keys or plays the input trace from boot
(lines "frame=N PRESS|RELEASE BUTTON", as the debugger's input_trace writes
them; tools/traces/), runs to frame N, then asks the game for
a capture (gCaptureRequest) and advances until that frame is recorded and
held. Into build/captures/NAME:
    lwram.bin, hwram.bin    both work RAMs with the frame held: the records,
                            and the vertices, faces and commands as drawn
    vdp1.bin                VDP1 VRAM once the frame is on screen (textures,
                            colour tables, the list)
    frame.png               the held frame on screen
    frame.json              the header and records, their pointers resolved:
                            meshes as level.meshes indices, sprites as
                            level.sprites indices
    0MAIN.elf               the program, whose symbols name what the dumps hold
The game is then let go, and must draw on. Last, checks the records against
the held frame: each mesh, static mesh and shadow transformed and shaded
again (meshxform.s's arithmetic) with the record's matrix and light must give
the vertices drawn at its base; and the ordering tables flush was given must
hold as many faces as it made commands.

The ELF beside CUE (0MAIN.elf) names the game's variables; build/0MAIN.elf
otherwise.
"""
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NM = os.path.join(ROOT, 'toolchain/gcc-14.2.0/sh2eb-elf/bin/sh2eb-elf-nm')
LWRAM = 0x00200000
HWRAM = 0x06000000
RAM_BYTES = 0x100000
VDP1_VRAM = 0x05C00000
VDP1_BYTES = 0x80000

CAPTURE_MAGICS = {0x43415031: False, 0x43415032: True}    # CAP1, CAP2: with a CaptureCache
CAPTURE_ARM, CAPTURE_RELEASE = 1, 2
FRAMES_OFFSET = 24                              # FrameStats.frames (system.h)
HEADER = struct.Struct('>4I3i9h2BH2x2Ii')      # CaptureHeader, 64 bytes
RECORD = struct.Struct('>BBHI12i2i3iIII4i')     # CaptureRecord, 104 bytes
OT_SIZE = 1281
OT_GROUPS = 41
ORDER = struct.Struct(f'>{OT_SIZE}I{OT_SIZE}I{OT_GROUPS}H{OT_GROUPS}HiII')    # CaptureOrder, 10424 bytes
DETAIL_VARIANTS, DETAIL_MAX_KEYS, DETAIL_SLOTS, DETAIL_MAX_UPLOADS = 7, 1088, 256, 32
CACHE_KEYS = DETAIL_VARIANTS * DETAIL_MAX_KEYS
CACHE_SLOTS = DETAIL_SLOTS * 8
CACHE_BYTES = CACHE_KEYS + CACHE_SLOTS + 8 + DETAIL_MAX_UPLOADS * 12    # CaptureCache, 10056 bytes
TYPES = {1: 'room', 2: 'object', 3: 'mesh', 4: 'static', 5: 'shadow', 6: 'sprite',
         7: 'viewport', 8: 'model', 9: 'static call', 10: 'shadow call', 11: 'rect'}
MESH = struct.Struct('>3hhHBBhhhh')             # Mesh (common.h), 20 bytes
SPRITE_BYTES = 16                               # Sprite (common.h)
DEFAULT_KEYS = '1500:R,R,R,R'                   # trace.py's: the HW_LOG overlay paged away

# meshxform.s
VIEW_MIN_F = 0x00100000
VIEW_MAX_F = 0x14000000
P_MAX = 0x007FF000
FRAME_WIDTH = 352
FRAME_HEIGHT = 224
COORD_MAX = 1023
CLIP_FRAME, CLIP_LEFT, CLIP_RIGHT, CLIP_TOP, CLIP_BOTTOM, CLIP_PLANE = 1, 2, 4, 8, 16, 32
MESH_LIGHT_NORMALS, MESH_LIGHT_SHADES = 1, 2
SHADOW_VERTICES = 8


def symbols(elf):
    syms = {}
    out = subprocess.run([NM, '-S', elf], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4:
            syms.setdefault(parts[3], []).append((int(parts[0], 16), int(parts[1], 16)))
        elif len(parts) == 3:
            syms.setdefault(parts[2], []).append((int(parts[0], 16), 0))
    return syms


def symbol(syms, name, array=False):
    """A variable's address; of statics named alike, the array's (bigger than a pointer)."""
    found = [a for a, s in syms.get(name, []) if not array or s > 4]
    if len(found) != 1:
        sys.exit(f'{name}: {len(found)} symbols')
    return found[0]


class Mednafen:
    """A Mednafen of our own, driven through its automation files in
    build/NAME_ipc, its settings in build/NAME_home."""

    def __init__(self, cue, name='capture'):
        exe = None
        for rel in ('mednafen_debug.exe', 'src/mednafen.exe', 'mednafen_gcc494.exe'):
            path = os.path.join(ROOT, 'tools/mednafen', rel)
            if not exe and os.path.exists(path):
                exe = path
        if not exe:
            sys.exit('no Mednafen in tools/mednafen')
        self.ipc = os.path.join(ROOT, f'build/{name}_ipc')
        home = os.path.join(ROOT, f'build/{name}_home')
        os.makedirs(self.ipc, exist_ok=True)
        if not os.path.exists(os.path.join(home, 'mednafen.cfg')):
            # trace.py's home holds the calibrated settings and the firmware
            seed = os.path.join(ROOT, 'build/trace_home')
            if not os.path.exists(os.path.join(seed, 'mednafen.cfg')):
                sys.exit(f'{seed} needs a mednafen.cfg and firmware/ (see trace.py)')
            os.makedirs(home, exist_ok=True)
            shutil.copy(os.path.join(seed, 'mednafen.cfg'), home)
            shutil.copytree(os.path.join(seed, 'firmware'), os.path.join(home, 'firmware'))
        self.action = os.path.join(self.ipc, 'mednafen_action.txt')
        self.ack_file = os.path.join(self.ipc, 'mednafen_ack.txt')
        for f in (self.action, self.ack_file):
            if os.path.exists(f):
                os.remove(f)
        self.seq = 0
        env = dict(os.environ, MEDNAFEN_HOME=home)
        self.proc = subprocess.Popen([exe, '--sound', '0', '-cd.image_memcache', '1', '--automation', self.ipc, cue],
                                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.DEVNULL, env=env)
        deadline = time.time() + 60
        while 'ready' not in self.ack():
            if self.proc.poll() is not None or time.time() > deadline:
                sys.exit('Mednafen did not start')
            time.sleep(0.2)

    def ack(self):
        try:
            with open(self.ack_file) as f:
                return f.read().strip()
        except OSError:
            return ''

    def send(self, cmd, expect, timeout=600):
        before = self.ack()
        self.seq += 1
        tmp = self.action + '.tmp'
        with open(tmp, 'w', newline='\n') as f:
            f.write(f'# capture {self.seq}{"." * (self.seq % 16)}\n{cmd}\n')
        for attempt in range(50):
            try:
                os.replace(tmp, self.action)
                break
            except PermissionError:     # Mednafen is reading it
                time.sleep(0.02 * (attempt + 1))
        deadline = time.time() + timeout
        while time.time() < deadline:
            reply = self.ack()
            if reply and reply != before and expect in reply:
                return reply
            time.sleep(0.05)
        sys.exit(f'{cmd}: no reply with "{expect}" (last: {self.ack()})')

    def read(self, address, size):
        part = os.path.join(self.ipc, 'part.bin')
        data = b''
        for at in range(address, address + size, 0x10000):
            n = min(0x10000, address + size - at)
            if os.path.exists(part):
                os.remove(part)
            self.send(f'dump_mem_bin {at:x} {n:x} {part.replace(os.sep, "/")}', 'dump_mem_bin', 30)
            for _ in range(50):
                if os.path.exists(part) and os.path.getsize(part) == n:
                    break
                time.sleep(0.05)
            with open(part, 'rb') as f:
                data += f.read()
        return data

    def word(self, address):
        return struct.unpack('>I', self.read(address, 4))[0]

    def quit(self):
        try:
            self.send('quit', 'quit', 5)
        except SystemExit:
            pass
        try:
            self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            print('Mednafen still running after quit', file=sys.stderr)


class Memory:
    """Both work RAMs as dumped, read by the SH-2's addresses."""

    def __init__(self, lwram, hwram):
        self.parts = ((LWRAM, lwram), (HWRAM, hwram))

    def bytes(self, address, size):
        address &= 0x0FFFFFFF       # the cache-through mirror
        for base, data in self.parts:
            if base <= address and address + size <= base + len(data):
                return data[address - base:address - base + size]
        raise ValueError(f'{address:#x} is in neither work RAM')

    def unpack(self, fmt, address):
        return struct.unpack(fmt, self.bytes(address, struct.calcsize(fmt)))


def capture(cue, elf, frame, keys, trace, folder, wait=30):
    syms = symbols(elf)
    request = symbol(syms, '_gCaptureRequest')
    pointer = symbol(syms, '_gCapture')
    med = Mednafen(cue)
    try:
        med.send('deterministic', 'deterministic', 10)
        if trace:
            med.send(f'input_playback {os.path.abspath(trace).replace(os.sep, "/")}', 'ok input_playback', 10)
        elif keys:
            at, buttons = keys.split(':')
            med.send(f'run_to_frame {at}', 'done run_to_frame')
            for b in buttons.split(','):
                med.send(f'input {b}', 'ok input', 10)
                med.send('frame_advance 10', 'done frame_advance', 30)
                med.send(f'input_release {b}', 'ok input_release', 10)
                med.send('frame_advance 10', 'done frame_advance', 30)
        med.send(f'run_to_frame {frame}', 'done run_to_frame')
        med.send(f'poke {request:08X} 00 00 00 {CAPTURE_ARM:02X}', 'ok poke', 10)
        header = 0
        for _ in range(wait):
            med.send('frame_advance 1', 'done frame_advance', 30)
            header = header or med.word(pointer)
            if header and med.word(header + 4):
                break
        else:
            sys.exit('the game recorded no frame (a build without capture.cpp: step 8 or after; '
                     'or no low work RAM left?)')
        at = int(med.send('status', 'status', 10).split('frame=')[1].split()[0])
        print(f'frame recorded at vblank {at}, held')
        os.makedirs(folder, exist_ok=True)
        for name, base, size in (('lwram', LWRAM, RAM_BYTES), ('hwram', HWRAM, RAM_BYTES)):
            with open(os.path.join(folder, name + '.bin'), 'wb') as f:
                f.write(med.read(base, size))
        # the held frame's list is uploaded, with the textures it brought into
        # the cache, drawn and shown a few vblanks on
        med.send('frame_advance 6', 'done frame_advance', 30)
        with open(os.path.join(folder, 'vdp1.bin'), 'wb') as f:
            f.write(med.read(VDP1_VRAM, VDP1_BYTES))
        shot = os.path.join(folder, 'frame.png')
        if os.path.exists(shot):
            os.remove(shot)
        med.send(f'screenshot {shot.replace(os.sep, "/")}', 'screenshot', 30)
        time.sleep(0.5)
        # let it go: the game must draw on
        frames = symbol(syms, '_gStats') + FRAMES_OFFSET
        held = med.word(frames)
        med.send(f'poke {request:08X} 00 00 00 {CAPTURE_RELEASE:02X}', 'ok poke', 10)
        med.send('frame_advance 30', 'done frame_advance', 30)
        if med.word(pointer) or med.word(frames) == held:
            sys.exit('the game did not let the frame go')
        print(f'released: {med.word(frames) - held} frames drawn in the 30 vblanks after')
    finally:
        med.quit()
    return syms


def records(mem, syms):
    pointer = symbol(syms, '_gCapture')
    at = mem.unpack('>I', pointer)[0]
    (magic, ready, count, faces, cx, cy, cz, *rest) = mem.unpack(HEADER.format, at)
    rotation, (water, overflow, meshes_count, meshes, sprites, caustics) = rest[:9], rest[9:]
    if magic not in CAPTURE_MAGICS or not ready:
        sys.exit(f'no capture recorded at {at:#x}')
    header = {'faces': faces, 'camera': [cx, cy, cz], 'rotation': rotation, 'cameraWater': water,
              'overflow': overflow, 'causticsFrame': caustics, 'meshesCount': meshes_count,
              'meshes': meshes, 'sprites': sprites, 'records': count, 'at': at}
    order = mem.unpack(ORDER.format, at + HEADER.size)
    header['order'] = at + HEADER.size
    header['farShare'], header['shadeFrame'], header['detailFrame'] = order[-3:]
    at += ORDER.size
    if CAPTURE_MAGICS[magic]:
        header['cache'] = at + HEADER.size
        header['uploads'] = cache_uploads(mem, header)
        at += CACHE_BYTES
    mesh_index = {mem.unpack('>I', meshes + 4 * i)[0] & 0x0FFFFFFF: i for i in range(meshes_count)}
    out = []
    for i in range(count):
        (kind, node, index, ref, *rest) = mem.unpack(RECORD.format, at + HEADER.size + i * RECORD.size)
        matrix, (ambient, rng, lx, ly, lz, flags, base, first, *extra) = rest[:12], rest[12:]
        r = {'type': TYPES[kind], 'index': index, 'matrix': matrix, 'ambient': ambient, 'range': rng,
             'light': [lx, ly, lz], 'flags': flags, 'ref': ref, 'base': base, 'first': first}
        if kind in (3, 4):
            r['node'] = node
            r['mesh'] = mesh_index.get(ref & 0x0FFFFFFF)
        elif kind == 1:
            r['window'] = extra
        elif kind == 2:
            r['kind'], r['owner'] = extra[:2]
        elif kind == 5:
            r['shadow'] = extra
        elif kind == 6:
            r['sprite'] = (ref - sprites) // SPRITE_BYTES
            r['position'], r['g'] = extra[:3], extra[3]
        elif kind == 7:
            r['window'] = extra
        elif kind == 8:
            r['item'], r['anim'], r['frame'], r['itemType'] = index, ambient, rng, lx
        elif kind == 9:
            r['place'] = [lx, ly, lz]
        elif kind == 10:
            r['item'], r['floor'] = index, extra[0]
        elif kind == 11:
            r['rect'], r['rectType'], r['colour'], r['bucket'] = extra, ambient, rng, lx
        out.append(r)
    # the faces each drew: up to the next draw's first
    draws = [r for r in out if r['type'] in ('mesh', 'static', 'shadow', 'sprite')]
    for r, n in zip(draws, draws[1:]):
        r['faces'] = (n['first'] - r['first']) // 16
    if draws:
        draws[-1]['faces'] = None       # past the last, the faces' end isn't recorded
    # where the frame as drawn is: the master's vertices and faces, the commands
    header['vertices'] = symbol(syms, '_gVertices')
    header['faceList'] = symbol(syms, '__ZL6gFaces')
    header['commands'] = symbol(syms, '__ZL5gCmds', array=True)
    return header, out


def cache_uploads(mem, header):
    """The uploads the frame's faces queued (CaptureCache): each the source,
    the slot's VDP1 VRAM offset, the variant, the width and height."""
    at = header['cache'] + CACHE_KEYS + CACHE_SLOTS
    frame, count = mem.unpack('>Ii', at)
    out = []
    for i in range(count):
        src, dst, size = mem.unpack('>3I', at + 8 + 12 * i)
        out.append({'src': src, 'dst': dst & 0xFFFFFF, 'variant': dst >> 24, 'w': size >> 16, 'h': size & 0xFFFF})
    return out


def order_tables(mem, header):
    """The ordering tables flush was given (CaptureOrder): the slave's and the
    master's, each a list of OT_SIZE chains of face addresses."""
    order = mem.unpack(ORDER.format, header['order'])
    tables = []
    for heads in (order[:OT_SIZE], order[OT_SIZE:2 * OT_SIZE]):
        chains = []
        for head in heads:
            chain = []
            while head:
                chain.append(head)
                head = mem.unpack('>I', head + 4)[0]
            chains.append(chain)
        tables.append(chains)
    return tables


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def s32(v):
    v &= 0xFFFFFFFF
    return v - 0x100000000 if v & 0x80000000 else v


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


def transform(r, vertices, div_table, intensity, normals, shades):
    """meshxform.s: each vertex as Vertex's 8 bytes (x y z g clip)."""
    m = r['matrix']
    rows = [s16(m[k]) for k in (0, 1, 2, 4, 5, 6, 8, 9, 10)]
    tx, ty, tz = m[3], m[7], m[11]
    ambient, rng = r['ambient'], r['range']
    out = []
    for (vx, vy, vz) in vertices:
        z = s32(tz + rows[6] * vx + rows[7] * vy + rows[8] * vz)
        clip = 0
        if not z > VIEW_MIN_F:
            z, clip = VIEW_MIN_F, CLIP_PLANE
        if z >= VIEW_MAX_F:
            z, clip = VIEW_MAX_F, CLIP_PLANE
        x = s16(s32(tx + rows[0] * vx + rows[1] * vy + rows[2] * vz) >> 14)
        y = s16(s32(ty + rows[3] * vx + rows[4] * vy + rows[5] * vz) >> 14)
        d = div_table[z >> 18]
        sx = s16(clamp(x * d, -P_MAX, P_MAX) >> 12) + FRAME_WIDTH // 2
        sy = s16(clamp(y * d, -P_MAX, P_MAX) >> 12) + FRAME_HEIGHT // 2
        if not (0 <= sx <= FRAME_WIDTH and 0 <= sy <= FRAME_HEIGHT):
            clip += CLIP_FRAME
            sx = clamp(sx, -COORD_MAX - 1, COORD_MAX)
            sy = clamp(sy, -COORD_MAX - 1, COORD_MAX)
            clip += (CLIP_LEFT if sx < 0 else 0) + (CLIP_RIGHT if sx > FRAME_WIDTH else 0)
            clip += (CLIP_TOP if sy < 0 else 0) + (CLIP_BOTTOM if sy > FRAME_HEIGHT else 0)
        out.append([sx, sy, z >> 14, 0, clip])
    if normals and rng:
        l = r['light']
        lx = (s16(m[0]) * s16(l[0]) + s16(m[4]) * s16(l[1]) + s16(m[8]) * s16(l[2])) >> 21
        ly = (s16(m[1]) * s16(l[0]) + s16(m[5]) * s16(l[1]) + s16(m[9]) * s16(l[2])) >> 21
        lz = (s16(m[2]) * s16(l[0]) + s16(m[6]) * s16(l[1]) + s16(m[10]) * s16(l[2])) >> 21
        for v, (nx, ny, nz) in zip(out, normals):
            dot = s16(nx * lx + ny * ly + nz * lz) * s16(rng)
            v[3] = clamp(s16((ambient - s16(dot >> 14)) >> 8), 0, 31)
    elif shades:
        for v, shade in zip(out, shades):
            v[3] = clamp(s16((ambient + (shade << 5)) >> 8), 0, 31)
    else:
        g = clamp(s16((intensity + ambient) >> 8), 0, 31)
        for v in out:
            v[3] = g
    return out


def mesh_parts(mem, address):
    """A mesh's vertices, intensity, and normals or shades (render.cpp meshParts)."""
    (cx, cy, cz, radius, intensity, count, lighting, quads, triangles, bound, turned) = mem.unpack(MESH.format, address)
    at = address + MESH.size
    vertices = [mem.unpack('>3h', at + 6 * i) for i in range(count)]
    at += 6 * count
    normals = shades = None
    if lighting == MESH_LIGHT_NORMALS:
        normals = [mem.unpack('>3b', at + 4 * i) for i in range(count)]
    elif lighting == MESH_LIGHT_SHADES:
        shades = list(mem.bytes(at, count))
    return vertices, intensity, normals, shades


def shadow_vertices(x, z, sx, sz):
    """render.cpp shadowVertices."""
    xn1, xp1 = s16(x - sx), s16(x + sx)
    xn2, xp2 = s16(xn1 - sx), s16(xp1 + sx)
    zn1, zp1 = s16(z - sz), s16(z + sz)
    zn2, zp2 = s16(zn1 - sz), s16(zp1 + sz)
    return [(xn1, 0, zp2), (xp1, 0, zp2), (xp2, 0, zp1), (xp2, 0, zn1),
            (xp1, 0, zn2), (xn1, 0, zn2), (xn2, 0, zn1), (xn2, 0, zp1)]


def check(mem, syms, recs):
    """Each draw's vertices again from its record, against those drawn."""
    table = symbol(syms, '_divTable')
    div_table = [mem.unpack('>H', table + 2 * i)[0] for i in range(1281)]
    checked = vertices = 0
    bad = []
    for i, r in enumerate(recs):
        if r['type'] in ('mesh', 'static'):
            points, intensity, normals, shades = mesh_parts(mem, r['ref'])
            if r['mesh'] is None:
                bad.append(f'record {i}: mesh {r["ref"]:#x} not in level.meshes')
        elif r['type'] == 'shadow':
            points, intensity, normals, shades = shadow_vertices(*r['shadow']), 0, None, None
        else:
            continue
        want = transform(r, points, div_table, intensity, normals, shades)
        drawn = mem.bytes(r['base'], 8 * len(want))
        for k, w in enumerate(want):
            got = list(struct.unpack('>hhhBB', drawn[8 * k:8 * k + 8]))
            if got != w:
                bad.append(f'record {i} ({r["type"]} {r.get("mesh", "")} node {r.get("node", "")}) '
                           f'vertex {k}: drew {got}, recorded gives {w}')
        checked += 1
        vertices += len(want)
    return checked, vertices, bad


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cue')
    ap.add_argument('--frame', type=int, default=1900)
    ap.add_argument('--keys', default=DEFAULT_KEYS)
    ap.add_argument('--trace')
    ap.add_argument('--out', default='ledge')
    ap.add_argument('--wait', type=int, default=30,
                    help='vblanks to wait for the frame after asking (more: ask while the level loads, '
                         'for its first frame)')
    args = ap.parse_args()

    cue = os.path.abspath(args.cue)
    elf = os.path.join(os.path.dirname(cue), '0MAIN.elf')
    if not os.path.exists(elf):
        elf = os.path.join(ROOT, 'build/0MAIN.elf')
    print(f'symbols: {os.path.normpath(elf)}')
    folder = os.path.join(ROOT, 'build/captures', args.out)
    syms = capture(cue, elf, args.frame, args.keys, args.trace, folder, args.wait)
    shutil.copy(elf, os.path.join(folder, '0MAIN.elf'))

    with open(os.path.join(folder, 'lwram.bin'), 'rb') as f:
        lwram = f.read()
    with open(os.path.join(folder, 'hwram.bin'), 'rb') as f:
        hwram = f.read()
    mem = Memory(lwram, hwram)
    header, recs = records(mem, syms)
    with open(os.path.join(folder, 'frame.json'), 'w', newline='\n') as f:
        json.dump({'header': header, 'records': recs}, f, indent=1)
    kinds = {}
    for r in recs:
        kinds[r['type']] = kinds.get(r['type'], 0) + 1
    print(f'{len(recs)} records ({", ".join(f"{n} {k}" for k, n in kinds.items())}), '
          f'{header["faces"]} commands' + (', SOME LEFT OUT' if header['overflow'] else ''))
    if 'uploads' in header:
        print(f'room texture cache: {len(header["uploads"])} textures queued for upload')

    checked, vertices, bad = check(mem, syms, recs)
    for line in bad[:20]:
        print(line)
    print(f'{checked} draws, {vertices} vertices checked against the frame: '
          + ('all match' if not bad else f'{len(bad)} MISMATCHES'))
    rooms, meshes = (sum(len(c) for c in t) for t in order_tables(mem, header))
    ordered = rooms + meshes == header['faces'] - 1     # the commands but the clear
    print(f'ordering tables: {rooms} room faces, {meshes} others, '
          + ('one command each' if ordered else f'BUT {header["faces"] - 1} COMMANDS'))
    bad += [] if ordered else ['ordering tables']
    print(f'-> {os.path.normpath(folder)}')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
