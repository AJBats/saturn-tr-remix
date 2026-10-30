// Stage 1 the game's way, the rooms seen (game.h): openlara room.h's portal
// walk (Room::getVisibleRooms, addVisibleRoom, checkPortal: each portal facing
// the camera projected into its room's window, the next room's window grown
// to it, past the draw distance a fill instead), then draw.h queueRoom's
// matrix for each room seen (the view placed at the room). Compiled as the
// game compiles room.h and draw.h (main.cpp, -Os: the Makefile), on
// common.cpp's matrices.
#include "faces.h"
#include "game.h"
#include "gamematrix.h"
#include "scene.h"

#include <string.h>

#define FRAME_WIDTH     352
#define FRAME_HEIGHT    224
#define PROJ_SHIFT      4
#define DIV_TABLE_SIZE  1281
#define VIEW_MAX_F      ((1024 * 20) << FIXED_SHIFT)
#define MAX_ROOMS_SEEN  128     // the game's MAX_ROOM_LIST is 32: room for made-up views
#define MAX_ROOM_JOBS   64
#define MAX_FILLS       64
#define FACE_WATER      (1u << 30)
#define ROOM_FLAG_WATER 1
#define X_MAX(a, b)     ((a) > (b) ? (a) : (b))
#define X_MIN(a, b)     ((a) < (b) ? (a) : (b))

#define PERSPECTIVE(x, y, z) {\
    int32_t dz = (z >> PROJ_SHIFT);\
    if (dz >= DIV_TABLE_SIZE) dz = DIV_TABLE_SIZE - 1;\
    int32_t d = divTable[dz];\
    x = (x * d) >> (16 - PROJ_SHIFT);\
    y = (y * d) >> (16 - PROJ_SHIFT);\
}

struct RectMinMax
{
    int32_t x0, y0, x1, y1;
};

struct Portal
{
    uint16_t roomIndex;
    int16_t n[3];
    int16_t v[4][3];
};

struct vec3i
{
    int32_t x, y, z;
};

static const RoomLevel* gLevel;
static uint32_t gLevelCount;
static const Portal* gPortals;
static const RoomView* gView;
static const int32_t* gCamera;
static uint32_t gCameraRoom;

// Room's state: its window and whether it is listed, reset after each frame
static RectMinMax* gClip;
static uint8_t* gVisible;
static uint16_t gList[MAX_ROOMS_SEEN + 1];
static int32_t gListCount;
static RoomSeen* gSeen;         // the rooms queued, in order
static int32_t gSeenCount;
static const RoomGeometry* gGeometry;   // RLGE
static uint8_t* gRoomGeometry;          // RGEO
static const RoomCaustics* gCaustics;   // CAUW
static FaceRoom* gJobs;         // the rooms queued with vertices placed, as stages 2 and 3 take them
static int32_t gJobCount;
static int16_t gCausticsTable[32];
static int32_t gVoids;          // the fills past the draw distance
static int32_t (*gFills)[4];    // ... the first MAX_FILLS, x0 y0 x1 y1
static int32_t gNearEdges;      // portals cut by the near plane, kept
static RectMinMax gViewport;

static void renderVoid(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if (gVoids < MAX_FILLS)
    {
        gFills[gVoids][0] = x0;
        gFills[gVoids][1] = y0;
        gFills[gVoids][2] = x1;
        gFills[gVoids][3] = y1;
    }
    gVoids++;
}

bool checkPortal(uint32_t index, const Portal* portal)
{
    const RoomLevel* info = gLevel + index;
    RectMinMax &clip = gClip[index];

    vec3i d;
    d.x = portal->v[0][0] - gCamera[0] + (info->x << 8);
    d.y = portal->v[0][1] - gCamera[1];
    d.z = portal->v[0][2] - gCamera[2] + (info->z << 8);

    Matrix &m = matrixGet();

    vec3i pv[4];

    if (portal->n[0] * d.x + portal->n[1] * d.y + portal->n[2] * d.z >= 0)
        return false;

    int32_t x0 = clip.x1 - (FRAME_WIDTH >> 1);
    int32_t y0 = clip.y1 - (FRAME_HEIGHT >> 1);
    int32_t x1 = clip.x0 - (FRAME_WIDTH >> 1);
    int32_t y1 = clip.y0 - (FRAME_HEIGHT >> 1);

    int32_t znear = 0, zfar = 0;

    for (int32_t i = 0; i < 4; i++)
    {
        int32_t px = portal->v[i][0];
        int32_t py = portal->v[i][1];
        int32_t pz = portal->v[i][2];

        int32_t x = m.e00 * px + m.e01 * py + m.e02 * pz + m.e03;
        int32_t y = m.e10 * px + m.e11 * py + m.e12 * pz + m.e13;
        int32_t z = m.e20 * px + m.e21 * py + m.e22 * pz + m.e23;

        pv[i].x = x;
        pv[i].y = y;
        pv[i].z = z;

        if (z <= 0) {
            znear++;
            continue;
        }

        if (z > VIEW_MAX_F)
        {
            z = VIEW_MAX_F;
            zfar++;
        }

        x >>= FIXED_SHIFT;
        y >>= FIXED_SHIFT;
        z >>= FIXED_SHIFT;

        PERSPECTIVE(x, y, z);

        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
    }

    if (znear == 4)
        return false;

    if (zfar == 4)
    {
        x0 = X_MAX(x0 + (FRAME_WIDTH >> 1), clip.x0);
        y0 = X_MAX(y0 + (FRAME_HEIGHT >> 1), clip.y0);
        x1 = X_MIN(x1 + (FRAME_WIDTH >> 1), clip.x1);
        y1 = X_MIN(y1 + (FRAME_HEIGHT >> 1), clip.y1);
        if (x0 < x1 && y0 < y1) {
            renderVoid(x0, y0, x1, y1);
        }
        return false;
    }

    x0 += (FRAME_WIDTH >> 1);
    y0 += (FRAME_HEIGHT >> 1);
    x1 += (FRAME_WIDTH >> 1);
    y1 += (FRAME_HEIGHT >> 1);

    if (znear)
    {
        gNearEdges++;
        vec3i* a = pv;
        vec3i* b = pv + 3;
        for (int32_t i = 0; i < 4; i++)
        {
            if ((a->z < 0) ^ (b->z < 0))
            {
                if (a->x < 0 && b->x < 0) {
                    x0 = 0;
                } else if (a->x > 0 && b->x > 0) {
                    x1 = FRAME_WIDTH;
                } else {
                    x0 = 0;
                    x1 = FRAME_WIDTH;
                }

                if (a->y < 0 && b->y < 0) {
                    y0 = 0;
                } else if (a->y > 0 && b->y > 0) {
                    y1 = FRAME_HEIGHT;
                } else {
                    y0 = 0;
                    y1 = FRAME_HEIGHT;
                }
            }
            b = a;
            a++;
        }
    }

    if (x0 < clip.x0) x0 = clip.x0;
    if (x1 > clip.x1) x1 = clip.x1;
    if (y0 < clip.y0) y0 = clip.y0;
    if (y1 > clip.y1) y1 = clip.y1;

    if (x0 >= x1 || y0 >= y1)
        return false;

    RectMinMax &next = gClip[portal->roomIndex];

    if (x0 < next.x0) next.x0 = x0;
    if (x1 > next.x1) next.x1 = x1;
    if (y0 < next.y0) next.y0 = y0;
    if (y1 > next.y1) next.y1 = y1;

    return true;
}

uint16_t* addVisibleRoom(uint32_t index, uint16_t* list)
{
    const RoomLevel* info = gLevel + index;

    matrixPush();
    matrixTranslateAbs(info->x << 8, 0, info->z << 8);

    for (int32_t i = 0; i < info->portals; i++)
    {
        const Portal* portal = gPortals + info->first + i;

        if (checkPortal(index, portal))
        {
            uint32_t next = portal->roomIndex;

            list = addVisibleRoom(next, list);

            if (!gVisible[next])
            {
                gVisible[next] = 1;
                *list++ = next;
            }
        }
    }

    matrixPop();

    return list;
}

// draw.h queueRoom's matrix, as renderRoom sees it
void queueRoom(uint32_t index)
{
    const RoomLevel* info = gLevel + index;
    const RectMinMax &clip = gClip[index];

    matrixPush();
    matrixTranslateAbs(info->x << 8, info->yTop, info->z << 8);

    RoomSeen &seen = gSeen[gSeenCount++];
    seen.room = index;
    seen.window[0] = clip.x0;
    seen.window[1] = clip.y0;
    seen.window[2] = clip.x1;
    seen.window[3] = clip.y1;
    memcpy(seen.m, &matrixGet(), sizeof(seen.m));

    matrixPop();
}

// rooms.cpp roomsQueue's job for each room queued with vertices (draw.h's
// camera room's water; at most MAX_ROOM_JOBS), placeRooms' place for its
// vertices from gBottom up (none: never drawn), roomBegin's caustics: as the
// stages after take them.
static int32_t roomJobs(const RoomSeen* seen, int32_t count, uint32_t cameraRoom, FaceRoom* jobs)
{
    bool cameraWater = gLevel[cameraRoom].flags & ROOM_FLAG_WATER;
    int32_t queued = 0, placed = 0, bottom = MASTER_VERTICES;
    bool wet = false;
    for (int32_t i = 0; i < count; i++)
    {
        uint32_t index = seen[i].room;
        const RoomLevel* info = gLevel + index;
        if (info->vertices == 0 || queued == MAX_ROOM_JOBS)
            continue;
        queued++;
        int32_t size = (info->vertices + 15) & ~15;
        if (bottom + size > MAX_VERTICES)
            continue;

        const RoomGeometry &g = gGeometry[index];
        FaceRoom &job = jobs[placed++];
        job.quads = (RoomQuad*)(gRoomGeometry + g.quads);
        job.triangles = (const RoomQuad*)(gRoomGeometry + g.triangles);
        job.points = (const RoomVertex*)(gRoomGeometry + g.points);
        job.vertices = gVertices + bottom;
        bottom += size;
        job.quadsCount = g.quadsCount;
        job.trianglesCount = g.trianglesCount;
        memcpy(job.facing, g.facing, sizeof(job.facing));
        job.camera[0] = gCamera[0] - (info->x << 8);
        job.camera[1] = gCamera[1] - info->yTop;
        job.camera[2] = gCamera[2] - (info->z << 8);
        for (int32_t r = 0; r < 3; r++)
        {
            for (int32_t c = 0; c < 3; c++) {
                job.m[r * 3 + c] = seen[i].m[r * 4 + c];
            }
            job.t[r] = seen[i].m[r * 4 + 3] >> (FIXED_SHIFT - 8);
        }
        memcpy(job.clip, seen[i].window, sizeof(job.clip));
        bool water = info->flags & ROOM_FLAG_WATER;
        job.flags = water || cameraWater ? FACE_WATER : 0;
        job.ripple = water != cameraWater;
        job.phase = gCaustics->phase;
        job.verticesCount = info->vertices;
        job.room = index;
        job.caustics = job.flags ? gCausticsTable : nullptr;
        wet = wet || job.flags;
    }
    if (wet)
    {
        for (int32_t i = 0; i < 32; i++) {
            gCausticsTable[i] = gCaustics->wave[(gCaustics->starts[i] + gCaustics->phase) & 31];
        }
    }
    return placed;
}

int32_t gameRoomJobsRun(const RoomLevel* level, const RoomSeen* seen, int32_t count, uint32_t cameraRoom,
                        const int32_t* camera, FaceRoom* jobs)
{
    const RoomLevel* sceneLevel = gLevel;
    const int32_t* sceneCamera = gCamera;
    gLevel = level;
    gCamera = camera;
    int32_t placed = roomJobs(seen, count, cameraRoom, jobs);
    gLevel = sceneLevel;
    gCamera = sceneCamera;
    return placed;
}

int32_t gameRoomsInit()
{
    gLevel = sceneArray<const RoomLevel>("RLVL", &gLevelCount);
    gPortals = sceneArray<const Portal>("RPRT");
    gView = sceneArray<const RoomView>("RVIS");
    gCamera = sceneArray<const PoseCamera>("CAMR")->pos;
    gCameraRoom = gView->camera;
    gGeometry = sceneArray<const RoomGeometry>("RLGE");
    gRoomGeometry = sceneArray<uint8_t>("RGEO");
    gCaustics = sceneArray<const RoomCaustics>("CAUW");
    gJobs = (FaceRoom*)sceneAlloc(MAX_ROOM_JOBS * sizeof(FaceRoom));
    gFills = (int32_t (*)[4])sceneAlloc(MAX_FILLS * sizeof(*gFills));
    gClip = (RectMinMax*)sceneAlloc(gLevelCount * sizeof(RectMinMax));
    gVisible = (uint8_t*)sceneAlloc(gLevelCount);
    gSeen = (RoomSeen*)sceneAlloc(MAX_ROOMS_SEEN * sizeof(RoomSeen));
    gViewport.x0 = gView->viewport[0];
    gViewport.y0 = gView->viewport[1];
    gViewport.x1 = gView->viewport[2];
    gViewport.y1 = gView->viewport[3];
    for (uint32_t i = 0; i < gLevelCount; i++)
    {
        gClip[i].x0 = FRAME_WIDTH;
        gClip[i].y0 = FRAME_HEIGHT;
        gClip[i].x1 = 0;
        gClip[i].y1 = 0;
    }
    return gView->count;
}

void gameRoomsRun(uint32_t room, const int32_t* camera, const int32_t* view, RoomSeen* out, int32_t &count,
                  int32_t &voids, int32_t &near, FaceRoom* jobs, int32_t &jobCount, int32_t (*fills)[4])
{
    const int32_t* sceneCamera = gCamera;
    RoomSeen* sceneSeen = gSeen;
    FaceRoom* sceneJobs = gJobs;
    int32_t (*sceneFills)[4] = gFills;
    gJobs = jobs;
    gFills = fills;
    Matrix sceneView = gMatrixStack[0];
    gCameraRoom = room;
    gCamera = camera;
    gCameraViewPos = camera;
    memcpy(&gMatrixStack[0], view, sizeof(Matrix));
    gSeen = out;
    gNearEdges = 0;
    gameRooms();
    count = gSeenCount;
    voids = gVoids;
    near = gNearEdges;
    jobCount = gJobCount;
    gJobs = sceneJobs;
    gFills = sceneFills;
    gCameraRoom = gView->camera;
    gCamera = sceneCamera;
    gCameraViewPos = sceneCamera;
    gSeen = sceneSeen;
    gMatrixStack[0] = sceneView;
    gameRooms();
}

void gameRooms()
{
    gMatrixPtr = gMatrixStack;
    gVoids = 0;
    gSeenCount = 0;
    uint32_t camera = gCameraRoom;
    gClip[camera] = gViewport;
    uint16_t* list = addVisibleRoom(camera, gList);
    *list++ = camera;
    gListCount = list - gList;
    for (int32_t i = 0; i < gListCount; i++) {
        queueRoom(gList[i]);
    }
    gJobCount = roomJobs(gSeen, gSeenCount, camera, gJobs);
    // the rooms reset for the next frame (draw.h drawRooms: room->reset())
    for (int32_t i = 0; i < gListCount; i++)
    {
        uint32_t r = gList[i];
        gVisible[r] = 0;
        gClip[r].x0 = FRAME_WIDTH;
        gClip[r].y0 = FRAME_HEIGHT;
        gClip[r].x1 = 0;
        gClip[r].y1 = 0;
    }
}

int32_t gameRoomJobsCheck(const FaceRoom* jobs, int32_t count)
{
    if (!jobs)
    {
        jobs = gJobs;
        count = gJobCount;
    }
    uint32_t rooms;
    const RoomScene* game = sceneArray<const RoomScene>("ROOM", &rooms);
    const int16_t (*caustics)[32] = (const int16_t (*)[32])sceneChunk("CAUS");
    int32_t bad = count != int32_t(rooms);
    for (int32_t k = 0; k < count; k++)
    {
        const FaceRoom &a = jobs[k];
        uint32_t i = 0;
        while (i < rooms && game[i].room != a.room) {
            i++;
        }
        if (i == rooms)
        {
            bad++;
            continue;
        }
        const RoomScene &b = game[i];
        bool wet = b.wet != 0;
        bad += memcmp(a.m, b.m, sizeof(a.m)) || a.t[0] != b.tx || a.t[1] != b.ty || a.t[2] != b.tz ||
               memcmp(a.clip, b.clip, sizeof(a.clip)) || memcmp(a.camera, b.camera, sizeof(a.camera)) ||
               a.phase != b.causticsFrame || (a.flags != 0) != wet || a.ripple != (b.ripple != 0) ||
               a.vertices != gVertices + b.base || a.verticesCount != b.verticesCount ||
               a.quadsCount != b.quadsCount || a.trianglesCount != b.trianglesCount ||
               memcmp(a.facing, b.facing, sizeof(a.facing)) || (const uint8_t*)a.quads != gRoomGeometry + b.quads ||
               (const uint8_t*)a.triangles != gRoomGeometry + b.triangles ||
               (const uint8_t*)a.points != gRoomGeometry + b.vertices || (a.caustics != nullptr) != wet ||
               (wet && memcmp(a.caustics, caustics[i], sizeof(caustics[i])));
    }
    return bad;
}

int32_t gameRoomsCheck(const RoomSeen* seen, int32_t count, int32_t &voids)
{
    if (!seen)
    {
        seen = gSeen;
        count = gSeenCount;
    }
    voids = gVoids;
    int32_t bad = count != int32_t(gView->count);
    for (int32_t i = 0; i < count && i < int32_t(gView->count); i++)
    {
        const RoomSeen &a = seen[i], &b = gView->queued[i];
        bad += a.room != b.room || memcmp(a.window, b.window, sizeof(a.window)) || memcmp(a.m, b.m, sizeof(a.m));
    }
    return bad;
}
