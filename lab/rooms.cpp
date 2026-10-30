// Stage 1 the new way, the rooms seen (rooms.h): the scene's view through
// rooms.s, the rooms' jobs, and their checks against the game's.
#include "rooms.h"
#include "game.h"

#include <stddef.h>
#include <string.h>

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
static uint32_t gRoomCount;
static int16_t gCausticsTable[32];

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
    gCtx.geometry = sceneArray<const RoomGeometry>("RLGE");
    gCtx.rgeo = sceneArray<uint8_t>("RGEO");
    gCtx.vertices = gVertices;
    gCtx.jobs = (FaceRoom*)sceneAlloc(ROOM_JOBS * sizeof(FaceRoom));
    gCtx.caustics = sceneArray<const RoomCaustics>("CAUW");
    gCtx.table = gCausticsTable;
    gCtx.fills = (int32_t (*)[4])sceneAlloc(ROOM_FILLS * sizeof(*gCtx.fills));
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

// The fills the capture's master drew before its first draw (renderVoid's
// renderRect: a face whose first corner's vertex holds x y, the next width
// height) against the walk's: those that differ, or a count unlike.
static int32_t fillsCheck()
{
    uint32_t draws;
    const DrawScene* draw = sceneArray<const DrawScene>("DRAW", &draws);
    const Face* faces = sceneArray<const Face>("MPOL");
    const Vertex* vertices = sceneArray<const Vertex>("VERT");
    int32_t n = 0, bad = 0;
    for (; n < int32_t(draws) && draw[n].type == DRAW_COPY; n++)
    {
        const Vertex* v = (const Vertex*)((const uint8_t*)vertices + faces[draw[n].first].indices[0]);
        bad += n >= ROOM_FILLS || gCtx.fills[n][0] != v[0].x || gCtx.fills[n][1] != v[0].y ||
               gCtx.fills[n][2] != v[0].x + v[1].x || gCtx.fills[n][3] != v[0].y + v[1].y;
    }
    return bad + (n != gCtx.voids);
}

void roomsPoison()
{
    memset(gCtx.seen, 0xA5, ROOMS_LISTED * sizeof(RoomSeen));
    memset(gCtx.jobs, 0xA5, ROOM_JOBS * sizeof(FaceRoom));
    gCtx.listed = 0;
    gCtx.jobCount = 0;
}

int32_t roomsSeenCheck()
{
    int32_t voids;
    int32_t bad = gameRoomsCheck(gCtx.seen, gCtx.listed, voids);
    return bad + (gCtx.voids != voids) + gameRoomJobsCheck(gCtx.jobs, gCtx.jobCount) + fillsCheck();
}

// Two lists of jobs: those that differ (the same caustics the same table's),
// or lists of other lengths.
static int32_t jobsOff(const FaceRoom* a, int32_t aCount, const FaceRoom* b, int32_t bCount)
{
    int32_t bad = aCount != bCount;
    for (int32_t i = 0; i < aCount && i < bCount; i++)
    {
        bad += memcmp(a + i, b + i, offsetof(FaceRoom, caustics)) || !a[i].caustics != !b[i].caustics ||
               (a[i].caustics && memcmp(a[i].caustics, b[i].caustics, 32 * sizeof(int16_t)));
    }
    return bad;
}

// --- made-up views ---------------------------------------------------------------

static uint32_t gSeed = 0x6C078965;

static uint32_t rnd()
{
    gSeed = gSeed * 1664525 + 1013904223;
    return gSeed;
}

#define ROOM_PASSES     128
#define JOB_PASSES      64
#define FAR_BACK        24576   // past the draw distance (20 blocks)

struct RoomPortal
{
    uint16_t room;
    int16_t n[3];           // a unit axis
    int16_t v[4][3];
};

// Lowest work RAM, zeroed (the jobs' padding compares equal).
template <typename T>
static T* lowArray(uint32_t count)
{
    T* p = (T*)sceneAllocLow(count * sizeof(T));
    if (p) {
        memset(p, 0, count * sizeof(T));
    }
    return p;
}

int32_t roomsSeldomCheck()
{
    static RoomSeen *game, *seen;
    static FaceRoom *gameJobs, *jobs;
    static RoomLevel* level;
    static int16_t table[32];
    static int32_t gameRects[ROOM_FILLS][4], rects[ROOM_FILLS][4];
    if (!game)
    {
        game = lowArray<RoomSeen>(ROOMS_LISTED);
        seen = lowArray<RoomSeen>(ROOMS_LISTED);
        gameJobs = lowArray<FaceRoom>(ROOM_JOBS);
        jobs = lowArray<FaceRoom>(ROOM_JOBS);
        level = lowArray<RoomLevel>(gRoomCount);
        if (!game || !seen || !gameJobs || !jobs || !level)
            return -1;
    }
    const uint32_t* sines = sceneArray<const uint32_t>("SINC");
    const RoomPortal* portals = (const RoomPortal*)gCtx.portals;
    RoomsContext ctx = gCtx;
    int32_t view[12];
    ctx.view = view;
    ctx.seen = seen;
    ctx.jobs = jobs;
    ctx.table = table;
    ctx.fills = rects;
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

        int32_t count, voids, near, jobCount;
        gameRoomsRun(ctx.cameraRoom, ctx.camera, view, game, count, voids, near, gameJobs, jobCount, gameRects);
        roomsSeenAsm(&ctx);
        roomsJobsAsm(&ctx);
        bad += count != ctx.listed || voids != ctx.voids;
        for (int32_t i = 0; i < voids && i < ROOM_FILLS; i++) {
            bad += memcmp(rects[i], gameRects[i], sizeof(rects[i])) != 0;
        }
        for (int32_t i = 0; i < count && i < ctx.listed; i++)
        {
            const RoomSeen &a = ctx.seen[i], &b = game[i];
            bad += a.room != b.room || memcmp(a.window, b.window, sizeof(a.window)) || memcmp(a.m, b.m, sizeof(a.m));
        }
        bad += jobsOff(ctx.jobs, ctx.jobCount, gameJobs, jobCount);
        fills += voids;
        edges += near;
        more += count > 1;
    }

    // the jobs on made-up lists (rooms at random, repeated, any window and
    // matrix), the level's rooms wet or dry at random and now and then
    // without vertices: short lists, and long ones past the vertices' room,
    // or of small rooms past the jobs' limit
    ctx.level = level;
    int32_t empty = 0, capped = 0, full = 0, wet = 0, dry = 0, ripples = 0;
    for (int32_t pass = 0; pass < JOB_PASSES; pass++)
    {
        for (uint32_t r = 0; r < gRoomCount; r++)
        {
            level[r] = gCtx.level[r];
            level[r].flags = (level[r].flags & ~1) | (rnd() >> 30 == 0);
            if ((pass & 3) == 3 && level[r].vertices) {
                level[r].vertices = 1 + (rnd() >> 16) % 40;
            }
            if (rnd() >> 29 == 0) {
                level[r].vertices = 0;
            }
        }
        int32_t count = 1 + int32_t((rnd() >> 16) % ((pass & 1) ? ROOMS_LISTED : 16)), placeable = 0;
        for (int32_t i = 0; i < count; i++)
        {
            seen[i].room = (rnd() >> 16) % gRoomCount;
            for (int32_t k = 0; k < 4; k++) {
                seen[i].window[k] = int32_t(rnd());
            }
            for (int32_t k = 0; k < 12; k++) {
                seen[i].m[k] = int32_t(rnd());
            }
            placeable += level[seen[i].room].vertices != 0;
            empty += level[seen[i].room].vertices == 0;
        }
        ctx.listed = count;
        ctx.cameraRoom = (rnd() >> 16) % gRoomCount;
        for (int32_t k = 0; k < 3; k++) {
            ctx.camera[k] = gCtx.camera[k] + int32_t((rnd() >> 8) % 131072) - 65536;
        }
        roomsJobsAsm(&ctx);
        int32_t n = gameRoomJobsRun(level, seen, count, ctx.cameraRoom, ctx.camera, gameJobs);
        bad += jobsOff(ctx.jobs, ctx.jobCount, gameJobs, n);
        capped += n == ROOM_JOBS && placeable > ROOM_JOBS;
        full += n < (placeable < ROOM_JOBS ? placeable : ROOM_JOBS);
        for (int32_t k = 0; k < n; k++)
        {
            wet += gameJobs[k].flags != 0;
            dry += gameJobs[k].flags == 0;
            ripples += gameJobs[k].ripple;
        }
    }
    return bad + !fills + !edges + !more + !empty + !capped + !full + !wet + !dry + !ripples;
}
