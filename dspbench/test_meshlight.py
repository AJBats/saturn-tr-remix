"""Runs the mesh DSP programs (meshlight.dsp, meshlight2.dsp) in tools/dspsim.py
over page 5's meshes (page_meshes.cpp's build(), the same numbers), checks
every vertex against the math they share with saturn/meshxform.s, and times
them as Mednafen and as the Saturn would.

usage: test_meshlight.py [v1|v2 ...]
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
from dspsim import DSP, Memory, load, s32  # noqa: E402

M32 = 0xFFFFFFFF

LARA = [(20, 24), (34, 34), (23, 29), (16, 18), (34, 34), (23, 29), (16, 18), (46, 40),
        (16, 23), (13, 16), (8, 6), (16, 23), (13, 16), (8, 6), (46, 85)]
DOOR = [(38, 24)]
WOLF = [(11, 16), (16, 19), (10, 9), (27, 35), (6, 5), (6, 5), (8, 6), (14, 15), (8, 6),
        (8, 6), (8, 6), (14, 15), (8, 6), (8, 6), (8, 6), (12, 14), (8, 6), (12, 10),
        (8, 6), (12, 14), (8, 6), (12, 10), (8, 6), (8, 6), (8, 6), (8, 6)]
PLANK = [(26, 16)]
ROWS = [(14189, 0, -8192, 4096, 15851, 7094, 7094, -4096, 12288),
        (14189, 4096, 7094, 0, 15851, -4096, -8192, 7094, 12288),
        (16384, 0, 0, 0, 14189, -8192, 0, 8192, 14189),
        (11585, 0, 11585, 0, 16384, 0, -11585, 0, 11585)]

DATA = 0x06040000       # the meshes
JOBS = 0x06070000       # the job list
OUT_V1 = 0x06080000
OUT_V2 = 0x060A0000
SCRATCH = 0x060C0000  # meshlight2.dsp's first output, of nothing
DSPDATA = 0x06050000  # meshlight3.dsp's vertices, groups of 4
JOBS3 = 0x06078000
OUT_V3 = 0x060B0000


class Seed:
    def __init__(self):
        self.v = 0x2B7E1516

    def next(self):
        self.v = (self.v * 1664525 + 1013904223) & M32
        return self.v


def take(state, nbytes):
    at = state['p']
    state['p'] += (nbytes + 15) & ~15
    return at


def build():
    """page_meshes.cpp build(): the meshes' vertices, normals and setups."""
    meshes = []
    seed = Seed()
    state = {'p': DATA}
    out = {'v1': OUT_V1, 'v2': OUT_V2}
    lists = [LARA, DOOR, WOLF, WOLF] + [PLANK] * 12
    for lst in lists:
        for count, faces in lst:
            m = len(meshes)
            whole = (count + 7) // 8 * 8
            mesh = {'count': count, 'src': take(state, whole * 6), 'normals': take(state, whole * 4)}
            verts, norms = [], []
            for _ in range(whole):
                c = []
                for _ in range(3):
                    c.append(((seed.next() >> 16) & 511) - 256)
                verts.append(c)
                n = []
                for k in range(4):
                    v = seed.next()
                    n.append(s8(v >> 24) if k == 3 else ((v >> 16) % 147) - 73)
                norms.append(n)
            x = seed.next()
            xf = {
                'rows': ROWS[m & 3],
                'tx': (((x >> 8) % 601) - 300) << 14,
                'ty': (((x >> 18) % 401) - 200) << 14,
                'tz': (2500 + ((x >> 4) % 6500)) << 14,
                'ambient': 5000 + ((x >> 12) % 800),
                'lx': ((x >> 20) % 147) - 73,
                'ly': ((x >> 12) % 147) - 73,
                'lz': ((x >> 4) % 147) - 73,
                'range': 1250 + ((x >> 2) % 200),
            }
            mesh.update(verts=verts, norms=norms, xf=xf, faces=faces)
            for k, base in out.items():
                mesh['out_' + k] = base
                out[k] += whole * 8
            meshes.append(mesh)
    return meshes


def s8(v):
    v &= 0xFF
    return v - 256 if v & 0x80 else v


def s16(v):
    v &= 0xFFFF
    return v - 65536 if v & 0x8000 else v


def xform_words(xf, normals_addr):
    r = xf['rows']
    pairs = [(r[0], r[1]), (r[2], r[3]), (r[4], r[5]), (r[6], r[7]), (r[8], 0)]
    words = [((a & 0xFFFF) << 16) | (b & 0xFFFF) for a, b in pairs]
    words += [xf['tx'], xf['ty'], xf['tz'], 0, xf['ambient'], xf['lx'], xf['ly'], xf['lz'], xf['range'],
              normals_addr, 0, 0, 1]
    return [w & M32 for w in words]


def store(mem, meshes):
    for mesh in meshes:
        v = mesh['verts']
        halves = [c & 0xFFFF for vert in v for c in vert]
        for i in range(0, len(halves), 2):
            mem.write(mesh['src'] + i * 2, (halves[i] << 16) | halves[i + 1])
        for i, n in enumerate(mesh['norms']):
            mem.write(mesh['normals'] + i * 4, ((n[0] & 0xFF) << 24) | ((n[1] & 0xFF) << 16) |
                      ((n[2] & 0xFF) << 8) | (n[3] & 0xFF))


def expected(mesh):
    """Each vertex's two longwords, as meshxform.s's shaded fast path makes them."""
    xf = mesh['xf']
    r = xf['rows']
    out = []
    for i in range(mesh['count']):
        vx, vy, vz = mesh['verts'][i]
        x = r[0] * vx + r[1] * vy + r[2] * vz + xf['tx']
        y = r[3] * vx + r[4] * vy + r[5] * vz + xf['ty']
        z = r[6] * vx + r[7] * vy + r[8] * vz + xf['tz']
        nx, ny, nz, _ = mesh['norms'][i]
        nl = nx * xf['lx'] + ny * xf['ly'] + nz * xf['lz']
        t = xf['ambient'] - ((nl * s16(xf['range'])) >> 14)
        shade = min(max(t, 0), 0x1FFF) & 0x1F00
        out.append(((((x >> 14) & 0xFFFF) << 16) | ((y >> 14) & 0xFFFF),
                    (((z >> 14) & 0xFFFF) << 16) | shade))
    return out


def run_v1(meshes, timing):
    prog, labels = load(os.path.join(HERE, 'meshlight.dsp'))
    mem = Memory()
    store(mem, meshes)
    addr = JOBS
    for mesh in meshes:
        words = [(mesh['count'] + 7) // 8, mesh['normals'] >> 2, mesh['src'] >> 2, mesh['out_v1'] >> 2]
        words += xform_words(mesh['xf'], mesh['normals'])
        for k, w in enumerate(words):
            mem.write(addr + k * 4, w)
        addr += 22 * 4
    mem.write(addr, 0)
    dsp = DSP(prog, mem, timing)
    for i, v in enumerate([4, 0, 0x1FFF, 0x1F00, 0x1FFF, 0xFFFF, 0x8000, 0xFF000000]):
        dsp.ram[0][16 + i] = v
    for i, v in enumerate([0xFFFF, 0x8000, 256]):
        dsp.ram[1][48 + i] = v
    for i, v in enumerate([0xFFFF0000, 0xFFFF, 0, 4]):
        dsp.ram[3][10 + i] = v
    dsp.ram[3][40] = JOBS >> 2
    dsp.run(0)
    return dsp, mem, 'out_v1'


# meshlight2.dsp's constants (page_meshes.cpp meshLight2Load)
V2_CONSTANTS = {4: 0xFFFF0000, 9: 0xFFFF, 14: 0xFFFF0000, 19: -4 & M32, 21: 0xFFFFE000, 22: 0x1F00,
                37: 65536, 38: 256, 39: 0xFF000000, 40: 0xFFFF, 41: 0x8000, 42: 4}


def run_v2(meshes, timing):
    prog, labels = load(os.path.join(HERE, 'meshlight2.dsp'))
    mem = Memory()
    store(mem, meshes)
    addr = JOBS
    for mesh in meshes:
        words = [mesh['count'], 0, mesh['src'] >> 2, 0]
        words += xform_words(mesh['xf'], mesh['normals'])
        for k, w in enumerate(words):
            mem.write(addr + k * 4, w)
        addr += 22 * 4
    mem.write(addr, 0)
    dsp = DSP(prog, mem, timing)
    for i, v in V2_CONSTANTS.items():
        dsp.ram[0][i] = v
    dsp.ram[0][46] = JOBS >> 2
    dsp.ram[0][51] = SCRATCH >> 2
    dsp.ram[0][50] = OUT_V2 >> 2      # the outputs follow each other from here
    dsp.run(0)
    return dsp, mem, 'out_v2'


def run_v3(meshes, timing):
    src = os.path.join(HERE, '..', 'build', 'dspbench', 'gen', 'meshlight3.dsp')
    os.makedirs(os.path.dirname(src), exist_ok=True)
    subprocess.check_call([sys.executable, os.path.join(HERE, 'gen_meshlight3.py'), src])
    prog, labels = load(src)
    mem = Memory()
    data, out, rec = DSPDATA, OUT_V3, JOBS3
    for mesh in meshes:
        groups = (mesh['count'] + 3) // 4
        mesh['out_v3'] = out
        out += groups * 32
        for v in range(groups * 4):
            vert = mesh['verts'][v] if v < mesh['count'] else [0, 0, 0]
            norm = mesh['norms'][v][:3] if v < mesh['count'] else [0, 0, 0]
            for k, c in enumerate(vert + norm):
                mem.write(data + (v * 6 + k) * 4, c)
        xf = mesh['xf']
        r = xf['rows']
        k = []
        for row, t in ((0, 'tx'), (1, 'ty'), (2, 'tz')):
            k += [4 * xf[t]] + [4 * r[row * 3 + c] for c in range(3)]
        k += [(xf['ambient'] << 16) | 0xFFFF] + [xf[a] * xf['range'] * -4 for a in ('lx', 'ly', 'lz')]
        for i, w in enumerate([data >> 2, groups - 1] + k):
            mem.write(rec + i * 4, w)
        data += groups * 24 * 4
        rec += 18 * 4
    mem.write(rec, DSPDATA >> 2)     # the list's end: readable, as the DSP fetches past it
    mem.write(rec + 4, M32)
    dsp = DSP(prog, mem, timing)
    for i, v in {16: 65536, 17: 0xFFFF, 18: 0x1F00, 20: (JOBS3 >> 2) + 18, 21: 20,
                 23: OUT_V3 >> 2, 24: JOBS3 >> 2}.items():
        dsp.ram[0][i] = v
    dsp.ram[3][16] = 1
    dsp.run(labels['start'])
    return dsp, mem, 'out_v3'


def check(meshes, mem, key):
    bad = 0
    for m, mesh in enumerate(meshes):
        for i, (xy, zg) in enumerate(expected(mesh)):
            got = (mem.read(mesh[key] + i * 8), mem.read(mesh[key] + i * 8 + 4))
            if got != (xy, zg):
                if bad < 5:
                    print(f'  mesh {m} vertex {i}: got {got[0]:08X} {got[1]:08X}, want {xy:08X} {zg:08X}')
                bad += 1
    return bad


def main():
    which = sys.argv[1:] or ['v1']
    meshes = build()
    nv = sum(m['count'] for m in meshes)
    print(f'{len(meshes)} meshes, {nv} vertices')
    runs = {'v1': run_v1, 'v2': run_v2, 'v3': run_v3}
    for name in which:
        for timing in ('mednafen', 'saturn'):
            dsp, mem, key = runs[name](meshes, timing)
            bad = check(meshes, mem, key)
            sh2 = dsp.cycles * 2
            print(f'{name} {timing:8s}: {sh2 / 28636:6.2f} ms, {sh2 / nv:6.1f} SH-2 cycles a vertex '
                  f'({dsp.executed} instructions, {dsp.stalls * 2 / 28636:.2f} ms waiting on DMA); '
                  f'{"exact" if not bad else f"{bad} vertices differ"}')


if __name__ == '__main__':
    main()
