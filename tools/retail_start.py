"""A copy of the retail disc with Lara starting somewhere else in a level, to
compare the original game with ours on the same view on hardware.

usage: retail_start.py ORIG.cue OUTDIR LEVEL room:x:z[:degrees]

Lara's ITEMDATA record in DATA/<LEVEL>.SAT moves to that sector (as
pack_level.py's LARA_START places her). The data track's sectors are raw
(MODE1/2352): the changed ones get their EDC and ECC computed again, which
the Saturn's CD block checks. The other tracks are copied as they are.
"""
import os
import shutil
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pack_level  # noqa: E402
import trsat  # noqa: E402

RAW = 2352
DATA = 16           # user data offset in a MODE1 sector
USER = 2048

# ECMA-130 Mode 1: EDC (CRC-32, polynomial 0xD8018001 reversed) over bytes
# 0..2063, stored at 2064; then P and Q Reed-Solomon parity over bytes 12..
_EDC = []
_ECC_F = [0] * 256
_ECC_B = [0] * 256
for i in range(256):
    j = ((i << 1) ^ (0x11D if i & 0x80 else 0)) & 0xFF
    _ECC_F[i] = j
    _ECC_B[i ^ j] = i
    e = i
    for _ in range(8):
        e = (e >> 1) ^ (0xD8018001 if e & 1 else 0)
    _EDC.append(e)


def _edc(data):
    e = 0
    for b in data:
        e = (e >> 8) ^ _EDC[(e ^ b) & 0xFF]
    return e


def _ecc(sector, major_count, minor_count, major_mult, minor_inc, dest):
    src = 12
    size = major_count * minor_count
    for major in range(major_count):
        index = (major >> 1) * major_mult + (major & 1)
        a = b = 0
        for _ in range(minor_count):
            t = sector[src + index]
            index += minor_inc
            if index >= size:
                index -= size
            a ^= t
            b ^= t
            a = _ECC_F[a]
        a = _ECC_B[_ECC_F[a] ^ b]
        sector[dest + major] = a
        sector[dest + major + major_count] = a ^ b


def seal(sector):
    """Fills in a Mode 1 sector's EDC, zero bytes and ECC in place."""
    sector[2064:2068] = struct.pack("<I", _edc(sector[0:2064]))
    sector[2068:2076] = bytes(8)
    _ecc(sector, 86, 24, 2, 86, 2076)
    _ecc(sector, 52, 43, 86, 88, 2248)


def user(track, lba):
    return track[lba * RAW + DATA:lba * RAW + DATA + USER]


def find(track, path):
    """LBA and size of a file on the ISO9660 data track."""
    pvd = user(track, 16)
    root = pvd[156:190]
    lba, size = struct.unpack_from(">I", root, 6)[0], struct.unpack_from(">I", root, 14)[0]
    for part in path.split("/"):
        found = None
        for s in range((size + USER - 1) // USER):
            d = user(track, lba + s)
            pos = 0
            while pos < USER and d[pos]:
                rec = d[pos:pos + d[pos]]
                name = rec[33:33 + rec[32]].decode("ascii", "replace").split(";")[0]
                if name == part:
                    found = (struct.unpack_from(">I", rec, 6)[0], struct.unpack_from(">I", rec, 14)[0])
                pos += d[pos]
        if not found:
            raise ValueError(f"{path}: {part} not found")
        lba, size = found
    return lba, size


def main():
    cue, out_dir, level, start = sys.argv[1:5]
    room, sx, sz, *deg = (int(v) for v in start.split(":"))

    src_dir = os.path.dirname(os.path.abspath(cue))
    lines = open(cue).read().splitlines()
    tracks = [l.split('"')[1] for l in lines if l.startswith("FILE")]
    track = bytearray(open(os.path.join(src_dir, tracks[0]), "rb").read())

    # the sealing must reproduce the original's own codes
    for lba in range(16, len(track) // RAW, 997):
        s = bytearray(track[lba * RAW:(lba + 1) * RAW])
        before = bytes(s)
        seal(s)
        if bytes(s) != before:
            raise ValueError(f"EDC/ECC differ from the original at sector {lba}")

    lba, size = find(track, f"DATA/{level}.SAT")
    data = bytearray(b"".join(user(track, lba + s) for s in range((size + USER - 1) // USER)))[:size]

    # where pack_level.py would put her
    data_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                            "external_resources", "disc", "files", "DATA")
    lvl = trsat.load(data_dir, level)
    lara = next(it for it in lvl.items if it["type"] == pack_level.ITEM["LARA"])
    original = (lara["room"], lara["x"], lara["y"], lara["z"], lara["rotation"])
    pack_level.move_lara(lvl, room, (sx, sz))

    # ITEMDATA: tag, 0x14, count, then 24-byte records (trsat.py)
    tag = data.find(b"ITEMDATA")
    if tag < 0 or struct.unpack_from(">I", data, tag + 8)[0] != 0x14:
        raise ValueError("ITEMDATA not found")
    count = struct.unpack_from(">I", data, tag + 12)[0]
    for i in range(count):
        rec = tag + 16 + i * 24
        if struct.unpack_from(">H", data, rec)[0] == pack_level.ITEM["LARA"]:
            if struct.unpack_from(">H3ih", data, rec + 2) != original:
                raise ValueError("Lara's record doesn't read as trsat.py reads it")
            struct.pack_into(">H3i", data, rec + 2, room, lara["x"], lara["y"], lara["z"])
            if deg:
                struct.pack_into(">h", data, rec + 16, pack_level.s16(round(deg[0] / 90) * 0x4000 & 0xFFFF))
            break
    else:
        raise ValueError("no Lara")

    changed = 0
    for s in range((size + USER - 1) // USER):
        chunk = data[s * USER:(s + 1) * USER]
        sector = bytearray(track[(lba + s) * RAW:(lba + s + 1) * RAW])
        if bytes(sector[DATA:DATA + len(chunk)]) != chunk:
            sector[DATA:DATA + len(chunk)] = chunk
            seal(sector)
            track[(lba + s) * RAW:(lba + s + 1) * RAW] = sector
            changed += 1

    os.makedirs(out_dir, exist_ok=True)
    open(os.path.join(out_dir, tracks[0]), "wb").write(track)
    for t in tracks[1:]:
        shutil.copyfile(os.path.join(src_dir, t), os.path.join(out_dir, t))
    shutil.copyfile(cue, os.path.join(out_dir, os.path.basename(cue)))
    print(f"{level}: Lara to room {room} sector {sx}:{sz} ({lara['x']}, {lara['y']}, {lara['z']}), "
          f"{changed} sector(s) changed -> {out_dir}")


if __name__ == "__main__":
    main()
