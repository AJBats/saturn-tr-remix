// The game's way, stage 2 (game.h): the rooms' vertices through saturn/xform.s,
// each room's RoomXform as rooms.cpp roomBegin makes it, and the master's
// draws' through saturn/meshxform.s transformMeshAsm, set up as render.cpp
// meshXformSetup and renderShadow set it up; from the scene's rooms and draws,
// into gVertices where the game put them.
#include "game.h"
#include "scene.h"

#include <string.h>

#define FIXED_SHIFT     14
#define VIEW_DIST       (1024 * 20)
#define VIEW_MIN_F      (64 << FIXED_SHIFT)
#define VIEW_MAX_F      (VIEW_DIST << FIXED_SHIFT)
#define FRAME_WIDTH     352
#define FRAME_HEIGHT    224
#define DIV_TABLE_SIZE  1281
#define MESH_LIGHT_NORMALS  1
#define MESH_LIGHT_SHADES   2
#define SHADOW_VERTICES 8

struct RoomXform
{
    int16_t v[3];
    int16_t rows[9];
    int32_t tx, ty, tz;
    int32_t x0, y0, x1, y1;
    const uint16_t* divTable;
    int32_t zMin, zMax;
    const int8_t* wibble;
    int32_t phase;
    const int16_t* caustics;
    int32_t count;
};

struct MeshXform
{
    int16_t rows[9];
    int32_t tx, ty, tz;
    const uint16_t* divTable;
    int32_t ambient;
    int32_t lx, ly, lz;
    int32_t range;
    const int8_t* normals;
    const uint8_t* shades;
    int32_t g;
    int32_t inside;
};

static_assert(sizeof(RoomXform) == 80 && sizeof(MeshXform) == 72, "as xform.s and meshxform.s read them");

extern "C" void transformRoomAsm(const RoomVertex* src, int32_t count, Vertex* dst, RoomXform* xf);
extern "C" void transformMeshAsm(const MeshVertex* src, int32_t count, Vertex* dst, MeshXform* xf);

// What meshxform.s's renderMeshAsm, which the lab doesn't run, takes from the
// game.
int32_t gBenchMode;
uint16_t divTable[DIV_TABLE_SIZE];

// Rooms across the water surface from the camera ripple on screen (rooms.cpp
// gWibble).
static const int8_t kWibble[32] = {
    0, 0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 0,
    0, 0, -1, -1, -1, -2, -2, -2, -2, -2, -2, -2, -1, -1, -1, 0
};

static const RoomScene* gRooms;
static uint32_t gRoomCount;
static const uint8_t* gRoomGeometry;
static const int16_t (*gCaustics)[32];
static const DrawScene* gDraws;
static const DrawXform* gXforms;
static uint32_t gDrawCount;
static const uint32_t* gMeshes;
static const uint8_t* gMeshGeometry;
static const Vertex* gSceneVertices;

template <int N>
static inline int32_t asr16(int32_t v)
{
    return int16_t(uint32_t(v) >> N);
}

static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

// rooms.cpp roomBegin's RoomXform for a room.
static void roomSetup(RoomXform &xf, const RoomScene &room, const int16_t* caustics)
{
    for (int32_t i = 0; i < 9; i++) {
        xf.rows[i] = room.m[i];
    }
    xf.tx = room.tx;
    xf.ty = room.ty;
    xf.tz = room.tz;
    xf.x0 = room.clip[0];
    xf.y0 = room.clip[1];
    xf.x1 = room.clip[2];
    xf.y1 = room.clip[3];
    xf.divTable = divTable;
    xf.zMin = VIEW_MIN_F >> (FIXED_SHIFT - 8);
    xf.zMax = VIEW_MAX_F >> (FIXED_SHIFT - 8);
    xf.wibble = room.ripple ? kWibble : nullptr;
    xf.phase = room.causticsFrame;
    xf.caustics = room.wet ? caustics : nullptr;
    xf.count = room.verticesCount;
}

// render.cpp meshXformSetup, the draw's matrix and light for the game's
// matrix stack and lights.
static void meshSetup(MeshXform &xf, const DrawXform &d, int32_t intensity, const int8_t* normals,
                      const uint8_t* shades, const int16_t* center, int32_t radius)
{
    const int32_t* m = d.m;
    xf.rows[0] = m[0]; xf.rows[1] = m[1]; xf.rows[2] = m[2];
    xf.rows[3] = m[4]; xf.rows[4] = m[5]; xf.rows[5] = m[6];
    xf.rows[6] = m[8]; xf.rows[7] = m[9]; xf.rows[8] = m[10];
    xf.tx = m[3];
    xf.ty = m[7];
    xf.tz = m[11];
    xf.divTable = divTable;
    xf.ambient = d.ambient;
    xf.range = d.range;
    xf.normals = nullptr;
    xf.shades = shades;
    xf.g = clamp(asr16<8>(intensity + d.ambient), 0, 31);
    xf.inside = 0;

    if (center)
    {
        int32_t cx = (m[0] * center[0] + m[1] * center[1] + m[2] * center[2] + m[3]) >> FIXED_SHIFT;
        int32_t cy = (m[4] * center[0] + m[5] * center[1] + m[6] * center[2] + m[7]) >> FIXED_SHIFT;
        int32_t cz = (m[8] * center[0] + m[9] * center[1] + m[10] * center[2] + m[11]) >> FIXED_SHIFT;
        int32_t zNear = cz - radius;
        xf.inside = (zNear > (VIEW_MIN_F >> FIXED_SHIFT)) && (cz + radius < VIEW_DIST) &&
                    (((cx < 0 ? -cx : cx) + radius) * 256 < ((FRAME_WIDTH >> 1) - 4) * zNear) &&
                    (((cy < 0 ? -cy : cy) + radius) * 256 < ((FRAME_HEIGHT >> 1) - 4) * zNear);
    }

    if (normals && d.range)
    {
        const int16_t* l = d.light;
        xf.lx = (m[0] * l[0] + m[4] * l[1] + m[8] * l[2]) >> (FIXED_SHIFT + 7);
        xf.ly = (m[1] * l[0] + m[5] * l[1] + m[9] * l[2]) >> (FIXED_SHIFT + 7);
        xf.lz = (m[2] * l[0] + m[6] * l[1] + m[10] * l[2]) >> (FIXED_SHIFT + 7);
        xf.normals = normals;
        xf.shades = nullptr;
    }
}

// render.cpp shadowVertices.
static void shadowVertices(MeshVertex* v, int32_t x, int32_t z, int32_t sx, int32_t sz)
{
    int16_t xns1 = x - sx;
    int16_t xps1 = x + sx;
    int16_t xns2 = xns1 - sx;
    int16_t xps2 = xps1 + sx;
    int16_t zns1 = z - sz;
    int16_t zps1 = z + sz;
    int16_t zns2 = zns1 - sz;
    int16_t zps2 = zps1 + sz;
    v[0] = { xns1, 0, zps2 };
    v[1] = { xps1, 0, zps2 };
    v[2] = { xps2, 0, zps1 };
    v[3] = { xps2, 0, zns1 };
    v[4] = { xps1, 0, zns2 };
    v[5] = { xns1, 0, zns2 };
    v[6] = { xns2, 0, zns1 };
    v[7] = { xns2, 0, zps1 };
}

// A mesh's vertex count, and the vertices drawn.
static int32_t drawVertices(const DrawScene &d)
{
    if (d.type == DRAW_SHADOW)
        return SHADOW_VERTICES;
    return d.type == DRAW_MESH ? gMeshGeometry[gMeshes[d.mesh] + 10] : 0;
}

void gameVerticesInit()
{
    gRooms = sceneArray<const RoomScene>("ROOM", &gRoomCount);
    gRoomGeometry = sceneArray<const uint8_t>("RGEO");
    gCaustics = (const int16_t (*)[32])sceneChunk("CAUS");
    gDraws = sceneArray<const DrawScene>("DRAW", &gDrawCount);
    gXforms = sceneArray<const DrawXform>("XFRM");
    gMeshes = sceneArray<const uint32_t>("MESH");
    gMeshGeometry = sceneArray<const uint8_t>("MGEO");
    gSceneVertices = sceneArray<const Vertex>("VERT");
}

void gameVerticesPrepare()
{
    memset(gVertices, 0, sizeof(gVertices));
}

void gameVerticesRooms()
{
    RoomXform xf;
    for (uint32_t i = 0; i < gRoomCount; i++)
    {
        const RoomScene &room = gRooms[i];
        roomSetup(xf, room, gCaustics[i]);
        transformRoomAsm((const RoomVertex*)(gRoomGeometry + room.vertices), room.verticesCount,
                         gVertices + room.base, &xf);
    }
}

void gameVerticesMeshes()
{
    MeshXform xf;
    for (uint32_t i = 0; i < gDrawCount; i++)
    {
        const DrawScene &d = gDraws[i];
        if (d.type == DRAW_SHADOW)
        {
            const DrawXform &x = gXforms[i];
            MeshVertex v[SHADOW_VERTICES];
            shadowVertices(v, x.shadow[0], x.shadow[1], x.shadow[2], x.shadow[3]);
            meshSetup(xf, x, 0, nullptr, nullptr, nullptr, 0);
            transformMeshAsm(v, SHADOW_VERTICES, gVertices + d.base, &xf);
        }
        else if (d.type == DRAW_MESH)
        {
            // the mesh's arrays (render.cpp meshParts)
            const uint8_t* mesh = gMeshGeometry + gMeshes[d.mesh];
            int32_t count = mesh[10];
            const MeshVertex* vertices = (const MeshVertex*)(mesh + 20);
            const uint8_t* after = mesh + 20 + count * 6;
            const int8_t* normals = mesh[11] == MESH_LIGHT_NORMALS ? (const int8_t*)after : nullptr;
            const uint8_t* shades = mesh[11] == MESH_LIGHT_SHADES ? after : nullptr;
            meshSetup(xf, gXforms[i], *(const uint16_t*)(mesh + 8), normals, shades, (const int16_t*)mesh,
                      *(const int16_t*)(mesh + 16));
            transformMeshAsm(vertices, count, gVertices + d.base, &xf);
        }
    }
}

int32_t gameVerticesCheck(int32_t &roomVertices, int32_t &meshVertices)
{
    int32_t bad = 0;
    roomVertices = meshVertices = 0;
    for (uint32_t i = 0; i < gRoomCount; i++)
    {
        const RoomScene &room = gRooms[i];
        roomVertices += room.verticesCount;
        for (int32_t k = room.base; k < room.base + room.verticesCount; k++) {
            bad += memcmp(gVertices + k, gSceneVertices + k, sizeof(Vertex)) != 0;
        }
    }
    for (uint32_t i = 0; i < gDrawCount; i++)
    {
        const DrawScene &d = gDraws[i];
        int32_t n = drawVertices(d);
        meshVertices += n;
        for (int32_t k = d.base; k < d.base + n; k++) {
            bad += memcmp(gVertices + k, gSceneVertices + k, sizeof(Vertex)) != 0;
        }
    }
    return bad;
}
