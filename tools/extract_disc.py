"""Extract the ISO9660 filesystem and IP.BIN from a raw MODE1/2352 Saturn data track.

Usage: python tools/extract_disc.py <track01.bin> <out_dir>
"""
import os
import struct
import sys

RAW_SECTOR = 2352
USER_OFFSET = 16
USER_SIZE = 2048


class Track:
    def __init__(self, path):
        self.f = open(path, "rb")

    def read(self, lba, count=1):
        out = bytearray()
        for i in range(count):
            self.f.seek((lba + i) * RAW_SECTOR + USER_OFFSET)
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

    pvd = track.read(16)
    assert pvd[1:6] == b"CD001", "no ISO9660 primary volume descriptor at LBA 16"
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
