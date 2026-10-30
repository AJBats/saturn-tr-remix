"""The rooms' visibility for the lab (lab/scene.h RoomLevel, RoomView), read
from a capture's dumps: every room of the level as the portal walk reads it
(openlara room.h Room::getVisibleRooms: its place, its top, whether it is
under water, whether it has vertices, its portals), the view the walk starts
from (the camera's room, the viewport, whether the camera is under water),
and as the reference the rooms the frame queued, in order, with the window
and the matrix each was queued with (the capture's room records).

    pack(mem, sym, recs, header) -> chunks RLVL, RPRT, RVIS

The structures' layouts come from the game's compiler (tools/gamelayout.py).
"""
import struct

from gamelayout import layout

LAYOUT = {
    'room': 'sizeof(Room)', 'r.info': 'offsetof(Room, info)', 'r.portals': 'offsetof(Room, data.portals)',
    'ri.x': 'offsetof(RoomInfo, x)', 'ri.z': 'offsetof(RoomInfo, z)', 'ri.yTop': 'offsetof(RoomInfo, yTop)',
    'ri.vertices': 'offsetof(RoomInfo, verticesCount)', 'ri.portals': 'offsetof(RoomInfo, portalsCount)',
    'ri.flags': 'offsetof(RoomInfo, flags)', 'portal': 'sizeof(Portal)',
    'L.roomsCount': 'offsetof(Level, roomsCount)',
}

# RoomLevel (lab/scene.h): x, z, yTop, flags, portals, vertices, first portal in RPRT
ROOM_LEVEL = struct.Struct('>3hBBH2xI')
# RoomView: the camera's room, the viewport, the camera under water, the rooms queued (RoomQueued each)
ROOM_VIEW = struct.Struct('>H2x4iB3xI')
ROOM_QUEUED = struct.Struct('>H2x4i12i')


def room_size():
    return layout(LAYOUT)['room']


def s32(v):
    return (v & 0xFFFFFFFF) - ((v & 0x80000000) << 1)


def pack(mem, sym, recs, header):
    L = layout(LAYOUT)
    u32 = lambda a: mem.unpack('>I', a)[0]
    level = sym('_level')
    count = mem.unpack('>H', level + L['L.roomsCount'])[0]
    rooms = sym('_rooms')
    levels, portals = bytearray(), bytearray()
    for i in range(count):
        room = rooms + i * L['room']
        info = u32(room + L['r.info'])
        n = mem.unpack('>B', info + L['ri.portals'])[0]
        levels += ROOM_LEVEL.pack(*mem.unpack('>h', info + L['ri.x']), *mem.unpack('>h', info + L['ri.z']),
                                  *mem.unpack('>h', info + L['ri.yTop']), mem.unpack('>B', info + L['ri.flags'])[0],
                                  n, mem.unpack('>H', info + L['ri.vertices'])[0], len(portals) // L['portal'])
        portals += mem.bytes(u32(room + L['r.portals']), n * L['portal'])
    queued = [r for r in recs if r['type'] == 'room']
    if not queued:
        raise SystemExit('no rooms queued')
    camera = queued[-1]     # getVisibleRooms lists the camera's room last
    view = ROOM_VIEW.pack(camera['index'], *(s32(v) for v in camera['window']), header['cameraWater'], len(queued))
    for r in queued:
        view += ROOM_QUEUED.pack(r['index'], *(s32(v) for v in r['window']), *(s32(v) for v in r['matrix']))
    return {'RLVL': bytes(levels), 'RPRT': bytes(portals), 'RVIS': bytes(view)}
