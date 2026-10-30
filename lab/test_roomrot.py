"""Runs roomrot.dsp (lab/gen_roomrot.py) in tools/dspsim.py over a lab
scene's rooms, checks every vertex's three sums against xform.s's arithmetic
(vertices.cpp vertexModel's), and times it as Mednafen and as the Saturn
would.

usage: test_roomrot.py SCENE.SCN
"""
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
from dspsim import DSP, Memory, load  # noqa: E402

M32 = 0xFFFFFFFF
ROOM = struct.Struct('>9hh3i4i3iiBBBBi4H6H3I')  # lab/scene.h RoomScene
GEOMETRY = 0x06040000   # the rooms' vertices
JOBS = 0x06070000
OUT = 0x06080000
DONE = 0x0606FFF0    # the rooms done


def chunks(path):
    data = open(path, 'rb').read()
    count = struct.unpack('>I', data[4:8])[0]
    out = {}
    for i in range(count):
        cid, offset, size, _ = struct.unpack('>4sIII', data[8 + 16 * i:24 + 16 * i])
        out[cid.decode()] = data[offset:offset + size]
    return out


def main():
    scene = chunks(sys.argv[1])
    rooms = [ROOM.unpack_from(scene['ROOM'], ROOM.size * i) for i in range(len(scene['ROOM']) // ROOM.size)]
    geometry = scene['RGEO']
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, 'roomrot.dsp')
        subprocess.run([sys.executable, os.path.join(HERE, 'gen_roomrot.py'), src], check=True)
        prog, labels = load(src)

    for timing in ('mednafen', 'saturn'):
        mem = Memory()
        for i in range(0, len(geometry), 4):
            mem.write(GEOMETRY + i, struct.unpack_from('>I', geometry + bytes(4), i)[0])
        want = {}
        out = OUT
        jobs = []
        total = 0
        for r in rooms:
            m, t = r[0:9], r[10:13]
            count, vertices = r[28], r[38]
            groups = (count + 3) // 4
            coefficients = []
            for row in range(3):
                coefficients += [t[row] & M32] + [(4 * m[3 * row + k]) & M32 for k in range(3)]
            jobs.append([(GEOMETRY + vertices) >> 2, out >> 2, groups] + coefficients)
            for k in range(count):
                x, y, z, g = geometry[vertices + 4 * k:vertices + 4 * k + 4]
                for row in range(3):
                    s = t[row] + 4 * (m[3 * row] * x + m[3 * row + 1] * y + m[3 * row + 2] * z)
                    want[out + 12 * k + 4 * row] = s & M32
            out += 12 * 4 * groups
            total += count
        jobs.append([0, 0, 0] + [0] * 12)
        for j, job in enumerate(jobs):
            for k, v in enumerate(job):
                mem.write(JOBS + 60 * j + 4 * k, v)
        dsp = DSP(prog, mem, timing)
        dsp.ram[0][16] = JOBS >> 2
        dsp.ram[0][17] = DONE >> 2
        dsp.ram[0][18] = 0
        dsp.ram[3][12] = 0xFF
        dsp.run(0)
        bad = sum(1 for addr, v in want.items() if mem.read(addr) != v)
        bad += mem.read(DONE) != len(rooms)
        sh2 = dsp.cycles * 2
        print(f'{timing}: {total} vertices in {len(rooms)} rooms, {bad} sums wrong; {sh2:.0f} SH-2 cycles '
              f'({sh2 / total:.1f} a vertex, {sh2 / 28636:.2f} ms), waiting on DMA {dsp.stalls * 2:.0f}; '
              f'{len(prog) - prog[::-1].index(next(w for w in prog[::-1] if w))} words')


if __name__ == '__main__':
    main()
