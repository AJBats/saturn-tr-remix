"""Build a Saturn disc image: an ISO9660 data track plus the original disc's
audio tracks.

usage: mkdisc.py --ip IP.BIN --cue ORIG.cue --out DIR --name NAME
                 DISCPATH=SRCPATH [DISCPATH=SRCPATH ...]

The data track is written as 2048-byte sectors (MODE1/2048 in the cue), which
Mednafen reads directly. IP.BIN fills the 16-sector system area; the BIOS
boots the first file in the root directory, so name the program 0MAIN.BIN.
Audio tracks are hard-linked next to the cue because Mednafen on Windows
does not follow '..' in cue FILE paths.
"""
import argparse
import os
import re
import struct
import sys

SECTOR = 2048
SYSTEM_AREA = 16
MIN_SECTORS = 300           # a track must be at least 4 seconds long
DATE = (126, 10, 24, 0, 0, 0, 0)   # 2026-10-24, fixed for reproducible images
DATE_STR = b"2026102400000000\x00"


def both32(v):
    return struct.pack("<I", v) + struct.pack(">I", v)


def both16(v):
    return struct.pack("<H", v) + struct.pack(">H", v)


def padded(s, n):
    s = s.encode("ascii")
    assert len(s) <= n
    return s + b" " * (n - len(s))


def iso_key(name):
    base, _, ext = name.partition(".")
    return (base, ext)


def check_name(name, is_dir):
    pat = r"[A-Z0-9_]{1,8}" if is_dir else r"[A-Z0-9_]{1,8}\.[A-Z0-9_]{0,3}"
    if not re.fullmatch(pat, name):
        sys.exit(f"error: '{name}' is not an ISO9660 level 1 name")


class Dir:
    def __init__(self, name, parent):
        self.name = name
        self.parent = parent
        self.dirs = {}
        self.files = {}     # name -> source path
        self.lba = 0
        self.size = 0
        self.number = 0     # 1-based path table index


def dir_record(ident, lba, size, is_dir):
    ln = 33 + len(ident) + (1 - len(ident) % 2)
    rec = bytearray(ln)
    rec[0] = ln
    rec[2:10] = both32(lba)
    rec[10:18] = both32(size)
    rec[18:25] = bytes(DATE)
    rec[25] = 2 if is_dir else 0
    rec[28:32] = both16(1)
    rec[32] = len(ident)
    rec[33:33 + len(ident)] = ident
    return bytes(rec)


def dir_entries(d):
    """(ident, target, is_dir) for every record of directory d, in ISO order."""
    out = [(b"\x00", d, True), (b"\x01", d.parent or d, True)]
    names = [(n, True) for n in d.dirs] + [(n, False) for n in d.files]
    for name, is_dir in sorted(names, key=lambda e: iso_key(e[0])):
        if is_dir:
            out.append((name.encode(), d.dirs[name], True))
        else:
            out.append(((name + ";1").encode(), name, False))
    return out


def pack_records(records):
    """Lay records into sectors without letting any straddle a boundary."""
    out = bytearray()
    for rec in records:
        if len(out) // SECTOR != (len(out) + len(rec) - 1) // SECTOR:
            out += bytes(SECTOR - len(out) % SECTOR)
        out += rec
    return out


def sectors(n):
    return (n + SECTOR - 1) // SECTOR


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ip", required=True)
    ap.add_argument("--cue", required=True, help="original cue sheet (audio tracks)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--name", required=True)
    ap.add_argument("--volume", default="TOMBRAIDER")
    ap.add_argument("entries", nargs="+")
    args = ap.parse_args()

    root = Dir("", None)
    for entry in args.entries:
        disc_path, _, src = entry.partition("=")
        parts = disc_path.split("/")
        d = root
        for p in parts[:-1]:
            check_name(p, True)
            d = d.dirs.setdefault(p, Dir(p, d))
        check_name(parts[-1], False)
        d.files[parts[-1]] = src

    # Path table order: breadth first, children sorted.
    order = [root]
    for d in order:
        order.extend(d.dirs[n] for n in sorted(d.dirs, key=iso_key))
    for i, d in enumerate(order, 1):
        d.number = i

    path_table_size = sum(8 + max(1, len(d.name)) + (max(1, len(d.name)) & 1) for d in order)
    assert path_table_size <= SECTOR
    l_table_lba = SYSTEM_AREA + 2
    m_table_lba = l_table_lba + 1
    lba = m_table_lba + 1

    # Directory sizes depend only on names, so size them before placing files.
    for d in order:
        d.size = len(pack_records(dir_record(i, 0, 0, dr) for i, _, dr in dir_entries(d)))
        d.size = sectors(d.size) * SECTOR
        d.lba = lba
        lba += d.size // SECTOR

    file_lba = {}
    for d in order:
        for name in sorted(d.files, key=iso_key):
            size = os.path.getsize(d.files[name])
            file_lba[(d, name)] = (lba, size)
            lba += max(1, sectors(size))
    total = max(lba, MIN_SECTORS)

    image = bytearray(total * SECTOR)
    ip = open(args.ip, "rb").read()
    if len(ip) > SYSTEM_AREA * SECTOR:
        sys.exit("error: IP.BIN is larger than the system area")
    image[0:len(ip)] = ip

    pvd = bytearray(SECTOR)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    pvd[8:40] = padded("SEGA SEGASATURN", 32)
    pvd[40:72] = padded(args.volume, 32)
    pvd[80:88] = both32(total)
    pvd[120:124] = both16(1)
    pvd[124:128] = both16(1)
    pvd[128:132] = both16(SECTOR)
    pvd[132:140] = both32(path_table_size)
    pvd[140:144] = struct.pack("<I", l_table_lba)
    pvd[148:152] = struct.pack(">I", m_table_lba)
    pvd[156:190] = dir_record(b"\x00", root.lba, root.size, True)
    pvd[190:318] = padded(args.volume, 128)
    pvd[318:446] = padded("TOMBRAIDERSATURN", 128)
    pvd[446:574] = padded("TOMBRAIDERSATURN", 128)
    pvd[574:702] = padded("", 128)
    pvd[702:739] = padded("CPY.TXT", 37)
    pvd[739:776] = padded("ABS.TXT", 37)
    pvd[776:813] = padded("BIB.TXT", 37)
    pvd[813:830] = DATE_STR
    pvd[830:847] = DATE_STR
    pvd[847:864] = b"0" * 16 + b"\x00"
    pvd[864:881] = DATE_STR
    pvd[881] = 1
    image[16 * SECTOR:17 * SECTOR] = pvd

    term = bytearray(SECTOR)
    term[0] = 255
    term[1:6] = b"CD001"
    term[6] = 1
    image[17 * SECTOR:18 * SECTOR] = term

    for table_lba, fmt in ((l_table_lba, "<"), (m_table_lba, ">")):
        table = bytearray()
        for d in order:
            ident = d.name.encode() or b"\x00"
            parent = d.parent.number if d.parent else 1
            table += bytes([len(ident), 0]) + struct.pack(fmt + "IH", d.lba, parent) + ident
            if len(ident) & 1:
                table += b"\x00"
        image[table_lba * SECTOR:table_lba * SECTOR + len(table)] = table

    for d in order:
        recs = []
        for ident, target, is_dir in dir_entries(d):
            if is_dir:
                recs.append(dir_record(ident, target.lba, target.size, True))
            else:
                flba, fsize = file_lba[(d, target)]
                recs.append(dir_record(ident, flba, fsize, False))
        data = pack_records(recs)
        image[d.lba * SECTOR:d.lba * SECTOR + len(data)] = data

    for (d, name), (flba, fsize) in file_lba.items():
        data = open(d.files[name], "rb").read()
        image[flba * SECTOR:flba * SECTOR + fsize] = data

    os.makedirs(args.out, exist_ok=True)
    track1 = f"{args.name}_track01.iso"
    with open(os.path.join(args.out, track1), "wb") as f:
        f.write(image)

    cue_dir = os.path.dirname(os.path.abspath(args.cue))
    blocks = re.split(r'(?m)^(?=FILE )', open(args.cue).read())
    blocks = [b for b in blocks if b.startswith("FILE ")]
    out = [f'FILE "{track1}" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n']
    for i, block in enumerate(blocks[1:], 2):
        src = re.match(r'FILE "([^"]+)"', block).group(1)
        link = f"{args.name}_track{i:02d}.bin"
        dst = os.path.join(args.out, link)
        if not os.path.exists(dst):
            os.link(os.path.join(cue_dir, src), dst)
        out.append(block.replace(f'FILE "{src}"', f'FILE "{link}"', 1))
    with open(os.path.join(args.out, f"{args.name}.cue"), "w", newline="\n") as f:
        f.write("".join(out))

    print(f"{args.name}.cue: data track {total} sectors, {len(file_lba)} files, "
          f"{len(blocks) - 1} audio tracks")


if __name__ == "__main__":
    main()
