// The rooms seen (rooms.s): the portal walk and each room's queued window
// and matrix, exactly the game's (room.h getVisibleRooms, draw.h queueRoom),
// then the jobs of the rooms with vertices, placed, as the vertices and the
// faces take them; the fills past the draw distance.
#ifndef RENDER_ROOMS_H
#define RENDER_ROOMS_H

#include <stdint.h>

#include "types.h"

namespace render {

// The rooms a walk may list: the game's MAX_ROOM_LIST is 32; room for more,
// so views that see further (the lab's made-up ones) can't overrun it.
#define ROOMS_LISTED    128
#define ROOM_JOBS       64      // rooms.cpp's MAX_ROOM_JOBS: those queued with vertices
#define ROOM_FILLS      64      // the fills past the draw distance kept (all counted)

struct RoomsContext
{
    const RoomLevel* level;     // +0  the level's rooms
    const void* portals;        // +4  their portals
    const uint16_t* divTable;   // +8
    int32_t camera[3];          // +12
    const int32_t* view;        // +24 the view's matrix (12 int32, no translation)
    int32_t (*clip)[4];         // +28 each room's window, x0 y0 x1 y1 (empty between frames)
    uint8_t* visible;           // +32 each room's listing (0 between frames)
    uint16_t* list;             // +36 the rooms listed, in order
    int32_t listed;             // +40
    RoomSeen* seen;             // +44 their windows and matrices, as queued
    int32_t viewport[4];        // +48
    uint32_t cameraRoom;        // +64
    int32_t voids;              // +68 the fills past the draw distance
    uint16_t* listEnd;          // +72 rooms.s's own
    int32_t k18, k20;           // +76 2^18, 2^20
    int32_t viewMax;            // +84 VIEW_MAX_F
    const RoomGeometry* geometry;   // +88 each room's
    uint8_t* rgeo;              // +92 where it lies
    Vertex* vertices;           // +96 where the jobs place their rooms' vertices
    FaceRoom* jobs;             // +100 the rooms listed with vertices, placed (roomsJobsAsm)
    int32_t jobCount;           // +104
    const RoomCaustics* caustics;   // +108 the caustics' wave
    int16_t* table;             // +112 the caustics' table, a vertex's shade offset by its index (32)
    int32_t (*fills)[4];        // +116 the fills' rectangles, x0 y0 x1 y1 (the first ROOM_FILLS)
};

extern "C" void roomsSeenAsm(RoomsContext* ctx);
extern "C" void roomsJobsAsm(RoomsContext* ctx);

// The level's rooms as the walk reads them (RoomLevel each, their portals in
// the game's layout), their geometry (RoomGeometry: offsets in rgeo), the
// caustics' wave (the caller moves its phase each frame), and the vertices
// the jobs place their rooms' in.
#define MAX_LEVEL_ROOMS 139     // openlara MAX_ROOMS: LEVEL7A
struct RoomsLevel
{
    const RoomLevel* level;
    uint32_t count;
    const void* portals;
    const RoomGeometry* geometry;
    uint8_t* rgeo;
    const RoomCaustics* caustics;
    Vertex* vertices;
};
void roomsInit(const RoomsLevel &level);

// The frame's view: the camera, the view's matrix (12 int32, no translation),
// the viewport (x0 y0 x1 y1), the camera's room.
void roomsView(const int32_t camera[3], const int32_t view[12], const int32_t viewport[4], uint32_t cameraRoom);

// The walk from the view, and the jobs: the rooms listed with vertices.
void roomsSeen();
FaceRoom* roomsJobs(uint32_t &count);

// The fills past the draw distance the walk found (renderVoid's: x0 y0 x1
// y1), the first ROOM_FILLS: their count.
const int32_t (*roomsFills(int32_t &count))[4];

// The walk's context, for checks.
RoomsContext& roomsContext();

}

#endif
