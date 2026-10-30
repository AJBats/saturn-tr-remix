"""The mesh store: a level's meshes as the renderer reads them
(renderer/types.h MeshStore), made from the meshes in the game's layout
(pack_level.py write_mesh: the Mesh header, its vertices, normals or shades,
quads and triangles). The level packer writes one for the whole level
(DATA/<level>.MSH); the lab's scenes carry one of the meshes they draw.

    build(meshes, index, textures, palette, pmod) -> bytes

meshes: each mesh's bytes in the game's layout, once each; index: each
level mesh's place in meshes, or NO_MESH; textures: the level's texture
entries (srca, colr, size, attr, color); palette: its 256 colours; pmod:
textured faces' draw mode.

The store: its header (MeshStore), each level mesh's record (uint16), each
record's offset in the store (uint32), each record's faces' offset from that
table's start (uint32: facepack's FaceSet), the materials (facepack
Material); then the records, each the mesh's 20-byte header as the game
keeps it, its groups for meshrot.dsp (4 vertices each: lit by normals, the 4
normals' bytes + 128 then the 4 vertices' int16 x y z, 10 longwords; else
the vertices alone, 6), its shades (a byte a vertex, to a longword), its
faces.
"""
import struct

import facepack

MAGIC = 0x4D534831          # 'MSH1'
HEADER = struct.Struct('>8I')
NO_MESH = 0xFFFF


def groups(mesh):
    """A mesh's vertices (and normals) as meshrot.dsp reads them
    (renderer/vertices.cpp packMesh): its last group's spare vertices 0."""
    count, lighting = mesh[10], mesh[11]
    at = facepack.MESH_HEADER
    vertices = [struct.unpack_from('>3h', mesh, at + 6 * k) for k in range(count)]
    normals = None
    if lighting == facepack.MESH_LIGHT_NORMALS:
        n = at + 6 * count
        normals = [struct.unpack_from('>3b', mesh, n + 4 * k) for k in range(count)]
    out = bytearray()
    for g in range(0, count, 4):
        if normals:
            for k in range(g, g + 4):
                if k < count:
                    out += bytes(((c + 128) & 0xFF) for c in normals[k]) + b'\0'
                else:
                    out += bytes(4)
        for k in range(g, g + 4):
            out += struct.pack('>3h', *(vertices[k] if k < count else (0, 0, 0)))
    return bytes(out)


def shades(mesh):
    """A pre-lit mesh's shades, a byte a vertex, to a longword; else none."""
    count = mesh[10]
    if mesh[11] != facepack.MESH_LIGHT_SHADES:
        return b''
    at = facepack.MESH_HEADER + 6 * count
    data = mesh[at:at + count]
    return data + bytes(-len(data) % 4)


def build(meshes, index, textures, palette, pmod):
    materials = facepack.Materials(textures, palette, pmod)
    records = []
    for mesh in meshes:
        faces = facepack.mesh_faces(mesh, materials)
        records.append((bytes(mesh[:facepack.MESH_HEADER]) + groups(mesh) + shades(mesh), faces))
    material_bytes = materials.bytes()

    index_at = HEADER.size
    meshes_at = (index_at + 2 * len(index) + 3) & ~3
    sets_at = meshes_at + 4 * len(records)
    materials_at = sets_at + 4 * len(records)
    at = materials_at + len(material_bytes)
    offsets, set_offsets, body = [], [], bytearray()
    for head, faces in records:
        offsets.append(at + len(body))
        body += head
        set_offsets.append(at + len(body) - sets_at)
        body += faces
        body += bytes(-len(body) % 4)
    out = bytearray(HEADER.pack(MAGIC, len(index), len(records), len(material_bytes) // facepack.MATERIAL.size,
                                index_at, meshes_at, sets_at, materials_at))
    out += struct.pack(f'>{len(index)}H', *index)
    out += bytes(meshes_at - len(out))
    out += struct.pack(f'>{len(offsets)}I', *offsets)
    out += struct.pack(f'>{len(set_offsets)}I', *set_offsets)
    out += material_bytes
    out += body
    return bytes(out)
