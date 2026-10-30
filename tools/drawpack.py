"""The frame's calls into the renderer, for the lab's draw list (lab/scene.h
CallRecord), from a capture's records: each viewport set, each item's model
(drawModel), room static (drawRoomStatic), shadow (drawShadow's floor),
sprite (renderSprite) and rectangle (renderRect: the HUD's; the walk's
fills, made before the first room is queued, are the walk's to make), in the
order the game made them; and what the renderer reads for them, in the
game's own layouts: the items (ItemObj), each with its extra's first bytes
(Lara's head, torso, arms and joints' meshes; an enemy's angles) and its own copy of its
animation (Anim, its frame offset moved so that its frame and the next lie
in AFRM, from a base AFRM_BASE uint16s before AFRM: the game's arithmetic,
unchanged, lands on them; Lara's arms' likewise), the level's models
(Model) and static meshes (StaticMesh); where the lab holds each level mesh
(MESH's index) and each model's nodes (PNOD), and each room's ambient and
lights (in LGHT).

    pack(mem, sym, recs, mesh_index, maps) -> chunks CALL, OITM, ANIM, AFRM, MODL, MIDX, MNOD, SMSH, RLIT

mesh_index: the lab's MESH index of each mesh address; maps: posepack's
(PNOD's offset by the level's node index, LGHT's index by room index). The
structures' layouts come from the game's compiler (tools/gamelayout.py).
"""
import struct

from gamelayout import layout

LAYOUT = {
    'item': 'sizeof(ItemObj)', 'i.type': 'offsetof(ItemObj, type)', 'i.anim': 'offsetof(ItemObj, animIndex)',
    'i.frame': 'offsetof(ItemObj, frameIndex)', 'i.room': 'offsetof(ItemObj, room)',
    'i.extra': 'offsetof(ItemObj, extra)', 'i.next': 'offsetof(ItemObj, nextItem)',
    'model': 'sizeof(Model)', 'm.count': 'offsetof(Model, count)', 'm.node': 'offsetof(Model, nodeIndex)',
    'anim': 'sizeof(Anim)', 'a.offset': 'offsetof(Anim, frameOffset)', 'a.begin': 'offsetof(Anim, frameBegin)',
    'a.end': 'offsetof(Anim, frameEnd)', 'a.rate': 'offsetof(Anim, frameRate)', 'frame': 'sizeof(AnimFrame)',
    'L.models': 'offsetof(Level, models)',
    'L.anims': 'offsetof(Level, anims)', 'L.frames': 'offsetof(Level, animFrames)',
    'L.statics': 'offsetof(Level, staticMeshes)',
    'L.meshes': 'offsetof(Level, meshes)', 'L.meshesCount': 'offsetof(Level, meshesCount)',
    'L.roomsCount': 'offsetof(Level, roomsCount)', 'staticMesh': 'sizeof(StaticMesh)',
    'room': 'sizeof(Room)', 'r.info': 'offsetof(Room, info)', 'r.meshes': 'offsetof(Room, data.meshes)',
    'ri.ambient': 'offsetof(RoomInfo, ambient)', 'ri.lights': 'offsetof(RoomInfo, lightsCount)',
    'ri.meshes': 'offsetof(RoomInfo, meshesCount)', 'roomMesh': 'sizeof(RoomMesh)',
    'xl.armR': 'offsetof(ExtraInfoLara, armR)', 'arm': 'sizeof(ExtraInfoLara::Arm)',
    'arm.anim': 'offsetof(ExtraInfoLara::Arm, animIndex)', 'arm.frame': 'offsetof(ExtraInfoLara::Arm, frameIndex)',
    'ITEM_LARA': 'ITEM_LARA', 'MAX_MODELS': 'MAX_MODELS', 'MAX_STATIC_MESHES': 'MAX_STATIC_MESHES',
}

# CallRecord (lab/scene.h): its kind, index, the faces' flags at the call, six arguments
CALL = struct.Struct('>BxHI6i')
CALL_VIEWPORT, CALL_MODEL, CALL_STATIC, CALL_SHADOW, CALL_SPRITE, CALL_RECT = 1, 2, 3, 4, 5, 6
EXTRA_BYTES = 228           # Lara's extra up to her joints' meshes' end; an enemy's angles first
AFRM_BASE = 0x10000         # uint16s: the lab's level.animFrames lies this far before AFRM
NONE16, NONE32 = 0xFFFF, 0xFFFFFFFF


def pack(mem, sym, recs, mesh_index, maps):
    L = layout(LAYOUT)
    u32 = lambda a: mem.unpack('>I', a)[0]
    u16 = lambda a: mem.unpack('>H', a)[0]
    u8 = lambda a: mem.unpack('>B', a)[0]
    level = sym('_level')
    items, rooms = sym('_items'), sym('_rooms')
    models, anims, frames = (u32(level + L[k]) for k in ('L.models', 'L.anims', 'L.frames'))
    # level.h: the level's models, each at its type's place (MAX_MODELS of them)
    models_count, rooms_count = L['MAX_MODELS'], u16(level + L['L.roomsCount'])
    item_record = L['item'] + 4 + EXTRA_BYTES

    calls, item_data, anim_data, frame_data = bytearray(), bytearray(), bytearray(), bytearray()
    item_of = {}                # a model call's item record, by item index (for its shadow's call)

    def model_count(model):
        return mem.unpack('>b', models + model * L['model'] + L['m.count'])[0]

    def anim_copy(anim_index, index, count, next_too):
        """Animation anim_index's own copy in ANIM, its frame index (and the
        next, next_too) in AFRM: the copy's index."""
        a = anims + anim_index * L['anim']
        size = (L['frame'] >> 1) + (count << 1)         # uint16s, as getFrames has it
        first = frames + (u32(a + L['a.offset']) >> 1) * 2 + index * size * 2
        at = len(frame_data) // 2
        frame_data.extend(mem.bytes(first, (2 if next_too else 1) * size * 2))
        anim_data.extend(struct.pack('>I', 2 * (AFRM_BASE + at - index * size)) + mem.bytes(a + 4, L['anim'] - 4))
        return len(anim_data) // L['anim'] - 1

    def room_of_mesh(mesh):
        for i in range(rooms_count):
            info = u32(rooms + i * L['room'] + L['r.info'])
            first = u32(rooms + i * L['room'] + L['r.meshes'])
            if first <= mesh < first + u8(info + L['ri.meshes']) * L['roomMesh']:
                return i, (mesh - first) // L['roomMesh']
        raise SystemExit(f'static {mesh:#x} in no room')

    queued = False
    for r in recs:
        kind = r['type']
        if kind == 'room':
            queued = True
        elif kind == 'viewport':
            calls += CALL.pack(CALL_VIEWPORT, 0, 0, *r['window'], 0, 0)
        elif kind == 'model':
            it = items + r['item'] * L['item']
            a = anims + r['anim'] * L['anim']
            count = model_count(r['itemType'])
            still = u16(a + L['a.begin']) == u16(a + L['a.end'])
            rate = max(u8(a + L['a.rate']), 1)
            index = 0 if still else (r['frame'] - u16(a + L['a.begin'])) // rate
            # getFrames reads frame index + 1 unless (index + 1) * rate reaches the
            # animation's end, as a frame number (past its last frame, for an
            # animation not at 0)
            next_too = not still and (index + 1) * rate < u16(a + L['a.end'])
            record = bytearray(mem.bytes(it, L['item']))
            struct.pack_into('>H', record, L['i.anim'], anim_copy(r['anim'], index, count, next_too))
            struct.pack_into('>H', record, L['i.frame'], r['frame'])
            struct.pack_into('>B', record, L['i.type'], r['itemType'])
            struct.pack_into('>I', record, L['i.room'], 0)
            struct.pack_into('>I', record, L['i.next'], 0)
            extra = u32(it + L['i.extra'])
            extra_bytes = bytearray(EXTRA_BYTES)
            if extra:
                extra_bytes[:] = mem.bytes(extra, EXTRA_BYTES)
                struct.pack_into('>I', record, L['i.extra'], 1)     # the lab points it at its copy
                if r['itemType'] == L['ITEM_LARA']:
                    for k in range(2):
                        arm = L['xl.armR'] + k * L['arm']
                        arm_anim = struct.unpack_from('>H', extra_bytes, arm + L['arm.anim'])[0]
                        if arm_anim:
                            arm_frame = struct.unpack_from('>H', extra_bytes, arm + L['arm.frame'])[0]
                            struct.pack_into('>H', extra_bytes, arm + L['arm.anim'],
                                             anim_copy(arm_anim, arm_frame, count, False))
            room = (u32(it + L['i.room']) - rooms) // L['room']
            item_of[r['item']] = len(item_data) // item_record
            item_data += record + struct.pack('>H2x', room) + extra_bytes
            calls += CALL.pack(CALL_MODEL, item_of[r['item']], r['flags'], 0, 0, 0, 0, 0, 0)
        elif kind == 'static call':
            room, index = room_of_mesh(r['ref'])
            xy, zf = mem.unpack('>2i', r['ref'])
            calls += CALL.pack(CALL_STATIC, room, r['flags'], xy, zf, *r['place'], index)
        elif kind == 'shadow call':
            calls += CALL.pack(CALL_SHADOW, item_of[r['item']], r['flags'], r['floor'], 0, 0, 0, 0, 0)
        elif kind == 'sprite':
            calls += CALL.pack(CALL_SPRITE, r['sprite'], r['flags'], *r['position'], r['g'], 0, 0)
        elif kind == 'rect' and queued:
            calls += CALL.pack(CALL_RECT, r['rectType'], r['flags'], *r['rect'], r['colour'], r['bucket'])

    meshes, meshes_count = u32(level + L['L.meshes']), u16(level + L['L.meshesCount'])
    midx = [mesh_index.get(u32(meshes + 4 * i) & 0x0FFFFFFF, NONE16) for i in range(meshes_count)]
    mnod = [maps['nodes'].get(u16(models + m * L['model'] + L['m.node']), NONE32) if model_count(m) > 0
            else NONE32 for m in range(models_count)]
    # level.h: the static meshes, each at its id's place (MAX_STATIC_MESHES of them)
    statics, statics_count = u32(level + L['L.statics']), L['MAX_STATIC_MESHES']
    rlit = bytearray()
    for i in range(rooms_count):
        info = u32(rooms + i * L['room'] + L['r.info'])
        first = maps['lights'].get(i, NONE32)
        rlit += struct.pack('>BBxxI', u8(info + L['ri.ambient']), u8(info + L['ri.lights']) if first != NONE32
                            else 0, first)
    return {'CALL': bytes(calls), 'OITM': bytes(item_data), 'ANIM': bytes(anim_data), 'AFRM': bytes(frame_data),
            'MODL': mem.bytes(models, models_count * L['model']),
            'MIDX': struct.pack(f'>{meshes_count}H', *midx), 'MNOD': struct.pack(f'>{models_count}I', *mnod),
            'SMSH': mem.bytes(statics, statics_count * L['staticMesh']), 'RLIT': bytes(rlit)}
