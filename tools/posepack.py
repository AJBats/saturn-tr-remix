"""The pose stage's inputs for the lab (lab/scene.h PoseItem), read from a
capture's dumps: each item the frame drew with its model's nodes, as
openlara/draw.h drawModel took it (its place and turn, its frames as
ItemObj::getFrames picks them, its extra angles; Lara's torso, head and
arms), and the level data its arithmetic reads (the nodes, the frames, the
sine table).

    pack(mem, sym, recs, first_draws) -> chunks POSE, PNOD, PFRM, SINC

recs: the capture's records; first_draws: each drawn item's first mesh's
draw (DRAW's index), by item index. The structures' layouts come from the
game's compiler (tools/gamelayout.py).
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
}

# PoseItem (lab/scene.h): lara, count, firstDraw, nodes, frameA, frameB, frameDelta, frameRate,
# visibleMask, pos[3], angle[3], extraCount, extra[4], torso[3], head[3], weapon, pad[3],
# then two PoseArm (frame, frameRate, angle[3], useBasis, pad)
POSE_ITEM = struct.Struct('>BBHIIIiiI3i3hh4h3h3hB3x')
POSE_ARM = struct.Struct('>Ii3hBx')
NONE = 0xFFFFFFFF


def pack(mem, sym, recs, first_draws):
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

    poses = bytearray()
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
    return {'POSE': bytes(poses), 'PNOD': bytes(node_data), 'PFRM': bytes(frame_data),
            'SINC': mem.bytes(sym('_gSinCosTable'), 4096 * 4)}
