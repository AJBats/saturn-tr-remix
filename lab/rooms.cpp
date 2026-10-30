// Stage 1 the new way, the rooms seen (rooms.h): the scene's view through
// rooms.s, and its checks against the game's.
#include "rooms.h"
#include "game.h"

#include <stddef.h>
#include <string.h>

static_assert(offsetof(RoomsContext, camera) == 12 && offsetof(RoomsContext, view) == 24 &&
              offsetof(RoomsContext, seen) == 44 && offsetof(RoomsContext, viewport) == 48 &&
              offsetof(RoomsContext, cameraRoom) == 64 && offsetof(RoomsContext, listEnd) == 72 &&
              offsetof(RoomsContext, viewMax) == 84, "rooms.s's RoomsContext");
static_assert(sizeof(RoomLevel) == 16 && offsetof(RoomLevel, first) == 12 && sizeof(RoomSeen) == 68,
              "rooms.s's RoomLevel, RoomSeen");

#define FRAME_WIDTH     352
#define FRAME_HEIGHT    224

static RoomsContext gCtx;
static int32_t gView[12];
static uint32_t gRoomCount;

void roomsInit()
{
    gCtx.level = sceneArray<const RoomLevel>("RLVL", &gRoomCount);
    gCtx.portals = sceneChunk("RPRT");
    gCtx.divTable = divTable;
    const PoseCamera* camera = sceneArray<const PoseCamera>("CAMR");
    const RoomView* view = sceneArray<const RoomView>("RVIS");
    for (int32_t r = 0; r < 3; r++)
    {
        gCtx.camera[r] = camera->pos[r];
        for (int32_t c = 0; c < 3; c++) {
            gView[r * 4 + c] = camera->rotation[r * 3 + c];
        }
        gView[r * 4 + 3] = 0;
    }
    gCtx.view = gView;
    gCtx.clip = (int32_t (*)[4])sceneAlloc(gRoomCount * sizeof(*gCtx.clip));
    gCtx.visible = (uint8_t*)sceneAlloc(gRoomCount);
    gCtx.list = (uint16_t*)sceneAlloc((ROOMS_LISTED + 1) * sizeof(uint16_t));
    gCtx.seen = (RoomSeen*)sceneAlloc(ROOMS_LISTED * sizeof(RoomSeen));
    for (uint32_t i = 0; i < gRoomCount; i++)
    {
        gCtx.clip[i][0] = FRAME_WIDTH;
        gCtx.clip[i][1] = FRAME_HEIGHT;
        gCtx.clip[i][2] = 0;
        gCtx.clip[i][3] = 0;
        gCtx.visible[i] = 0;
    }
    memcpy(gCtx.viewport, view->viewport, sizeof(gCtx.viewport));
    gCtx.cameraRoom = view->camera;
    gCtx.k18 = 1 << 18;
    gCtx.k20 = 1 << 20;
    gCtx.viewMax = (1024 * 20) << 14;
}

void roomsSeen()
{
    roomsSeenAsm(&gCtx);
}

int32_t roomsSeenCheck()
{
    int32_t voids;
    int32_t bad = gameRoomsCheck(gCtx.seen, gCtx.listed, voids);
    return bad + (gCtx.voids != voids);
}

// --- made-up views ---------------------------------------------------------------

static uint32_t gSeed = 0x6C078965;

static uint32_t rnd()
{
    gSeed = gSeed * 1664525 + 1013904223;
    return gSeed;
}

#define ROOM_PASSES     128
#define FAR_BACK        24576   // past the draw distance (20 blocks)

struct RoomPortal
{
    uint16_t room;
    int16_t n[3];           // a unit axis
    int16_t v[4][3];
};

int32_t roomsSeldomCheck()
{
    static RoomSeen* game;
    if (!game)
    {
        game = (RoomSeen*)sceneAllocLow(ROOMS_LISTED * sizeof(RoomSeen));
        if (!game)
            return -1;
    }
    const uint32_t* sines = sceneArray<const uint32_t>("SINC");
    const RoomPortal* portals = (const RoomPortal*)gCtx.portals;
    RoomsContext ctx = gCtx;
    int32_t view[12];
    ctx.view = view;
    int32_t bad = 0, fills = 0, edges = 0, more = 0;
    for (int32_t pass = 0; pass < ROOM_PASSES; pass++)
    {
        // the camera turned, and moved about its room, into another, or far
        // back from a side portal of another facing it (past the draw distance)
        uint32_t yaw = rnd() >> 16, pitch = (rnd() >> 18) - 0x2000;
        if ((pass & 3) == 1)
        {
            ctx.cameraRoom = rnd() % gRoomCount;
            const RoomLevel &room = gCtx.level[ctx.cameraRoom];
            ctx.camera[0] = (room.x << 8) + 512 + int32_t(rnd() % 3072);
            ctx.camera[1] = room.yTop + 256 + int32_t(rnd() % 1024);
            ctx.camera[2] = (room.z << 8) + 512 + int32_t(rnd() % 3072);
        }
        else if ((pass & 3) == 3)
        {
            const RoomPortal* portal = nullptr;
            while (!portal)
            {
                ctx.cameraRoom = rnd() % gRoomCount;
                const RoomLevel &room = gCtx.level[ctx.cameraRoom];
                if (room.portals)
                {
                    portal = portals + room.first + rnd() % room.portals;
                    if (portal->n[1])
                        portal = nullptr;
                }
            }
            const RoomLevel &room = gCtx.level[ctx.cameraRoom];
            int32_t centre[3] = {};
            for (int32_t c = 0; c < 4; c++) {
                for (int32_t k = 0; k < 3; k++) {
                    centre[k] += portal->v[c][k];
                }
            }
            ctx.camera[0] = (room.x << 8) + centre[0] / 4 + portal->n[0] * FAR_BACK;
            ctx.camera[1] = centre[1] / 4;
            ctx.camera[2] = (room.z << 8) + centre[2] / 4 + portal->n[2] * FAR_BACK;
            yaw = portal->n[2] > 0 ? 0x8000 : portal->n[2] < 0 ? 0 : portal->n[0] > 0 ? 0xC000 : 0x4000;
            pitch = 0;
        }
        else
        {
            ctx.cameraRoom = gCtx.cameraRoom;
            for (int32_t k = 0; k < 3; k++) {
                ctx.camera[k] = gCtx.camera[k] + int32_t(rnd() % 2048) - 1024;
            }
        }
        uint32_t scY = sines[(yaw & 0xFFFF) >> 4], scX = sines[(pitch & 0xFFFF) >> 4];
        int32_t sy = int32_t(scY) >> 16, cy = int32_t(scY) << 16 >> 16;
        int32_t sx = int32_t(scX) >> 16, cx = int32_t(scX) << 16 >> 16;
        view[0] = cy;               view[1] = 0;   view[2] = -sy;              view[3] = 0;
        view[4] = (sx * sy) >> 14;  view[5] = cx;  view[6] = (sx * cy) >> 14;  view[7] = 0;
        view[8] = (cx * sy) >> 14;  view[9] = -sx; view[10] = (cx * cy) >> 14; view[11] = 0;

        int32_t count, voids, near;
        gameRoomsRun(ctx.cameraRoom, ctx.camera, view, game, count, voids, near);
        roomsSeenAsm(&ctx);
        bad += count != ctx.listed || voids != ctx.voids;
        for (int32_t i = 0; i < count && i < ctx.listed; i++)
        {
            const RoomSeen &a = ctx.seen[i], &b = game[i];
            bad += a.room != b.room || memcmp(a.window, b.window, sizeof(a.window)) || memcmp(a.m, b.m, sizeof(a.m));
        }
        fills += voids;
        edges += near;
        more += count > 1;
    }
    return bad + !fills + !edges + !more;
}
