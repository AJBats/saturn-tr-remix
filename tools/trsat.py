"""Reader for Tomb Raider (Saturn) level files: .SAT (rooms), .SAD (objects,
animations, meshes), .SPR (sprites) and .SND (sounds).

Each file is a sequence of chunks tagged with 8 ASCII characters. The size
and count words after a tag are not reliable record sizes, so every chunk is
parsed explicitly; the chunk grammar follows OpenLara's readSAT
(external_resources/OpenLara/src/format.h). A chunk that does not end exactly
where the next tag starts raises an error.

usage: python tools/trsat.py <DATA dir> <LEVEL>   (prints a summary)
"""
import os
import struct
import sys


class Reader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def eof(self):
        return self.pos >= len(self.data)

    def tag(self):
        t = self.data[self.pos:self.pos + 8]
        self.pos += 8
        return t.decode("ascii", "replace")

    def u8(self):
        v = self.data[self.pos]
        self.pos += 1
        return v

    def s8(self):
        v = struct.unpack_from(">b", self.data, self.pos)[0]
        self.pos += 1
        return v

    def u16(self):
        v = struct.unpack_from(">H", self.data, self.pos)[0]
        self.pos += 2
        return v

    def s16(self):
        v = struct.unpack_from(">h", self.data, self.pos)[0]
        self.pos += 2
        return v

    def u32(self):
        v = struct.unpack_from(">I", self.data, self.pos)[0]
        self.pos += 4
        return v

    def s32(self):
        v = struct.unpack_from(">i", self.data, self.pos)[0]
        self.pos += 4
        return v

    def raw(self, n):
        v = self.data[self.pos:self.pos + n]
        self.pos += n
        return v

    def expect(self, value, what):
        v = self.u32()
        if v != value:
            raise ValueError(f"{what}: expected 0x{value:X}, got 0x{v:X} at 0x{self.pos - 4:X}")


class Room:
    def __init__(self, index):
        self.index = index
        self.x = self.z = self.y_bottom = self.y_top = 0
        self.vertices = []      # (x, y, z, color16)
        self.quads = []         # (v0, v1, v2, v3, texture, transparent)
        self.triangles = []     # (v0, v1, v2, texture)
        self.sprites = []       # (vertex, texture)
        self.static_meshes = [] # dict(x, y, z, rotation, intensity, unknown, mesh_id)
        self.portals = []       # (room, normal(xyz), [4 x (xyz)])
        self.z_sectors = self.x_sectors = 0
        self.sectors = []       # (floor_index, box_index, room_below, floor, room_above, ceiling)
        self.ambient = self.ambient2 = 0
        self.lights = []        # dict(x, y, z, intensity, radius)
        self.alternate = -1
        self.flags = 0


class Level:
    def __init__(self):
        self.rooms = []
        self.room_textures = []     # dict per ROOMTINF record
        self.room_tex_qtr = b""
        self.room_tex_sub = b""
        self.room_pal24 = b""
        self.room_pal16 = b""
        self.floors = []
        self.cameras = []
        self.sound_sources = []
        self.boxes = []
        self.overlaps = []
        self.zones = {}             # (tag, flipped) -> list
        self.anim_ranges = []       # (first, last)
        self.items = []
        self.anims = []
        self.states = []
        self.ranges = []
        self.commands = []
        self.nodes = []             # uint32 stream
        self.models = []
        self.static_meshes = []
        self.frames = []            # uint16 stream (Saturn angle word order)
        self.mesh_offsets = []
        self.mesh_data = b""
        self.object_textures = []
        self.object_tex_data = b""
        self.item_textures = []
        self.item_tex_data = b""
        self.sprite_textures = []
        self.sprite_tex_data = b""
        self.sprite_sequences = []
        self.sound_map = []
        self.sound_infos = []
        self.samples = {}           # index -> bytes


def read_tex_info(r, item):
    t = {}
    if item:
        t["index"] = r.u16()
    t["tile"] = r.u16()                    # offset/8 of 4-bit pixels
    t["sub"] = [r.u16() for _ in range(4)] # room: quadrants in TSUB; object: flip variants
    t["clut"] = r.u16()                    # offset/8 of the 16-colour table, relative to tile
    t["w"] = r.u8() * 8
    t["h"] = r.u8()
    t["i5"] = r.u16()
    if item:
        if r.u16() != t["i5"]:
            raise ValueError("ITEXTINF trailing word mismatch")
    return t


def parse_room_mesh(r, room):
    r.u32()                                 # data size
    vcount = r.u16()
    for _ in range(vcount):
        room.vertices.append((r.s16(), r.s16(), r.s16(), r.u16()))
    r.u16()                                 # face count (includes invisible faces)
    for _ in range(r.u16()):
        ftype = r.u16()
        count = r.u16()
        for _ in range(count):
            if ftype == 39:                 # sprite
                room.sprites.append((r.u16() >> 4, r.u16() >> 4))
            elif ftype in (34, 36):         # triangle: invisible, solid
                v = [r.u16() >> 4 for _ in range(3)]
                flags = r.u16()
                if ftype == 36:
                    room.triangles.append((*v, flags >> 4))
            elif ftype in (33, 35, 37):     # quad: transparent, invisible, solid
                v = [r.u16() >> 4 for _ in range(4)]
                flags = r.u16()
                if ftype != 35:
                    room.quads.append((*v, flags >> 4, ftype == 33))
            else:
                raise ValueError(f"room {room.index}: unknown face type {ftype}")


def read_sat(lvl, data):
    r = Reader(data)
    room = None
    zone_seen = set()
    while not r.eof():
        tag = r.tag()
        if tag == "ROOMFILE":
            r.expect(0, tag); r.expect(0x20, tag)
        elif tag == "ROOMTINF":
            r.expect(0x10, tag)
            lvl.room_textures = [read_tex_info(r, False) for _ in range(r.u32())]
        elif tag == "ROOMTQTR":
            r.expect(1, tag); lvl.room_tex_qtr = r.raw(r.u32())
        elif tag == "ROOMTSUB":
            r.expect(1, tag); lvl.room_tex_sub = r.raw(r.u32())
        elif tag == "ROOMTPAL":
            r.expect(3, tag); lvl.room_pal24 = r.raw(r.u32() * 3)
        elif tag == "ROOMSPAL":
            r.expect(2, tag); lvl.room_pal16 = r.raw(r.u32() * 2)
        elif tag == "ROOMDATA":
            r.expect(0x44, tag)
            lvl.rooms = [Room(i) for i in range(r.u32())]
        elif tag == "ROOMNUMB":
            r.expect(0, tag); room = lvl.rooms[r.u32()]
        elif tag == "MESHPOS ":
            room.x, room.z, room.y_bottom, room.y_top = r.s32(), r.s32(), r.s32(), r.s32()
        elif tag == "MESHSIZE":
            flag = r.u32()
            if flag == 0x14:
                for _ in range(r.u32()):
                    room.static_meshes.append(dict(x=r.s32(), y=r.s32(), z=r.s32(), rotation=r.u16(),
                                                   intensity=r.u16(), unknown=r.u16(), mesh_id=r.u16()))
            elif flag == 0x02:
                parse_room_mesh(r, room)
            else:
                raise ValueError(f"MESHSIZE flag 0x{flag:X}")
        elif tag == "DOORDATA":
            target = lvl.rooms[r.u32()]
            for _ in range(r.u32()):
                p_room = r.u16()
                normal = (r.s16(), r.s16(), r.s16())
                verts = [(r.s16(), r.s16(), r.s16()) for _ in range(4)]
                target.portals.append((p_room, normal, verts))
        elif tag == "FLOORDAT":
            room.z_sectors, room.x_sectors = r.u32(), r.u32()
        elif tag == "FLOORSIZ":
            r.expect(8, tag)
            count = r.u32()
            if count != room.z_sectors * room.x_sectors:
                raise ValueError("FLOORSIZ count mismatch")
            room.sectors = [(r.u16(), r.u16(), r.u8(), r.s8(), r.u8(), r.s8()) for _ in range(count)]
        elif tag == "FLORDATA":
            r.expect(2, tag); lvl.floors = [r.u16() for _ in range(r.u32())]
        elif tag == "LIGHTAMB":
            room.ambient, room.ambient2 = r.u32(), r.u32()
        elif tag == "RM_FLIP ":
            r.expect(2, tag)
            v = r.u32()
            room.alternate = -1 if v == 0xFFFFFFFF else v
        elif tag == "RM_FLAGS":
            r.expect(2, tag); room.flags = r.u32()
        elif tag == "LIGHTSIZ":
            r.expect(0x14, tag)
            for _ in range(r.u32()):
                x, y, z = r.s32(), r.s32(), r.s32()
                i1, i2 = r.s16(), r.s16()
                radius = r.s32()
                room.lights.append(dict(x=x, y=y, z=z, intensity=i1, intensity2=i2, radius=radius * 2))
        elif tag == "CAMERAS ":
            r.expect(0x10, tag)
            lvl.cameras = [(r.s32(), r.s32(), r.s32(), r.s16(), r.u16()) for _ in range(r.u32())]
        elif tag == "SOUNDFX ":
            r.expect(0x10, tag)
            lvl.sound_sources = [(r.s32(), r.s32(), r.s32(), r.u16(), r.u16()) for _ in range(r.u32())]
        elif tag == "BOXES   ":
            r.expect(0x14, tag)
            lvl.boxes = [(r.s32(), r.s32(), r.s32(), r.s32(), r.s16(), r.u16()) for _ in range(r.u32())]
        elif tag == "OVERLAPS":
            r.expect(2, tag); lvl.overlaps = [r.u16() for _ in range(r.u32())]
        elif tag in ("GND_ZONE", "GND_ZON2", "FLY_ZONE"):
            r.expect(2, tag)
            flipped = tag in zone_seen
            zone_seen.add(tag)
            lvl.zones[(tag, flipped)] = [r.u16() for _ in range(r.u32())]
        elif tag == "ARANGES ":
            r.expect(8, tag); lvl.anim_ranges = [(r.u32(), r.u32()) for _ in range(r.u32())]
        elif tag == "ITEMDATA":
            r.expect(0x14, tag)
            for _ in range(r.u32()):
                lvl.items.append(dict(type=r.u16(), room=r.u16(), x=r.s32(), y=r.s32(), z=r.s32(),
                                      rotation=r.s16(), intensity=r.s16(), intensity2=r.s16(), flags=r.u16()))
        elif tag == "ROOMEND ":
            r.expect(0, tag); r.expect(0, tag)
        else:
            raise ValueError(f"SAT: unknown chunk '{tag}' at 0x{r.pos - 8:X}")


def read_sad(lvl, data):
    r = Reader(data)
    while not r.eof():
        tag = r.tag()
        if tag == "OBJFILE ":
            r.expect(0, tag); r.expect(0x20, tag)
        elif tag == "ANIMS   ":
            r.expect(0x22, tag)
            for _ in range(r.u32()):
                a = dict(frame_offset=r.u32(), frame_size=r.u8(), frame_rate=r.u8(), state=r.u16())
                a["speed"] = (r.u16() << 16) | r.u16()
                a["accel"] = (r.u16() << 16) | r.u16()
                a.update(frame_begin=r.u16(), frame_end=r.u16(), next_anim=r.u16(), next_frame=r.u16(),
                         states_count=r.u16(), states_start=r.u16(), commands_count=r.u16(),
                         commands_start=r.u16())
                lvl.anims.append(a)
        elif tag == "CHANGES ":
            r.expect(8, tag)
            for _ in range(r.u32()):
                state, count, start, dummy = r.u16(), r.u16(), r.u16(), r.u16()
                lvl.states.append((state, count, start))
        elif tag == "RANGES \0":
            r.expect(8, tag)
            lvl.ranges = [(r.u16(), r.u16(), r.u16(), r.u16()) for _ in range(r.u32())]
        elif tag == "COMMANDS":
            r.expect(2, tag); lvl.commands = [r.s16() for _ in range(r.u32())]
        elif tag == "ANIBONES":
            r.expect(4, tag); lvl.nodes = [r.s32() for _ in range(r.u32())]
        elif tag == "ANIMOBJ ":
            r.expect(0x38, tag)
            for _ in range(r.u32()):
                if r.u16() != 0:
                    raise ValueError("ANIMOBJ padding")
                m = dict(type=r.u16(), count=r.u16(), start=r.u16(), node=r.u32(), frame=r.u32(),
                         anim=r.u16())
                if r.u16() != m["anim"]:
                    raise ValueError("ANIMOBJ anim mismatch")
                lvl.models.append(m)
        elif tag == "STATOBJ ":
            r.expect(0x20, tag)
            for _ in range(r.u32()):
                s = dict(id=r.u32(), mesh=r.u16())
                s["vbox"] = tuple(r.s16() for _ in range(6))
                s["cbox"] = tuple(r.s16() for _ in range(6))
                s["flags"] = r.u16()
                lvl.static_meshes.append(s)
        elif tag == "FRAMES  ":
            r.expect(2, tag); lvl.frames = [r.u16() for _ in range(r.u32())]
        elif tag == "MESHPTRS":
            r.expect(4, tag); lvl.mesh_offsets = [r.s32() for _ in range(r.u32())]
        elif tag == "MESHDATA":
            r.expect(2, tag); lvl.mesh_data = r.raw(r.u32() * 2)
        elif tag == "OTEXTINF":
            r.expect(0x10, tag)
            lvl.object_textures = [read_tex_info(r, False) for _ in range(r.u32())]
        elif tag == "OTEXTDAT":
            r.expect(1, tag); lvl.object_tex_data = r.raw(r.u32())
        elif tag == "ITEXTINF":
            r.expect(0x14, tag)
            lvl.item_textures = [read_tex_info(r, True) for _ in range(r.u32())]
        elif tag == "ITEXTDAT":
            r.expect(1, tag); lvl.item_tex_data = r.raw(r.u32())
        elif tag == "OBJEND  ":
            r.expect(0, tag); r.expect(0, tag)
        else:
            raise ValueError(f"SAD: unknown chunk '{tag}' at 0x{r.pos - 8:X}")


# Mesh face group types
MESH_T_TEX_UNK, MESH_T_COLOR, MESH_T_TEX, MESH_T_TEX_TRANSP = 2, 4, 8, 16
MESH_R_COLOR, MESH_R_TEX, MESH_R_TRANSP, MESH_R_FLIP_TEX, MESH_R_FLIP_TRANSP = 5, 9, 17, 49, 57
MESH_TRIANGLES = (MESH_T_TEX_UNK, MESH_T_COLOR, MESH_T_TEX, MESH_T_TEX_TRANSP)
MESH_QUADS = (MESH_R_COLOR, MESH_R_TEX, MESH_R_TRANSP, MESH_R_FLIP_TEX, MESH_R_FLIP_TRANSP)


class Mesh:
    """A mesh from MESHDATA. Faces are (type, indices, flags) with the group type
    kept so callers can tell colour/texture/transparent/flipped faces apart."""

    def __init__(self, data, offset):
        r = Reader(data)
        r.pos = offset
        self.center = (r.s16(), r.s16(), r.s16())
        self.radius = r.s16()
        self.flags = r.u16()
        vcount = r.s16()
        self.vertices = [(r.s16(), r.s16(), r.s16()) for _ in range(vcount)]
        ncount = r.s16()
        if abs(ncount) != vcount:
            raise ValueError(f"mesh at 0x{offset:X}: normal count {ncount} vs {vcount} vertices")
        if ncount > 0:
            self.normals = [(r.s16(), r.s16(), r.s16()) for _ in range(vcount)]
            self.intensities = None
        else:
            self.normals = None
            self.intensities = [r.s16() for _ in range(vcount)]
        fcount = r.u16()
        self.faces = []
        for _ in range(r.u16()):
            ftype = r.u16()
            count = r.u16()
            for _ in range(count):
                if ftype in MESH_TRIANGLES:
                    idx = tuple(r.u16() >> 5 for _ in range(3))
                elif ftype in MESH_QUADS:
                    idx = tuple(r.u16() >> 5 for _ in range(4))
                else:
                    raise ValueError(f"mesh at 0x{offset:X}: unknown face type {ftype}")
                if max(idx) >= vcount:
                    raise ValueError(f"mesh at 0x{offset:X}: index out of range")
                self.faces.append((ftype, idx, r.u16()))
        if len(self.faces) != fcount:
            raise ValueError(f"mesh at 0x{offset:X}: {len(self.faces)} faces, header says {fcount}")
        self.end = r.pos


def meshes(lvl):
    return [Mesh(lvl.mesh_data, off) for off in lvl.mesh_offsets]


TR1_TYPE_MAX = 191


def read_spr(lvl, data):
    r = Reader(data)
    while not r.eof():
        tag = r.tag()
        if tag == "SPRFILE ":
            r.expect(0, tag); r.expect(0x20, tag)
        elif tag == "SPRITINF":
            r.expect(0x10, tag)
            for _ in range(r.u32()):
                s = dict(tile=r.u16(), clut=r.u16(), w=r.u8() * 8, h=r.u8(),
                         l=r.s16(), t=r.s16(), r=r.s16(), b=r.s16())
                if r.s16() != s["b"]:
                    raise ValueError("SPRITINF trailing word mismatch")
                lvl.sprite_textures.append(s)
        elif tag == "SPRITDAT":
            r.expect(1, tag); lvl.sprite_tex_data = r.raw(r.u32())
        elif tag == "OBJECTS ":
            r.expect(0, tag)
            for _ in range(r.u32()):
                if r.u16() != 0:
                    raise ValueError("OBJECTS padding")
                stype = r.u16()
                count = 1 if stype >= TR1_TYPE_MAX else -r.s16()
                lvl.sprite_sequences.append((stype, count, r.u16()))
        elif tag == "SPRITEND":
            r.expect(0, tag); r.expect(0, tag)
        else:
            raise ValueError(f"SPR: unknown chunk '{tag}' at 0x{r.pos - 8:X}")


def read_snd(lvl, data):
    r = Reader(data)
    while not r.eof():
        tag = r.tag()
        if tag == "SOUNDFX ":
            r.expect(0, tag); r.expect(0, tag)
        elif tag == "SAMPLUT ":
            r.expect(2, tag); lvl.sound_map = [r.s16() for _ in range(r.u32())]
        elif tag == "SAMPINFS":
            r.expect(8, tag)
            lvl.sound_infos = [(r.u16(), r.u16(), r.u16(), r.u16()) for _ in range(r.u32())]
        elif tag == "SAMPLE  ":
            index = r.u32()
            lvl.samples[index] = r.raw(r.u32())
        elif tag == "ENDFILE\0":
            r.expect(0, tag); r.expect(0, tag)
        else:
            raise ValueError(f"SND: unknown chunk '{tag}' at 0x{r.pos - 8:X}")


def load(data_dir, name):
    lvl = Level()
    lvl.name = name
    for ext, reader in ((".SAT", read_sat), (".SAD", read_sad), (".SPR", read_spr), (".SND", read_snd)):
        path = os.path.join(data_dir, name + ext)
        if os.path.exists(path):
            reader(lvl, open(path, "rb").read())
    return lvl


def main():
    lvl = load(sys.argv[1], sys.argv[2])
    nv = sum(len(r.vertices) for r in lvl.rooms)
    nq = sum(len(r.quads) for r in lvl.rooms)
    nt = sum(len(r.triangles) for r in lvl.rooms)
    print(f"{lvl.name}: {len(lvl.rooms)} rooms ({nv} vertices, {nq} quads, {nt} triangles), "
          f"{len(lvl.items)} items, {len(lvl.models)} models, {len(lvl.mesh_offsets)} meshes, "
          f"{len(lvl.anims)} anims, {len(lvl.frames)} frame words, {len(lvl.boxes)} boxes, "
          f"{len(lvl.floors)} floor words, {len(lvl.room_textures)} room textures, "
          f"{len(lvl.object_textures)} object textures, {len(lvl.sprite_textures)} sprites, "
          f"{len(lvl.samples)} samples")


if __name__ == "__main__":
    main()
