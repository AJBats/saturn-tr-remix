"""A level's statics and items as occmodel's sort mode draws them: every mesh in world space, items in
their model's standing pose (or first frame) at their start place, statics where the room puts them.
Sprites (pickups, plants) aren't included.

usage: python -I tools/occmodel/export_objects.py LEVEL OUT.bin

Format, little-endian: int objects; per object int room, kind (0 static, 1 item), id (static's mesh id
or item index), meshes; per mesh float centre xyz, int faces; per face int corners (3 or 4), float
corners xyz.
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, ROOT + '/tools')
import diet  # noqa: E402
import trsat  # noqa: E402


def quadrant_yaw(rotation):
    """A level item's or static's yaw as the game keeps it: whole quarter turns."""
    v = rotation & 0xFFFF
    v = v - 0x10000 if v >= 0x8000 else v
    return (int(v / 0x4000) * 0x4000) & 0xFFFF


def placed(mesh, M):
    faces = []
    for ftype, idx, flags in mesh.faces:
        faces.append([diet.apply(M, mesh.vertices[i]) for i in idx])
    centre = diet.apply(M, mesh.center)
    return centre, faces


def first_frame(lvl, model):
    a = lvl.anims[model['anim']]
    return diet.frame_at(lvl, a['frame_offset'] // 2, model['count'])


def main():
    level, path = sys.argv[1], sys.argv[2]
    lvl = trsat.load(f'{ROOT}/external_resources/disc/files/DATA', level)
    meshes = trsat.meshes(lvl)
    statics = {s['id']: s for s in lvl.static_meshes}
    objects = []
    for room in lvl.rooms:
        for s in room.static_meshes:
            st = statics.get(s['mesh_id'])
            if st is None:
                continue
            M = (diet.rot(0, quadrant_yaw(s['rotation']), 0), [s['x'], s['y'], s['z']])
            objects.append((room.index, 0, s['mesh_id'], [placed(meshes[st['mesh']], M)]))
    models = {m['type']: m for m in lvl.models}
    for k, it in enumerate(lvl.items):
        model = models.get(it['type'])
        if model is None or model['count'] == 0 or it['flags'] & 0x100:     # 0x100: starts invisible
            continue
        try:
            pose = diet.standing_pose(lvl, model)
            frame = first_frame(lvl, model)
        except (IndexError, ValueError):
            continue
        parents, offsets = diet.skeleton(lvl, model)
        base = (diet.rot(0, quadrant_yaw(it['rotation']), 0), [it['x'], it['y'], it['z']])
        root = (diet.matmul(base[0], diet.rot(*pose[0:3])), diet.apply(base, frame['p']))
        parts = []
        for j in range(model['count']):
            mesh = meshes[model['start'] + j]
            if not mesh.faces:
                continue
            H = diet.held(parents, offsets, pose, j, 0) if j else ([[1, 0, 0], [0, 1, 0], [0, 0, 1]], [0, 0, 0])
            M = (diet.matmul(root[0], H[0]), diet.apply(root, H[1]))
            parts.append(placed(mesh, M))
        if parts:
            objects.append((it['room'], 1, k, parts))
    b = bytearray(struct.pack('<i', len(objects)))
    nf = 0
    for room, kind, ident, parts in objects:
        b += struct.pack('<4i', room, kind, ident, len(parts))
        for centre, faces in parts:
            b += struct.pack('<3fi', *centre, len(faces))
            for f in faces:
                b += struct.pack('<i', len(f))
                for c in f:
                    b += struct.pack('<3f', *c)
                nf += 1
    open(path, 'wb').write(b)
    print(f'{level}: {len(objects)} objects, {nf} faces, {len(b)} bytes')


main()
