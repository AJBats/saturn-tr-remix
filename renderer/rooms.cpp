// The rooms seen (rooms.h): the walk and the jobs through rooms.s.
#include "rooms.h"

#include <stddef.h>
#include <string.h>

namespace render {

static_assert(offsetof(RoomsContext, camera) == 12 && offsetof(RoomsContext, view) == 24 &&
              offsetof(RoomsContext, seen) == 44 && offsetof(RoomsContext, viewport) == 48 &&
              offsetof(RoomsContext, cameraRoom) == 64 && offsetof(RoomsContext, listEnd) == 72 &&
              offsetof(RoomsContext, viewMax) == 84 && offsetof(RoomsContext, geometry) == 88 &&
              offsetof(RoomsContext, jobs) == 100 && offsetof(RoomsContext, table) == 112 &&
              offsetof(RoomsContext, fills) == 116,
              "rooms.s's RoomsContext");
static_assert(sizeof(RoomLevel) == 16 && offsetof(RoomLevel, first) == 12 && sizeof(RoomSeen) == 68,
              "rooms.s's RoomLevel, RoomSeen");
static_assert(sizeof(FaceRoom) == 112 && offsetof(FaceRoom, quadsCount) == 16 && offsetof(FaceRoom, camera) == 32 &&
              offsetof(FaceRoom, m) == 44 && offsetof(FaceRoom, t) == 64 && offsetof(FaceRoom, clip) == 76 &&
              offsetof(FaceRoom, flags) == 92 && offsetof(FaceRoom, verticesCount) == 104 &&
              offsetof(FaceRoom, room) == 106 && offsetof(FaceRoom, caustics) == 108 &&
              sizeof(RoomGeometry) == 32 && offsetof(RoomGeometry, quadsCount) == 12 &&
              offsetof(RoomCaustics, starts) == 68, "rooms.s's FaceRoom, RoomGeometry, RoomCaustics");

#define FRAME_WIDTH     352
#define FRAME_HEIGHT    224

static RoomsContext gCtx;
static int32_t gView[12];
static int32_t gClip[MAX_LEVEL_ROOMS][4];
static uint8_t gVisible[MAX_LEVEL_ROOMS];
static uint16_t gList[ROOMS_LISTED + 1];
static RoomSeen gSeen[ROOMS_LISTED];
static FaceRoom gJobs[ROOM_JOBS];
static int16_t gCausticsTable[32];
static int32_t gFills[ROOM_FILLS][4];

void roomsInit(const RoomsLevel &level)
{
    gCtx.level = level.level;
    gCtx.portals = level.portals;
    gCtx.divTable = divTable;
    gCtx.view = gView;
    gCtx.clip = gClip;
    gCtx.visible = gVisible;
    gCtx.list = gList;
    gCtx.seen = gSeen;
    for (uint32_t i = 0; i < level.count && i < MAX_LEVEL_ROOMS; i++)
    {
        gClip[i][0] = FRAME_WIDTH;
        gClip[i][1] = FRAME_HEIGHT;
        gClip[i][2] = 0;
        gClip[i][3] = 0;
        gVisible[i] = 0;
    }
    gCtx.k18 = 1 << 18;
    gCtx.k20 = 1 << 20;
    gCtx.viewMax = (1024 * 20) << 14;
    gCtx.geometry = level.geometry;
    gCtx.rgeo = level.rgeo;
    gCtx.vertices = level.vertices;
    gCtx.jobs = gJobs;
    gCtx.caustics = level.caustics;
    gCtx.table = gCausticsTable;
    gCtx.fills = gFills;
}

void roomsView(const int32_t camera[3], const int32_t view[12], const int32_t viewport[4], uint32_t cameraRoom)
{
    for (int32_t k = 0; k < 3; k++) {
        gCtx.camera[k] = camera[k];
    }
    for (int32_t k = 0; k < 12; k++) {
        gView[k] = view[k];
    }
    for (int32_t k = 0; k < 4; k++) {
        gCtx.viewport[k] = viewport[k];
    }
    gCtx.cameraRoom = cameraRoom;
}

RoomsContext& roomsContext()
{
    return gCtx;
}

void roomsSeen()
{
    roomsSeenAsm(&gCtx);
    roomsJobsAsm(&gCtx);
}

FaceRoom* roomsJobs(uint32_t &count)
{
    count = gCtx.jobCount;
    return gCtx.jobs;
}

const int32_t (*roomsFills(int32_t &count))[4]
{
    count = gCtx.voids < ROOM_FILLS ? gCtx.voids : ROOM_FILLS;
    return gCtx.fills;
}

}
