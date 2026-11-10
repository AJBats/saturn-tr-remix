"""Extract the ISO9660 filesystem and IP.BIN from a disc's raw 2352-byte data
track: the Saturn's, in MODE1 sectors, or the PlayStation's, in MODE2 form 1.

Usage: python tools/extract_disc.py <track01.bin> <out_dir>
"""
import os
import struct
import sys

RAW_SECTOR = 2352
USER_SIZE = 2048
PVD_LBA = 16
SYNC = b"\x00" + b"\xff" * 10 + b"\x00"
# Where a sector's user data starts, by its mode (its byte 15): mode 2 has a
# subheader of 8 bytes before it.
USER_OFFSETS = {1: 16, 2: 24}


class Track:
    def __init__(self, path):
        if not os.path.exists(path):
            sys.exit(f"{path}: no such file (README.md, The discs)")
        self.f = open(path, "rb")
        self.f.seek(PVD_LBA * RAW_SECTOR)
        header = self.f.read(16)
        if header[:12] != SYNC or header[15:16] not in (b"\x01", b"\x02"):
            sys.exit(f"{path}: not a raw 2352-byte data track (README.md, The discs)")
        self.user_offset = USER_OFFSETS[header[15]]

    def read(self, lba, count=1):
        out = bytearray()
        for i in range(count):
            self.f.seek((lba + i) * RAW_SECTOR + self.user_offset)
            out += self.f.read(USER_SIZE)
        return bytes(out)

    def read_extent(self, lba, size):
        sectors = (size + USER_SIZE - 1) // USER_SIZE
        return self.read(lba, sectors)[:size]


def parse_dir(track, lba, size):
    data = track.read_extent(lba, size)
    pos = 0
    while pos < len(data):
        rec_len = data[pos]
        if rec_len == 0:
            # Records never straddle sectors; skip to the next one.
            pos = (pos // USER_SIZE + 1) * USER_SIZE
            continue
        rec = data[pos:pos + rec_len]
        extent = struct.unpack_from("<I", rec, 2)[0]
        length = struct.unpack_from("<I", rec, 10)[0]
        flags = rec[25]
        name_len = rec[32]
        name = rec[33:33 + name_len]
        pos += rec_len
        if name in (b"\x00", b"\x01"):
            continue
        yield name.decode("ascii").split(";")[0], extent, length, bool(flags & 2)


def walk(track, lba, size, out_dir, listing, rel=""):
    os.makedirs(out_dir, exist_ok=True)
    for name, extent, length, is_dir in parse_dir(track, lba, size):
        path = os.path.join(out_dir, name)
        rel_path = f"{rel}/{name}" if rel else name
        if is_dir:
            walk(track, extent, length, path, listing, rel_path)
        else:
            with open(path, "wb") as f:
                f.write(track.read_extent(extent, length))
            listing.append((rel_path, extent, length))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    track = Track(sys.argv[1])
    out_dir = sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)

    with open(os.path.join(out_dir, "IP.BIN"), "wb") as f:
        f.write(track.read(0, 16))

    pvd = track.read(PVD_LBA)
    assert pvd[1:6] == b"CD001", f"no ISO9660 primary volume descriptor at LBA {PVD_LBA}"
    root = pvd[156:156 + 34]
    root_lba = struct.unpack_from("<I", root, 2)[0]
    root_size = struct.unpack_from("<I", root, 10)[0]

    listing = []
    walk(track, root_lba, root_size, os.path.join(out_dir, "files"), listing)

    with open(os.path.join(out_dir, "filelist.txt"), "w") as f:
        for rel_path, extent, length in sorted(listing, key=lambda e: e[1]):
            f.write(f"{extent:8d} {length:10d}  {rel_path}\n")
    print(f"extracted {len(listing)} files to {out_dir}")


if __name__ == "__main__":
    main()
