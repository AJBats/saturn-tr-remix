"""Runs meshrot.dsp (lab/gen_meshrot.py) in tools/dspsim.py over the bench's
page 5 meshes (dspbench/test_meshlight.py), the planks unlit by their
normals, checks every vertex against meshxform.s's arithmetic, and times it
as Mednafen and as the Saturn would, beside meshlight3.dsp over the same
meshes.

usage: test_meshrot.py
"""
import copy
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
sys.path.insert(0, os.path.join(HERE, '..', 'dspbench'))
from dspsim import DSP, Memory, load  # noqa: E402
import test_meshlight as bench  # noqa: E402

M32 = 0xFFFFFFFF
PACKED = 0x06050000     # the meshes' groups
JOBS = 0x06078000
OUT = 0x060B0000


def pack(mesh, lit):
    """A mesh's groups as vertices.cpp packMesh lays them out."""
    words = []
    count = mesh['count']
    for g in range((count + 3) // 4):
        if lit:
            for v in range(4 * g, 4 * g + 4):
                n = mesh['norms'][v][:3] if v < count else [0, 0, 0]
                words.append(sum(((c + 128) & 0xFF) << (24 - 8 * i) for i, c in enumerate(n)))
        halves = []
        for v in range(4 * g, 4 * g + 4):
            halves += mesh['verts'][v] if v < count else [0, 0, 0]
        words += [((halves[i] & 0xFFFF) << 16) | (halves[i + 1] & 0xFFFF) for i in range(0, 12, 2)]
    return words


def run(meshes, timing, prog, labels):
    mem = Memory()
    data, out, rec = PACKED, OUT, JOBS
    for mesh in meshes:
        lit = any(mesh['xf'][a] for a in ('lx', 'ly', 'lz'))
        words = pack(mesh, lit)
        for i, w in enumerate(words):
            mem.write(data + 4 * i, w)
        groups = (mesh['count'] + 3) // 4
        mesh['out_rot'] = out
        out += groups * 32
        xf = mesh['xf']
        r = xf['rows']
        k = []
        for row, t in ((0, 'tx'), (1, 'ty'), (2, 'tz')):
            k += [4 * xf[t]] + [4 * r[row * 3 + c] for c in range(3)]
        light = [xf[a] * xf['range'] * -4 for a in ('lx', 'ly', 'lz')]
        k += [((xf['ambient'] << 16) | 0xFFFF) - 128 * sum(light)] + light
        tail = [data >> 2, groups] + ([labels['normals'], 0, 10] if lit else [labels['coords'], 4, 6])
        for i, w in enumerate(k + tail):
            mem.write(rec + 4 * i, w)
        data += 4 * len(words)
        rec += 21 * 4
    for i in range(21):
        mem.write(rec + 4 * i, 0)       # the list's end
    dsp = DSP(prog, mem, timing)
    for i, v in enumerate([65536, 0xFFFF, 0x1F00, 0xFF]):
        dsp.ram[1][24 + i] = v
    dsp.ram[2][10] = 1
    dsp.ram[3][20] = JOBS >> 2
    dsp.ram[3][21] = (OUT >> 2) - 8
    dsp.run(labels['start'])
    return dsp, mem, 'out_rot'


def main():
    meshes = bench.build()
    for mesh in meshes[-12:]:           # the planks
        for a in ('lx', 'ly', 'lz'):
            mesh['xf'][a] = 0
    nv = sum(m['count'] for m in meshes)
    lit = sum(m['count'] for m in meshes if any(m['xf'][a] for a in ('lx', 'ly', 'lz')))
    print(f'{len(meshes)} meshes, {nv} vertices, {lit} lit by their normals')
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, 'meshrot.dsp')
        subprocess.run([sys.executable, os.path.join(HERE, 'gen_meshrot.py'), src], check=True)
        prog, labels = load(src)
        words = len(prog) - next(i for i, w in enumerate(reversed(prog)) if w)
    print(f'meshrot.dsp: {words} instructions')
    for timing in ('mednafen', 'saturn'):
        for name in ('v3', 'packed'):
            ms = copy.deepcopy(meshes)
            if name == 'v3':
                dsp, mem, key = bench.run_v3(ms, timing)
            else:
                dsp, mem, key = run(ms, timing, prog, labels)
            bad = bench.check(ms, mem, key)
            sh2 = dsp.cycles * 2
            print(f'{name:6s} {timing:8s}: {sh2 / 28636:6.2f} ms, {sh2 / nv:6.1f} SH-2 cycles a vertex '
                  f'({dsp.executed} instructions, {dsp.stalls * 2 / 28636:.2f} ms waiting on DMA); '
                  f'{"exact" if not bad else f"{bad} vertices differ"}')


if __name__ == '__main__':
    main()
