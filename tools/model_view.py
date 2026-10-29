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
import diet  # noqa: E402
import trsat  # noqa: E402
from diet import (COLOUR_TYPES, FLIP_TYPES, TRANSP_TYPES, TRIANGLE_ARRANGEMENT, TRIANGLE_VARIANT,  # noqa: E402
                  WOLF_EAR_CARDS, WOLF_JOINTS, WOLF_LEG_CARDS, WOLF_MERGES, WOLF_STATES, WOLF_TAIL_CARDS,
                  frame_at, rgb)
from pack_level import ITEM, TextureTable, hinted_faces, texture_average  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))

BRIDGE_ROOMS = (13, 14)
CONTEXT_ROOMS = (13, 14, 16)


class Atlas(diet.Atlas):
    """diet.Atlas's images packed into one picture."""

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
            if arrangement & 2:
                # the game culls by the triangle as stored, then draws the
                # parallelogram, whichever way round it turned out
                f["s"] = 1
            f["t"] = atlas.object(flags & 0x0FFF, TRIANGLE_VARIANT[code], transp, True)
        faces.append(f)
    out = {"v": [c for v in mesh.vertices for c in v], "f": faces}
    if mesh.normals:
        out["n"] = [round(c / math.sqrt(max(1, sum(q * q for q in n))), 3) for n in mesh.normals for c in n]
    else:
        out["s"] = [round(max(0.0, min(1.0, 1 - i / 8191)), 3) for i in mesh.intensities]
    return out


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


def card_json(card, atlas):
    r = lambda ns: [[round(c, 3) for c in n] for n in ns]
    return {"e0": card["e0"], "e1": card["e1"], "w": 2 * card["half"], "col": card["col"],
            "t": atlas.add_image(*card["image"]), "ns0": r(card["ns0"]), "ns1": r(card["ns1"])}


def plank_json(mesh, atlas, mj, apart):
    """The plank as mesh_json has it, its inside faces marked, and the
    simplified plank (diet.plank_parts, the faces of draw order hints apart
    as the packer keeps them)."""
    inside, groups = diet.plank_parts(mesh, atlas, apart)
    for fi in inside:
        mj["f"][fi]["in"] = 1
    faces = []
    for g in groups:
        m = g["merged"]
        if m:
            faces.append({"c": m["c"], "t": atlas.add_image(*m["image"]), "tiles": m["tiles"]})
        else:
            faces += [mj["f"][fi] for fi in g["faces"]]
    return {"v": mj["v"], "n": mj.get("n"), "s": mj.get("s"), "f": faces}


def rope_json(mesh, mj):
    out = []
    for st in diet.rope_strands(mesh):
        out.append({"pts": st["pts"], "r": round(st["r"]),
                    "seg": [{"t": mj["f"][seg["faces"][0]]["t"], "u": seg["u"], "faces": seg["faces"]}
                            for seg in st["seg"]],
                    "shade": None if st["intensity"] is None else
                    round(max(0.0, min(1.0, 1 - st["intensity"] / 8191)), 3)})
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
    start = next(m for m in lvl.models if m["type"] == ITEM["WOLF"])["start"]
    wolf["cards"] = {str(j): card_json(diet.card_of(meshes[start + j], atlas), atlas)
                     for j in WOLF_LEG_CARDS + WOLF_TAIL_CARDS}
    for j in WOLF_EAR_CARDS:
        wolf["cards"][str(j)] = card_json(diet.card_of(meshes[start + j], atlas, axis=1, view=2), atlas)
    wolf["tailCards"] = WOLF_TAIL_CARDS
    wolf["earCards"] = WOLF_EAR_CARDS

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
    hinted = hinted_faces(lvl, meshes)
    for mi in plank_meshes:
        mj = mesh_json(meshes[mi], atlas)
        plank_simple[str(mi)] = plank_json(meshes[mi], atlas, mj, hinted.get(mi))
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
    rope_meshes = {st["mesh"] for st in lvl.static_meshes if st["id"] in diet.ROPE_STATICS.get(lvl.name, ())}
    for mi in static_meshes:
        mj = mesh_json(meshes[mi], atlas)
        static_out[str(mi)] = mj
        if mi in rope_meshes:
            ropes[str(mi)] = rope_json(meshes[mi], mj)

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
