"""List the leading tagged chunks in Saturn TR1 level files (.SAT/.SAD/.SPR/.SND).

Each chunk starts with an 8-byte ASCII tag. Most are followed by two
big-endian u32s, element size and element count, then size*count payload
bytes. The rest (ROOMDATA, MESHPOS, ANIMS, ...) have per-tag layouts; this
walker stops at the first one of those. OpenLara's readSAT
(external_resources/OpenLara/src/format.h) documents the full grammar.

Usage: python tools/sat_chunks.py <file> [<file> ...]
"""
import struct
import sys

FILE_HEADERS = {b"ROOMFILE", b"OBJFILE ", b"SPRFILE ", b"SOUNDFX "}
FLAT = {b"ROOMTINF", b"ROOMTQTR", b"ROOMTSUB", b"ROOMTPAL", b"ROOMSPAL",
        b"SPRITINF", b"SPRITDAT", b"SAMPLUT "}


def main():
    for path in sys.argv[1:]:
        data = open(path, "rb").read()
        print(f"== {path} ({len(data)} bytes)")
        pos = 0
        while pos + 16 <= len(data):
            tag = data[pos:pos + 8]
            name = tag.decode("ascii", "replace")
            size, count = struct.unpack_from(">II", data, pos + 8)
            if tag in FILE_HEADERS:
                payload = 0
            elif tag in FLAT:
                payload = size * count
            else:
                print(f"  {pos:08x}  {name:8s}  (per-tag layout, not decoded yet)")
                break
            print(f"  {pos:08x}  {name:8s}  size={size:<8d} count={count:<8d} payload={payload}")
            pos += 16 + payload


if __name__ == "__main__":
    main()
