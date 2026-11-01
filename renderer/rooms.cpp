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
              offsetof(RoomsContext, fills) == 116 && offsetof(RoomsContext, farScaled) == 120 &&
              offsetof(RoomsContext, rotation) == 124 && offsetof(RoomsContext, entries) == 144 &&
              offsetof(RoomsContext, entryCount) == 148 && sizeof(RoomEntry) == 8,
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
static RoomEntry gEntries[ROOM_ENTRIES];
static int16_t gEntryFirst[MAX_LEVEL_ROOMS];    // each room's first entry, then gEntryNext's
static int16_t gEntryNext[ROOM_ENTRIES];
static bool gEntriesListed;     // gEntryFirst made for the walk's entries
static bool gItemsTest;
static const uint32_t* gSines;

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
    gCtx.farScaled = 1;
    gCtx.entries = gEntries;
    gSines = level.sines;
}

void roomsFarScaled(bool on)
{
    gCtx.farScaled = on;
}

void roomsFar(int32_t far)
{
    gCtx.viewMax = far << 14;
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
    gEntriesListed = false;
}

void roomsItemsTest(bool on)
{
    gItemsTest = on;
}

// The test's margin, world units: the frames between A and B, an enemy's
// head and neck turned past its frame's box, the planes' rounding.
#define SEEN_MARGIN     256
#define SEEN_DEPTH      24      // rooms back from an item's: further, it is taken as seen
#define WALL            (-127 * 256)

// A box turned about y: its centre from the camera, its heading's sine and
// cosine (2^14), its half sizes along its own x y z; places and sizes in 16
// world units (BOX_SHIFT: the test's sums fit a longword), sizes rounded up.
#define BOX_SHIFT       4
#define BOX_FAR         (1 << 14)   // a centre further than this many units, the box is taken as seen
struct Box
{
    int32_t c[3];
    int32_t sin, cos;
    int32_t h[3];
};

static uint8_t gSeenState[MAX_LEVEL_ROOMS];    // 1 searched, 2 seen from the camera's room

// v >> s, arithmetic, s 0 .. 63, the result's 32 bits, in the CPU's own
// shifts (GCC's 64-bit shift is a library call).
static inline int32_t shr64(int64_t v, int32_t s)
{
    int32_t high = int32_t(uint64_t(v) >> 32);
    uint32_t low = uint32_t(v);
    if (s >= 32)
        return high >> (s - 32);
    if (!s)
        return int32_t(low);
    return int32_t((uint32_t(high) << (32 - s)) | (low >> s));
}

// The number of bits v takes.
static int32_t bitLength(uint32_t v)
{
    int32_t bits = 0;
    if (v >> 16) {
        v >>= 16;
        bits += 16;
    }
    if (v >> 8) {
        v >>= 8;
        bits += 8;
    }
    if (v >> 4) {
        v >>= 4;
        bits += 4;
    }
    if (v >> 2) {
        v >>= 2;
        bits += 2;
    }
    if (v >> 1) {
        v >>= 1;
        bits += 1;
    }
    return bits + int32_t(v);
}

// A portal's planes through the camera and each of its edges, their normals
// cut to 15 bits and turned to its inside (0 0 0: the camera in the plane),
// kept for the frame in a slot of PLANE_SLOTS once made.
#define PLANE_SLOTS     32
static int16_t gPlanes[PLANE_SLOTS][4][3];
static int8_t gEntrySlot[ROOM_ENTRIES];     // -1: none yet
static int32_t gSlotsUsed;

static const int16_t (*portalPlanes(int32_t e, int16_t (*own)[3]))[3]
{
    if (gEntrySlot[e] >= 0)
        return gPlanes[gEntrySlot[e]];
    int16_t (*planes)[3] = own;
    if (gSlotsUsed < PLANE_SLOTS)
    {
        gEntrySlot[e] = int8_t(gSlotsUsed);
        planes = gPlanes[gSlotsUsed++];
    }
    const RoomEntry &entry = gEntries[e];
    const RoomLevel &from = gCtx.level[entry.from];
    const int16_t* corner = (const int16_t*)(entry.portal + 8);
    int32_t w[4][3];
    for (int32_t i = 0; i < 4; i++)
    {
        w[i][0] = (from.x << 8) + corner[3 * i] - gCtx.camera[0];
        w[i][1] = corner[3 * i + 1] - gCtx.camera[1];
        w[i][2] = (from.z << 8) + corner[3 * i + 2] - gCtx.camera[2];
    }
    int32_t m[3];
    for (int32_t k = 0; k < 3; k++) {
        m[k] = w[0][k] + w[1][k] + w[2][k] + w[3][k];
    }
    for (int32_t i = 0; i < 4; i++)
    {
        const int32_t* a = w[i];
        const int32_t* b = w[(i + 1) & 3];
        int64_t n[3] = {
            int64_t(a[1]) * b[2] - int64_t(a[2]) * b[1],
            int64_t(a[2]) * b[0] - int64_t(a[0]) * b[2],
            int64_t(a[0]) * b[1] - int64_t(a[1]) * b[0],
        };
        int64_t big = 0;
        for (int32_t k = 0; k < 3; k++)
        {
            int64_t v = n[k] < 0 ? -n[k] : n[k];
            big = v > big ? v : big;
        }
        uint32_t high = uint32_t(uint64_t(big) >> 32);
        int32_t bits = high ? 32 + bitLength(high) : bitLength(uint32_t(big));
        int32_t shift = bits > 15 ? bits - 15 : 0;
        int32_t nn[3];
        for (int32_t k = 0; k < 3; k++) {
            nn[k] = shr64(n[k], shift);
        }
        int64_t inner = int64_t(nn[0]) * m[0] + int64_t(nn[1]) * m[1] + int64_t(nn[2]) * m[2];
        for (int32_t k = 0; k < 3; k++) {
            planes[i][k] = int16_t(inner < 0 ? -nn[k] : nn[k]);
        }
    }
    return planes;
}

// The box reaching the inner side of each of entry e's planes (some ray from
// the camera through the portal may meet it).
static bool throughPortal(int32_t e, const Box &s)
{
    int16_t own[4][3];
    const int16_t (*planes)[3] = portalPlanes(e, own);
    for (int32_t i = 0; i < 4; i++)
    {
        const int16_t* nn = planes[i];
        // the centre's distance, the box's reach toward the plane along its
        // own x (cos, 0, -sin), y and z (sin, 0, cos)
        int32_t d = nn[0] * s.c[0] + nn[1] * s.c[1] + nn[2] * s.c[2];
        int32_t ax = (nn[0] * s.cos - nn[2] * s.sin) >> 14;
        int32_t az = (nn[0] * s.sin + nn[2] * s.cos) >> 14;
        int32_t reach = (ax < 0 ? 1 - ax : ax + 1) * s.h[0] + (nn[1] < 0 ? -nn[1] : nn[1]) * s.h[1] +
                        (az < 0 ? 1 - az : az + 1) * s.h[2];
        if (d + reach < 0)
            return false;
    }
    return true;
}

// Whether the box could show in room through some chain of the walk's
// entries back to the camera's room.
static bool seenFrom(uint32_t room, const Box &s, int32_t depth)
{
    if (room == gCtx.cameraRoom || depth > SEEN_DEPTH)
        return true;
    if (gSeenState[room])
        return gSeenState[room] == 2;
    gSeenState[room] = 1;
    for (int32_t e = gEntryFirst[room]; e >= 0; e = gEntryNext[e])
    {
        const RoomEntry &entry = gEntries[e];
        if (throughPortal(e, s) && seenFrom(entry.from, s, depth + 1))
        {
            gSeenState[room] = 2;
            return true;
        }
    }
    return false;
}

static bool boxSeen(uint32_t room, const Box &s)
{
    memset(gSeenState, 0, sizeof(gSeenState));
    return seenFrom(room, s, 0);
}

extern "C" int32_t roomsItemSeen(const ItemRecord* item, const int16_t* frameA, const int16_t* frameB,
                                 const CallRecord* call)
{
    uint32_t room = item->room;
    if (!gItemsTest || room == gCtx.cameraRoom || room >= MAX_LEVEL_ROOMS || gCtx.entryCount > ROOM_ENTRIES)
        return 1;
    if (!gEntriesListed)
    {
        for (uint32_t i = 0; i < MAX_LEVEL_ROOMS; i++) {
            gEntryFirst[i] = -1;
        }
        memset(gEntrySlot, -1, sizeof(gEntrySlot));
        gSlotsUsed = 0;
        // a portal the walk came back through listed once
        for (int32_t e = gCtx.entryCount - 1; e >= 0; e--)
        {
            int32_t same = gEntryFirst[gEntries[e].room];
            while (same >= 0 && gEntries[same].portal != gEntries[e].portal) {
                same = gEntryNext[same];
            }
            if (same >= 0)
                continue;
            gEntryNext[e] = gEntryFirst[gEntries[e].room];
            gEntryFirst[gEntries[e].room] = int16_t(e);
        }
        gEntriesListed = true;
    }
    // both frames' boxes in one, its centre turned to the item's heading
    int32_t lo[3], hi[3];
    for (int32_t k = 0; k < 3; k++)
    {
        lo[k] = frameA[2 * k] < frameB[2 * k] ? frameA[2 * k] : frameB[2 * k];
        hi[k] = frameA[2 * k + 1] > frameB[2 * k + 1] ? frameA[2 * k + 1] : frameB[2 * k + 1];
    }
    uint32_t sc = gSines[uint16_t(item->item.angle[1]) >> 4];
    Box s;
    s.sin = int16_t(sc >> 16);
    s.cos = int16_t(sc);
    int32_t cx = (lo[0] + hi[0]) >> 1, cz = (lo[2] + hi[2]) >> 1;
    int32_t c[3] = {
        item->item.pos[0] + ((cx * s.cos + cz * s.sin) >> 14) - gCtx.camera[0],
        item->item.pos[1] + ((lo[1] + hi[1]) >> 1) - gCtx.camera[1],
        item->item.pos[2] + ((cz * s.cos - cx * s.sin) >> 14) - gCtx.camera[2],
    };
    for (int32_t k = 0; k < 3; k++)
    {
        if (c[k] >= BOX_FAR << BOX_SHIFT || c[k] <= -(BOX_FAR << BOX_SHIFT))
            return 1;
        s.c[k] = c[k] >> BOX_SHIFT;
        s.h[k] = ((hi[k] - lo[k] + SEEN_MARGIN * 2) >> (BOX_SHIFT + 1)) + 2;
    }
    if (boxSeen(room, s))
        return 1;
    // its shadow: the box's floor plan, flat on the floor below it
    const CallRecord* next = call + 1;
    if (next->kind == CALL_SHADOW && next->index == call->index && next->args[0] != WALL)
    {
        int32_t floor = next->args[0] - gCtx.camera[1];
        if (floor >= BOX_FAR << BOX_SHIFT || floor <= -(BOX_FAR << BOX_SHIFT))
            return 1;
        s.c[1] = floor >> BOX_SHIFT;
        s.h[1] = (SEEN_MARGIN >> BOX_SHIFT) + 2;
        return boxSeen(room, s);
    }
    return 0;
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
