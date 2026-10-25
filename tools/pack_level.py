"""Pack a Tomb Raider (Saturn) level into the PKD layout the OpenLara fixed
core reads in place (openlara/fmt/pkd.h, structs in openlara/common.h with
__SATURN__ defined). Big-endian, section offsets relative to the file start.

The table layouts follow OpenLara's 32X packer (src/platform/gba/packer/
out_32X.h), which targets the same big-endian SH-2 core, with Saturn face
and texture layouts. Input is the original Saturn level files (tools/trsat.py).

Texture table: [room textures][object textures][item textures]. Each entry
holds VDP1 command fields; until texture upload exists they are zero apart
from the size, attributes and an average colour used for flat shading.

usage: python tools/pack_level.py <DATA dir> <LEVEL> <out.PKD>
"""
import math
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trsat  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

FACE_TYPE_F, FACE_TYPE_FT, FACE_TYPE_FTA = 1, 2, 3
FACE_TYPE_SHIFT = 14
TEX_ATTR_AKILL = 1

NODE_FLAG_ROTX, NODE_FLAG_ROTY, NODE_FLAG_ROTZ = 1 << 2, 1 << 3, 1 << 4


def item_types():
    """ITEM_* enum from openlara/common.h, as {name: value}."""
    src = open(os.path.join(ROOT, "openlara", "common.h")).read()
    block = src[src.index("#define ITEM_TYPES(E)"):src.index("#define DECL_ENUM")]
    names = re.findall(r"E\(\s*(\w+)\s*\)", block)
    return {name: i for i, name in enumerate(names)}


ITEM = item_types()

# Joints rotated by enemy AI (head tracking), from OpenLara's TR1_PC::fixHeadMask.
HEAD_JOINTS = {
    "WOLF": [(2, NODE_FLAG_ROTY)], "BEAR": [(13, NODE_FLAG_ROTY)],
    "CROCODILE_LAND": [(7, NODE_FLAG_ROTY)], "CROCODILE_WATER": [(7, NODE_FLAG_ROTY)],
    "LION_MALE": [(19, NODE_FLAG_ROTY)], "LION_FEMALE": [(19, NODE_FLAG_ROTY)],
    "PUMA": [(19, NODE_FLAG_ROTY)], "GORILLA": [(13, NODE_FLAG_ROTY)],
    "RAT_LAND": [(1, NODE_FLAG_ROTY)], "RAT_WATER": [(1, NODE_FLAG_ROTY)],
    "REX": [(10, NODE_FLAG_ROTY), (11, NODE_FLAG_ROTY)], "RAPTOR": [(21, NODE_FLAG_ROTY)],
    "MUTANT_1": [(0, NODE_FLAG_ROTY), (2, NODE_FLAG_ROTY)],
    "MUTANT_2": [(0, NODE_FLAG_ROTY), (2, NODE_FLAG_ROTY)],
    "MUTANT_3": [(0, NODE_FLAG_ROTY), (2, NODE_FLAG_ROTY)],
    "CENTAUR": [(10, NODE_FLAG_ROTX | NODE_FLAG_ROTY)], "MUMMY": [(2, NODE_FLAG_ROTY)],
    "LARSON": [(6, NODE_FLAG_ROTY)], "PIERRE": [(6, NODE_FLAG_ROTY)],
    "SKATER": [(0, NODE_FLAG_ROTY)], "COWBOY": [(0, NODE_FLAG_ROTY)], "MR_T": [(0, NODE_FLAG_ROTY)],
    "NATLA": [(2, NODE_FLAG_ROTX | NODE_FLAG_ROTZ)], "ADAM": [(1, NODE_FLAG_ROTY)],
}


class Out:
    def __init__(self):
        self.b = bytearray()

    def pos(self):
        return len(self.b)

    def align4(self):
        while len(self.b) & 3:
            self.b.append(0)
        return len(self.b)

    def pack(self, fmt, *values):
        self.b += struct.pack(">" + fmt, *values)

    def raw(self, data):
        self.b += data


def s16(v):
    return v - 0x10000 if v >= 0x8000 else v


def quadrant(angle):
    """TR yaw (int16) to the packer's 2-bit quadrant, (angle / 0x4000 + 2) as in C."""
    return int(s16(angle & 0xFFFF) / 0x4000) + 2


def shade_from_color16(c):
    """Saturn room vertex colour 0x8000 | (k,k,k) (VDP1 gouraud, 16 = neutral)
    to OpenLara's 0 (lit) .. 31 (dark) shade."""
    return 31 - (c & 31)


def texture_average(data, tile, clut, w, h, transparent):
    """Average RGB555 colour of a 4-bit CLUT character."""
    pix = tile << 3
    lut = (clut + tile) << 3
    if lut + 32 > len(data) or pix + w * h // 2 > len(data):
        return 0x8000 | (16 << 10) | (16 << 5) | 16
    colors = struct.unpack_from(">16H", data, lut)
    r = g = b = n = 0
    for i in range(w * h // 2):
        byte = data[pix + i]
        for idx in (byte >> 4, byte & 15):
            if transparent and idx == 0:
                continue
            c = colors[idx]
            r += c & 31
            g += (c >> 5) & 31
            b += (c >> 10) & 31
            n += 1
    if n == 0:
        return 0x8000
    return 0x8000 | ((b // n) << 10) | ((g // n) << 5) | (r // n)


class Packer:
    def __init__(self, lvl):
        self.lvl = lvl
        self.meshes = trsat.meshes(lvl)
        self.palette = []           # RGB555 colours of coloured faces
        self.room_tex_base = 0
        self.obj_tex_base = len(lvl.room_textures)
        self.item_tex_base = self.obj_tex_base + len(lvl.object_textures)
        self.room_transparent = set()
        self.obj_transparent = set()
        self.item_tex_index = {t["index"]: i for i, t in enumerate(lvl.item_textures)}
        self.inventory_meshes = set()
        for m in lvl.models:
            name = next((n for n, v in ITEM.items() if v == m["type"]), "")
            if name.startswith("INV_") and lvl.item_textures:
                self.inventory_meshes.update(range(m["start"], m["start"] + m["count"]))

    def color_index(self, color):
        if color not in self.palette:
            if len(self.palette) == 256:
                raise ValueError("more than 256 face colours")
            self.palette.append(color)
        return self.palette.index(color)

    # --- rooms -----------------------------------------------------------

    def write_rooms(self, out):
        lvl = self.lvl
        base = out.align4()
        out.raw(bytes(56 * len(lvl.rooms)))
        infos = []

        for room in lvl.rooms:
            if room.x % 256 or room.z % 256:
                raise ValueError(f"room {room.index}: origin not on a 256 grid")
            ys = [v[1] for v in room.vertices]
            y_top = min(ys) if ys else room.y_top
            y_bottom = max(ys) if ys else room.y_bottom

            quads = out.align4()
            for v0, v1, v2, v3, tex, transp in room.quads:
                if transp:
                    self.room_transparent.add(tex)
                ftype = FACE_TYPE_FTA if transp else FACE_TYPE_FT
                out.pack("I4H", (ftype << FACE_TYPE_SHIFT) | (self.room_tex_base + tex), v0, v1, v2, v3)

            triangles = out.align4()
            for v0, v1, v2, tex in room.triangles:
                out.pack("I4H", (FACE_TYPE_FT << FACE_TYPE_SHIFT) | (self.room_tex_base + tex), v0, v1, v2, 0)

            vertices = out.align4()
            for x, y, z, color in room.vertices:
                px, py, pz = x >> 8, (y - y_top) >> 8, z >> 8
                if not (0 <= px < 256 and 0 <= py < 256 and 0 <= pz < 256):
                    raise ValueError(f"room {room.index}: vertex {x},{y},{z} out of packed range")
                out.pack("4B", px, py, pz, shade_from_color16(color))

            sprites = out.align4()
            for vertex, tex in room.sprites:
                x, y, z, color = room.vertices[vertex]
                out.pack("3h2B", x, y, z, shade_from_color16(color) * 8, tex)

            portals = out.align4()
            for p_room, normal, verts in room.portals:
                out.pack("H3h", p_room, *normal)
                for v in verts:
                    out.pack("3h", *v)

            sectors = out.align4()
            for s in room.sectors:
                out.pack("2HBbBb", *s)

            lights = out.align4()
            for l in room.lights:
                out.pack("3h2B", l["x"] - room.x, l["y"], l["z"] - room.z,
                         min(255, l["radius"] >> 8), min(255, max(0, l["intensity"]) >> 5))

            meshes = out.align4()
            for m in room.static_meshes:
                if m["mesh_id"] > 63:
                    raise ValueError(f"room {room.index}: static mesh id {m['mesh_id']} > 63")
                flags = (quadrant(m["rotation"]) << 6) | m["mesh_id"]
                xy = (((m["x"] - room.x) & 0xFFFF) << 16) | (m["y"] & 0xFFFF)
                zf = (((m["z"] - room.z) & 0xFFFF) << 16) | (min(255, m["intensity"] >> 5) << 8) | flags
                out.pack("2I", xy, zf)

            water = room.flags & 1
            infos.append(struct.pack(">4h4H4B4B8I",
                room.x // 256, room.z // 256, y_bottom, y_top,
                len(room.quads), len(room.triangles), len(room.vertices), len(room.sprites),
                len(room.portals), len(room.lights), len(room.static_meshes), min(255, room.ambient >> 5),
                room.x_sectors, room.z_sectors, room.alternate & 0xFF, water,
                quads, triangles, vertices, sprites, portals, sectors, lights, meshes))

        out.b[base:base + 56 * len(lvl.rooms)] = b"".join(infos)
        return base

    # --- meshes ----------------------------------------------------------

    def mesh_face_flags(self, mesh_index, ftype, flags):
        lvl = self.lvl
        if ftype in (trsat.MESH_T_COLOR, trsat.MESH_R_COLOR):
            return (FACE_TYPE_F << FACE_TYPE_SHIFT) | self.color_index(flags)

        if ftype in trsat.MESH_TRIANGLES:
            tex = flags & 0x0FFF
        else:
            tex = flags >> 4
        transp = ftype in (trsat.MESH_T_TEX_TRANSP, trsat.MESH_R_TRANSP, trsat.MESH_R_FLIP_TRANSP)

        if mesh_index in self.inventory_meshes:
            index = self.item_tex_base + self.item_tex_index.get(tex, 0)
        else:
            if tex >= len(lvl.object_textures):
                raise ValueError(f"mesh {mesh_index}: texture {tex} out of range")
            index = self.obj_tex_base + tex
        if transp:
            self.obj_transparent.add(index)
        return ((FACE_TYPE_FTA if transp else FACE_TYPE_FT) << FACE_TYPE_SHIFT) | index

    def write_mesh(self, out, index, mesh):
        quads = [f for f in mesh.faces if len(f[1]) == 4]
        tris = [f for f in mesh.faces if len(f[1]) == 3]
        intensity = sum(mesh.intensities) // len(mesh.intensities) if mesh.intensities else 0
        if len(mesh.vertices) > 255:
            raise ValueError(f"mesh {index}: {len(mesh.vertices)} vertices")
        out.pack("4hH2B4h", *mesh.center, mesh.radius, intensity, len(mesh.vertices), 0,
                 len(quads), len(tris), 0, 0)
        for v in mesh.vertices:
            out.pack("3h", *v)
        for ftype, idx, flags in quads:
            out.pack("H4B", self.mesh_face_flags(index, ftype, flags), *idx)
        for ftype, idx, flags in tris:
            out.pack("H4B", self.mesh_face_flags(index, ftype, flags), *idx, 0)

    # --- animation -------------------------------------------------------

    def frames_pc_order(self):
        """Saturn stores each joint's two rotation words swapped relative to PC,
        which is the order the core decodes. Frames are consecutive and sized
        by their own joint count (word 9)."""
        f = list(self.lvl.frames)
        pos = 0
        while pos < len(f):
            count = f[pos + 9]
            for j in range(count):
                i = pos + 10 + j * 2
                f[i], f[i + 1] = f[i + 1], f[i]
            pos += 10 + 2 * count
        return f

    def node_flags(self):
        nodes = list(self.lvl.nodes)
        for m in self.lvl.models:
            for name, joints in HEAD_JOINTS.items():
                if m["type"] == ITEM[name]:
                    for joint, mask in joints:
                        nodes[m["node"] + joint * 4] |= mask
        return nodes

    # --- textures --------------------------------------------------------

    def texture_entry(self, data, t, transparent):
        color = texture_average(data, t["tile"], t["clut"], t["w"], t["h"], transparent)
        attr = TEX_ATTR_AKILL if transparent else 0
        return struct.pack(">6H", 0, 0, ((t["w"] // 8) << 8) | t["h"], attr, color, 0)

    # --- level -----------------------------------------------------------

    def pack(self):
        lvl = self.lvl
        out = Out()
        out.raw(bytes(172))     # header, written last

        rooms = self.write_rooms(out)

        floors = out.align4()
        out.pack(f"{len(lvl.floors)}H", *lvl.floors)

        mesh_base = out.align4()
        mesh_offsets = []
        written = {}
        for i, (src_offset, mesh) in enumerate(zip(lvl.mesh_offsets, self.meshes)):
            if src_offset not in written:
                written[src_offset] = out.align4() - mesh_base
                self.write_mesh(out, i, mesh)
            mesh_offsets.append(written[src_offset])

        mesh_offsets_pos = out.align4()
        out.pack(f"{len(mesh_offsets)}i", *mesh_offsets)

        anims = out.align4()
        for a in lvl.anims:
            out.pack("I2BH2i8H", a["frame_offset"], a["frame_rate"], a["frame_size"], a["state"],
                     s32(a["speed"]), s32(a["accel"]), a["frame_begin"], a["frame_end"],
                     a["next_anim"], a["next_frame"], a["states_count"], a["states_start"],
                     a["commands_count"], a["commands_start"])

        states = out.align4()
        for state, count, start in lvl.states:
            out.pack("2BH", state, count, start)

        ranges = out.align4()
        for r in lvl.ranges:
            out.pack("4H", *r)

        commands = out.align4()
        out.pack(f"{len(lvl.commands)}h", *lvl.commands)

        nodes = out.align4()
        n = self.node_flags()
        for i in range(0, len(n), 4):
            out.pack("3hH", n[i + 1], n[i + 2], n[i + 3], n[i])

        frames = out.align4()
        f = self.frames_pc_order()
        out.pack(f"{len(f)}H", *f)

        models = out.align4()
        for m in lvl.models:
            out.pack("BbHHH", m["type"], m["count"], m["start"], m["node"] // 4, m["anim"])

        static_meshes = out.align4()
        for s in lvl.static_meshes:
            vb = s["vbox"]
            cx, cy, cz = (vb[0] + vb[1]) >> 1, (vb[2] + vb[3]) >> 1, (vb[4] + vb[5]) >> 1
            dx, dy, dz = (vb[1] - vb[0]) >> 1, (vb[3] - vb[2]) >> 1, (vb[5] - vb[4]) >> 1
            radius = int(math.sqrt(dx * dx + dy * dy + dz * dz))
            out.pack("2HI", s["id"], s["mesh"], s["flags"])
            out.pack("2I", ((cx & 0xFFFF) << 16) | (cy & 0xFFFF), ((cz & 0xFFFF) << 16) | (radius & 0xFFFF))
            out.pack("6h", *vb)
            out.pack("6h", *s["cbox"])

        textures = out.align4()
        for i, t in enumerate(lvl.room_textures):
            out.raw(self.texture_entry(lvl.room_tex_qtr, t, i in self.room_transparent))
        for i, t in enumerate(lvl.object_textures):
            out.raw(self.texture_entry(lvl.object_tex_data, t, self.obj_tex_base + i in self.obj_transparent))
        for i, t in enumerate(lvl.item_textures):
            out.raw(self.texture_entry(lvl.item_tex_data, t, self.item_tex_base + i in self.obj_transparent))
        textures_count = len(lvl.room_textures) + len(lvl.object_textures) + len(lvl.item_textures)

        sprites = out.align4()
        for s in lvl.sprite_textures:
            out.pack("4H4h", 0, 0, ((s["w"] // 8) << 8) | s["h"], TEX_ATTR_AKILL, s["l"], s["t"], s["r"], s["b"])

        sprite_sequences = out.align4()
        for stype, count, start in lvl.sprite_sequences:
            out.pack("2HhH", stype, 0, -count, start)

        cameras = out.align4()
        for c in lvl.cameras:
            out.pack("3ihH", *c)

        sound_sources = out.align4()
        for s in lvl.sound_sources:
            out.pack("3i2H", *s)

        boxes = out.align4()
        for min_z, max_z, min_x, max_x, floor, overlap in lvl.boxes:
            out.pack("4BhH", min_z // 1024, (max_z + 1) // 1024, min_x // 1024, (max_x + 1) // 1024,
                     floor, overlap)

        overlaps = out.align4()
        out.pack(f"{len(lvl.overlaps)}H", *lvl.overlaps)

        zones = []
        for flipped in (False, True):
            for tag in ("GND_ZONE", "GND_ZON2", "FLY_ZONE"):
                zones.append(out.align4())
                z = lvl.zones[(tag, flipped)]
                out.pack(f"{len(z)}H", *z)

        anim_tex = out.align4()
        out.pack("h", len(lvl.anim_ranges))
        for first, last in lvl.anim_ranges:
            out.pack("h", last - first)
            for t in range(first, last + 1):
                out.pack("H", self.room_tex_base + t)

        items = out.align4()
        for it in lvl.items:
            room = lvl.rooms[it["room"]]
            if it["flags"] & ~0x3F1F:
                raise ValueError(f"item flags 0x{it['flags']:X}")
            intensity = 0 if it["intensity"] < 0 else it["intensity"] >> 5
            out.pack("2B3hHH", it["type"], it["room"], it["x"] - room.x, it["y"], it["z"] - room.z,
                     intensity, it["flags"] | (quadrant(it["rotation"]) << 14))

        palette = out.align4()
        colors = self.palette + [0] * (256 - len(self.palette))
        out.pack("256H", *colors)

        header = struct.pack(">I14H", 0x53415431,     # 'SAT1'
            0, len(lvl.rooms), len(lvl.models), len(lvl.mesh_offsets), len(lvl.static_meshes),
            len(lvl.sprite_sequences), len(lvl.sound_sources), len(lvl.boxes), textures_count,
            len(lvl.sprite_textures), len(lvl.items), len(lvl.cameras), 0, 0)
        header += struct.pack(">35I",
            palette, 0, 0, rooms, floors, mesh_base, mesh_offsets_pos, anims, states, ranges,
            commands, nodes, frames, models, static_meshes, textures, sprites, sprite_sequences,
            cameras, sound_sources, boxes, overlaps, *zones, anim_tex, items, 0, 0, 0, 0, 0)
        assert len(header) == 172
        out.b[0:172] = header
        return bytes(out.b)


def s32(v):
    return v - 0x100000000 if v >= 0x80000000 else v


def main():
    data_dir, name, out_path = sys.argv[1:4]
    lvl = trsat.load(data_dir, name)
    data = Packer(lvl).pack()
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(data)
    print(f"{name}: {len(data)} bytes -> {out_path}")


if __name__ == "__main__":
    main()
