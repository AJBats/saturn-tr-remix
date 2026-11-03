"""HD viewer of the wolf, the rat and the LEVEL1 bridges beside the
simplifications being considered for them (build/geo/model_view.html, a page
with the assets inside: game data, so it stays in build/). For judging what a
change does to an asset before trying it on the Saturn.

The animals: every animation, under the game's own light, with toggles that
fuse joints to one held pose (what merging them into their parent's mesh would
draw), draw segments as cards that turn to the camera, drop the inside copies
of faces drawn both ways and join each segment's end to its parent's. The bridges: the twelve plank
items, the rope statics and the rooms around them (in their textures' mean
colours), with toggles that drop the planks' inside faces, draw each plank
face as one quad with its tiles combined, and draw the ropes as cards.

Textures are the object textures as VDP1 draws them: the texture's corners
on the quad's corners A B C D, colour 0 see-through on transparent faces,
textured triangles as the parallelogram v0, v1, v1 + v2 - v0 with the
triangle variant whose end codes (colour 15) trim it.

usage: python tools/model_view.py <DATA dir> <out.html>

The page's address sets its toggles, e.g. model_view.html#jaw,ears,cards,
hold=typical,anim=7,t=0.5,pause, #rat,lower,tailCards,earCards,join,tail=2, #croc,playAll or
#bridge,tiles,ropes,pixels, and cam=x:y:z:tx:ty:tz places the camera (game
coordinates).
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
from pack_level import ITEM, TextureTable, texture_average  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))

BRIDGE_ROOMS = (13, 14)
CONTEXT_ROOMS = (13, 14, 16)


# The rat (joints as its model has them; the water rat's has the same meshes
# and joints, and its own animations). What the viewer offers: the user's call.
RAT_JOINTS = ["body", "neck", "head", "jaw", "ear R", "ear L",
              "shoulder R", "forearm R", "fore paw R", "haunch",
              "tail 1", "tail 2", "tail 3", "tail 4", "tail 5",
              "thigh R", "shin R", "hock R", "hind paw R",
              "thigh L", "shin L", "hock L", "hind paw L",
              "shoulder L", "forearm L", "fore paw L"]
RAT_STATES = "NONE STOP CHARGE RUN BITE DEATH POSE".split()
RAT_WATER_STATES = "NONE SWIM BITE DEATH".split()
RAT_FUSES = [("ears", "Ears into the head", [4, 5]),
             ("jaw", "Jaw into the head", [3]),
             ("paws", "Paws into the lower legs", [8, 18, 22, 25]),
             ("hocks", "Hocks into the shins", [17, 21]),
             ("neck", "Neck into the body", [1]),
             ("head", "Head into the neck (the game turns it towards Lara; it would stop)", [2]),
             ("haunch", "Haunch into the body", [9])]
RAT_TAIL = ("tail", [("Tail: 5 pieces, as it is", []),
                     ("Tail: 3 pieces (tail 2 and 4 fused)", [11, 13]),
                     ("Tail: 2 pieces (tail 2, 3 and 5 fused)", [11, 12, 14]),
                     ("Tail: 1 piece", [11, 12, 13, 14])])
RAT_CARDS = [("lower", "Lower limbs as cards", [7, 8, 16, 17, 18, 20, 21, 22, 24, 25]),
             ("upper", "Upper limbs as cards", [6, 23, 15, 19]),
             ("tailCards", "Tail as cards", [10, 11, 12, 13, 14]),
             ("earCards", "Ears as cards", [4, 5])]
RAT_EAR_CARDS = [4, 5]      # upright, seen from the front
RAT_TAIL_JOINTS = [10, 11, 12, 13, 14]
RAT_NO_JOIN = [3]           # the jaw opens: its top and the head's underside are the mouth
RAT_JOIN_WIDE = {1: 20, 2: 25, 8: 25, 18: 25, 22: 25, 25: 25}     # neck and head all round, paws
RAT_CHOSEN = (["ears"], ["lower", "upper", "tailCards", "earCards"])    # the user's, 2026-11-03

# The crocodile (the land's and the water's have the same meshes and joints)
CROC_JOINTS = ["body", "chest", "upper foreleg L", "lower foreleg L", "fore paw L",
               "upper foreleg R", "lower foreleg R", "fore paw R", "snout", "lower jaw", "rear body",
               "tail 1", "tail 2", "tail 3", "tail 4",
               "thigh R", "shin R", "hind paw R", "thigh L", "shin L", "hind paw L"]
CROC_STATES = "NONE STOP RUN WALK FAST_TURN ATTACK ATTACK_2 DEATH".split()
CROC_WATER_STATES = "NONE SWIM ATTACK DEATH".split()
CROC_FUSES = [("jaw", "Jaw into the snout", [9]),
              ("paws", "Paws into the lower legs", [4, 7, 17, 20]),
              ("chest", "Chest into the body", [1]),
              ("rear", "Rear body into the body", [10])]
CROC_TAIL = ("tail", [("Tail: 4 pieces, as it is", []),
                      ("Tail: 2 pieces (tail 2 and 4 fused)", [12, 14]),
                      ("Tail: 1 piece", [12, 13, 14])])
CROC_CARDS = [("lower", "Lower limbs as cards", [3, 4, 6, 7, 16, 17, 19, 20]),
              ("upper", "Upper limbs as cards", [2, 5, 15, 18]),
              ("tailEnd", "End of the tail as cards", [13, 14]),
              ("tailCards", "Start of the tail as cards", [11, 12])]
CROC_TAIL_JOINTS = [11, 12, 13, 14]

# The gorilla
GORILLA_JOINTS = ["hips", "thigh L", "shin L", "foot L", "thigh R", "shin R", "foot R", "chest",
                  "upper arm R", "forearm R", "hand R", "upper arm L", "forearm L", "hand L", "head", "jaw"]
GORILLA_STATES = "NONE STOP WALK RUN ATTACK DEATH WARNING_1 WARNING_2 RUN_LEFT RUN_RIGHT JUMP".split()
GORILLA_FUSES = [("jaw", "Jaw into the head", [15]),
                 ("hands", "Hands into the forearms", [10, 13]),
                 ("feet", "Feet into the shins", [3, 6]),
                 ("head", "Head into the chest (the game turns it towards Lara; it would stop)", [14]),
                 ("chest", "Chest into the hips", [7])]
GORILLA_JOIN_WIDE = {14: 50}    # the head to the chest
GORILLA_CARDS = [("lower", "Shins, feet, forearms, hands as cards", [2, 3, 5, 6, 9, 10, 12, 13]),
                 ("upper", "Thighs and upper arms as cards", [1, 4, 8, 11])]

# The bear
BEAR_JOINTS = ["body", "upper foreleg L", "lower foreleg L", "fore paw L",
               "upper foreleg R", "lower foreleg R", "fore paw R",
               "thigh R", "shin R", "hind paw R", "thigh L", "shin L", "hind paw L",
               "neck", "head", "ear R", "ear L", "jaw"]
BEAR_STATES = "WALK STOP HIND RUN HOWL GROWL BITE ATTACK EAT DEATH".split()
BEAR_FUSES = [("ears", "Ears into the head", [15, 16]),
              ("jaw", "Jaw into the head", [17]),
              ("paws", "Paws into the lower legs", [3, 6, 9, 12]),
              ("neck", "Neck into the body", [13]),
              ("head", "Head into the neck (the game turns it towards Lara; it would stop)", [14])]
BEAR_CARDS = [("lower", "Lower limbs as cards", [2, 3, 5, 6, 8, 9, 11, 12]),
              ("upper", "Upper limbs as cards", [1, 4, 7, 10]),
              ("earCards", "Ears as cards", [15, 16])]
BEAR_EAR_CARDS = [15, 16]
BEAR_JOIN_WIDE = {3: 25, 6: 25, 9: 25, 12: 25, 14: 36}      # paws, the head to the neck
WOLF_JOIN_WIDE = {2: 35, 3: 32}     # the neck to the chest and the head to the neck, all round

ANIMAL_FOOT = "Counts of what each side gives the renderer; for milliseconds, run it in the game."

JOIN_REACH = 15             # units a segment's end may lie from its parent's to be joined to it
JOIN_RING = 3               # the fewest corners that make an end


def inside_copies(mesh):
    """The faces drawn twice, once each way round, whose copy faces into the
    mesh's middle: a closed box's, which nothing can ever see. A thin shape's
    copies (an ear) lie about through the middle, and a face joined to nothing
    (a row of teeth) can be seen from behind: both stay."""
    centre = [sum(c) / len(mesh.vertices) for c in zip(*mesh.vertices)]
    sets = {}
    for fi, (ftype, idx, flags) in enumerate(mesh.faces):
        sets.setdefault(tuple(sorted(idx)), []).append(fi)
    out = []
    for fis in sets.values():
        if len(fis) < 2:
            continue
        corners = set(mesh.faces[fis[0]][1])
        if not any(corners & set(f[1]) for fi, f in enumerate(mesh.faces) if fi not in fis):
            continue
        for fi in fis:
            v = [mesh.vertices[i] for i in mesh.faces[fi][1]]
            n = diet.unit(diet.cross(diet.sub(v[1], v[0]), diet.sub(v[2], v[0])))     # away from the side drawn
            if diet.dot(diet.sub(centre, v[0]), [-c for c in n]) > 1:
                out.append(fi)
    return out


def join_rings(meshes, start, parents, offsets, skip=(), wide=None):
    """Where a segment's end meets its parent's in the model's rest pose (no
    joint turned): {joint: {p: parent, map: {vertex: the parent's}, caps: its
    faces that go, pcaps: the parent's, pull: the parent's vertices moved to a
    corner, gap: how far a corner moves}}. Each corner of the end goes to the
    nearest of the parent's, each of those taken once, within JOIN_REACH; a
    joint in wide has its own reach, and a corner over shares one when the end
    has more than the parent's. What goes: the faces on those corners alone, and
    the stubs that hid the bend, a thigh's knee or a chest's cone: faces on
    those corners and vertices lying past them inside the other segment. A face
    that stays on such a vertex takes it to the nearest corner."""
    out = {}
    for j in range(1, len(parents)):
        if j in skip:
            continue
        p = parents[j]
        child, parent = meshes[start + j], meshes[start + p]
        placed = [[c + o for c, o in zip(v, offsets[j])] for v in child.vertices]
        reach = (wide or {}).get(j, JOIN_REACH)
        pairs = sorted((diet.norm(diet.sub(v, pv)), i, k) for i, v in enumerate(placed) for k, pv in enumerate(parent.vertices))
        taken, ring, gap = set(), {}, 0
        for d, i, k in pairs:
            if d > reach:
                break
            if i not in ring and k not in taken:
                taken.add(k)
                ring[i] = k
                gap = max(gap, d)
        if len(ring) < JOIN_RING:
            continue
        if j in (wide or {}):
            for d, i, k in pairs:
                if d > reach:
                    break
                if i not in ring and k in taken:
                    ring[i] = k
                    gap = max(gap, d)
        centre = mean([placed[i] for i in ring])
        others = [v for i, v in enumerate(placed) if i not in ring]
        along = diet.sub(mean(others), centre) if others else [0, 0, 0]
        ahead = lambda v: sum(a * b for a, b in zip(diet.sub(v, centre), along))  # noqa: E731

        behind = {i for i, v in enumerate(placed) if i not in ring and ahead(v) < 0 and in_box(v, parent.vertices)}
        past = {k for k, v in enumerate(parent.vertices) if k not in taken and ahead(v) > 0 and in_box(v, placed)}
        caps = [fi for fi, f in enumerate(child.faces)
                if all(i in ring or i in behind for i in f[1]) or len({ring.get(i, -1 - i) for i in f[1]}) < 3]
        pcaps = [fi for fi, f in enumerate(parent.faces) if all(k in taken or k in past for k in f[1])]

        def nearest(v):
            return min(taken, key=lambda k: diet.norm(diet.sub(v, parent.vertices[k])))
        for fi, f in enumerate(child.faces):
            if fi not in caps:
                for i in f[1]:
                    if i in behind:
                        ring[i] = nearest(placed[i])
        pull = {}
        for fi, f in enumerate(parent.faces):
            if fi not in pcaps:
                for k in f[1]:
                    if k in past:
                        pull[str(k)] = nearest(parent.vertices[k])
        out[j] = {"p": p, "map": {str(i): k for i, k in ring.items()}, "gap": round(gap), "caps": caps, "pcaps": pcaps, "pull": pull}
    return out


def mean(vs):
    return [sum(c) / len(vs) for c in zip(*vs)]


def in_box(v, vs):
    return all(min(c) <= x <= max(c) for x, c in zip(v, zip(*vs)))


class Atlas(diet.Atlas):
    """diet.Atlas's images packed into one picture, from any level's textures
    (level: those asked for from here on are that level's)."""

    def level(self, lvl):
        self.lvl = lvl
        self.keys = {}

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
    """The model's skeleton, meshes and animations, read as tools/diet.py
    reads them for the meshes the game draws."""
    model = next(m for m in lvl.models if m["type"] == type_)
    parents, offsets = diet.skeleton(lvl, model)
    out = {"parents": parents, "offsets": offsets, "anims": [],
           "meshes": [mesh_json(meshes[model["start"] + j], atlas) for j in range(model["count"])]}
    for ai in diet.model_anims(lvl, model):
        a = lvl.anims[ai]
        state = a["state"]
        name = states[state] if states and state < len(states) else str(state)
        out["anims"].append({"name": f"{ai - model['anim']}: {name}", "state": name, "rate": a["frame_rate"],
                             "next": a["next_anim"] - model["anim"], "kf": diet.keyframes(lvl, model, ai)})
    return out


def ribbon_json(meshes, start, chain, faces):
    """A chain of segments (each a tapering box from its parent's end) as one
    strip facing the camera: each segment's near end's middle and half width,
    the texture of its biggest side (faces: the viewer's, by joint) and which
    way that runs, each end's corner normals; the last's far end."""
    seg = []
    for j in chain:
        mesh = meshes[start + j]
        xs = list(zip(*mesh.vertices))
        lo, hi = [min(a) for a in xs], [max(a) for a in xs]
        ext = [h - l for l, h in zip(lo, hi)]
        axis = ext.index(max(ext))
        near = lo[axis] if abs(lo[axis]) < abs(hi[axis]) else hi[axis]
        far = hi[axis] if near == lo[axis] else lo[axis]

        def end(at):
            vs = [i for i, v in enumerate(mesh.vertices) if abs(v[axis] - at) < ext[axis] / 4]
            mid = [sum(mesh.vertices[i][k] for i in vs) / len(vs) for k in range(3)]
            spans = [max(mesh.vertices[i][k] for i in vs) - min(mesh.vertices[i][k] for i in vs)
                     for k in range(3) if k != axis]
            return mid, sum(spans) / 4, [diet.unit(mesh.normals[i]) for i in vs] if mesh.normals else []
        best, area = None, -1
        for fi, (ftype, idx, flags) in enumerate(mesh.faces):
            if len(idx) == 4 or best is None:
                v = [mesh.vertices[i] for i in idx]
                a = diet.face_area(mesh, idx) if len(idx) == 4 else 0
                if a > area or best is None:
                    best, area = fi, a
        idx = mesh.faces[best][1]
        v = [mesh.vertices[i] for i in idx]
        u_along = len(idx) == 4 and diet.norm(diet.sub(v[1], v[0])) > diet.norm(diet.sub(v[3], v[0]))
        f = faces[j]["f"][best]
        p, h, ns = end(near)
        seg.append({"j": j, "p": p, "h": h, "n": end_normal(ns), "t": f["t"], "col": f.get("col"), "u": u_along})
        tip, tip_h, tip_ns = end(far)
    return {"seg": seg, "tip": tip, "tipH": tip_h, "tipN": end_normal(tip_ns)}


def end_normal(normals):
    """As diet.card_of takes an end's: its corners' normals averaged, a unit
    vector; None when they cancel out."""
    s = [sum(n[k] for n in normals) for k in range(3)]
    return [round(c, 3) for c in diet.unit(s)] if diet.norm(s) > 0.2 else None


def pin_card(card, parents, offsets, j):
    """A card's ends on its joint's pivot and its child's, where those lie
    near them: the next card along then starts where this one ends, however
    the joint bends."""
    line = diet.sub(card["e1"], card["e0"])
    axis = max(range(3), key=lambda k: abs(line[k]))
    length = abs(line[axis])
    # e0 lies at the low end of the axis: which end is the near one?
    near, far = ("e0", "e1") if abs(card["e0"][axis]) < abs(card["e1"][axis]) else ("e1", "e0")
    if abs(card[near][axis]) < 0.3 * length:
        card[near] = [0, 0, 0]
    kids = [k for k in range(len(parents)) if parents[k] == j]
    for k in kids:
        if abs(offsets[k][axis] - card[far][axis]) < 0.3 * length:
            card[far] = list(offsets[k])
            break
    return card


def animal_json(lvl, meshes, atlas, type_, title, states, names, fuses, choices, card_groups, flat_cards, no_join, tail,
                chosen=([], []), foot=ANIMAL_FOOT, wide_join=None):
    """model_json with what the viewer offers for an animal; tail: its joints,
    which may be drawn as one ribbon; chosen: the fuses and card groups on to
    begin with; wide_join: seams that reach further than most (join_rings)."""
    out = model_json(lvl, meshes, atlas, ITEM[type_], states)
    model = next(m for m in lvl.models if m["type"] == ITEM[type_])
    start = model["start"]
    stops = [i for i, a in enumerate(out["anims"]) if a["state"] == "STOP"]
    stop = next((i for i in stops if len(out["anims"][i]["kf"]) == 1), stops[0])
    out["title"] = title
    out["defaults"] = {"fuses": chosen[0], "cards": chosen[1]}
    out["names"] = names
    out["ref"] = out["anims"][stop]["kf"][0]["r"]
    out["fuses"] = [{"key": k, "label": label, "joints": joints} for k, label, joints in fuses]
    out["choices"] = [{"key": k, "options": [{"label": label, "joints": joints} for label, joints in options]}
                      for k, options in choices]
    out["cardGroups"] = [{"key": k, "label": label, "joints": joints} for k, label, joints in card_groups]
    out["cards"] = {}
    for _, _, joints in card_groups:
        for j in joints:
            if j in flat_cards:
                card = diet.card_of(meshes[start + j], atlas, axis=1, view=2)
            else:
                card = pin_card(diet.card_of(meshes[start + j], atlas), out["parents"], out["offsets"], j)
            out["cards"][str(j)] = card_json(card, atlas)
            if j in flat_cards:
                out["cards"][str(j)]["face"] = 1
    copies = {str(j): inside_copies(meshes[start + j]) for j in range(model["count"])}
    out["insideCopies"] = {j: fis for j, fis in copies.items() if fis}
    out["join"] = join_rings(meshes, start, out["parents"], out["offsets"], no_join, wide_join)
    out["ribbon"] = ribbon_json(meshes, start, tail, out["meshes"]) if tail else None
    out["foot"] = foot
    return out


def add_water(animal, lvl, meshes, atlas, type_, states):
    """The water model's animations after the land model's: same meshes and joints."""
    water = model_json(lvl, meshes, atlas, ITEM[type_], states)
    if water["parents"] != animal["parents"] or water["offsets"] != animal["offsets"]:
        raise ValueError(f"{type_}'s skeleton isn't the land model's")
    first = len(animal["anims"])
    for a in water["anims"]:
        a["name"] = "water " + a["name"]
        a["next"] += first
    animal["anims"] += water["anims"]


def card_json(card, atlas):
    return {"e0": card["e0"], "e1": card["e1"], "w": 2 * card["half"], "col": card["col"],
            "t": atlas.add_image(*card["image"]), "n0": card["n0"], "n1": card["n1"]}


def plank_json(mesh, atlas, mj, apart):
    """The plank as mesh_json has it, its inside faces marked, and the
    simplified plank (diet.plank_parts, with the tiles apart that the packer
    keeps apart: diet.PLANK_APART)."""
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

    labels = {"ears": "Ears into the head", "jaw": "Jaw into the head", "paws": "Paws into the lower legs",
              "tail": "Tail as one piece", "chest": "Chest into the hips"}
    wolf = animal_json(lvl, meshes, atlas, "WOLF", "wolf", WOLF_STATES, WOLF_JOINTS,
                       [(k, labels[k], joints) for k, joints in WOLF_MERGES.items()] +
                       [("neck", "Neck into the chest", [2]), ("head", "Head into the neck", [3])], [],
                       [("cards", "Lower legs as cards", WOLF_LEG_CARDS), ("tailCards", "Tail as cards", WOLF_TAIL_CARDS),
                        ("earCards", "Ears as cards", WOLF_EAR_CARDS)], WOLF_EAR_CARDS, WOLF_MERGES["jaw"], WOLF_TAIL_CARDS,
                       wide_join=WOLF_JOIN_WIDE)
    bear = animal_json(lvl, meshes, atlas, "BEAR", "bear", BEAR_STATES, BEAR_JOINTS, BEAR_FUSES, [], BEAR_CARDS,
                       BEAR_EAR_CARDS, [17], None, wide_join=BEAR_JOIN_WIDE)

    # the rat: the Cistern's, with the water rat's animations after the land rat's
    rat_level = trsat.load(data_dir, "LEVEL7A")
    rat_meshes = trsat.meshes(rat_level)
    atlas.level(rat_level)
    rat = animal_json(rat_level, rat_meshes, atlas, "RAT_LAND", "rat", RAT_STATES, RAT_JOINTS, RAT_FUSES, [RAT_TAIL], RAT_CARDS,
                      RAT_EAR_CARDS, RAT_NO_JOIN, RAT_TAIL_JOINTS, RAT_CHOSEN,
                      ANIMAL_FOOT + " Today, Cistern room 90 spot, Mednafen: 4.1 ms of master mesh work for the rat seen there (issue 256).",
                      RAT_JOIN_WIDE)
    add_water(rat, rat_level, rat_meshes, atlas, "RAT_WATER", RAT_WATER_STATES)
    croc = animal_json(rat_level, rat_meshes, atlas, "CROCODILE_LAND", "crocodile", CROC_STATES, CROC_JOINTS, CROC_FUSES,
                       [CROC_TAIL], CROC_CARDS, [], [9], CROC_TAIL_JOINTS,
                       foot=ANIMAL_FOOT + " Today, Cistern room 90 spot, Mednafen: 3.5 ms of master mesh work a crocodile (issue 256).")
    add_water(croc, rat_level, rat_meshes, atlas, "CROCODILE_WATER", CROC_WATER_STATES)
    gorilla = animal_json(rat_level, rat_meshes, atlas, "GORILLA", "gorilla", GORILLA_STATES, GORILLA_JOINTS, GORILLA_FUSES, [],
                          GORILLA_CARDS, [], [15], None, wide_join=GORILLA_JOIN_WIDE)
    atlas.level(lvl)

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
    item_names = {v: k for k, v in ITEM.items()}
    for mi in plank_meshes:
        mj = mesh_json(meshes[mi], atlas)
        name = item_names[next(m["type"] for m in lvl.models if m["start"] == mi)]
        groups = diet.PLANK_APART.get((lvl.name, name), [])
        plank_simple[str(mi)] = plank_json(meshes[mi], atlas, mj, {fi: k for k, group in enumerate(groups) for fi in group})
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
            "wolf": wolf, "rat": rat, "croc": croc, "gorilla": gorilla, "bear": bear,
            "bridge": {"planks": planks, "meshes": plank_out, "simple": plank_simple, "statics": statics,
                       "staticMeshes": static_out, "ropes": ropes, "rooms": rooms, "camera": camera}}
    template = open(os.path.join(HERE, "model_view.html"), encoding="utf-8").read()
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(template.replace("/*DATA*/null", json.dumps(data, separators=(",", ":"))))
    print(f"{out_path}: {os.path.getsize(out_path) // 1024} KB, atlas {aw}x{ah}, {len(atlas.images)} images, "
          f"{len(planks)} planks, {len(statics)} statics; anims: wolf {len(wolf['anims'])}, rat {len(rat['anims'])}, "
          f"crocodile {len(croc['anims'])}, gorilla {len(gorilla['anims'])}, bear {len(bear['anims'])}")


def quadrant_yaw(rotation):
    """A level item's or static's yaw as the game keeps it: whole quarter turns."""
    v = rotation & 0xFFFF
    v = v - 0x10000 if v >= 0x8000 else v
    return (int(v / 0x4000) * 0x4000) & 0xFFFF


if __name__ == "__main__":
    main()
