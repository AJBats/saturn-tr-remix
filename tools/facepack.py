"""Faces packed for the new face stage (lab/faces.h): what a face's VDP1
command takes from the level, made ready once, so the stage copies it.

A material is a command's first three words and what may be done to them:
the control word (the command, a texture's flip, and a jump to CMDLINK: the
stage chains each command to the one before it in its bucket), the draw mode
and colour
(or colour table), the character and its size; one for each texture and way
of drawing it that a face uses (a mesh triangle drawn as a parallelogram
keeps its end codes, so it takes a material of its own). Material 0 is the
shadow's.

A mesh's faces are its quads, then its triangles, as the game adds them:
each with its corners as byte offsets into the mesh's vertices and its
material. A triangle is drawn as the parallelogram v0, x, x + y - v0, y, its
v0 at corner A or D, where x, y are v1, v2 or (swapped) v2, v1; or, coloured
or marked so, as a quad with D = C. Its corners are kept as given: its back
face test is theirs.
"""
import struct

VERTEX_BYTES = 8
FACE_TYPE_SHIFT = 14
FACE_TEXTURE = 0x3FFF
FACE_TYPE_F, FACE_TYPE_AVG = 1, 10
TEX_ATTR_AKILL, TEX_ATTR_DIR = 0x0001, 0x0030
CMD_DISTORTED_SPRITE, CMD_POLYGON, CMD_SKIP, CMD_JUMP = 0x0002, 0x0004, 0x4000, 0x1000
PMOD_GOURAUD, PMOD_LUT16, PMOD_SPD_OFF, PMOD_ECD_OFF, PMOD_MESH = 0x0004, 0x0008, 0x0040, 0x0080, 0x0100
PMOD_NO_ECD_SPD = PMOD_SPD_OFF | PMOD_ECD_OFF
BLACK = 0x8000
MESH_HEADER = 20
MESH_LIGHT_NORMALS, MESH_LIGHT_SHADES = 1, 2

MATERIAL = struct.Struct('>4I')         # faces.h Material
QUAD = struct.Struct('>4H2H')           # faces.h FaceQuad
TRIANGLE = struct.Struct('>4H2H')       # faces.h FaceTriangle
SET = struct.Struct('>2H')              # faces.h FaceSet

MAT_SHADEABLE = 1           # a shaded copy of its colour table may stand in for gouraud
TRI_PARALLELOGRAM = 1       # FaceTriangle.flags
TRI_V0_AT_D = 2
TRI_SWAP = 4


class Materials:
    """The level's materials, each made once."""

    def __init__(self, textures, palette, texture_pmod):
        self.textures = textures            # (srca, colr, size, attr, color) each
        self.palette = palette
        self.pmod = texture_pmod
        self.index = {}
        self.data = bytearray()
        self.add(('shadow',), CMD_POLYGON << 16, (PMOD_NO_ECD_SPD | PMOD_MESH) << 16 | BLACK, 0, 0)

    def add(self, key, w0, w1, w2, flags):
        if key not in self.index:
            self.index[key] = len(self.data) // MATERIAL.size
            self.data += MATERIAL.pack(w0 | CMD_JUMP << 16, w1, w2, flags)
        return self.index[key]

    def face(self, flags, parallelogram):
        """render.cpp faceMaterial, for a mesh face's flags."""
        kind = (flags >> FACE_TYPE_SHIFT) & 15
        index = flags & FACE_TEXTURE
        if kind == FACE_TYPE_F:
            return self.add(('colour', index), CMD_POLYGON << 16,
                            (PMOD_NO_ECD_SPD | PMOD_GOURAUD) << 16 | self.palette[index], 0, 0)
        srca, colr, size, attr, color = self.textures[index]
        if kind == FACE_TYPE_AVG or not srca:
            if kind != FACE_TYPE_AVG and attr & TEX_ATTR_AKILL:
                return self.add(('skip',), CMD_SKIP << 16, 0, 0, 0)
            return self.add(('mean', index), CMD_POLYGON << 16, (PMOD_NO_ECD_SPD | PMOD_GOURAUD) << 16 | color, 0, 0)
        pmod = self.pmod
        if not parallelogram:
            pmod |= PMOD_ECD_OFF        # end codes trim parallelograms to their triangle
        if not attr & TEX_ATTR_AKILL:
            pmod |= PMOD_SPD_OFF        # colour 0 is opaque
        shadeable = MAT_SHADEABLE if pmod & 0x3F == PMOD_LUT16 | PMOD_GOURAUD else 0
        return self.add(('texture', index, parallelogram), (CMD_DISTORTED_SPRITE | (attr & TEX_ATTR_DIR)) << 16,
                        pmod << 16 | colr, srca << 16 | size, shadeable)

    def bytes(self):
        return bytes(self.data)


def mesh_faces(mesh, materials):
    """A mesh as the game keeps it (render.cpp meshParts) to its FaceSet."""
    vcount = mesh[10]
    lighting = mesh[11]
    quads, triangles = struct.unpack_from('>hh', mesh, 12)
    at = MESH_HEADER + 6 * vcount
    if lighting == MESH_LIGHT_NORMALS:
        at += 4 * vcount
    elif lighting == MESH_LIGHT_SHADES:
        at += (vcount + 1) & ~1
    out = bytearray(SET.pack(quads, triangles))
    for _ in range(quads):
        flags, a, b, c, d = struct.unpack_from('>H4B', mesh, at)
        at += 6
        out += QUAD.pack(a * VERTEX_BYTES, b * VERTEX_BYTES, c * VERTEX_BYTES, d * VERTEX_BYTES,
                         materials.face(flags, False), 0)
    for _ in range(triangles):
        flags, v0, v1, v2, arrangement = struct.unpack_from('>H4B', mesh, at)
        at += 6
        kind = (flags >> FACE_TYPE_SHIFT) & 15
        # render.cpp faceAddMeshTriangles
        if kind in (FACE_TYPE_F, FACE_TYPE_AVG) or arrangement & 0x80:
            out += TRIANGLE.pack(v0 * VERTEX_BYTES, v1 * VERTEX_BYTES, v2 * VERTEX_BYTES, 0,
                                 materials.face(flags, False), 0)
            continue
        tri = TRI_PARALLELOGRAM | (TRI_V0_AT_D if arrangement & 1 else 0) | (TRI_SWAP if arrangement & 2 else 0)
        out += TRIANGLE.pack(v0 * VERTEX_BYTES, v1 * VERTEX_BYTES, v2 * VERTEX_BYTES, tri,
                             materials.face(flags, True), 0)
    return bytes(out)
