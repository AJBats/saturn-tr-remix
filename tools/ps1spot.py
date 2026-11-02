"""A copy of the retail PS1 disc that boots into a level at a spot, to compare views with ours.

    ps1spot.py CUE OUT ROOM@X:Y:Z YAW [LEVEL NUMBER]

Copies the data track to OUT and hard-links the audio tracks. Two things change, in the copy:
Lara's item in the level file (her room, place and heading), and the game's main loop, which
starts the level instead of the title. Default: the Cistern (LEVEL7A, level 8).

The sectors touched get their EDC and ECC again, as a drive's read checks them.
"""
import argparse
import os
import re
import shutil
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trsat

RAW = 2352
USER = 24
SIZE = 2048
SECTOR_EDC = 0x818
TITLE_CALL = 0x80028B18     # the main loop's first: jal the title, its answer the level to start

ECC_F = [0] * 256
ECC_B = [0] * 256
EDC = [0] * 256
for i in range(256):
    j = ((i << 1) ^ (0x11D if i & 0x80 else 0)) & 0xFF
    ECC_F[i] = j
    ECC_B[i ^ j] = i
    e = i
    for _ in range(8):
        e = (e >> 1) ^ (0xD8018001 if e & 1 else 0)
    EDC[i] = e


def ecc_block(sector, major_count, minor_count, major_mult, minor_inc, dest):
    size = major_count * minor_count
    for major in range(major_count):
        index = (major >> 1) * major_mult + (major & 1)
        a = b = 0
        for _ in range(minor_count):
            t = sector[12 + index]
            index += minor_inc
            if index >= size:
                index -= size
            a ^= t
            b ^= t
            a = ECC_F[a]
        a = ECC_B[ECC_F[a] ^ b]
        sector[dest + major] = a
        sector[dest + major + major_count] = a ^ b


def seal(sector):
    """Mode 2 form 1: EDC over the subheader and data; ECC with the address taken as zero."""
    crc = 0
    for b in sector[16:SECTOR_EDC]:
        crc = EDC[(crc ^ b) & 0xFF] ^ (crc >> 8)
    struct.pack_into('<I', sector, SECTOR_EDC, crc)
    address = bytes(sector[12:16])
    sector[12:16] = b'\0\0\0\0'
    ecc_block(sector, 86, 24, 2, 86, 0x81C)
    ecc_block(sector, 52, 43, 86, 88, 0x8C8)
    sector[12:16] = address


def read(f, lba, size):
    out = bytearray()
    for k in range((size + SIZE - 1) // SIZE):
        f.seek((lba + k) * RAW + USER)
        out += f.read(SIZE)
    return out[:size]


def find(f, path):
    pvd = read(f, 16, SIZE)
    lba, size = struct.unpack_from('<I', pvd, 158)[0], struct.unpack_from('<I', pvd, 166)[0]
    for part in path.split('/'):
        data = read(f, lba, size)
        pos = 0
        while pos < len(data):
            n = data[pos]
            if not n:
                pos = (pos // SIZE + 1) * SIZE
                continue
            rec = data[pos:pos + n]
            pos += n
            if bytes(rec[33:33 + rec[32]]).decode('latin1').split(';')[0] == part:
                lba, size = struct.unpack_from('<I', rec, 2)[0], struct.unpack_from('<I', rec, 10)[0]
                break
        else:
            raise SystemExit(f'{path}: no {part}')
    return lba, size


def write(f, lba, off, data):
    """data over a file's bytes at off, each sector touched sealed again."""
    while data:
        k = off // SIZE
        at = off % SIZE
        n = min(len(data), SIZE - at)
        f.seek((lba + k) * RAW)
        sector = bytearray(f.read(RAW))
        sector[USER + at:USER + at + n] = data[:n]
        seal(sector)
        f.seek((lba + k) * RAW)
        f.write(sector)
        data = data[n:]
        off += n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cue')
    ap.add_argument('out')
    ap.add_argument('spot')
    ap.add_argument('yaw')
    ap.add_argument('level', nargs='?', default='LEVEL7A')
    ap.add_argument('number', nargs='?', type=int, default=8)
    a = ap.parse_args()
    room, x, y, z = map(int, re.fullmatch(r'(\d+)@(-?\d+):(-?\d+):(-?\d+)', a.spot).groups())
    yaw = int(a.yaw, 0)
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    sat = trsat.Level()
    sat.name = a.level
    trsat.read_sat(sat, open(os.path.join(root, 'external_resources/disc/files/DATA', a.level + '.SAT'), 'rb').read())
    lara = next(i for i in sat.items if i['type'] == 0)

    src = os.path.dirname(a.cue)
    os.makedirs(a.out, exist_ok=True)
    cue = open(a.cue).read()
    for k, t in enumerate(re.findall(r'FILE "([^"]+)"', cue)):
        new = f'tomb_track{k + 1:02d}.bin'
        cue = cue.replace(t, new)
        path = os.path.join(a.out, new)
        if os.path.exists(path):
            continue
        if k:
            os.link(os.path.join(src, t), path)
        else:
            shutil.copy(os.path.join(src, t), path)
    open(os.path.join(a.out, 'tomb.cue'), 'w').write(cue)

    with open(os.path.join(a.out, 'tomb_track01.bin'), 'r+b') as f:
        lba, size = find(f, f'PSXDATA/{a.level}.PSX')
        level = read(f, lba, size)
        record = struct.pack('<hhiii', 0, lara['room'], lara['x'], lara['y'], lara['z'])
        at = [m.start() for m in re.finditer(re.escape(record), level)]
        if len(at) != 1:
            raise SystemExit(f'Lara at the level start found {len(at)} times: make a new OUT')
        write(f, lba, at[0], struct.pack('<hhiiih', 0, room, x, y, z, yaw - 65536 if yaw >= 32768 else yaw))

        exe_lba, _ = find(f, 'SLUS_001.52')
        text = struct.unpack_from('<I', read(f, exe_lba, 0x800), 0x18)[0]
        off = TITLE_CALL - text + 0x800
        old = read(f, exe_lba, off + 4)[off:off + 4]
        if old != bytes.fromhex('bb63010c'):
            raise SystemExit(f'not the disc this was read from: {old.hex()} at the title call')
        write(f, exe_lba, off, struct.pack('<I', 0x24020000 | a.number))     # li v0,number
    print(f'{a.out}/tomb.cue: {a.level}, Lara room {room} at {x} {y} {z} heading {yaw:#06x}')


if __name__ == '__main__':
    main()
