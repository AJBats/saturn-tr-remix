"""The pose stage's inputs for the lab (lab/scene.h PoseItem), read from a
capture's dumps: each item the frame drew with its model's nodes, as
openlara/draw.h drawModel took it (its place and turn, its frames as
ItemObj::getFrames picks them, its extra angles; Lara's torso, head and
arms), and the level data its arithmetic reads (the nodes, the frames, the
sine table); and what lights each item (PoseLight: its intensity, its room's
place, ambient and lights). Beside the items, the frame's static meshes
(PoseStatic: their place, quarter turn and intensity, as drawRoomStatic takes
them) and the items' shadows (PoseShadow: the item, the floor under it).

    pack(mem, sym, recs, first_draws, header, statics, shadows, maps=None)
        -> chunks POSE, PNOD, PFRM, SINC, PLIT, LGHT, STAT, SHDW

maps, if given, gets where PNOD holds each model's nodes ('nodes', by the
level's node index) and where LGHT holds each room's lights ('lights', by
the room's index).

recs: the capture's records; first_draws: each drawn item's first mesh's
draw (DRAW's index), by item index; header: the capture's; statics: each
static mesh's draw and its object record, in draw order; shadows: each
shadow's draw and its owner's item index. The structures' layouts come from
the game's compiler (tools/gamelayout.py).

The floor under a shadow is the sector data's (game logic); the capture has
the shadow's matrix, the view moved to the item's x and z at the floor's
height (matrixTranslateAbs: plain products of the view's rows), from which
the floor comes exactly: one unknown, three rows to solve and check it.
"""
import struct

from gamelayout import layout

LAYOUT = {
    'item': 'sizeof(ItemObj)', 'i.pos': 'offsetof(ItemObj, pos)', 'i.angle': 'offsetof(ItemObj, angle)',
    'i.anim': 'offsetof(ItemObj, animIndex)', 'i.frame': 'offsetof(ItemObj, frameIndex)',
    'i.type': 'offsetof(ItemObj, type)', 'i.visible': 'offsetof(ItemObj, visibleMask)',
    'i.extra': 'offsetof(ItemObj, extra)',
    'model': 'sizeof(Model)', 'm.count': 'offsetof(Model, count)', 'm.node': 'offsetof(Model, nodeIndex)',
    'node': 'sizeof(ModelNode)',
    'anim': 'sizeof(Anim)', 'a.offset': 'offsetof(Anim, frameOffset)', 'a.rate': 'offsetof(Anim, frameRate)',
    'a.begin': 'offsetof(Anim, frameBegin)', 'a.end': 'offsetof(Anim, frameEnd)',
    'frame': 'sizeof(AnimFrame)',
    'L.models': 'offsetof(Level, models)', 'L.nodes': 'offsetof(Level, nodes)', 'L.anims': 'offsetof(Level, anims)',
    'L.frames': 'offsetof(Level, animFrames)',
    'xl.head': 'offsetof(ExtraInfoLara, head)', 'xl.torso': 'offsetof(ExtraInfoLara, torso)',
    'xl.armR': 'offsetof(ExtraInfoLara, armR)', 'xl.weapon': 'offsetof(ExtraInfoLara, weapon)',
    'arm': 'sizeof(ExtraInfoLara::Arm)', 'arm.angle': 'offsetof(ExtraInfoLara::Arm, angle)',
    'arm.anim': 'offsetof(ExtraInfoLara::Arm, animIndex)', 'arm.frame': 'offsetof(ExtraInfoLara::Arm, frameIndex)',
    'arm.flash': 'offsetof(ExtraInfoLara::Arm, flash)', 'arm.basis': 'offsetof(ExtraInfoLara::Arm, useBasis)',
    'NODE_FLAG_ROTX': 'NODE_FLAG_ROTX', 'NODE_FLAG_ROTY': 'NODE_FLAG_ROTY', 'NODE_FLAG_ROTZ': 'NODE_FLAG_ROTZ',
    'ITEM_LARA': 'ITEM_LARA',
    'i.room': 'offsetof(ItemObj, room)', 'i.intensity': 'offsetof(ItemObj, intensity)',
    'room': 'sizeof(Room)', 'r.info': 'offsetof(Room, info)', 'r.lights': 'offsetof(Room, data.lights)',
    'ri.x': 'offsetof(RoomInfo, x)', 'ri.z': 'offsetof(RoomInfo, z)', 'ri.lights': 'offsetof(RoomInfo, lightsCount)',
    'ri.ambient': 'offsetof(RoomInfo, ambient)', 'light': 'sizeof(Light)',
    'r.meshes': 'offsetof(Room, data.meshes)', 'roomMesh': 'sizeof(RoomMesh)', 'rooms': 'sizeof(Room)',
}

# PoseItem (lab/scene.h): lara, count, firstDraw, nodes, frameA, frameB, frameDelta, frameRate,
# visibleMask, pos[3], angle[3], extraCount, extra[4], torso[3], head[3], weapon, pad[3],
# then two PoseArm (frame, frameRate, angle[3], useBasis, pad)
POSE_ITEM = struct.Struct('>BBHIIIiiI3i3hh4h3h3hB3x')
POSE_ARM = struct.Struct('>Ii3hBx')
# PoseLight (lab/scene.h): the room's x and z, the item's intensity, the room's ambient and lights
# (count, first in LGHT); LGHT: Light (int16 x y z, radius, intensity)
POSE_LIGHT = struct.Struct('>2h3BxI')
# PoseStatic (lab/scene.h): its draw, quarter turn, intensity (STATIC_MESH_INTENSITY), place in the
# world; PoseShadow: its draw, its item (POSE's), the floor under it
POSE_STATIC = struct.Struct('>HBxi3i')
POSE_SHADOW = struct.Struct('>HHi')
NONE = 0xFFFFFFFF


def pack(mem, sym, recs, first_draws, header, statics, shadows, maps=None):
    L = layout(LAYOUT)
    u32 = lambda a: mem.unpack('>I', a)[0]
    u16 = lambda a: mem.unpack('>H', a)[0]
    level = sym('_level')
    models, nodes, anims, frames = (u32(level + L[k]) for k in ('L.models', 'L.nodes', 'L.anims', 'L.frames'))
    items = sym('_items')

    node_data, frame_data = bytearray(), bytearray()
    node_at, frame_at = {}, {}

    def model_nodes(model):
        """The model's nodes in PNOD (count - 1 of them, the first mesh's none)."""
        index = u16(models + model * L['model'] + L['m.node'])
        count = mem.unpack('>b', models + model * L['model'] + L['m.count'])[0]
        if index not in node_at:
            node_at[index] = len(node_data)
            node_data.extend(mem.bytes(nodes + index * L['node'], max(count - 1, 0) * L['node']))
        return node_at[index]

    def frame(address, count):
        """A frame (AnimFrame and its count packed angles) in PFRM."""
        if address not in frame_at:
            frame_at[address] = len(frame_data)
            frame_data.extend(mem.bytes(address, L['frame'] + 4 * count))
        return frame_at[address]

    def frames_of(anim_index, frame_index, count):
        """ItemObj::getFrames: frame A, frame B, the delta and the rate."""
        a = anims + anim_index * L['anim']
        base = frames + (u32(a + L['a.offset']) >> 1) * 2
        begin, end = u16(a + L['a.begin']), u16(a + L['a.end'])
        if begin == end:
            return base, base, 0, 1
        rate = mem.unpack('>B', a + L['a.rate'])[0]
        size = (L['frame'] >> 1) + (count << 1)     # in uint16s
        f = frame_index - begin
        index_a = f // rate                         # the 1, 2, 4 and slow paths agree (f >= 0)
        delta = f - index_a * rate
        index_b = index_a + 1
        if index_b * rate >= end:                   # the game's test, against the absolute end
            index_b = index_a
        fa, fb = base + index_a * size * 2, base + index_b * size * 2
        if not delta or fa == fb:
            return fa, fb, 0, rate
        if index_b * rate > end:
            rate -= index_b * rate - end
        return fa, fb, delta, rate

    poses, pose_lights, lights = bytearray(), bytearray(), bytearray()
    light_at = {}
    for r in recs:
        if r['type'] != 'object' or r['kind'] != 0 or r['index'] not in first_draws:
            continue
        it = items + r['index'] * L['item']
        model = mem.unpack('>B', it + L['i.type'])[0]
        count = mem.unpack('>b', models + model * L['model'] + L['m.count'])[0]
        anim, frame_index = u16(it + L['i.anim']), u16(it + L['i.frame'])
        a, b, delta, rate = frames_of(anim, frame_index, count)
        lara = model == L['ITEM_LARA']
        extra = u32(it + L['i.extra'])
        extras = []
        if extra and not lara:
            # an enemy's: rotHead, rotNeck, ... as many as its nodes' ROT flags take
            flags = L['NODE_FLAG_ROTX'] | L['NODE_FLAG_ROTY'] | L['NODE_FLAG_ROTZ']
            first = model_nodes(model)
            takes = sum(bin(struct.unpack_from('>H', node_data, first + k * 8 + 6)[0] & flags).count('1')
                        for k in range(count - 1))
            extras = list(mem.unpack(f'>{takes}h', extra))
        if len(extras) > 4:
            raise SystemExit(f'item {r["index"]}: {len(extras)} extra angles')
        torso = head = (0, 0, 0)
        weapon = 0
        arms = []
        if lara:
            torso = mem.unpack('>3h', extra + L['xl.torso'])
            head = mem.unpack('>3h', extra + L['xl.head'])
            weapon = mem.unpack('>B', extra + L['xl.weapon'])[0]
            for k in range(2):
                arm = extra + L['xl.armR'] + k * L['arm']
                if mem.unpack('>h', arm + L['arm.flash'])[0]:
                    raise SystemExit("Lara's muzzle flash: the lab's pose leaves it out")
                arm_anim = u16(arm + L['arm.anim'])
                at, arm_rate = NONE, 0
                if arm_anim:
                    fa = anims + arm_anim * L['anim']
                    size = (L['frame'] >> 1) + (count << 1)
                    address = frames + (u32(fa + L['a.offset']) >> 1) * 2 + u16(arm + L['arm.frame']) * size * 2
                    at, arm_rate = frame(address, count), mem.unpack('>B', fa + L['a.rate'])[0]
                arms.append(POSE_ARM.pack(at, arm_rate, *mem.unpack('>3h', arm + L['arm.angle']),
                                          mem.unpack('>B', arm + L['arm.basis'])[0]))
        else:
            arms = [POSE_ARM.pack(NONE, 0, 0, 0, 0, 0)] * 2
        poses += POSE_ITEM.pack(int(lara), count, first_draws[r['index']], model_nodes(model),
                                frame(a, count), frame(b, count), delta, rate, u32(it + L['i.visible']),
                                *mem.unpack('>3i', it + L['i.pos']), *mem.unpack('>3h', it + L['i.angle']),
                                len(extras), *(extras + [0] * (4 - len(extras))), *torso, *head, weapon)
        poses += b''.join(arms)
        room = u32(it + L['i.room'])
        info = u32(room + L['r.info'])
        count_lights = mem.unpack('>B', info + L['ri.lights'])[0]
        if room not in light_at:
            light_at[room] = len(lights) // L['light']
            lights += mem.bytes(u32(room + L['r.lights']), count_lights * L['light'])
        pose_lights += POSE_LIGHT.pack(*mem.unpack('>h', info + L['ri.x']), *mem.unpack('>h', info + L['ri.z']),
                                       mem.unpack('>B', it + L['i.intensity'])[0],
                                       mem.unpack('>B', info + L['ri.ambient'])[0], count_lights, light_at[room])
    # the static meshes: drawRoomStatic's place, quarter turn and intensity
    rooms = sym('_rooms')
    static_data = bytearray()
    for draw, owner in statics:
        room = rooms + owner['owner'] * L['rooms']
        info = u32(room + L['r.info'])
        xy, zf = mem.unpack('>2I', u32(room + L['r.meshes']) + owner['index'] * L['roomMesh'])
        x = (mem.unpack('>h', info + L['ri.x'])[0] << 8) + s32(xy) // 65536
        y = s16(xy)
        z = (mem.unpack('>h', info + L['ri.z'])[0] << 8) + s32(zf) // 65536
        static_data += POSE_STATIC.pack(draw, (zf >> 6) & 3, ((zf >> 8) & 0xFF) << 5, x, y, z)
    # the shadows: their floors, from their matrices
    shadow_data = bytearray()
    cam = header['camera']
    view = [s16(v) for v in header['rotation']]
    for draw, item, r in shadows:
        px, py, pz = struct.unpack_from('>3i', poses, item * (POSE_ITEM.size + 2 * POSE_ARM.size) + 28)
        dx, dz = px - cam[0], pz - cam[2]
        t = [s32(r['matrix'][k]) for k in (3, 7, 11)]
        row = max(range(3), key=lambda k: abs(view[3 * k + 1]))
        rest = view[3 * row] * dx + view[3 * row + 2] * dz
        dy = (t[row] - rest) // view[3 * row + 1]
        for guess in (dy, dy + 1, dy - 1):
            if all(s32(view[3 * k] * dx + view[3 * k + 1] * guess + view[3 * k + 2] * dz) == t[k] for k in range(3)):
                break
        else:
            raise SystemExit(f'shadow at draw {draw}: no floor gives its matrix')
        shadow_data += POSE_SHADOW.pack(draw, item, guess + cam[1])
    if maps is not None:
        maps['nodes'] = dict(node_at)
        maps['lights'] = {(room - sym('_rooms')) // L['rooms']: at for room, at in light_at.items()}
    return {'POSE': bytes(poses), 'PNOD': bytes(node_data), 'PFRM': bytes(frame_data),
            'SINC': mem.bytes(sym('_gSinCosTable'), 4096 * 4), 'PLIT': bytes(pose_lights), 'LGHT': bytes(lights),
            'STAT': bytes(static_data), 'SHDW': bytes(shadow_data)}


def s16(v):
    return (v & 0xFFFF) - ((v & 0x8000) << 1)


def s32(v):
    return (v & 0xFFFFFFFF) - ((v & 0x80000000) << 1)
