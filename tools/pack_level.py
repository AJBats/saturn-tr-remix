"""Pack a Tomb Raider (Saturn) level into the PKD layout the OpenLara fixed
core reads in place (openlara/fmt/pkd.h, structs in openlara/common.h with
__SATURN__ defined). Big-endian, section offsets relative to the file start.

The table layouts follow OpenLara's 32X packer (src/platform/gba/packer/
out_32X.h), which targets the same big-endian SH-2 core, with Saturn face
and texture layouts. Input is the original Saturn level files (tools/trsat.py).

Texture entries hold VDP1 command fields addressing the texture blob, which
the game loads into VDP1 VRAM at VRAM_TEX_BASE (saturn/vram.h), plus an
average colour for untextured drawing.

Room textures are drawn from the full-resolution set (ROOMTSUB), which goes
to a separate file the game keeps in work RAM and streams into VDP1 VRAM as
faces use it (saturn/detail.h): per texture, the whole texture. The original
stores it as four quadrants, for faces split near the camera, and draws the
rest with a quarter-resolution set (ROOMTQTR), which the blob leaves out but
for its colour tables. The game rearranges a texture into quadrants for the
faces it splits.

Sound effects keep the original's samples (16-bit PCM at 5512.5 Hz). They
go to a separate file the game loads into sound RAM; the PKD holds the sound
map, the sound infos and each sample's offset in that file.

Inventory item textures go to another file, which the game puts in the VRAM
after the blob (where full-res room textures are cached during play) while
the inventory is open.

The remastered colour tables go to another file, which the game swaps with
the original ones in VRAM to compare them (saturn/palettes.h).

usage: python tools/pack_level.py <DATA dir> <LEVEL> <out.PKD>
       (also writes <out>.TEX, the texture blob loaded at VRAM_TEX_BASE,
       <out>.SUB, the full-resolution room textures, <out>.SFX, the sound
       samples, <out>.ITX, the inventory item textures, and <out>.PAL, the
       remastered colour tables)

For testing, LARA_START=<room> in the environment starts Lara in that room,
on the walkable floor nearest its centre; LARA_START=<room>:<x>:<z> on that
sector; LARA_START=<room>:<x>:<z>:<degrees> also turns her to that heading,
to the nearest quarter turn (items keep only that).
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
FACE_TEXTURE_MAX = 0x3FFF
DETAIL_UNIT = 256   # Texture.reserved: full-res offset / DETAIL_UNIT + 1

NODE_FLAG_ROTX, NODE_FLAG_ROTY, NODE_FLAG_ROTZ = 1 << 2, 1 << 3, 1 << 4


def item_types():
    """ITEM_* enum from openlara/common.h, as {name: value}."""
    src = open(os.path.join(ROOT, "openlara", "common.h")).read()
    block = src[src.index("#define ITEM_TYPES(E)"):src.index("#define DECL_ENUM")]
    names = re.findall(r"E\(\s*(\w+)\s*\)", block)
    return {name: i for i, name in enumerate(names)}


ITEM = item_types()


def vram_tex_base():
    src = open(os.path.join(ROOT, "saturn", "vram.h")).read()
    return int(re.search(r"#define VRAM_TEX_BASE\s+(0x[0-9A-Fa-f]+)", src).group(1), 16)


VRAM_TEX_BASE = vram_tex_base()

# VDP1 CMDCTRL direction bits, kept in texture attributes
TEX_ATTR_FLIP_H = 0x10

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


def texture_average(pixels, colors, transparent, end_codes=False):
    """Average RGB555 colour of 4-bit pixels drawn with 16 colours, leaving out
    colour 0 when it is transparent and colour 15 when it is VDP1's end code
    (the triangle variants of object textures fill their unused half with it).
    Grey without pixels."""
    if pixels is None:
        return 0x8000 | (16 << 10) | (16 << 5) | 16
    r = g = b = n = 0
    for byte in pixels:
        for idx in (byte >> 4, byte & 15):
            if (transparent and idx == 0) or (end_codes and idx == 15):
                continue
            c = colors[idx]
            r += c & 31
            g += (c >> 5) & 31
            b += (c >> 10) & 31
            n += 1
    if n == 0:
        return 0x8000
    return 0x8000 | ((b // n) << 10) | ((g // n) << 5) | (r // n)


class TextureTable:
    """Texture entries as VDP1 command fields, plus the blob loaded at
    VRAM_TEX_BASE. Room textures come first with fixed indices (animated
    texture ranges refer to them): they have no character in the blob (srca
    0), only their colour tables, and their size is a quadrant's, half the
    full-res texture's each way. Object texture variants are added as faces
    use them."""

    def __init__(self, lvl):
        self.lvl = lvl
        self.sub = whole_textures(lvl)
        self.blob = bytearray()
        self.obj_off = self.add_segment(lvl.object_tex_data)
        self.room_off = self.add_segment(b"".join(lvl.room_tex_qtr[self.room_lut(t):self.room_lut(t) + 32]
                                                  for t in lvl.room_textures))
        self.spr_off = self.add_segment(lvl.sprite_tex_data)
        # the VRAM after the blob: full-res room texture cache, or item textures
        self.cache_off = (len(self.blob) + 31) & ~31
        if self.cache_off + len(lvl.item_tex_data) > 0x80000 - VRAM_TEX_BASE:
            raise ValueError(f"textures need {self.cache_off + len(lvl.item_tex_data)} bytes of VDP1 VRAM")
        self.entries = []   # srca, colr, size, attr, full-res key
        self.sources = []   # each entry's pixels, colours, colour table's blob offset (None outside it), end codes
        self.index = {}
        for i, t in enumerate(lvl.room_textures):
            pix = t["sub"][0] * 8
            n = t["w"] * t["h"] * 2  # the whole full-res texture
            lut = self.room_lut(t)
            colr = self.room_off + i * 32
            if pix + n > len(self.sub) or lut + 32 > len(lvl.room_tex_qtr):
                raise ValueError(f"room texture outside the level's data: {t}")
            self.sources.append((self.sub[pix:pix + n], struct.unpack_from(">16H", lvl.room_tex_qtr, lut), colr, False))
            self.entries.append([0, (VRAM_TEX_BASE + colr) >> 3, ((t["w"] // 8) << 8) | t["h"], 0, self.detail_key(t)])

    @staticmethod
    def room_lut(t):
        return ((t["clut"] + t["tile"]) & 0xFFFF) * 8

    def add_segment(self, data):
        # colour lookup tables in VRAM must be 32-byte aligned
        while len(self.blob) & 31:
            self.blob.append(0)
        off = len(self.blob)
        self.blob += data
        return off

    def entry(self, data, seg_off, t, tile, attr, end_codes=False):
        """Texture t's tile in 'data', at seg_off in the blob (or in the VRAM
        after it, for item textures)."""
        pix = tile * 8
        lut = ((t["clut"] + tile) & 0xFFFF) * 8
        n = t["w"] * t["h"] // 2
        if pix + n > len(data) or lut + 32 > len(data):
            self.sources.append((None, None, None, False))
        else:
            in_blob = seg_off != self.cache_off
            self.sources.append((data[pix:pix + n], struct.unpack_from(">16H", data, lut),
                                 seg_off + lut if in_blob else None, end_codes))
        base = VRAM_TEX_BASE + seg_off
        return [(base + pix) >> 3, (base + lut) >> 3, ((t["w"] // 8) << 8) | t["h"], attr, 0]

    @staticmethod
    def detail_key(t):
        quadrant = t["w"] * t["h"] // 2
        offset = t["sub"][0] * 8
        if any(t["sub"][q] * 8 != offset + q * quadrant for q in range(4)) or offset % DETAIL_UNIT:
            raise ValueError(f"room texture quadrants not contiguous: {t}")
        return offset // DETAIL_UNIT + 1

    def room(self, tex, transparent):
        if transparent:
            self.entries[tex][3] |= TEX_ATTR_AKILL
        return tex

    def object(self, tex, variant, attr):
        key = ("obj", tex, variant, attr)
        if key not in self.index:
            t = self.lvl.object_textures[tex]
            tile = t["tile"] if variant == 0 else t["sub"][variant - 1]
            self.index[key] = len(self.entries)
            self.entries.append(self.entry(self.lvl.object_tex_data, self.obj_off, t, tile, attr, variant != 0))
        return self.index[key]

    def item(self, tex, attr):
        """Inventory item textures, in VRAM after the blob while the inventory is open."""
        key = ("item", tex, attr)
        if key not in self.index:
            t = self.lvl.item_textures[tex]
            self.index[key] = len(self.entries)
            self.entries.append(self.entry(self.lvl.item_tex_data, self.cache_off, t, t["tile"], attr))
        return self.index[key]

    def pack(self):
        """Each entry: srca, colr, size, attr, mean colour, full-res key, mean
        colour under the remastered colour tables, and 0 for the game to fill
        (Texture.contrast)."""
        remastered = dict(self.remastered_palettes())
        out = []
        for e, (pixels, colors, lut, end_codes) in zip(self.entries, self.sources):
            transparent = e[3] & TEX_ATTR_AKILL
            mean = texture_average(pixels, colors, transparent, end_codes)
            alt = texture_average(pixels, remastered[lut], transparent, end_codes) if lut in remastered else mean
            out.append(struct.pack(">8H", *e[:4], mean, e[4], alt, 0))
        return b"".join(out)

    def remastered_palettes(self):
        """The blob's colour tables made to suit VDP1's gouraud shading, as
        (blob offset, 16 colours) for each that changes. Each table's colours
        are weighed by how often its textures use them."""
        uses = {}  # table offset -> [use count per index]
        akill = set()

        def count(pixels, lut, attr):
            counts = uses.setdefault(lut, [0] * 16)
            for b in pixels:
                counts[b >> 4] += 1
                counts[b & 15] += 1
            if attr & TEX_ATTR_AKILL:
                akill.add(lut)

        for e, (pixels, _, lut, _) in zip(self.entries, self.sources):
            if lut is not None:  # inventory item textures live outside the blob
                count(pixels, lut, e[3])
        for s in self.lvl.sprite_textures:
            pix = self.spr_off + s["tile"] * 8
            count(self.blob[pix:pix + s["w"] * s["h"] // 2], self.spr_off + s["clut"] * 8, TEX_ATTR_AKILL)

        out = []
        for lut, counts in sorted(uses.items()):
            colors = list(struct.unpack_from(">16H", self.blob, lut))
            if lut in akill:
                counts[0] = 0  # transparent
            new = remaster_palette(colors, counts, keep_first=lut in akill)
            if new != colors:
                out.append((lut, new))
        return out


# Colour tables remastered for VDP1's gouraud shading, which adds to or takes
# from each 5-bit channel instead of scaling it. Darkening takes the dark
# colours of a texture to black first (speckle) and its weakest channels first
# (a colour cast), so the dark colours are drawn toward the texture's mean and
# a little toward grey. The bright end is left alone, washing out under bright
# light as the original does.
PALETTE_GAIN = 1.10      # the PS1's brightness: the Saturn's tables are ~10% darker
PALETTE_LIFT = 0.55      # dark colours keep this much of their distance below the mean
PALETTE_DESAT = 0.35     # and their weak channels move this far toward their grey


def remaster_palette(colors, counts, keep_first):
    def channels(c):
        return [c & 31, (c >> 5) & 31, (c >> 10) & 31]

    def luma(ch):
        return 0.30 * ch[0] + 0.59 * ch[1] + 0.11 * ch[2]

    total = sum(counts)
    if total == 0:
        return colors
    mean = [sum(channels(c)[k] * n for c, n in zip(colors, counts)) / total * PALETTE_GAIN for k in range(3)]
    mean_luma = luma(mean)
    out = []
    for i, c in enumerate(colors):
        if (keep_first and i == 0) or c & 0x7FFF == 0:
            out.append(c)
            continue
        ch = [q * PALETTE_GAIN for q in channels(c)]
        if luma(ch) < mean_luma:
            ch = [m + (q - m) * PALETTE_LIFT for q, m in zip(ch, mean)]
            grey = luma(ch)
            ch = [q + (grey - q) * PALETTE_DESAT if q < grey else q for q in ch]
        r, g, b = (max(0, min(31, round(q))) for q in ch)
        out.append((c & 0x8000) | (b << 10) | (g << 5) | r)
    return out


# Textured mesh triangles, after the original's handlers (0x0603E6D0, one per
# code in the top 3 bits of the face flags): each is drawn as the
# parallelogram v0, v1, v1 + v2 - v0, with a texture variant whose end codes
# stop every line at the triangle's far edge. Per code: the variant
# (n = sub[n - 1]) and the corner arrangement packed with the indices,
# bit 0: v0 at corner D (else A), bit 1: the other two corners swapped.
TRIANGLE_VARIANT = {0: 1, 10: 1, 2: 2, 8: 2, 4: 3, 14: 3, 6: 4, 12: 4}
TRIANGLE_ARRANGEMENT = {0: 0, 4: 0, 10: 2, 14: 2, 2: 1, 6: 1, 8: 3, 12: 3}
TRIANGLE_PLAIN = 0x80   # a textured triangle drawn without the parallelogram

# Mesh.hasNormals: what follows a mesh's vertices
MESH_LIGHT_NORMALS = 1  # an 8-bit normal (and a pad byte) per vertex
MESH_LIGHT_SHADES = 2   # a shade per vertex (intensity >> 5), padded to even


class Packer:
    def __init__(self, lvl):
        self.lvl = lvl
        self.meshes = trsat.meshes(lvl)
        self.palette = []           # RGB555 colours of coloured faces
        self.textures = TextureTable(lvl)
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
                ftype = FACE_TYPE_FTA if transp else FACE_TYPE_FT
                out.pack("I4H", (ftype << FACE_TYPE_SHIFT) | self.textures.room(tex, transp), v0, v1, v2, v3)

            triangles = out.align4()
            for v0, v1, v2, tex in room.triangles:
                out.pack("I4H", (FACE_TYPE_FT << FACE_TYPE_SHIFT) | self.textures.room(tex, False), v0, v1, v2, 0)

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

        transp = ftype in (trsat.MESH_T_TEX_TRANSP, trsat.MESH_R_TRANSP, trsat.MESH_R_FLIP_TRANSP)
        attr = TEX_ATTR_AKILL if transp else 0
        if ftype in trsat.MESH_TRIANGLES:
            tex, variant = flags & 0x0FFF, TRIANGLE_VARIANT[flags >> 12]
        else:
            tex, variant = flags >> 4, 0
            if ftype in (trsat.MESH_R_FLIP_TEX, trsat.MESH_R_FLIP_TRANSP):
                attr |= TEX_ATTR_FLIP_H

        if mesh_index in self.inventory_meshes:
            index = self.textures.item(self.item_tex_index.get(tex, 0), attr)
        else:
            if tex >= len(lvl.object_textures):
                raise ValueError(f"mesh {mesh_index}: texture {tex} out of range")
            index = self.textures.object(tex, variant, attr)
        if index > FACE_TEXTURE_MAX:
            raise ValueError("texture index does not fit the face flags")
        return ((FACE_TYPE_FTA if transp else FACE_TYPE_FT) << FACE_TYPE_SHIFT) | index

    def write_mesh(self, out, index, mesh):
        """Vertices are followed by their lighting: normals (as 8-bit vectors)
        for meshes lit by the room's lights, or shades for pre-lit ones."""
        quads = [f for f in mesh.faces if len(f[1]) == 4]
        tris = [f for f in mesh.faces if len(f[1]) == 3]
        intensity = sum(mesh.intensities) // len(mesh.intensities) if mesh.intensities else 0
        if len(mesh.vertices) > 255:
            raise ValueError(f"mesh {index}: {len(mesh.vertices)} vertices")
        lighting = MESH_LIGHT_NORMALS if mesh.normals else MESH_LIGHT_SHADES if mesh.intensities else 0
        # the radius is for collision, and often leaves vertices out
        cx, cy, cz = mesh.center
        bound = math.ceil(max((math.sqrt((x - cx) ** 2 + (y - cy) ** 2 + (z - cz) ** 2)
                               for x, y, z in mesh.vertices), default=0))
        if bound > 0x7FFF:
            raise ValueError(f"mesh {index}: bounding radius {bound}")
        out.pack("4hH2B4h", *mesh.center, mesh.radius, intensity, len(mesh.vertices), lighting,
                 len(quads), len(tris), bound, 0)
        for v in mesh.vertices:
            out.pack("3h", *v)
        if mesh.normals:
            for n in mesh.normals:  # 16384 long; 127 here
                out.pack("4b", *(max(-127, min(127, c >> 7)) for c in n), 0)
        elif mesh.intensities:
            out.pack(f"{len(mesh.intensities)}B", *(max(0, min(255, i >> 5)) for i in mesh.intensities))
            if len(mesh.intensities) & 1:
                out.pack("B", 0)
        for ftype, idx, flags in quads:
            out.pack("H4B", self.mesh_face_flags(index, ftype, flags), *idx)
        for ftype, idx, flags in tris:
            if ftype == trsat.MESH_T_COLOR:
                arrangement = 0
            elif index in self.inventory_meshes:
                arrangement = TRIANGLE_PLAIN    # item textures have no triangle variants
            else:
                arrangement = TRIANGLE_ARRANGEMENT[flags >> 12]
            out.pack("H4B", self.mesh_face_flags(index, ftype, flags), *idx, arrangement)

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
        out.raw(self.textures.pack())
        textures_count = len(self.textures.entries)

        sprites = out.align4()
        spr_base = VRAM_TEX_BASE + self.textures.spr_off
        for s in lvl.sprite_textures:
            out.pack("4H4h", (spr_base + s["tile"] * 8) >> 3, (spr_base + s["clut"] * 8) >> 3,
                     ((s["w"] // 8) << 8) | s["h"], TEX_ATTR_AKILL, s["l"], s["t"], s["r"], s["b"])

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
                out.pack("H", t)

        items = out.align4()
        for it in lvl.items:
            room = lvl.rooms[it["room"]]
            if it["flags"] & ~0x3F1F:
                raise ValueError(f"item flags 0x{it['flags']:X}")
            intensity = 0 if it["intensity"] < 0 else it["intensity"] >> 5
            out.pack("2B3hHH", it["type"], it["room"], it["x"] - room.x, it["y"], it["z"] - room.z,
                     intensity, it["flags"] | (quadrant(it["rotation"]) << 14))

        camera_frames = out.align4()
        for frame in lvl.camera_frames:
            out.pack("8h", *frame)

        palette = out.align4()
        colors = self.palette + [0] * (256 - len(self.palette))
        out.pack("256H", *colors)

        sound_map = out.align4()
        out.pack(f"{len(lvl.sound_map)}h", *lvl.sound_map)

        sound_infos = out.align4()
        for info in lvl.sound_infos:
            out.pack("4H", *info)

        # offsets of the samples in the .SFX file, and its size after the last
        sound_offsets = out.align4()
        offsets, self.sfx = [], bytearray()
        for i in range(len(lvl.samples)):
            offsets.append(len(self.sfx))
            self.sfx += lvl.samples[i]
            while len(self.sfx) & 3:
                self.sfx.append(0)
        out.pack(f"{len(offsets) + 1}i", *offsets, len(self.sfx))

        header = struct.pack(">I14H", 0x53415431,     # 'SAT1'
            0, len(lvl.rooms), len(lvl.models), len(lvl.mesh_offsets), len(lvl.static_meshes),
            len(lvl.sprite_sequences), len(lvl.sound_sources), len(lvl.boxes), textures_count,
            len(lvl.sprite_textures), len(lvl.items), len(lvl.cameras), len(lvl.camera_frames), len(offsets))
        header += struct.pack(">35I",
            palette, 0, 0, rooms, floors, mesh_base, mesh_offsets_pos, anims, states, ranges,
            commands, nodes, frames, models, static_meshes, textures, sprites, sprite_sequences,
            cameras, sound_sources, boxes, overlaps, *zones, anim_tex, items,
            camera_frames, sound_map, sound_infos, 0, sound_offsets)
        assert len(header) == 172
        out.b[0:172] = header
        return bytes(out.b)


def whole_textures(lvl):
    """The full-res set with each texture's four quadrants (clockwise from top
    left, as ROOMTSUB holds them) put together into one."""
    out = bytearray(lvl.room_tex_sub)
    done = set()
    for t in lvl.room_textures:
        offset, w, h = t["sub"][0] * 8, t["w"], t["h"]
        if offset in done:
            continue
        done.add(offset)
        row = w // 2
        quad = row * h
        for q, (ox, oy) in enumerate(((0, 0), (1, 0), (1, 1), (0, 1))):
            for r in range(h):
                src = offset + q * quad + r * row
                dst = offset + (oy * h + r) * row * 2 + ox * row
                out[dst:dst + row] = lvl.room_tex_sub[src:src + row]
    return bytes(out)


def s32(v):
    return v - 0x100000000 if v >= 0x80000000 else v


def borrow_title_glyphs(lvl, data_dir):
    """The levels' fonts stop short of the passport's arrow glyphs ($l, $m),
    which only TITLE has: copy the missing ones in. The font's sprites must be
    contiguous, so the sequence moves to the end, as a copy of the level's
    glyphs followed by the borrowed ones."""
    seq = next((i for i, s in enumerate(lvl.sprite_sequences) if s[0] == ITEM["GLYPHS"]), None)
    if seq is None or lvl.name == "TITLE":
        return
    title = trsat.load(data_dir, "TITLE")
    _, count, start = lvl.sprite_sequences[seq]
    _, title_count, title_start = next(s for s in title.sprite_sequences if s[0] == ITEM["GLYPHS"])
    if count >= title_count:
        return

    new_start = len(lvl.sprite_textures)
    lvl.sprite_textures += [dict(s) for s in lvl.sprite_textures[start:start + count]]
    data = bytearray(lvl.sprite_tex_data)
    data += bytes(-len(data) % 8)
    for s in title.sprite_textures[title_start + count:title_start + title_count]:
        tile = title.sprite_tex_data[s["tile"] * 8:s["tile"] * 8 + s["w"] * s["h"] // 2]  # 4-bit pixels
        clut = title.sprite_tex_data[s["clut"] * 8:s["clut"] * 8 + 32]
        lvl.sprite_textures.append(dict(s, tile=len(data) // 8, clut=(len(data) + len(tile)) // 8))
        data += tile + clut
    lvl.sprite_tex_data = bytes(data)
    lvl.sprite_sequences[seq] = (ITEM["GLYPHS"], title_count, new_start)


def read_camera_frames(lvl, data_dir):
    """A cutscene's camera path, CINDATA/<level>.CIN: a frame count, then per
    frame the target, the position, fov and roll as 16-bit values. Unlike the
    rest of the disc, these files are little-endian."""
    lvl.camera_frames = []
    path = os.path.join(data_dir, "..", "CINDATA", lvl.name + ".CIN")
    if not os.path.exists(path):
        return
    data = open(path, "rb").read()
    count = struct.unpack_from("<H", data)[0]
    if len(data) != 2 + count * 16:
        raise ValueError(f"{path}: {len(data)} bytes for {count} frames")
    lvl.camera_frames = [struct.unpack_from("<8h", data, 2 + i * 16) for i in range(count)]


def move_lara(lvl, room_index, sector=None):
    room = lvl.rooms[room_index]
    best = None
    for sx in range(1, room.x_sectors - 1):
        for sz in range(1, room.z_sectors - 1):
            if sector and (sx, sz) != sector:
                continue
            _, box, below, floor, _, _ = room.sectors[sx * room.z_sectors + sz]
            if box == 0xFFFF or below != 0xFF or floor == -127:
                continue
            d = (sx - room.x_sectors / 2) ** 2 + (sz - room.z_sectors / 2) ** 2
            if best is None or d < best[0]:
                best = (d, sx, sz, floor)
    if best is None:
        raise ValueError(f"room {room_index}: no walkable sector")
    _, sx, sz, floor = best
    lara = next(it for it in lvl.items if it["type"] == ITEM["LARA"])
    lara.update(room=room_index, x=room.x + sx * 1024 + 512, y=floor * 256, z=room.z + sz * 1024 + 512)


def main():
    data_dir, name, out_path = sys.argv[1:4]
    lvl = trsat.load(data_dir, name)
    borrow_title_glyphs(lvl, data_dir)
    read_camera_frames(lvl, data_dir)
    if os.environ.get("LARA_START"):
        start = [int(v) for v in os.environ["LARA_START"].split(":")]
        move_lara(lvl, start[0], tuple(start[1:3]) if len(start) >= 3 else None)
        if len(start) == 4:
            lara = next(it for it in lvl.items if it["type"] == ITEM["LARA"])
            lara["rotation"] = (round(start[3] / 90) * 0x4000) & 0xFFFF
    packer = Packer(lvl)
    data = packer.pack()
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(data)
    tex_path = os.path.splitext(out_path)[0] + ".TEX"
    with open(tex_path, "wb") as f:
        f.write(packer.textures.blob)
    sub_path = os.path.splitext(out_path)[0] + ".SUB"
    with open(sub_path, "wb") as f:
        f.write(packer.textures.sub)
    sfx_path = os.path.splitext(out_path)[0] + ".SFX"
    with open(sfx_path, "wb") as f:
        f.write(packer.sfx)
    with open(os.path.splitext(out_path)[0] + ".ITX", "wb") as f:
        f.write(lvl.item_tex_data)
    palettes = packer.textures.remastered_palettes()
    with open(os.path.splitext(out_path)[0] + ".PAL", "wb") as f:
        f.write(struct.pack(">I", len(palettes)))
        for lut, colors in palettes:
            f.write(struct.pack(">I16H", lut, *colors))
    print(f"{name}: {len(data)} bytes -> {out_path}, {len(packer.textures.blob)} bytes -> {tex_path}, "
          f"{len(lvl.room_tex_sub)} bytes -> {sub_path}, {len(packer.sfx)} bytes -> {sfx_path}, "
          f"{len(packer.textures.entries)} textures, {len(palettes)} colour tables remastered")


if __name__ == "__main__":
    main()
