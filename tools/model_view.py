"""HD viewer of the LEVEL1 wolf and bridges beside the simplifications being
considered for them (build/geo/model_view.html, a page with the assets
inside: game data, so it stays in build/). For judging what a change does to
an asset before trying it on the Saturn.

The wolf: every animation, with toggles that freeze joints to the standing
pose (what merging them into their parent's mesh would draw) and draw the
lower legs as cards that turn to the camera. The bridges: the twelve plank
items, the rope statics and the rooms around them (in their textures' mean
colours), with toggles that drop the planks' inside faces, draw each plank
face as one quad with its tiles combined, and draw the ropes as cards.

Textures are the object textures as VDP1 draws them: the texture's corners
on the quad's corners A B C D, colour 0 see-through on transparent faces,
textured triangles as the parallelogram v0, v1, v1 + v2 - v0 with the
triangle variant whose end codes (colour 15) trim it.

usage: python tools/model_view.py <DATA dir> <out.html>

The page's address sets its toggles, e.g. model_view.html#jaw,ears,cards,
hold=typical,anim=7,t=0.5,pause or #bridge,tiles,ropes,pixels, and
cam=x:y:z:tx:ty:tz places the camera (game coordinates).
"""
import base64
import io
import json
import math
import os
import struct
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trsat  # noqa: E402
from pack_level import ITEM, TRIANGLE_ARRANGEMENT, TRIANGLE_VARIANT, TextureTable, texture_average  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
COLOUR_TYPES = (trsat.MESH_T_COLOR, trsat.MESH_R_COLOR)
TRANSP_TYPES = (trsat.MESH_T_TEX_TRANSP, trsat.MESH_R_TRANSP, trsat.MESH_R_FLIP_TRANSP)
FLIP_TYPES = (trsat.MESH_R_FLIP_TEX, trsat.MESH_R_FLIP_TRANSP)

WOLF_STATES = "NONE STOP WALK RUN JUMP STALK ATTACK HOWL SLEEP GROWL TURN DEATH BITE".split()
WOLF_JOINTS = ["hips", "chest", "neck", "head", "ear R", "ear L", "jaw",
               "shoulder R", "forearm R", "fore lower R", "fore paw R",
               "shoulder L", "forearm L", "fore lower L", "fore paw L",
               "thigh R", "shin R", "hind lower R", "hind paw R",
               "thigh L", "shin L", "hind lower L", "hind paw L",
               "tail 1", "tail 2", "tail 3"]
# joints frozen to the standing pose when merged into their parent
WOLF_MERGES = {"ears": [4, 5], "jaw": [6], "paws": [10, 14, 18, 22], "tail": [24, 25], "chest": [1]}
WOLF_CARDS = [8, 9, 10, 12, 13, 14, 16, 17, 18, 20, 21, 22]
BRIDGE_ROOMS = (13, 14)
CONTEXT_ROOMS = (13, 14, 16)


def rgb(c):
    return [round((c & 31) * 255 / 31), round(((c >> 5) & 31) * 255 / 31), round(((c >> 10) & 31) * 255 / 31)]


class Atlas:
    """Object texture images, each once per (texture, variant, see-through
    colour 0, end codes), packed into one picture; a white tile for colour
    faces."""

    def __init__(self, lvl):
        self.lvl = lvl
        self.keys = {}
        self.images = [(2, 2, bytes([255] * 16))]

    def object(self, tex, variant, akill, ecd):
        key = (tex, variant, akill, ecd)
        if key not in self.keys:
            t = self.lvl.object_textures[tex]
            tile = t["tile"] if variant == 0 else t["sub"][variant - 1]
            self.keys[key] = len(self.images)
            self.images.append(self.decode(self.lvl.object_tex_data, t, tile, akill, ecd))
        return self.keys[key]

    @staticmethod
    def decode(data, t, tile, akill, ecd):
        w, h = t["w"], t["h"]
        pix = tile * 8
        lut = ((t["clut"] + tile) & 0xFFFF) * 8
        colors = struct.unpack_from(">16H", data, lut)
        out = bytearray()
        for b in data[pix:pix + w * h // 2]:
            for idx in (b >> 4, b & 15):
                out += bytes(rgb(colors[idx]))
                out.append(0 if (akill and idx == 0) or (ecd and idx == 15) else 255)
        return w, h, bytes(out)

    def add_image(self, w, h, rgba):
        self.images.append((w, h, rgba))
        return len(self.images) - 1

    def mean(self, index):
        w, h, px = self.images[index]
        n = r = g = b = 0
        for i in range(0, len(px), 4):
            if px[i + 3]:
                r += px[i]; g += px[i + 1]; b += px[i + 2]; n += 1
        return [r // max(n, 1), g // max(n, 1), b // max(n, 1)]

    def pack(self):
        """Shelves, a texel of each image's edge repeated around it; returns
        the PNG and each image's rectangle in texels (x, y, w, h)."""
        order = sorted(range(len(self.images)), key=lambda i: -self.images[i][1])
        width, x, y, shelf = 1024, 0, 0, 0
        rects = [None] * len(self.images)
        for i in order:
            w, h, _ = self.images[i]
            if x + w + 2 > width:
                x, y, shelf = 0, y + shelf, 0
            rects[i] = (x + 1, y + 1, w, h)
            x += w + 2
            shelf = max(shelf, h + 2)
        height = 1 << max(1, math.ceil(math.log2(y + shelf)))
        pic = Image.new("RGBA", (width, height))
        for i, (w, h, px) in enumerate(self.images):
            img = Image.frombytes("RGBA", (w, h), px)
            rx, ry = rects[i][:2]
            padded = Image.new("RGBA", (w + 2, h + 2))
            padded.paste(img.resize((w + 2, h + 2), Image.NEAREST))
            padded.paste(img, (1, 1))
            pic.paste(padded, (rx - 1, ry - 1))
        buf = io.BytesIO()
        pic.save(buf, "PNG", optimize=True)
        return base64.b64encode(buf.getvalue()).decode(), width, height, rects


def mesh_json(mesh, atlas):
    """Vertices, lighting and faces. A face's corners c are A B C D (vertex
    indices, -1 for a parallelogram's fourth corner, which is c1 + c2 - c0 of
    p); t its image, col its colour, fh its texture mirrored."""
    faces = []
    for ftype, idx, flags in mesh.faces:
        f = {}
        transp = ftype in TRANSP_TYPES
        if ftype in COLOUR_TYPES:
            f["col"] = rgb(flags)
            f["t"] = 0
            f["c"] = list(idx) if len(idx) == 4 else [idx[0], idx[1], idx[2], idx[2]]
        elif len(idx) == 4:
            f["c"] = list(idx)
            f["t"] = atlas.object(flags >> 4, 0, transp, False)
            if ftype in FLIP_TYPES:
                f["fh"] = 1
        else:
            code = flags >> 12
            v0, v1, v2 = idx
            arrangement = TRIANGLE_ARRANGEMENT[code]
            if arrangement & 2:
                v1, v2 = v2, v1
            f["p"] = [v0, v1, v2]
            f["c"] = [v1, -1, v2, v0] if arrangement & 1 else [v0, v1, -1, v2]
            f["t"] = atlas.object(flags & 0x0FFF, TRIANGLE_VARIANT[code], transp, True)
        faces.append(f)
    out = {"v": [c for v in mesh.vertices for c in v], "f": faces}
    if mesh.normals:
        out["n"] = [round(c / math.sqrt(max(1, sum(q * q for q in n))), 3) for n in mesh.normals for c in n]
    else:
        out["s"] = [round(max(0.0, min(1.0, 1 - i / 8191)), 3) for i in mesh.intensities]
    return out


def frame_at(lvl, word, count):
    f = lvl.frames
    s16 = lambda v: v - 65536 if v >= 32768 else v
    pos = [s16(f[word + 6 + i]) for i in range(3)]
    rots = []
    for j in range(count):
        s0, s1 = f[word + 10 + 2 * j], f[word + 11 + 2 * j]
        rots += [(s0 & 0x3FF0) << 2 & 0xFFFF, ((s0 & 0xF) << 12 | (s1 & 0xFC00) >> 4) & 0xFFFF, (s1 & 0x3FF) << 6]
    return {"p": pos, "r": rots}


def model_json(lvl, meshes, atlas, type_, states=None):
    model = next(m for m in lvl.models if m["type"] == type_)
    starts = sorted({m["anim"] for m in lvl.models})
    later = [a for a in starts if a > model["anim"]]
    anims = range(model["anim"], min(later) if later else len(lvl.anims))
    count = model["count"]
    parents, offsets, stack, parent = [-1], [[0, 0, 0]], [], 0
    for j in range(1, count):
        flags, x, y, z = lvl.nodes[model["node"] + (j - 1) * 4: model["node"] + j * 4]
        if flags & 1:
            parent = stack.pop() if stack else 0
        if flags & 2:
            stack.append(parent)
        parents.append(parent)
        offsets.append([x, y, z])
        parent = j
    out = {"parents": parents, "offsets": offsets, "anims": [],
           "meshes": [mesh_json(meshes[model["start"] + j], atlas) for j in range(count)]}
    for ai in anims:
        a = lvl.anims[ai]
        n = (a["frame_end"] - a["frame_begin"]) // a["frame_rate"] + 1
        size = 10 + 2 * count
        state = a["state"]
        name = states[state] if states and state < len(states) else str(state)
        out["anims"].append({"name": f"{ai - model['anim']}: {name}", "state": name, "rate": a["frame_rate"],
                             "next": a["next_anim"] - model["anim"],
                             "kf": [frame_at(lvl, a["frame_offset"] // 2 + k * size, count) for k in range(n)]})
    return out


def card_of(mesh, atlas):
    """A box-like mesh as a card: the middle of each end of its long side,
    its thickness across, and its faces' mean colour by area."""
    xs = list(zip(*mesh.vertices))
    lo, hi = [min(a) for a in xs], [max(a) for a in xs]
    ext = [h - l for l, h in zip(lo, hi)]
    axis = ext.index(max(ext))
    mid = [(l + h) / 2 for l, h in zip(lo, hi)]
    e0, e1 = list(mid), list(mid)
    e0[axis], e1[axis] = lo[axis], hi[axis]
    width = sum(e for i, e in enumerate(ext) if i != axis) / 2
    total, colour = 0.0, [0.0, 0.0, 0.0]
    for ftype, idx, flags in mesh.faces:
        v = [mesh.vertices[i] for i in idx]
        a = 0.5 * norm(cross(sub(v[1], v[0]), sub(v[2], v[0])))
        if len(v) == 4:
            a += 0.5 * norm(cross(sub(v[2], v[0]), sub(v[3], v[0])))
        if ftype in COLOUR_TYPES:
            c = rgb(flags)
        else:
            tex = flags >> 4 if len(idx) == 4 else flags & 0x0FFF
            c = atlas.mean(atlas.object(tex, 0, False, False))
        total += a
        colour = [q + a * k for q, k in zip(colour, c)]
    return {"e0": e0, "e1": e1, "w": width, "col": [round(q / max(total, 1)) for q in colour]}


def sub(a, b): return [x - y for x, y in zip(a, b)]
def cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
def dot(a, b): return sum(x * y for x, y in zip(a, b))
def norm(a): return math.sqrt(dot(a, a))


def plank_simplified(mesh, atlas, mj):
    """The plank with its inside faces gone and each of its faces' tiles as
    one quad: faces sharing a plane and a facing become one quad over their
    rectangle, with their textures placed side by side as they lie."""
    centre = [sum(c) / len(mesh.vertices) for c in zip(*mesh.vertices)]
    groups = {}
    inside = []
    for fi, (ftype, idx, flags) in enumerate(mesh.faces):
        v = [mesh.vertices[i] for i in idx]
        n = cross(sub(v[1], v[0]), sub(v[2], v[0]))     # points away from the side drawn
        n = [round(c / norm(n), 3) for c in n]
        # drawn side facing the plank's middle: always behind another face
        if dot(sub(centre, v[0]), [-c for c in n]) > 1:
            inside.append(fi)
            continue
        # tilted planks' tiles miss their plane by a unit or two
        for (gn, gd), fis in groups.items():
            if dot(gn, n) > 0.995 and abs(dot(gn, v[0]) - gd) < 8:
                fis.append(fi)
                break
        else:
            groups[(tuple(n), dot(n, v[0]))] = [fi]
    faces = []
    for fis in groups.values():
        faces.append(merge_tiles(mesh, [mesh.faces[i] for i in fis], [mj["f"][i] for i in fis], atlas))
    return {"v": mj["v"], "n": mj.get("n"), "s": mj.get("s"), "f": faces}, inside


def merge_tiles(mesh, faces, fjs, atlas):
    if len(faces) == 1:
        return fjs[0]
    P = lambda i: mesh.vertices[i]
    a0 = P(faces[0][1][0])
    ab = sub(P(faces[0][1][1]), a0)
    ad = sub(P(faces[0][1][3]), a0)
    kinds = {f[0] for f in faces}
    if len(kinds) != 1 or len(faces[0][1]) != 4 or faces[0][0] in COLOUR_TYPES:
        raise ValueError("only textured quads of one kind are combined")
    transp, flip = faces[0][0] in TRANSP_TYPES, faces[0][0] in FLIP_TYPES
    cells = []
    for ftype, idx, flags in faces:
        if max(map(abs, sub(sub(P(idx[1]), P(idx[0])), ab) + sub(sub(P(idx[3]), P(idx[0])), ad))) > 4:
            raise ValueError("tiles laid differently")
        # d = col * ab + row * ad: the sides of tilted planks are sheared
        d = sub(P(idx[0]), a0)
        g00, g01, g11 = dot(ab, ab), dot(ab, ad), dot(ad, ad)
        r0_, r1_ = dot(d, ab), dot(d, ad)
        det = g00 * g11 - g01 * g01
        cells.append((round((r0_ * g11 - r1_ * g01) / det), round((r1_ * g00 - r0_ * g01) / det), flags >> 4, idx))
    c0, r0 = min(c[0] for c in cells), min(c[1] for c in cells)
    cols = max(c[0] for c in cells) - c0 + 1
    rows = max(c[1] for c in cells) - r0 + 1
    if cols * rows != len(cells):
        raise ValueError("tiles leave holes")
    t0 = atlas.lvl.object_textures[cells[0][2]]
    w, h = t0["w"], t0["h"]
    pic = Image.new("RGBA", (w * cols, h * rows))
    corner = {}
    for col, row, tex, idx in cells:
        tw, th, px = atlas.images[atlas.object(tex, 0, transp, False)]
        if (tw, th) != (w, h):
            raise ValueError("tiles of different sizes")
        img = Image.frombytes("RGBA", (w, h), px)
        if flip:    # each tile mirrored in its own cell, the whole drawn unmirrored
            img = img.transpose(Image.FLIP_LEFT_RIGHT)
        pic.paste(img, ((col - c0) * w, (row - r0) * h))
        corner[(col - c0, row - r0)] = idx
    a = corner[(0, 0)][0]
    b = corner[(cols - 1, 0)][1]
    c = corner[(cols - 1, rows - 1)][2]
    d = corner[(0, rows - 1)][3]
    return {"c": [a, b, c, d], "t": atlas.add_image(w * cols, h * rows, pic.tobytes()), "tiles": len(cells)}


def rope_strands(mesh, mj):
    """The three-sided tubes of a rope static (the thick one is its post):
    per strand the middle of each ring of corners in order, the tube's
    radius and each segment's texture and faces. Vertices by index: strands
    meet at shared positions."""
    pos = mesh.vertices
    ring, strand = {}, {}

    def find(sets, i):
        while sets.setdefault(i, i) != i:
            sets[i] = sets[sets[i]]
            i = sets[i]
        return i
    quads = [(fi, f) for fi, f in enumerate(mesh.faces) if len(f[1]) == 4]
    for fi, (ftype, idx, flags) in quads:
        e = [norm(sub(pos[idx[(k + 1) % 4]], pos[idx[k]])) for k in range(4)]
        # the two short edges go round the tube: their ends share a ring
        for k in ((0, 2) if e[0] + e[2] < e[1] + e[3] else (1, 3)):
            ring[find(ring, idx[k])] = find(ring, idx[(k + 1) % 4])
        for i in idx:
            strand[find(strand, i)] = find(strand, idx[0])
    # a tube's sides have their own corners at its middle rings, a unit or
    # so apart
    seen = list(ring)
    for a_, i in enumerate(seen):
        for j in seen[a_ + 1:]:
            if find(strand, i) == find(strand, j) and norm(sub(pos[i], pos[j])) <= 2:
                ring[find(ring, i)] = find(ring, j)
    rings = {}
    for i in list(ring):
        rings.setdefault(find(ring, i), set()).add(i)
    centre = {r: [sum(pos[i][c] for i in vs) / len(vs) for c in range(3)] for r, vs in rings.items()}
    links = {}
    for fi, (ftype, idx, flags) in quads:
        rs = tuple(sorted({find(ring, i) for i in idx}))
        if len(rs) != 2:
            raise ValueError(f"face {fi} is not one segment of a tube")
        links.setdefault(rs, []).append(fi)
    strands = {}
    for (r0, r1), fis in links.items():
        strands.setdefault(find(strand, next(iter(rings[r0]))), []).append((r0, r1, fis))
    out = []
    for segs in strands.values():
        deg = {}
        for r0, r1, _ in segs:
            deg[r0] = deg.get(r0, 0) + 1
            deg[r1] = deg.get(r1, 0) + 1
        order = [next(r for r, n in deg.items() if n == 1)]
        left = list(segs)
        seg_faces = []
        while left:
            s = next(s for s in left if order[-1] in s[:2])
            left.remove(s)
            order.append(s[1] if s[0] == order[-1] else s[0])
            seg_faces.append(s[2])
        radius = sum(norm(sub(pos[i], centre[r])) for r in order for i in rings[r]) / sum(len(rings[r]) for r in order)
        segments = []
        for fis in seg_faces:
            ftype, idx, flags = mesh.faces[fis[0]]
            # texture u (corner A to B) along the rope, or v
            along = norm(sub(pos[idx[1]], pos[idx[0]])) > norm(sub(pos[idx[3]], pos[idx[0]]))
            segments.append({"t": mj["f"][fis[0]]["t"], "u": 1 if along else 0, "faces": fis})
        shade = None
        if "s" in mj:
            vs = {i for r in order for i in rings[r]}
            shade = round(sum(mj["s"][i] for i in vs) / len(vs), 3)
        out.append({"pts": [[round(c) for c in centre[r]] for r in order], "r": round(radius), "seg": segments,
                    "shade": shade})
    return out


def room_json(lvl, room, means):
    vs = [c for v in room.vertices for c in (room.x + v[0], v[1], room.z + v[2])]
    shade = [round((v[3] & 31) / 16, 3) for v in room.vertices]
    faces = []
    for q in room.quads:
        faces.append({"c": list(q[:4]), "col": means[q[4]] if q[4] < len(means) else [128, 128, 128]})
    for t in room.triangles:
        faces.append({"c": [t[0], t[1], t[2], t[2]], "col": means[t[3]] if t[3] < len(means) else [128, 128, 128]})
    return {"v": vs, "s": shade, "f": faces}


def main():
    data_dir, out_path = sys.argv[1], sys.argv[2]
    lvl = trsat.load(data_dir, "LEVEL1")
    meshes = trsat.meshes(lvl)
    atlas = Atlas(lvl)

    wolf = model_json(lvl, meshes, atlas, ITEM["WOLF"], WOLF_STATES)
    wolf["names"] = WOLF_JOINTS
    wolf["merges"] = WOLF_MERGES
    stop = next(i for i, a in enumerate(wolf["anims"]) if a["state"] == "STOP" and len(a["kf"]) == 1)
    wolf["ref"] = wolf["anims"][stop]["kf"][0]["r"]
    wolf["cards"] = {str(j): card_of(meshes[next(m for m in lvl.models if m["type"] == ITEM["WOLF"])["start"] + j], atlas)
                     for j in WOLF_CARDS}

    # the bridges: plank items with their model's first frame, rope statics
    planks, plank_meshes = [], {}
    for k, it in enumerate(lvl.items):
        if it["type"] in (ITEM["BRIDGE_FLAT"], ITEM["BRIDGE_TILT_1"], ITEM["BRIDGE_TILT_2"]) and it["room"] in BRIDGE_ROOMS:
            model = next(m for m in lvl.models if m["type"] == it["type"])
            a = lvl.anims[model["anim"]]
            frame = frame_at(lvl, a["frame_offset"] // 2, 1)
            planks.append({"item": k, "mesh": model["start"], "pos": [it["x"], it["y"], it["z"]],
                           "yaw": quadrant_yaw(it["rotation"]), "frame": frame})
            plank_meshes[model["start"]] = None
    plank_out, plank_simple = {}, {}
    for mi in plank_meshes:
        mj = mesh_json(meshes[mi], atlas)
        plank_simple[str(mi)], inside = plank_simplified(meshes[mi], atlas, mj)
        for fi in inside:
            mj["f"][fi]["in"] = 1
        plank_out[str(mi)] = mj
    statics, static_meshes = [], {}
    for ri in BRIDGE_ROOMS:
        for s in lvl.rooms[ri].static_meshes:
            st = next(x for x in lvl.static_meshes if x["id"] == s["mesh_id"])
            statics.append({"id": s["mesh_id"], "mesh": st["mesh"], "pos": [s["x"], s["y"], s["z"]],
                            "yaw": quadrant_yaw(s["rotation"]),
                            "light": round(max(0.0, min(1.0, 1 - s["intensity"] / 8191)), 3)})
            static_meshes[st["mesh"]] = None
    static_out, ropes = {}, {}
    for mi in static_meshes:
        mj = mesh_json(meshes[mi], atlas)
        static_out[str(mi)] = mj
        if mi in (213, 214):
            ropes[str(mi)] = rope_strands(meshes[mi], mj)

    table = TextureTable(lvl)
    means = []
    for i in range(len(lvl.room_textures)):
        pixels, colors, _, _ = table.sources[i]
        means.append(rgb(texture_average(pixels, colors, False)))
    rooms = [room_json(lvl, lvl.rooms[ri], means) for ri in CONTEXT_ROOMS]

    png, aw, ah, rects = atlas.pack()
    camera = {"pos": [30156, 3496, 49293],      # the game's view at the ledge (build ea032c9)
              "rot": [6455, 0, 15059, 2710, 16116, -1162, -14813, 2949, 6349]}
    data = {"atlas": {"png": png, "w": aw, "h": ah, "rects": rects},
            "wolf": wolf,
            "bridge": {"planks": planks, "meshes": plank_out, "simple": plank_simple, "statics": statics,
                       "staticMeshes": static_out, "ropes": ropes, "rooms": rooms, "camera": camera}}
    template = open(os.path.join(HERE, "model_view.html"), encoding="utf-8").read()
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(template.replace("/*DATA*/null", json.dumps(data, separators=(",", ":"))))
    print(f"{out_path}: {os.path.getsize(out_path) // 1024} KB, atlas {aw}x{ah}, {len(atlas.images)} images, "
          f"wolf {len(wolf['anims'])} anims, {len(planks)} planks, {len(statics)} statics")


def quadrant_yaw(rotation):
    """A level item's or static's yaw as the game keeps it: whole quarter turns."""
    v = rotation & 0xFFFF
    v = v - 0x10000 if v >= 0x8000 else v
    return (int(v / 0x4000) * 0x4000) & 0xFFFF


if __name__ == "__main__":
    main()
