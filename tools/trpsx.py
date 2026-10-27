"""Read the PlayStation Tomb Raider's level files (PSXDATA/*.PSX) far enough
to compare them with the Saturn's: textures, rooms and their faces, meshes,
object textures, sound samples and the sound map and infos. The layout
follows OpenLara's loadTR1_PSX (external_resources/OpenLara/src/format.h).

Usage: python tools/trpsx.py <PSXDATA dir> <LEVEL>   (prints a summary)
"""
import os
import struct
import sys

MAGIC = 0x56414270
TILE_SIZE = 256 * 256 // 2  # 4-bit indices
TILE_COUNT = 13
CLUT_COUNT = 1024


class Reader:
    def __init__(self, data, pos=0):
        self.data = data
        self.pos = pos

    def unpack(self, fmt):
        v = struct.unpack_from("<" + fmt, self.data, self.pos)
        self.pos += struct.calcsize("<" + fmt)
        return v

    def u8(self): return self.unpack("B")[0]
    def u16(self): return self.unpack("H")[0]
    def s16(self): return self.unpack("h")[0]
    def u32(self): return self.unpack("I")[0]
    def s32(self): return self.unpack("i")[0]

    def skip(self, count, size):
        self.pos += count * size

    def raw(self, n):
        v = self.data[self.pos:self.pos + n]
        self.pos += n
        return v


class Room:
    def __init__(self, index):
        self.index = index
        self.vertices = []   # (x, y, z, luminance 0..255: 255 bright)
        self.quads = []      # (v0, v1, v2, v3, texture) in the PC order
        self.triangles = []  # (v0, v1, v2, texture)
        self.sprites = []
        self.lights = []
        self.static_meshes = []


class Level:
    pass


def read_room(r, index):
    room = Room(index)
    room.x, room.z, room.y_bottom, room.y_top = r.s32(), r.s32(), r.s32(), r.s32()
    size = r.u32()
    start = r.pos
    r.u16()
    for _ in range(r.u16()):
        room.vertices.append(r.unpack("hhhH"))
    for _ in range(r.u16()):
        v = r.unpack("4HH")
        # quads are strips on the PlayStation: swap to the PC's order
        room.quads.append((v[0], v[1], v[3], v[2], v[4]))
    for _ in range(r.u16()):
        room.triangles.append(r.unpack("4H"))
    for _ in range(r.u16()):
        room.sprites.append(r.unpack("2H"))
    r.pos = start + size * 2

    r.skip(r.u16(), 32)  # portals
    zs, xs = r.u16(), r.u16()
    r.skip(zs * xs, 8)  # sectors
    room.ambient = r.u16()
    for _ in range(r.u16()):
        x, y, z, intensity, _pad, radius = r.unpack("iiiHHi")
        room.lights.append((x, y, z, intensity, radius))
    for _ in range(r.u16()):
        x, y, z, rotation, intensity, mesh, _pad = r.unpack("iiiHHHH")
        room.static_meshes.append(dict(x=x, y=y, z=z, rotation=rotation, intensity=intensity, mesh=mesh))
    room.alternate = r.s16()
    room.flags = r.u16()
    return room


def load(data_dir, name):
    data = open(os.path.join(data_dir, name + ".PSX"), "rb").read()
    lvl = Level()
    lvl.name = name
    r = Reader(data)

    lvl.samples = []
    if struct.unpack_from("<I", data, 4)[0] == MAGIC:
        r.pos = 16
        tex_offset = r.u32()
        count = struct.unpack_from("<H", data, 22)[0]
        r.pos = 2086 + count * 512
        sizes = [r.u16() * 8 for _ in range(count)]
        pos = 2600 + count * 512
        for size in sizes:
            lvl.samples.append(data[pos:pos + size])  # PlayStation ADPCM
            pos += size
        r.pos = tex_offset + 8
    else:
        r.pos = 8  # cutscenes: no sounds

    lvl.tiles = [r.raw(TILE_SIZE) for _ in range(TILE_COUNT)]
    lvl.cluts = [r.unpack("16H") for _ in range(CLUT_COUNT)]

    r.u32()
    lvl.rooms = [read_room(r, i) for i in range(r.u16())]
    r.skip(r.u32(), 2)  # floor data
    lvl.mesh_data = r.raw(r.u32() * 2)
    lvl.mesh_offsets = list(r.unpack(f"{r.u32()}I"))
    r.skip(r.u32(), 32)  # animations
    r.skip(r.u32(), 6)  # state changes
    r.skip(r.u32(), 8)  # anim ranges
    r.skip(r.u32(), 2)  # anim commands
    r.skip(r.u32(), 4)  # mesh tree
    r.skip(r.u32(), 2)  # frames
    r.skip(r.u32(), 20)  # models
    r.skip(r.u32(), 32)  # static meshes

    lvl.object_textures = []
    for _ in range(r.u32()):
        x0, y0, clut, x1, y1, tile, x2, y2, _u, x3, y3, attribute = r.unpack("BBHBBHBBHBBH")
        lvl.object_textures.append(dict(clut=clut, tile=tile & 0x3FFF, attribute=attribute,
                                        uv=[(x0, y0), (x1, y1), (x2, y2), (x3, y3)]))

    r.skip(r.u32(), 16)  # sprite textures
    r.skip(r.u32(), 8)  # sprite sequences
    r.skip(r.u32(), 16)  # cameras
    r.skip(r.u32(), 16)  # sound sources
    boxes = r.u32()
    r.skip(boxes, 20)
    r.skip(r.u32(), 2)  # overlaps
    r.skip(6 * boxes, 2)  # zones
    r.skip(r.u32(), 2)  # animated textures
    r.skip(r.u32(), 22)  # items
    lvl.sound_map = list(r.unpack("256h"))
    lvl.sound_infos = [r.unpack("4H") for _ in range(r.u32())]
    return lvl


def texel(lvl, tile, x, y):
    b = lvl.tiles[tile][(y * 256 + x) // 2]
    return (b >> 4) if (x & 1) else (b & 15)


def texture_rect(t):
    xs = [u for u, v in t["uv"]]
    ys = [v for u, v in t["uv"]]
    return min(xs), min(ys), max(xs) + 1, max(ys) + 1


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    lvl = load(sys.argv[1], sys.argv[2])
    faces = sum(len(r.quads) + len(r.triangles) for r in lvl.rooms)
    print(f"{lvl.name}: {len(lvl.rooms)} rooms, {faces} room faces, {len(lvl.object_textures)} object textures, "
          f"{len(set(t['clut'] for t in lvl.object_textures))} cluts used, {len(lvl.samples)} samples "
          f"({sum(map(len, lvl.samples))} bytes ADPCM)")


if __name__ == "__main__":
    main()
