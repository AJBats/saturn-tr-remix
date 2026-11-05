"""Simplified versions of a level's assets: the meshes the renderer draws in
place of the originals (tools/pack_level.py puts them in the level's mesh
store; the game's own mesh table keeps the originals, whose collision
spheres stay as they were), drawn side by side with the originals by the HD
viewer (tools/model_view.py).

The animals (ANIMALS, the user's choices in the viewer): each joined into one
shell, a segment's end sharing its parent's corners where the two meet in the
model's rest pose (join_rings), the faces that closed those ends and the stubs
that hid the bend gone, as are the inside copies of faces drawn both ways.

The wolf's ears are merged into its head and the last two tail joints into the
first, held in the standing pose; its lower legs and tail, and the rat's limbs,
tail and ears, are drawn as cards. A card is a quad from one end of a segment
to the other that the renderer places on the screen to face the camera,
textured with the segment seen from the side. Its texture runs along it, x from
its first end to its second, so that VDP1 draws its lines along the card: a few
long lines rather than many short ones. An upright card (an ear) would go thin
seen from above, so it keeps its whole length across the view.

The bridges: each plank's inside faces gone and the tiles of each of its
faces drawn as one quad, their textures placed side by side, but for the tiles
of its top, each sorting apart so that what stands on it isn't painted over, and
tiles a draw order hint (saturn/depth_hints.txt) needs apart (PLANK_APART); the rope
statics' three-sided tubes (ropes and posts) drawn as cards, for the statics
the user has looked over (ROPE_STATICS).

The geometry here is neutral: meshes as lists of vertices, lighting, faces
and cards, which the packer turns into its records and the viewer into its
page.
"""
import copy
import math
import struct

import trsat

COLOUR_TYPES = (trsat.MESH_T_COLOR, trsat.MESH_R_COLOR)
TRANSP_TYPES = (trsat.MESH_T_TEX_TRANSP, trsat.MESH_R_TRANSP, trsat.MESH_R_FLIP_TRANSP)
FLIP_TYPES = (trsat.MESH_R_FLIP_TEX, trsat.MESH_R_FLIP_TRANSP)

# Textured mesh triangles, after the original's handlers (0x0603E6D0, one per
# code in the top 3 bits of the face flags): each is drawn as the
# parallelogram v0, v1, v1 + v2 - v0, with a texture variant whose end codes
# stop every line at the triangle's far edge. Per code: the variant
# (n = sub[n - 1]) and the corner arrangement packed with the indices,
# bit 0: v0 at corner D (else A), bit 1: the other two corners swapped.
TRIANGLE_VARIANT = {0: 1, 10: 1, 2: 2, 8: 2, 4: 3, 14: 3, 6: 4, 12: 4}
TRIANGLE_ARRANGEMENT = {0: 0, 4: 0, 10: 2, 14: 2, 2: 1, 6: 1, 8: 3, 12: 3}

# The wolf (joints as its model has them)
WOLF_JOINTS = ["hips", "chest", "neck", "head", "ear R", "ear L", "jaw",
               "shoulder R", "forearm R", "fore lower R", "fore paw R",
               "shoulder L", "forearm L", "fore lower L", "fore paw L",
               "thigh R", "shin R", "hind lower R", "hind paw R",
               "thigh L", "shin L", "hind lower L", "hind paw L",
               "tail 1", "tail 2", "tail 3"]
WOLF_STATES = "NONE STOP WALK RUN JUMP STALK ATTACK HOWL SLEEP GROWL TURN DEATH BITE".split()
# joints that can be merged into their parent, held in one pose
WOLF_MERGES = {"ears": [4, 5], "jaw": [6], "paws": [10, 14, 18, 22], "tail": [24, 25], "chest": [1]}
WOLF_LEG_CARDS = [8, 9, 10, 12, 13, 14, 16, 17, 18, 20, 21, 22]
WOLF_TAIL_CARDS = [23, 24, 25]
WOLF_EAR_CARDS = [4, 5]         # upright, seen from the front
# what the game's diet draws (the user's choice, 2026-10-28)
WOLF_DIET_MERGES = ["ears", "tail"]
WOLF_DIET_CARDS = WOLF_LEG_CARDS + WOLF_TAIL_CARDS

# Where a segment's end meets its parent's: how far a corner may lie from the
# parent's to be joined to it, and the fewest corners that make an end. A joint
# of an animal's "wide" has its own reach: a seam the user asked for that the
# model's corners miss by more.
JOIN_REACH = 15
JOIN_RING = 3

# The rat's card joints, as the viewer groups them: lower limbs, upper limbs,
# tail, ears (upright, seen from the front)
RAT_LOWER_CARDS = [7, 8, 16, 17, 18, 20, 21, 22, 24, 25]
RAT_UPPER_CARDS = [6, 23, 15, 19]
RAT_TAIL_CARDS = [10, 11, 12, 13, 14]
RAT_EAR_CARDS = [4, 5]
_RAT = {"held": RAT_EAR_CARDS, "cards": RAT_LOWER_CARDS + RAT_UPPER_CARDS + RAT_TAIL_CARDS + RAT_EAR_CARDS,
        "facing": RAT_EAR_CARDS, "apart": [3], "wide": {1: 20, 2: 25, 8: 25, 18: 25, 22: 25, 25: 25}}
RAPTOR_CLAWS = [12, 13, 14, 18, 19, 20]
# What the game's diet does to each animal, by model (the user's choices,
# 2026-11-03; the wolf's cards from 2026-10-28): joints held still and merged
# into their parent ("held": a leaf chain's, renderer/pose.s), joints drawn as
# cards ("cards"), those of them that keep their length across the view
# ("facing"), segments left apart though they meet ("apart": a jaw, which
# opens) and seams reaching further than JOIN_REACH ("wide"). Of a seam: the
# only corners it takes ("only": a throat), what of the parent stays past it
# ("stay") and vertices it takes besides its ring ("attach": a crest's root on
# the ridge it stands on). "plates": joints drawn as those two faces of a flat
# box (plate_of). The raptor's and T-rex's legs stay apart: their knees and
# ankles bend too far for a seam, and fixed to a bend a leg loses its feel.
ANIMALS = {
    "WOLF": {"held": [j for k in WOLF_DIET_MERGES for j in WOLF_MERGES[k]], "cards": WOLF_DIET_CARDS,
             "apart": WOLF_MERGES["jaw"], "wide": {2: 35, 3: 32}},
    "RAT_LAND": _RAT,
    "RAT_WATER": _RAT,
    "CROCODILE_LAND": {"apart": [9]},
    "CROCODILE_WATER": {"apart": [9]},
    "GORILLA": {"apart": [15], "wide": {14: 50}},
    "BEAR": {"apart": [17], "wide": {3: 25, 6: 25, 9: 25, 12: 25, 14: 36}},
    "RAPTOR": {"held": RAPTOR_CLAWS, "cards": RAPTOR_CLAWS + [9, 10, 11, 15, 16, 17], "apart": [23, 2, 3, 4, 6, 7, 8],
               "wide": {21: 20, 22: 40}, "only": {22: [1, 5, 13, 14]}, "stay": {22: [16, 17, 18]},
               "attach": {21: {23: 17}}},
    "REX": {"held": [16, 19], "plates": {16: [1, 3], 19: [1, 3]}, "apart": [13, 2, 3, 6, 7, 15, 18], "wide": {9: 25}},
}

CARD_UNITS = 5      # model units a texel along a card, about the wolf's own textures
# A card end's normal, as long as this: a box's corner normals lean out as
# much as along the bone, so their lighting averaged round it is that of the
# along-bone half alone, about 0.58 of a whole normal's.
CARD_LIGHT = 0.58
ROPE_CARD_WIDTH = 1.6   # a tube's card, in radii: between a three-sided tube's narrowest and widest
# Half a card's width at most, in model units: faces.s keeps the width on the
# screen in sixteenths of a pixel, and its products in 32 bits, to this.
CARD_HALF_MAX = 255
# The rope statics drawn as cards, by level: those the user judged in the
# viewer (2026-10-28, the LEVEL1 bridges' posts and ropes). The detector
# finds tubes in many other statics; each is added here once it is seen.
ROPE_STATICS = {"LEVEL1": (33, 34)}
# Tiles of a plank face kept as a quad of their own, by level and plank model
# (each group's faces as the mesh has them), where a draw order hint
# (saturn/depth_hints.txt) has to name them apart from the rest of their
# face: LEVEL1's tilted planks slope through the ledge top's plane at the
# bridges, and the two tiles of their top below it are drawn behind it.
PLANK_APART = {("LEVEL1", "BRIDGE_TILT_2"): [[2, 3]]}
# The Caves' hanging vines: static 8 a hollow square tube of four curtains, static 6 one curtain. A curtain is 12
# see-through tiles, more than half empty, each there twice, one for each side. VDP1 walks a quad's empty texels as it
# does its filled ones, so each side becomes tall strips round what's filled (vine_strips): the user's idea.
# The renderer leaves out a strip with a corner near the camera (FACE_NEAR_CUT, frame.cpp): the user's idea too.
VINE_STATICS = {"LEVEL1": (6, 8)}
VINE_STRIP = 16     # texels across: a near one goes in pieces small enough to pass for the near plane
VINE_GAP = 12       # rows


def rgb(c):
    return [round((c & 31) * 255 / 31), round(((c >> 5) & 31) * 255 / 31), round(((c >> 10) & 31) * 255 / 31)]


def rgb555(c):
    return 0x8000 | (round(c[2] * 31 / 255) << 10) | (round(c[1] * 31 / 255) << 5) | round(c[0] * 31 / 255)


def sub(a, b): return [x - y for x, y in zip(a, b)]
def add(a, b): return [x + y for x, y in zip(a, b)]
def cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
def dot(a, b): return sum(x * y for x, y in zip(a, b))
def norm(a): return math.sqrt(dot(a, a))
def unit(a): return [c / (norm(a) or 1) for c in a]


class Atlas:
    """Object texture images as RGBA, each once per (texture, variant,
    see-through colour 0, end codes), and images made here; a white tile
    first, for colour faces."""

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

    def indexed(self, tex):
        """An object texture's 4-bit texels (one per byte, rows from the
        top), its size and its 16-colour table."""
        t = self.lvl.object_textures[tex]
        data = self.lvl.object_tex_data
        pix = t["tile"] * 8
        lut = ((t["clut"] + t["tile"]) & 0xFFFF) * 8
        texels = [q for b in data[pix:pix + t["w"] * t["h"] // 2] for q in (b >> 4, b & 15)]
        return t["w"], t["h"], texels, list(struct.unpack_from(">16H", data, lut))

    def mean(self, index):
        w, h, px = self.images[index]
        n = r = g = b = 0
        for i in range(0, len(px), 4):
            if px[i + 3]:
                r += px[i]; g += px[i + 1]; b += px[i + 2]; n += 1
        return [r // max(n, 1), g // max(n, 1), b // max(n, 1)]


def quantize(w, h, rgba, colours=15):
    """An RGBA image as 4-bit texels and a 16-colour table: colour 0 for
    see-through texels, the image's colours in the rest (the most used, each
    texel then the nearest), as VDP1 draws a texture with SPD off."""
    counts = {}
    for i in range(0, len(rgba), 4):
        if rgba[i + 3]:
            c = rgb555(rgba[i:i + 3])
            counts[c] = counts.get(c, 0) + 1
    table = sorted(counts, key=lambda c: -counts[c])[:colours]
    chans = [((c & 31), (c >> 5) & 31, (c >> 10) & 31) for c in table]
    texels = []
    for i in range(0, len(rgba), 4):
        if not rgba[i + 3]:
            texels.append(0)
            continue
        c = rgb555(rgba[i:i + 3])
        if c in table:
            texels.append(1 + table.index(c))
            continue
        q = ((c & 31), (c >> 5) & 31, (c >> 10) & 31)
        texels.append(1 + min(range(len(chans)), key=lambda k: sum((a - b) ** 2 for a, b in zip(q, chans[k]))))
    pixels = bytes((texels[i] << 4) | texels[i + 1] for i in range(0, len(texels), 2))
    return pixels, [0] + table + [0] * (colours - len(table))


# --- frames and poses --------------------------------------------------------

def frame_at(lvl, word, count):
    """A frame of the FRAMES stream: its offset and each joint's rotation
    (16-bit angles x, y, z, as matrixFrame decodes them)."""
    f = lvl.frames
    s16 = lambda v: v - 65536 if v >= 32768 else v
    pos = [s16(f[word + 6 + i]) for i in range(3)]
    rots = []
    for j in range(count):
        s0, s1 = f[word + 10 + 2 * j], f[word + 11 + 2 * j]
        rots += [(s0 & 0x3FF0) << 2 & 0xFFFF, ((s0 & 0xF) << 12 | (s1 & 0xFC00) >> 4) & 0xFFFF, (s1 & 0x3FF) << 6]
    return {"p": pos, "r": rots}


def model_anims(lvl, model):
    starts = sorted({m["anim"] for m in lvl.models})
    later = [a for a in starts if a > model["anim"]]
    return range(model["anim"], min(later) if later else len(lvl.anims))


def keyframes(lvl, model, ai):
    a = lvl.anims[ai]
    n = (a["frame_end"] - a["frame_begin"]) // a["frame_rate"] + 1
    size = 10 + 2 * model["count"]
    return [frame_at(lvl, a["frame_offset"] // 2 + k * size, model["count"]) for k in range(n)]


def skeleton(lvl, model):
    """Each joint's parent and offset from it, the node flags' push and pop
    followed."""
    parents, offsets, stack, parent = [-1], [[0, 0, 0]], [], 0
    for j in range(1, model["count"]):
        flags, x, y, z = lvl.nodes[model["node"] + (j - 1) * 4: model["node"] + j * 4]
        if flags & 1:
            parent = stack.pop() if stack else 0
        if flags & 2:
            stack.append(parent)
        parents.append(parent)
        offsets.append([x, y, z])
        parent = j
    return parents, offsets


def standing_pose(lvl, model):
    """Each joint's rotation in the model's standing animation (state 1, one
    frame); a model without one (a water rat), its first frame."""
    for ai in model_anims(lvl, model):
        a = lvl.anims[ai]
        if a["state"] == 1 and a["frame_begin"] == a["frame_end"]:
            return keyframes(lvl, model, ai)[0]["r"]
    return keyframes(lvl, model, model["anim"])[0]["r"]


def rot(ax, ay, az):
    """matrixRotateYXZ: R = Ry Rx Rz, 16-bit angles."""
    def sc(a):
        t = a * 2 * math.pi / 65536
        return math.sin(t), math.cos(t)
    sy, cy = sc(ay); sx, cx = sc(ax); sz, cz = sc(az)
    Ry = [[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]]
    Rx = [[1, 0, 0], [0, cx, -sx], [0, sx, cx]]
    Rz = [[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]]
    return matmul(matmul(Ry, Rx), Rz)


def matmul(A, B):
    return [[sum(A[i][k] * B[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def apply(M, v):
    R, t = M
    return [t[i] + sum(R[i][k] * v[k] for k in range(3)) for i in range(3)]


def rotate(M, v):
    return [sum(M[0][i][k] * v[k] for k in range(3)) for i in range(3)]


def held(parents, offsets, pose, joint, ancestor):
    """The transform from joint's space to ancestor's with the joints between
    held in pose (each a 16-bit angle triple)."""
    chain = []
    j = joint
    while j != ancestor:
        chain.append(j)
        j = parents[j]
    M = ([[1, 0, 0], [0, 1, 0], [0, 0, 1]], [0, 0, 0])
    for j in reversed(chain):
        R = rot(*pose[j * 3:j * 3 + 3])
        M = (matmul(M[0], R), apply(M, offsets[j]))
    return M


# --- cards -------------------------------------------------------------------

def face_area(mesh, idx):
    v = [mesh.vertices[i] for i in idx]
    a = 0.5 * norm(cross(sub(v[1], v[0]), sub(v[2], v[0])))
    if len(v) == 4:
        a += 0.5 * norm(cross(sub(v[2], v[0]), sub(v[3], v[0])))
    return a


def plate_of(mesh, keep):
    """A flat box as a flat plate: keep's two faces, opposite each other, drawn
    on the plane between them, each seen from its own side, its own texture and
    its place in 3D; the rest of the box gone. Four vertices, each the middle of
    a corner of one face and the nearest of the other's."""
    plate = copy.copy(mesh)
    first, second = mesh.faces[keep[0]], mesh.faces[keep[1]]
    pair = {}
    for i in first[1]:
        pair[i] = min(second[1], key=lambda k: norm(sub(mesh.vertices[i], mesh.vertices[k])))
    order = list(first[1])
    at = {i: n for n, i in enumerate(order)}
    at.update({pair[i]: at[i] for i in order})
    def middle(values, i):
        return tuple(round((a + b) / 2) for a, b in zip(values[i], values[pair[i]]))
    plate.vertices = [middle(mesh.vertices, i) for i in order]
    if mesh.normals:
        plate.normals = [middle(mesh.normals, i) for i in order]
    if mesh.intensities:
        plate.intensities = [(mesh.intensities[i] + mesh.intensities[pair[i]]) // 2 for i in order]
    plate.faces = [(f[0], tuple(at[i] for i in f[1]), f[2]) for f in (first, second)]
    return plate


def card_of(mesh, atlas, axis=None, view=None):
    """A box-like mesh as a card: the middle of each end of its long side
    (or of the given axis), half its thickness across, its faces' mean
    colour by area, an image of it seen from the side (along x, or z when x
    is its length, or along the given view) shaded a little across so the
    flat card reads as round, and each end's light: its corners' normals
    averaged (along the bone, for a box), or None, and the corners' normals
    themselves (ns0, ns1)."""
    xs = list(zip(*mesh.vertices))
    lo, hi = [min(a) for a in xs], [max(a) for a in xs]
    ext = [h - l for l, h in zip(lo, hi)]
    if axis is None:
        axis = ext.index(max(ext))
    mid = [(l + h) / 2 for l, h in zip(lo, hi)]
    e0, e1 = list(mid), list(mid)
    e0[axis], e1[axis] = lo[axis], hi[axis]
    width = sum(e for i, e in enumerate(ext) if i != axis) / 2
    total, colour = 0.0, [0.0, 0.0, 0.0]
    for ftype, idx, flags in mesh.faces:
        a = face_area(mesh, idx)
        if ftype in COLOUR_TYPES:
            c = rgb(flags)
        else:
            tex = flags >> 4 if len(idx) == 4 else flags & 0x0FFF
            c = atlas.mean(atlas.object(tex, 0, False, False))
        total += a
        colour = [q + a * k for q, k in zip(colour, c)]
    if view is None:
        view = 0 if axis != 0 else 2
    across = 3 - axis - view
    image = bake_side(mesh, atlas, view, across, axis, lo, hi)
    ends = [[0.0, 0.0, 0.0], [0.0, 0.0, 0.0]]
    if mesh.normals:
        for v, n in zip(mesh.vertices, mesh.normals):
            end = ends[0 if v[axis] < mid[axis] else 1]
            for k in range(3):
                end[k] += n[k] / max(1.0, norm(n))
    normals = [unit(e) if norm(e) > 0.2 else None for e in ends]
    corners = [[], []]
    if mesh.normals:
        for v, n in zip(mesh.vertices, mesh.normals):
            corners[0 if v[axis] < mid[axis] else 1].append(unit(n))
    return {"e0": e0, "e1": e1, "half": width / 2, "col": [round(q / max(total, 1)) for q in colour],
            "image": image, "n0": normals[0], "n1": normals[1], "ns0": corners[0], "ns1": corners[1]}


def bake_side(mesh, atlas, view, across, along, lo, hi):
    """The mesh's faces as seen from +view, orthographic: an RGBA image with
    x along the card from its low end, about a texel every CARD_UNITS and a
    multiple of 8 long (VDP1's textures are a multiple of 8 wide), and 8
    texels across."""
    w = max(8, -(-round((hi[along] - lo[along]) / CARD_UNITS) // 8) * 8)
    h = 8
    faces = []
    for ftype, idx, flags in mesh.faces:
        P = [mesh.vertices[i] for i in idx]
        if ftype in COLOUR_TYPES:
            faces.append((P[:3] if len(P) == 3 else P, None, rgb(flags), False))
        elif len(idx) == 4:
            faces.append((P, atlas.object(flags >> 4, 0, ftype in TRANSP_TYPES, False), None, ftype in FLIP_TYPES))
        else:
            code = flags >> 12
            v0, v1, v2 = idx
            arrangement = TRIANGLE_ARRANGEMENT[code]
            if arrangement & 2:
                v1, v2 = v2, v1
            p0, p1, p2 = (mesh.vertices[i] for i in (v0, v1, v2))
            x = [b + c - a for a, b, c in zip(p0, p1, p2)]
            quad = [p1, x, p2, p0] if arrangement & 1 else [p0, p1, x, p2]
            faces.append((quad, atlas.object(flags & 0x0FFF, TRIANGLE_VARIANT[code], ftype in TRANSP_TYPES, True),
                          None, False))
    out = bytearray()
    for i in range(h):
        c = lo[across] + (i + 0.5) / h * (hi[across] - lo[across])
        for j in range(w):
            a = lo[along] + (j + 0.5) / w * (hi[along] - lo[along])
            best = None
            for P, img, col, flip in faces:
                hit = cover(P, across, along, c, a)
                if not hit:
                    continue
                depth, u, v = hit
                if best and depth <= best[0]:
                    continue
                if img is None:
                    best = (depth, col)
                    continue
                tw, th, px = atlas.images[img]
                tu = 1 - u if flip else u
                k = (min(th - 1, int(v * th)) * tw + min(tw - 1, int(tu * tw))) * 4
                if px[k + 3]:
                    best = (depth, list(px[k:k + 3]))
            if best is None:
                out += bytes(4)
                continue
            shade = 0.92 + 0.2 * math.sin(math.pi * (i + 0.5) / h)
            out += bytes(min(255, round(q * shade)) for q in best[1]) + bytes([255])
    return w, h, bytes(out)


def transposed(w, h, texels, flip):
    """A texture's 4-bit texels turned so that its rows run along x: w x h
    becomes h x w, h stretched to a multiple of 8 texels (VDP1's textures are
    a multiple of 8 wide); flip mirrors the original across (a face drawn
    flipped). Returns the new width, height and texels, a byte each."""
    nw = -(-h // 8) * 8
    out = []
    for y in range(w):
        u = w - 1 - y if flip else y
        for x in range(nw):
            out.append(texels[(x * h // nw) * w + u])
    return nw, w, out


def cover(P, across, along, c, a):
    """Where the point (c, a) falls on face P seen along the third axis: the
    face's depth there and its texture coordinates (A at 0,0, B at 1,0, D at
    0,1, each half of the quad its own triangle), or None."""
    view = 3 - across - along
    tris = [((0, 1, 2), ((0, 0), (1, 0), (1, 1)))]
    if len(P) == 4:
        tris.append(((0, 2, 3), ((0, 0), (1, 1), (0, 1))))
    for (i0, i1, i2), uv in tris:
        x0, y0 = P[i0][across], P[i0][along]
        x1, y1 = P[i1][across], P[i1][along]
        x2, y2 = P[i2][across], P[i2][along]
        d = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
        if abs(d) < 1e-9:
            continue
        l0 = ((y1 - y2) * (c - x2) + (x2 - x1) * (a - y2)) / d
        l1 = ((y2 - y0) * (c - x2) + (x0 - x2) * (a - y2)) / d
        l2 = 1 - l0 - l1
        if min(l0, l1, l2) < -1e-6:
            continue
        depth = l0 * P[i0][view] + l1 * P[i1][view] + l2 * P[i2][view]
        return depth, l0 * uv[0][0] + l1 * uv[1][0] + l2 * uv[2][0], l0 * uv[0][1] + l1 * uv[1][1] + l2 * uv[2][1]
    return None


# --- planks ------------------------------------------------------------------

def plank_parts(mesh, atlas, apart=None):
    """A plank's inside faces (sides drawn facing its middle, which only show
    through the gaps between boards) and the rest in groups sharing a plane
    and a facing; a group of tiles comes with its quad (corners A B C D) and
    its textures side by side as one RGBA image. The tiles of its top, seen
    from above, each stay a group of their own. Faces with different keys in
    apart (by face index; the draw order hints naming them) never share a
    group, so that each hint still names faces of its own."""
    apart = apart or {}
    centre = [sum(c) / len(mesh.vertices) for c in zip(*mesh.vertices)]
    groups = []
    inside = []
    for fi, (ftype, idx, flags) in enumerate(mesh.faces):
        v = [mesh.vertices[i] for i in idx]
        n = unit(cross(sub(v[1], v[0]), sub(v[2], v[0])))     # points away from the side drawn
        if dot(sub(centre, v[0]), [-c for c in n]) > 1:
            inside.append(fi)
            continue
        # tilted planks' tiles miss their plane by a unit or two
        key = ("top", fi) if n[1] > 0.7 else apart.get(fi)  # n points away from the side drawn; y is down
        for g in groups:
            if g["key"] == key and dot(g["n"], n) > 0.995 and abs(dot(g["n"], v[0]) - g["d"]) < 8:
                g["faces"].append(fi)
                break
        else:
            groups.append({"n": n, "d": dot(n, v[0]), "key": key, "faces": [fi]})
    for g in groups:
        g["merged"] = merge_tiles(mesh, g["faces"], atlas) if len(g["faces"]) > 1 else None
    return inside, groups


def merge_tiles(mesh, fis, atlas):
    from PIL import Image   # only here: packing a level without planks needs no Pillow
    faces = [mesh.faces[i] for i in fis]
    P = lambda i: mesh.vertices[i]
    a0 = P(faces[0][1][0])
    ab = sub(P(faces[0][1][1]), a0)
    ad = sub(P(faces[0][1][3]), a0)
    kinds = {f[0] for f in faces}
    if len(kinds) != 1 or len(faces[0][1]) != 4 or faces[0][0] in COLOUR_TYPES:
        raise ValueError("only textured quads of one kind are combined")
    ftype = faces[0][0]
    transp, flip = ftype in TRANSP_TYPES, ftype in FLIP_TYPES
    cells = []
    for _, idx, flags in faces:
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
    if len({c[:2] for c in cells}) != len(cells):
        raise ValueError("tiles overlap")
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
    return {"c": [corner[(0, 0)][0], corner[(cols - 1, 0)][1], corner[(cols - 1, rows - 1)][2],
                  corner[(0, rows - 1)][3]],
            "image": (w * cols, h * rows, pic.tobytes()), "transp": transp, "tiles": len(cells)}


# --- rope statics ------------------------------------------------------------

def rope_strands(mesh):
    """The three-sided tubes of a rope static (the thick one is its post):
    per strand the middle of each ring of corners in order, the tube's
    radius, each segment's faces (the first gives the card its texture) and
    whether that texture's u runs along it, and the mean intensity of its
    corners. Vertices by index: strands meet at shared positions. None when
    a quad is not a segment of a tube."""
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
            return None
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
        ends = [r for r, n in deg.items() if n == 1]
        if len(ends) != 2:
            return None
        order = [ends[0]]
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
            along = norm(sub(pos[idx[1]], pos[idx[0]])) > norm(sub(pos[idx[3]], pos[idx[0]]))
            segments.append({"faces": fis, "u": 1 if along else 0})
        intensity = None
        if mesh.intensities:
            vs = {i for r in order for i in rings[r]}
            intensity = sum(mesh.intensities[i] for i in vs) / len(vs)
        out.append({"pts": [[round(c) for c in centre[r]] for r in order], "r": radius, "seg": segments,
                    "intensity": intensity})
    return out


# --- the game's diet ---------------------------------------------------------
#
# A simplified mesh for the packer: the source mesh's centre and radius (its
# collision sphere stays), vertices with normals or intensities, faces as the
# level has them (type, corners, flags, and the mesh and face they came from)
# or merged ("merged": corners, an RGBA image, and the mesh and faces they
# came from), and cards: e0, e1, half width, texture ("image": RGBA and
# whether it has see-through texels, or "indexed": its size, 4-bit texels a
# byte each, the 16-colour table and whether colour 0 is see-through), x
# along the card from e0, and each end's light (unit normals along the bone,
# or intensities).

class DietMesh:
    def __init__(self, source):
        self.center = source.center
        self.radius = source.radius
        self.vertices = []
        self.normals = [] if source.normals else None
        self.intensities = [] if source.intensities else None
        self.faces = []     # (ftype, idx, flags, source mesh, source face) or {"merged": ...}
        self.cards = []
        self.back = 0       # the vertices from its parent's job's first to its own (a seam's)
        self.seam_reach = 0     # how far a corner on its parent's can lie from its centre
        self.near_cut = False   # a quad with a corner near the camera is left out (frame.cpp)

    def vertex_count(self):
        """As the packer writes it: its cards' four each, one for none."""
        return max(1, len(self.vertices) + 4 * len(self.cards))

    def add_mesh(self, mesh, index, M=None, skip=(), seam=None, pull=None, keep=()):
        """mesh's faces but those in skip, its vertices placed by M (into
        this mesh's space) when given. seam: its vertices that are its
        parent's, {vertex: the parent's in its own DietMesh}, a face's corner
        there being -1 - that. pull: vertices that take another's place. keep:
        vertices kept whether a face uses them or not (a child's seam's)."""
        seam = seam or {}
        pull = pull or {}
        used = {pull.get(i, i) for fi, (_, idx, _) in enumerate(mesh.faces) if fi not in skip for i in idx}
        used = sorted({i for i in used if i not in seam} | set(keep))
        remap = {}
        for i in used:
            remap[i] = len(self.vertices)
            v = mesh.vertices[i]
            self.vertices.append([round(c) for c in apply(M, v)] if M else list(v))
            if self.normals is not None:
                n = mesh.normals[i]
                self.normals.append([round(c) for c in rotate(M, n)] if M else list(n))
            if self.intensities is not None:
                self.intensities.append(mesh.intensities[i])
        for fi, (ftype, idx, flags) in enumerate(mesh.faces):
            if fi not in skip:
                corners = (pull.get(i, i) for i in idx)
                self.faces.append((ftype, tuple(-1 - seam[i] if i in seam else remap[i] for i in corners),
                                   flags, index, fi))
        return remap


def inside_copies(mesh):
    """The faces drawn twice, once each way round, whose copy faces into the
    mesh's middle: a closed box's, which nothing can ever see. A thin shape's
    copies (an ear) lie about through the middle, and a sheet with an open edge
    (a row of teeth, the crest on a neck) can be seen from behind: both stay."""
    def edges(idx):
        return {frozenset((idx[i], idx[(i + 1) % len(idx)])) for i in range(len(idx))}
    centre = [sum(c) / len(mesh.vertices) for c in zip(*mesh.vertices)]
    sets = {}
    for fi, (ftype, idx, flags) in enumerate(mesh.faces):
        sets.setdefault(tuple(sorted(idx)), []).append(fi)
    out = []
    for fis in sets.values():
        if len(fis) < 2:
            continue
        others = set().union(*(edges(f[1]) for fi, f in enumerate(mesh.faces) if fi not in fis))
        if not edges(mesh.faces[fis[0]][1]) <= others:
            continue
        for fi in fis:
            v = [mesh.vertices[i] for i in mesh.faces[fi][1]]
            n = unit(cross(sub(v[1], v[0]), sub(v[2], v[0])))     # away from the side drawn
            if dot(sub(centre, v[0]), [-c for c in n]) > 1:
                out.append(fi)
    return out


def mean(vs):
    return [sum(c) / len(vs) for c in zip(*vs)]


def in_box(v, vs):
    return all(min(c) <= x <= max(c) for x, c in zip(v, zip(*vs)))


def join_rings(meshes, start, parents, offsets, skip=(), wide=None, only=None, stay=None, attach=None):
    """Where a segment's end meets its parent's in the model's rest pose (no
    joint turned): {joint: {p: parent, map: {vertex: the parent's}, caps: its
    faces that go, pcaps: the parent's, pull: the parent's vertices moved to a
    corner, gap: how far a corner moves}}. Each corner of the end goes to the
    nearest of the parent's, each of those taken once, within JOIN_REACH; a
    joint in wide has its own reach, and a corner over shares one when the end
    has more than the parent's. What goes: the faces on those corners alone, and
    the stubs that hid the bend, a thigh's knee or a chest's cone: faces on
    those corners and vertices lying past them inside the other segment, but for
    a see-through sheet's, which runs on regardless (a neck's crest). A face that
    stays on such a vertex takes it to the nearest corner. A joint in only joins
    those of its corners alone: a throat, where the rest of its end stands free of
    the parent. A joint in stay names those of the parent's vertices past the ring
    that stay as they are, with their faces: what fills the gap behind the free part.
    The parent's other faces on a vertex past the ring go. A joint in attach has
    those vertices on those of the parent's as well: a crest's root, which would
    leave the ridge it stands on as the seam takes the ridge with the parent."""
    def sheet(mesh):
        return {i for f in mesh.faces if f[0] in TRANSP_TYPES for i in f[1]}

    out = {}
    for j in range(1, len(parents)):
        if j in skip:
            continue
        p = parents[j]
        child, parent = meshes[start + j], meshes[start + p]
        placed = [[c + o for c, o in zip(v, offsets[j])] for v in child.vertices]
        reach = (wide or {}).get(j, JOIN_REACH)
        chosen = (only or {}).get(j)
        pairs = sorted((norm(sub(v, pv)), i, k) for i, v in enumerate(placed) for k, pv in enumerate(parent.vertices)
                       if chosen is None or i in chosen)
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
        for i, k in (attach or {}).get(j, {}).items():
            ring[i] = k
        centre = mean([placed[i] for i in ring])
        others = [v for i, v in enumerate(placed) if i not in ring]
        along = sub(mean(others), centre) if others else [0, 0, 0]

        def ahead(v):
            return dot(sub(v, centre), along)
        behind = {i for i, v in enumerate(placed)
                  if i not in ring and i not in sheet(child) and ahead(v) < 0 and in_box(v, parent.vertices)}
        past = {k for k, v in enumerate(parent.vertices)
                if k not in taken and k not in sheet(parent) and k not in (stay or {}).get(j, ())
                and ahead(v) > 0 and in_box(v, placed)}
        caps = [fi for fi, f in enumerate(child.faces)
                if all(i in ring or i in behind for i in f[1]) or len({ring.get(i, -1 - i) for i in f[1]}) < 3]
        pcaps = [fi for fi, f in enumerate(parent.faces) if all(k in taken or k in past for k in f[1])
                 or (j in (stay or {}) and any(k in past for k in f[1]))]

        def nearest(v):
            return min(taken, key=lambda k: norm(sub(v, parent.vertices[k])))
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
        out[j] = {"p": p, "map": {str(i): k for i, k in ring.items()}, "gap": round(gap), "caps": caps, "pcaps": pcaps,
                  "pull": pull}
    return out


def pin_card(card, parents, offsets, j):
    """A card's ends on its joint's pivot and its child's, where those lie
    near them: the next card along then starts where this one ends, however
    the joint bends."""
    line = sub(card["e1"], card["e0"])
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


def animal_diet(lvl, model, meshes, atlas, spec):
    """An animal's simplified meshes by joint (ANIMALS), and the joints held
    still. The joints between a seam's two segments are known here, so each
    child's job's distance from its parent's is too: jobs lie one after another
    in joint order, whole groups of 4 vertices each (renderer/vertices.cpp), a
    joint held still having none."""
    start, count = model["start"], model["count"]
    parents, offsets = skeleton(lvl, model)
    frozen = sorted(spec.get("held", ()))
    cards, facing = set(spec.get("cards", ())), set(spec.get("facing", ()))
    # a joint held still is neither turned nor drawn (its node's
    # NODE_FLAG_STILL, renderer/pose.s): the matrix after it stays its
    # parent's, so a joint under it that moves would be drawn off it
    for j in range(1, count):
        if parents[j] in frozen and j not in frozen:
            raise ValueError(f"model {model['type']} joint {j} moves but its parent {parents[j]} is held still")
    if 0 in frozen:
        raise ValueError(f"model {model['type']}: the root joint can't be held still")
    pose = standing_pose(lvl, model) if frozen else None
    kept = lambda j: j if j not in frozen else kept(parents[j])
    # seams between segments drawn whole, each joint's parent drawn too
    whole = cards | set(frozen)
    no_seam = set(spec.get("apart", ())) | whole | {j for j in range(1, count) if parents[j] in whole}
    rings = join_rings(meshes, start, parents, offsets, no_seam, spec.get("wide"), spec.get("only"), spec.get("stay"),
                       spec.get("attach"))
    out = {j: DietMesh(meshes[start + j]) for j in range(count)}
    remaps = {}
    for j in range(count):
        k = kept(j)
        M = held(parents, offsets, pose, j, k) if j != k else None
        mesh = meshes[start + j]
        if j in spec.get("plates", {}):
            mesh = plate_of(mesh, spec["plates"][j])
        if j in cards:
            c = card_of(mesh, atlas, axis=1, view=2) if j in facing else pin_card(card_of(mesh, atlas), parents, offsets, j)
            if j in facing and norm(c["e1"]) > norm(c["e0"]):
                raise ValueError(f"model {model['type']} joint {j}: a facing card's second end is its base")
            e0, e1 = (apply(M, c["e0"]), apply(M, c["e1"])) if M else (c["e0"], c["e1"])
            n0, n1 = c["n0"], c["n1"]
            if M:
                n0 = rotate(M, n0) if n0 else None
                n1 = rotate(M, n1) if n1 else None
            out[k].cards.append({"e0": e0, "e1": e1, "half": c["half"], "facing": j in facing,
                                 "texture": ("image", c["image"], True), "light": ("normals", n0, n1)})
            continue
        ring = rings.get(j)
        skip = set(inside_copies(mesh)) | set(ring["caps"] if ring else ())
        pull, keep = {}, set()
        for r in rings.values():
            if r["p"] == j:
                skip |= set(r["pcaps"])
                pull.update({int(v): q for v, q in r["pull"].items()})
                keep |= set(r["map"].values())
        seam = {int(i): remaps[ring["p"]][q] for i, q in ring["map"].items()} if ring else None
        remap = out[k].add_mesh(mesh, start + j, M, skip, seam, pull, keep)
        if j == k:
            remaps[j] = remap
    for j, r in rings.items():
        out[j].back = sum((out[m].vertex_count() + 3) & ~3 for m in range(r["p"], j) if m not in frozen)
        # a parent's corner keeps its distance from the joint the segment turns
        # about, the mesh's own origin
        corners = meshes[start + r["p"]].vertices
        out[j].seam_reach = norm(out[j].center) + max(norm(sub(corners[q], offsets[j])) for q in r["map"].values())
    return out, frozen


def plank_diet(mesh, index, atlas, apart=None):
    inside, groups = plank_parts(mesh, atlas, apart)
    out = DietMesh(mesh)
    keep = [fi for g in groups for fi in g["faces"] if g["merged"] is None]
    remap = out.add_mesh(mesh, index, skip=set(range(len(mesh.faces))) - set(keep))
    for g in groups:
        m = g["merged"]
        if m is None:
            continue
        for i in m["c"]:
            if i not in remap:
                remap[i] = len(out.vertices)
                out.vertices.append(list(mesh.vertices[i]))
                if out.normals is not None:
                    out.normals.append(list(mesh.normals[i]))
                if out.intensities is not None:
                    out.intensities.append(mesh.intensities[i])
        out.faces.append({"merged": True, "c": [remap[i] for i in m["c"]], "image": m["image"],
                          "transp": m["transp"], "src": index, "from": g["faces"]})
    return out


def level_diet(lvl, meshes, model_name):
    """The meshes the diet simplifies in lvl, {mesh index: DietMesh}: the
    animals' (ANIMALS), the planks', the rope statics' (ROPE_STATICS) and the
    vines' (VINE_STATICS); and
    the joints it holds still, {model type: joints}. model_name(type) names a
    model's type as common.h's ITEM_TYPES does."""
    atlas = Atlas(lvl)
    out, still = {}, {}
    for m in lvl.models:
        name = model_name(m["type"])
        if name in ANIMALS:
            simplified, frozen = animal_diet(lvl, m, meshes, atlas, ANIMALS[name])
            out.update({m["start"] + j: dm for j, dm in simplified.items()})
            if frozen:
                still[m["type"]] = frozen
        elif name in ("BRIDGE_FLAT", "BRIDGE_TILT_1", "BRIDGE_TILT_2"):
            i = m["start"]
            groups = PLANK_APART.get((lvl.name, name), [])
            apart = {fi: k for k, group in enumerate(groups) for fi in group}
            out[i] = plank_diet(meshes[i], i, atlas, apart)
    ropes = ROPE_STATICS.get(lvl.name, ())
    for st in lvl.static_meshes:
        if st["id"] in ropes:
            dm = rope_diet(meshes[st["mesh"]], st["mesh"], atlas)
            if dm is None:
                raise ValueError(f"static {st['id']} (ROPE_STATICS) is not a rope static")
            if st["mesh"] in out:
                raise ValueError(f"mesh {st['mesh']} simplified twice")
            out[st["mesh"]] = dm
    for st in lvl.static_meshes:
        if st["id"] in VINE_STATICS.get(lvl.name, ()):
            out[st["mesh"]] = vine_diet(meshes[st["mesh"]], st["mesh"], atlas)
    # a seam's corners lie in the job its model's draws put before the mesh's:
    # nothing else may draw it
    seams = {i for i, dm in out.items() if dm.back}
    for st in lvl.static_meshes:
        if st["mesh"] in seams:
            raise ValueError(f"static {st['id']} draws mesh {st['mesh']}, which has a seam")
    for m in lvl.models:
        if model_name(m["type"]) not in ANIMALS and seams & set(range(m["start"], m["start"] + m["count"])):
            raise ValueError(f"model {m['type']} draws a mesh with a seam")
    return out, still


def vine_strips(out, mesh, index, fis, atlas, width, side):
    """A side of a curtain as tall strips hanging down, width texels across: its tiles pasted together, then each
    column's run of filled rows a quad of its own with its own texture, on the 8 texel steps VDP1's size wants. Rows
    empty for VINE_GAP or more split a run. Corners are shared across a mesh, as a strip's are with its neighbours'."""
    m = merge_tiles(mesh, fis, atlas)
    w, h, rgba = m["image"]
    corners = [list(mesh.vertices[i]) for i in m["c"]]
    light = [mesh.intensities[i] for i in m["c"]] if out.intensities is not None else None
    normals = [list(mesh.normals[i]) for i in m["c"]] if out.normals is not None else None
    filled = [[rgba[4 * (y * w + x) + 3] != 0 for x in range(w)] for y in range(h)]
    shared = out.__dict__.setdefault("shared", {})

    def mix(values, u, v):
        return (1 - u) * (1 - v) * values[0] + u * (1 - v) * values[1] + u * v * values[2] + (1 - u) * v * values[3]

    def vertex(u, v):
        at = tuple(round(mix([c[k] for c in corners], u, v)) for k in range(3))
        if at not in shared:
            shared[at] = len(out.vertices)
            out.vertices.append(list(at))
            if light is not None:
                out.intensities.append(round(mix(light, u, v)))
            if normals is not None:
                out.normals.append([round(mix([n[k] for n in normals], u, v)) for k in range(3)])
        return shared[at]

    for x0 in range(0, w, width):
        runs = []
        for y in range(h):
            if any(filled[y][x0:x0 + width]):
                if runs and y - runs[-1][1] < VINE_GAP:
                    runs[-1][1] = y
                else:
                    runs.append([y, y])
        for y0, y1 in runs:
            tall = (y1 - y0 + 8) & ~7
            y1 = min(h, y0 + tall)
            y0 = y1 - tall
            image = bytearray()
            for y in range(y0, y1):
                start = 4 * (y * w + x0)
                image += rgba[start:start + 4 * width]
            u0, u1, v0, v1 = x0 / w, (x0 + width) / w, y0 / h, y1 / h
            out.faces.append({"merged": True, "c": [vertex(u0, v0), vertex(u1, v0), vertex(u1, v1), vertex(u0, v1)],
                              "image": (width, tall, bytes(image)), "transp": m["transp"], "src": index, "from": fis,
                              "side": side, "rect": (x0, y0, width, tall), "mosaic": m["image"]})


def vine_diet(mesh, index, atlas, width=None):
    """A vine static with each side of each curtain as strips (vine_strips)."""
    sides = {}
    for fi, (_, idx, _) in enumerate(mesh.faces):
        v = [mesh.vertices[i] for i in idx]
        n = unit(cross(sub(v[1], v[0]), sub(v[2], v[0])))
        sides.setdefault((round(n[0]), round(n[2]), round(dot(n, v[0]) / 16)), []).append(fi)
    out = DietMesh(mesh)
    out.near_cut = True
    out.add_mesh(mesh, index, skip=set(range(len(mesh.faces))))
    for side, fis in enumerate(sides.values()):
        vine_strips(out, mesh, index, fis, atlas, width or VINE_STRIP, side)
    return out


def rope_diet(mesh, index, atlas):
    """A rope static's tubes as cards, each segment's textured with its first
    face's texture turned to run along it from the end at the face's corner
    A (where the texture's first row is); None if mesh isn't one."""
    strands = rope_strands(mesh)
    if not strands or any(s["intensity"] is None for s in strands):
        return None
    out = DietMesh(mesh)
    in_strand = {fi for s in strands for seg in s["seg"] for fi in seg["faces"]}
    out.add_mesh(mesh, index, skip=in_strand)
    for s in strands:
        shade = s["intensity"]
        for k, seg in enumerate(s["seg"]):
            if seg["u"]:
                return None     # every texture here runs v along its tube
            ftype, idx, flags = mesh.faces[seg["faces"][0]]
            e0, e1 = s["pts"][k], s["pts"][k + 1]
            a = mesh.vertices[idx[0]]
            if norm(sub(a, e1)) < norm(sub(a, e0)):
                e0, e1 = e1, e0
            w, h, texels, colours = atlas.indexed(flags >> 4)
            w, h, texels = transposed(w, h, texels, ftype in FLIP_TYPES)
            out.cards.append({"e0": e0, "e1": e1, "half": s["r"] * ROPE_CARD_WIDTH / 2,
                              "texture": ("indexed", w, h, tuple(texels), tuple(colours), ftype in TRANSP_TYPES),
                              "light": ("intensities", shade, shade)})
    return out
