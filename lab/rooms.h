// Stage 1 the new way, the rooms seen (rooms.s): the portal walk and each
// room's queued window and matrix, exactly the game's (gamerooms.cpp), in one
// call.
#ifndef ROOMS_H
#define ROOMS_H

#include <stdint.h>

#include "faces.h"
#include "scene.h"

// The rooms a walk may list: the game's MAX_ROOM_LIST is 32; the lab's room
// for more, so made-up views that see further can't overrun it.
#define ROOMS_LISTED    128
#define ROOM_JOBS       64      // rooms.cpp's MAX_ROOM_JOBS: those queued with vertices
#define ROOM_FILLS      64      // the fills past the draw distance kept (all counted)

struct RoomsContext
{
    const RoomLevel* level;     // +0  RLVL
    const void* portals;        // +4  RPRT
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
    const RoomGeometry* geometry;   // +88 RLGE
    uint8_t* rgeo;              // +92 RGEO
    Vertex* vertices;           // +96 gVertices
    FaceRoom* jobs;             // +100 the rooms listed with vertices, placed (roomsJobsAsm)
    int32_t jobCount;           // +104
    const RoomCaustics* caustics;   // +108 CAUW
    int16_t* table;             // +112 the caustics' table, a vertex's shade offset by its index (32)
    int32_t (*fills)[4];        // +116 the fills' rectangles, x0 y0 x1 y1 (the first ROOM_FILLS)
};

extern "C" void roomsSeenAsm(RoomsContext* ctx);
extern "C" void roomsJobsAsm(RoomsContext* ctx);

// The scene's view for the new way; the rooms it lists and their jobs (the
// rooms with vertices, placed, as stages 2 and 3 take them); against the
// capture's (RVIS, ROOM): the rooms that differ (or a list of another length).
void roomsInit();
void roomsSeen();
FaceRoom* roomsJobs(uint32_t &count);

// The fills past the draw distance the walk found (renderVoid's: x0 y0 x1
// y1), the first ROOM_FILLS: their count.
const int32_t (*roomsFills(int32_t &count))[4];
int32_t roomsSeenCheck();

// What the walk makes (the rooms listed, their jobs) overwritten with what it
// never makes, so a check after a frame that skipped it fails.
void roomsPoison();

// What the scene seldom gives the walk: made-up places and turns of the
// camera (portals past the draw distance, cut by the near plane, facing
// away), and the jobs made-up lists of rooms give (rooms without vertices,
// past the jobs' limit or the vertices' room, wet, dry, rippling), against
// the game's way on them: those that differ (and paths not taken).
int32_t roomsSeldomCheck();

#endif
