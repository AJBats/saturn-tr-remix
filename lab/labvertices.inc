// The vertices module's lab half (renderer/vertices.cpp includes it when
// RENDER_LAB): the scene's rooms and draws for it, its stage-2 runs and
// their checks against the game's way, and the bench's v3 beside it for its
// time. It sees the module's own buffers.
#include "game.h"
#include "labrender.h"
#include "meshlight3_dsp.h"
#include "scene.h"

using namespace render;

#define SELDOM_VERTICES 512

// A mesh's job for meshlight3.dsp (dspbench/gen_meshlight3.py), the bench's
// v3, run beside for its time: its vertices laid out for it, a longword each.
struct MeshDspJob3
{
    uint32_t src;               // its vertices / 4
    int32_t groups;             // of 4 vertices, - 1; -1 ends the list
    int32_t k[16];              // 4t 4m 4m 4m a row; ambient << 16 | 0xFFFF and l * range * -4
};

// What feeding leaves of a job but its DSP job, for the check.
struct FeedResult
{
    int32_t inside, far, shading;
    uint32_t shadow[SHADOW_VERTICES / 4 * 6];
};

static const Vertex* gSceneVertices;    // the vertices as the game drew them (VERT)
static MeshDspJob3 gDspJobs3[MAX_DRAWS + 1];
static int32_t* gMeshReady;         // the meshes as meshlight3.dsp takes them (x y z nx ny nz a longword)
static int32_t** gReady;            // each mesh's and shadow slot's there

// The rooms' seldom paths' check: made-up rotations and shades, both loops'
// projections and the model's (in the scene's arena).
static RoomRotated* gSeldomRotated;
static RoomVertex* gSeldomPoints;
static Vertex* gSeldomAsm;
static Vertex* gSeldomModel;

// The feeding's check: the C's jobs and results, made-up transforms and the
// jobs' parts made up (each in the scene's arena).
static MeshDspJob* gFeedC;
static FeedResult* gFeedResults;
static DrawXform* gFeedXforms;
static MeshParts* gFeedParts;

// A mesh's groups as the level keeps them for meshrot.dsp (tools/meshstore.py):
// lit by its normals, a group's 4 normals (bytes nx ny nz each plus 128, and
// a spare 0) then its 4 vertices' x y z; else the vertices alone. Its last
// group's spare vertices 0. Returns its longwords.
static int32_t packMesh(uint32_t* out, const MeshVertex* v, const int8_t* normals, int32_t count)
{
    uint32_t* at = out;
    for (int32_t g = 0; g < count; g += 4)
    {
        if (normals)
        {
            for (int32_t k = g; k < g + 4; k++)
            {
                const int8_t* n = normals + 4 * k;
                *at++ = k < count ? uint32_t(n[0] + 128) << 24 | uint32_t(n[1] + 128) << 16 |
                                    uint32_t(n[2] + 128) << 8 : 0;
            }
        }
        int16_t* c = (int16_t*)at;
        for (int32_t k = g; k < g + 4; k++, c += 3)
        {
            bool real = k < count;
            c[0] = real ? v[k].x : 0;
            c[1] = real ? v[k].y : 0;
            c[2] = real ? v[k].z : 0;
        }
        at += 6;
    }
    return at - out;
}

static inline int32_t row16(const int16_t* m, int32_t t, int32_t x, int32_t y, int32_t z)
{
    return int32_t(uint32_t(t) + uint32_t(m[0] * x) + uint32_t(m[1] * y) + uint32_t(m[2] * z));
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

// A mesh's vertices and normals in meshlight3.dsp's layout, its last group's
// spare vertices 0.
static void readyVertices(int32_t* ready, const MeshVertex* v, const int8_t* normals, int32_t count)
{
    int32_t whole = (count + 3) & ~3;
    for (int32_t k = 0; k < whole; k++, ready += 6)
    {
        bool real = k < count;
        ready[0] = real ? v[k].x : 0;
        ready[1] = real ? v[k].y : 0;
        ready[2] = real ? v[k].z : 0;
        ready[3] = real && normals ? normals[4 * k] : 0;
        ready[4] = real && normals ? normals[4 * k + 1] : 0;
        ready[5] = real && normals ? normals[4 * k + 2] : 0;
    }
}

const char* verticesLabInit(const FaceRoom* rooms, uint32_t roomCount)
{
    verticesRoomsSet(rooms, roomCount);
    uint8_t order[MAX_ROOMS];
    for (uint32_t i = 0; i < gRoomCount; i++) {
        order[i] = i;
    }
    roomOrder(order);
    gDraws = sceneArray<const DrawScene>("DRAW", &gDrawCount);
    const DrawXform* xforms = sceneArray<const DrawXform>("XFRM");
    gSceneVertices = sceneArray<const Vertex>("VERT");
    VerticesLevel level;
    level.store = sceneArray<const MeshStore>("MSTO");
    level.alloc = sceneAlloc;
    const char* failed = verticesInit(level);
    if (failed)
        return failed;

    // the meshes as the game keeps them (MESH, MGEO: the store's records in
    // its order), in meshlight3.dsp's layout, and the shadows' slots
    const uint32_t* meshes = sceneArray<const uint32_t>("MESH");
    const uint8_t* geometry = sceneArray<const uint8_t>("MGEO");
    uint32_t readyGroups = MAX_DRAW_SHADOWS * SHADOW_VERTICES / 4;
    for (uint32_t m = 0; m < gMeshCount; m++) {
        readyGroups += (geometry[meshes[m] + 10] + 3) >> 2;
    }
    gMeshReady = (int32_t*)sceneAlloc(readyGroups * 24 * 4);
    gReady = (int32_t**)sceneAllocLow((gMeshCount + MAX_DRAW_SHADOWS) * sizeof(int32_t*));
    gFeedParts = (MeshParts*)sceneAllocLow(gDrawCount * sizeof(MeshParts));
    gSeldomRotated = (RoomRotated*)sceneAllocLow(SELDOM_VERTICES * sizeof(RoomRotated));
    gSeldomPoints = (RoomVertex*)sceneAllocLow(SELDOM_VERTICES * sizeof(RoomVertex));
    gSeldomAsm = (Vertex*)sceneAllocLow(SELDOM_VERTICES * sizeof(Vertex));
    gSeldomModel = (Vertex*)sceneAllocLow(SELDOM_VERTICES * sizeof(Vertex));
    if (!gMeshReady || !gReady || !gSeldomModel)
        return "no room for the meshes' layouts";
    gFeedXforms = (DrawXform*)sceneAllocLow(gDrawCount * sizeof(DrawXform));
    gFeedC = (MeshDspJob*)sceneAllocLow((gDrawCount + 1) * sizeof(MeshDspJob));
    gFeedResults = (FeedResult*)sceneAllocLow(gDrawCount * sizeof(FeedResult));
    if (!gFeedResults || !gFeedParts)
        return "no room for the feeding's check";
    int32_t* ready = gMeshReady;
    for (uint32_t m = 0; m < gMeshCount; m++)
    {
        const uint8_t* mesh = geometry + meshes[m];
        int32_t count = mesh[10];
        const int8_t* normals = mesh[11] == MESH_LIGHT_NORMALS ? (const int8_t*)(mesh + 20 + count * 6) : nullptr;
        gReady[m] = ready;
        readyVertices(ready, (const MeshVertex*)(mesh + 20), normals, count);
        ready += ((count + 3) & ~3) * 6;
    }
    for (uint32_t s = 0; s < MAX_DRAW_SHADOWS; s++)
    {
        gReady[gMeshCount + s] = ready;
        ready += SHADOW_VERTICES * 6;
    }
    verticesDrawsSet(gDraws, gDrawCount, xforms);
    return nullptr;
}

// --- rooms ---------------------------------------------------------------

// xform.s's multiplies for a room's vertex: its bytes times 4 against each
// row, the translation first.
static RoomRotated roomModel(const FaceRoom &room, int32_t k)
{
    const RoomVertex* p = room.points + k;
    int32_t x = p->x * 4, y = p->y * 4, z = p->z * 4;
    return { row16(room.m, room.t[0], x, y, z), row16(room.m + 3, room.t[1], x, y, z),
             row16(room.m + 6, room.t[2], x, y, z) };
}

uint32_t verticesRoomsDsp()
{
    uint32_t t0 = roomsDspStart();
    while (dspBusy());
    uint32_t cycles = ticksCycles(t0, ticks());
    cachePurge();       // the DSP wrote behind the cache
    return cycles;
}

int32_t verticesRoomsDspCheck()
{
    int32_t bad = 0;
    for (uint32_t i = 0; i < gRoomCount; i++)
    {
        const RoomRotated* a = gRoomRotated + gRotatedAt[i];
        for (int32_t k = 0; k < gRooms[i].verticesCount; k++)
        {
            RoomRotated b = roomModel(gRooms[i], k);
            bad += a[k].x != b.x || a[k].y != b.y || a[k].z != b.z;
        }
    }
    return bad;
}

// roomproject.s's arithmetic (xform.s's from its multiplies on) in C: the
// model both its loops answer to where the scene seldom takes them.
// paths[]: the vertices each path took (SeldomPath).
enum SeldomPath { PATH_NEAR, PATH_FAR, PATH_BOUND, PATH_PRODUCT, PATH_FRAME, PATH_LEFT, PATH_RIGHT, PATH_TOP,
                  PATH_BOTTOM, PATH_FOG, PATH_FOG_MAX, PATH_CAUSTICS_MIN, PATH_CAUSTICS_MAX, PATH_RIPPLE, PATHS };

static void roomProjectModel(const RoomRotated* src, int32_t count, Vertex* dst, const RoomProject &rp,
                             int32_t* paths)
{
    for (int32_t k = 0; k < count; k++)
    {
        int32_t x = clamp(src[k].x, -XY_MAX, XY_MAX);
        int32_t y = clamp(src[k].y, -XY_MAX, XY_MAX);
        paths[PATH_BOUND] += x != src[k].x || y != src[k].y;
        int32_t z = src[k].z;
        int32_t shade = rp.points[k].g << 8;
        uint32_t clip = 0;
        if (!(z > rp.zMin))
        {
            z = rp.zMin;
            clip = CLIP_PLANE;
            paths[PATH_NEAR]++;
        }
        else if (z >= rp.zMax)
        {
            z = rp.zMax;
            clip = CLIP_PLANE;
            paths[PATH_FAR]++;
        }
        int32_t zz = uint32_t(z) >> 8;
        int32_t d = rp.divTable[zz >> 4];
        int32_t px = clamp(int16_t(uint32_t(x) >> 8) * d, -P_MAX, P_MAX);
        int32_t py = clamp(int16_t(uint32_t(y) >> 8) * d, -P_MAX, P_MAX);
        paths[PATH_PRODUCT] += px != int16_t(uint32_t(x) >> 8) * d || py != int16_t(uint32_t(y) >> 8) * d;
        int32_t sx = int16_t(uint32_t(px) >> 12) + (FRAME_WIDTH >> 1);
        int32_t sy = int16_t(uint32_t(py) >> 12) + (FRAME_HEIGHT >> 1);
        if (uint32_t(sx) > FRAME_WIDTH || uint32_t(sy) > FRAME_HEIGHT)
        {
            clip += CLIP_FRAME;
            paths[PATH_FRAME]++;
            sx = clamp(sx, -COORD_MAX - 1, COORD_MAX);
            sy = clamp(sy, -COORD_MAX - 1, COORD_MAX);
        }
        clip += (sx < rp.x0 ? CLIP_LEFT : 0) + (sx > rp.x1 ? CLIP_RIGHT : 0) +
                (sy < rp.y0 ? CLIP_TOP : 0) + (sy > rp.y1 ? CLIP_BOTTOM : 0);
        paths[PATH_LEFT] += sx < rp.x0;
        paths[PATH_RIGHT] += sx > rp.x1;
        paths[PATH_TOP] += sy < rp.y0;
        paths[PATH_BOTTOM] += sy > rp.y1;
        if (rp.caustics)
        {
            int32_t s = shade + rp.caustics[(rp.count - count + k) & 31];
            paths[PATH_CAUSTICS_MIN] += s < 0;
            paths[PATH_CAUSTICS_MAX] += s > SHADE_MAX;
            shade = clamp(s, 0, SHADE_MAX);
        }
        if (rp.wibble && !(clip & CLIP_FRAME))
        {
            paths[PATH_RIPPLE]++;
            sx += rp.wibble[(rp.phase + sx) & 31];
            sy += rp.wibble[(rp.phase + sy) & 31];
        }
        if (zz - FOG_MIN > 0)
        {
            paths[PATH_FOG]++;
            paths[PATH_FOG_MAX] += shade + zz - FOG_MIN > SHADE_MAX;
            shade = clamp(shade + zz - FOG_MIN, 0, SHADE_MAX);
        }
        uint32_t* out = (uint32_t*)(dst + k);
        out[0] = uint32_t(sx) << 16 | (sy & 0xFFFF);
        out[1] = uint32_t(zz) << 16 | (shade >> 8) << 8 | clip;
    }
}

int32_t verticesRoomsSeldomCheck()
{
    // made-up rotations through every path: behind and past the planes,
    // beyond the view bound, products beyond what is projected, each side of
    // the frame and of the window, fogged and not, shades past 31; dry rooms
    // (the second loop) with the whole frame and with a window, then a wet
    // one rippling (the first)
    static const int16_t kCaustics[32] = {
        -9000, 9000, -300, 300, 0, 4000, -4000, 120, -120, 8000, -8000, 50, -50, 2000, -2000, 0,
        700, -700, 0, 0, 6000, -6000, 1, -1, 3000, -3000, 0, 9000, -9000, 15, -15, 0
    };
    const int32_t windows[3][4] = { { 0, 0, 352, 224 }, { 100, 60, 220, 150 }, { 40, 30, 300, 200 } };
    uint32_t seed = 0x2B7E1516;
    auto next = [&seed] { seed = seed * 1664525 + 1013904223; return seed; };
    int32_t bad = 0;
    int32_t dry[PATHS] = {}, wet[PATHS] = {};
    for (int32_t w = 0; w < 3; w++)
    {
        RoomProject rp;
        rp.points = gSeldomPoints;
        rp.x0 = windows[w][0];
        rp.y0 = windows[w][1];
        rp.x1 = windows[w][2];
        rp.y1 = windows[w][3];
        rp.divTable = divTable;
        rp.zMin = VIEW_MIN_F >> (FIXED_SHIFT - 8);
        rp.zMax = VIEW_MAX_F >> (FIXED_SHIFT - 8);
        rp.wibble = w == 2 ? kWibble : nullptr;
        rp.phase = 7;
        rp.caustics = w == 2 ? kCaustics : nullptr;
        rp.count = SELDOM_VERTICES;
        // first each edge exactly: at z 256 << 8 the divide is 4096, a pixel
        // a view unit; the frame's, the window's, VDP1's, the projected
        // bound's, on x and y (a pixel's fraction made up); the view bound;
        // the planes; the fog's start and its shade a step either side of 31
        int32_t n = 0;
        auto put = [&](int32_t x, int32_t y, int32_t z, int32_t g)
        {
            if (n < SELDOM_VERTICES)
            {
                gSeldomRotated[n] = { x, y, z };
                gSeldomPoints[n++].g = g;
            }
        };
        auto frac = [&next] { return int32_t(next() >> 24); };
        auto near = [&next](int32_t c) { return (int32_t(next() % 160) - 80 + c) * 256; };
        const int32_t zd = 256 << 8;
        const int32_t xs[] = { -1, 0, 1, 351, 352, 353, rp.x0 - 1, rp.x0, rp.x0 + 1, rp.x1 - 1, rp.x1, rp.x1 + 1,
                               -1025, -1024, -1023, 1022, 1023, 1024, 176 + 2047, 176 + 2048, 176 - 2047, 176 - 2048 };
        const int32_t ys[] = { -1, 0, 1, 223, 224, 225, rp.y0 - 1, rp.y0, rp.y0 + 1, rp.y1 - 1, rp.y1, rp.y1 + 1,
                               -1025, -1024, -1023, 1022, 1023, 1024, 112 + 2047, 112 + 2048, 112 - 2047, 112 - 2048 };
        for (int32_t i = 0; i < int32_t(sizeof(xs) / sizeof(xs[0])); i++)
        {
            put((xs[i] - 176) * 256 | frac(), near(0), zd, next() >> 24);
            put(near(0), (ys[i] - 112) * 256 | frac(), zd, next() >> 24);
            put((xs[i] - 176) * 256 | frac(), (ys[i] - 112) * 256 | frac(), zd, next() >> 24);
        }
        const int32_t bounds[] = { XY_MAX, XY_MAX + 1, -XY_MAX, -XY_MAX - 1, 0x7FFFFF, -0x800000 };
        for (int32_t b : bounds)
        {
            put(b, near(0), rp.zMin + int32_t(next() % uint32_t(rp.zMax - rp.zMin)), next() >> 24);
            put(near(0), b, rp.zMin + int32_t(next() % uint32_t(rp.zMax - rp.zMin)), next() >> 24);
        }
        const int32_t planes[] = { rp.zMin - 1, rp.zMin, rp.zMin + 1, rp.zMax - 1, rp.zMax, rp.zMax + 1 };
        for (int32_t z : planes) {
            put(near(0), near(0), z, next() >> 24);
        }
        const int32_t fogs[] = { FOG_MIN - 1, FOG_MIN, FOG_MIN + 1, FOG_MIN + 255, FOG_MIN + 256, FOG_MIN + 257 };
        const int32_t shades[] = { 30, 31, 32, 255 };
        for (int32_t f : fogs)
        {
            for (int32_t g : shades) {
                put(near(0), near(0), f << 8 | frac(), g);
            }
        }

        // then made up at random
        for (int32_t k = n; k < SELDOM_VERTICES; k++)
        {
            RoomRotated &r = gSeldomRotated[k];
            uint32_t kind = next() >> 29;
            r.z = kind == 0 ? -int32_t(next() >> 12) + (rp.zMin + 256)
                : kind == 1 ? rp.zMax - 256 + int32_t(next() >> 12)
                : kind == 2 ? rp.zMin + int32_t(next() >> 20)
                : rp.zMin + int32_t(next() % uint32_t(rp.zMax - rp.zMin));
            int32_t spread = (next() >> 30) == 0 ? 0x4000000 : (r.z > 0 ? r.z : 4096) >> (next() >> 30);
            r.x = int32_t(next() % uint32_t(2 * spread + 1)) - spread;
            r.y = int32_t(next() % uint32_t(2 * spread + 1)) - spread;
            gSeldomPoints[k].g = next() >> 24;
        }
        roomProjectAsm(gSeldomRotated, SELDOM_VERTICES, gSeldomAsm, &rp);
        roomProjectModel(gSeldomRotated, SELDOM_VERTICES, gSeldomModel, rp, w == 2 ? wet : dry);
        for (int32_t k = 0; k < SELDOM_VERTICES; k++) {
            bad += memcmp(gSeldomAsm + k, gSeldomModel + k, sizeof(Vertex)) != 0;
        }
    }

    // a path never taken counts as every vertex off
    for (int32_t p = 0; p < PATHS; p++)
    {
        bool wetOnly = p == PATH_CAUSTICS_MIN || p == PATH_CAUSTICS_MAX || p == PATH_RIPPLE;
        if ((!wetOnly && !dry[p]) || !wet[p]) {
            bad += SELDOM_VERTICES;
        }
    }
    return bad;
}

void verticesRooms()
{
    cachePurge();       // the DSP wrote behind the cache
    for (uint32_t i = 0; i < gRoomCount; i++) {
        roomProject(i);
    }
}

// --- meshes --------------------------------------------------------------

// render.cpp meshXformSetup's test, the bounding sphere wholly in the frame
// and between the planes; and whether the sphere might reach past 1 << 29 in
// z, either way (by over a margin for meshes whose vertices stray past it).
static bool meshInside(const DrawXform &d, const int16_t* center, int32_t radius, bool &far)
{
    const int32_t* m = d.m;
    int32_t cx = row(m, m[3], center[0], center[1], center[2]) >> FIXED_SHIFT;
    int32_t cy = row(m + 4, m[7], center[0], center[1], center[2]) >> FIXED_SHIFT;
    int32_t cz = row(m + 8, m[11], center[0], center[1], center[2]) >> FIXED_SHIFT;
    int32_t zFar = int32_t(uint32_t(cz) + uint32_t(radius));
    int32_t zNear = int32_t(uint32_t(cz) - uint32_t(radius));
    far = zFar > 30000 || zNear < -30000;
    int32_t ax = int32_t((uint32_t(cx < 0 ? -uint32_t(cx) : cx) + uint32_t(radius)) << 8);
    int32_t ay = int32_t((uint32_t(cy < 0 ? -uint32_t(cy) : cy) + uint32_t(radius)) << 8);
    return (zNear > (VIEW_MIN_F >> FIXED_SHIFT)) && (zFar < VIEW_DIST) &&
           (ax < int32_t(uint32_t((FRAME_WIDTH >> 1) - 4) * uint32_t(zNear))) &&
           (ay < int32_t(uint32_t((FRAME_HEIGHT >> 1) - 4) * uint32_t(zNear)));
}

// The feeding in C, meshfeed.s's model: the DSP's jobs to dsp.
static void verticesFeedC(MeshDspJob* jobs)
{
    for (int32_t i = 0; i < gJobCount; i++)
    {
        MeshJob &job = gJobs[i];
        const DrawXform &x = *job.xform;
        MeshDspJob &dsp = jobs[i];
        if (!job.parts->center)
        {
            const int16_t* s = x.shadow;
            MeshVertex v[SHADOW_VERTICES];
            shadowVertices(v, s[0], s[1], s[2], s[3]);
            packMesh((uint32_t*)job.parts->packed, v, nullptr, SHADOW_VERTICES);
        }
        bool far = false;
        job.inside = job.parts->center && meshInside(x, job.parts->center, job.parts->radius, far);
        job.far = far;

        // the rows times 4; the light: by its normals, the DSP's arithmetic
        // keeping a shade in 0 .. 31 while ambient - range .. ambient + range
        // does (|n . l| < 1 << 14) and the normals' +128 comes off in int32;
        // else a shade for all, or the vertices' own (meshShadesAsm), or by
        // the normals here
        const int32_t t[3] = { x.m[3], x.m[7], x.m[11] };
        for (int32_t r = 0; r < 3; r++)
        {
            dsp.k[4 * r] = int32_t(uint32_t(t[r]) << 2);
            for (int32_t k = 0; k < 3; k++) {
                dsp.k[4 * r + 1 + k] = int32_t(uint32_t(x.m[4 * r + k]) << 2);
            }
        }
        int32_t range = x.range * -4;
        int32_t ambient = 0, lx = 0, ly = 0, lz = 0, bias = 0;
        if (job.parts->normals && x.range)
        {
            // with range <= 4095 and |lx| + |ly| + |lz| <= 512 the bias lies
            // within +-2^30, the first coefficient within int32
            meshLight(x, lx, ly, lz);
            bool safe = x.ambient >= x.range && x.ambient + x.range <= 0x1FFF &&
                        (lx < 0 ? -lx : lx) + (ly < 0 ? -ly : ly) + (lz < 0 ? -lz : lz) <= 512;
            job.shading = safe ? MESH_SHADED : MESH_NORMALS;
            ambient = x.ambient;
            if (safe) {
                bias = 128 * (lx + ly + lz) * range;
            } else {
                lx = ly = lz = 0;
            }
        }
        else if (job.parts->shades)
        {
            job.shading = MESH_SHADES;
        }
        else
        {
            job.shading = MESH_SHADED;
            ambient = clamp(int16_t(uint32_t(job.parts->intensity + x.ambient) >> 8), 0, 31) << 8;
        }
        dsp.k[12] = int32_t((uint32_t(ambient) << 16 | 0xFFFF) - uint32_t(bias));
        dsp.k[13] = lx * range;
        dsp.k[14] = ly * range;
        dsp.k[15] = lz * range;
        dsp.src = dspAddress(job.parts->packed);
        dsp.groups = (job.parts->count + 3) >> 2;
        dsp.entry = job.parts->normals ? MESHROT_DSP_NORMALS : MESHROT_DSP_COORDS;
        dsp.place = job.parts->normals ? 0 : 4;
        dsp.longwords = job.parts->normals ? 10 : 6;
    }
    jobs[gJobCount].groups = 0;
}

void verticesFeed()
{
    verticesFeedAsm(gJobs, gJobCount, gDspJobs, kTails);
}

// The C's feeding of the jobs as they stand, then the assembly's: the jobs
// whose DSP job, results or shadow differ.
static int32_t feedCompare()
{
    verticesFeedC(gFeedC);
    for (int32_t i = 0; i < gJobCount; i++)
    {
        const MeshJob &job = gJobs[i];
        FeedResult &r = gFeedResults[i];
        r.inside = job.inside;
        r.far = job.far;
        r.shading = job.shading;
        if (!job.parts->center) {
            memcpy(r.shadow, job.parts->packed, sizeof(r.shadow));
        }
    }
    memset(gDspJobs, 0xA5, (gJobCount + 1) * sizeof(MeshDspJob));
    verticesFeed();
    int32_t bad = 0;
    for (int32_t i = 0; i < gJobCount; i++)
    {
        const MeshJob &job = gJobs[i];
        const FeedResult &r = gFeedResults[i];
        bad += memcmp(gDspJobs + i, gFeedC + i, sizeof(MeshDspJob)) || job.inside != r.inside ||
               job.far != r.far || job.shading != r.shading ||
               (!job.parts->center && memcmp(r.shadow, job.parts->packed, sizeof(r.shadow)));
    }
    return bad + (gDspJobs[gJobCount].groups != 0);
}

int32_t verticesFeedCheck()
{
    // the scene's transforms; then made up, each pass a kind for every job:
    // far either way, behind the camera, off to each side, at the near plane,
    // lights the DSP can't take (range past the ambient, the light too
    // strong), meshes lit by none, the transforms at random; then the scene's
    // with every mesh lit by its vertices' shades, then by one for all
    int32_t bad = feedCompare();
    const DrawXform* scene[MAX_DRAWS];
    const MeshParts* parts[MAX_DRAWS];
    for (int32_t i = 0; i < gJobCount; i++)
    {
        scene[i] = gJobs[i].xform;
        parts[i] = gJobs[i].parts;
        gFeedParts[i] = *parts[i];
        gJobs[i].parts = gFeedParts + i;
    }
    uint32_t seed = 0x3243F6A8;
    auto next = [&seed] { seed = seed * 1664525 + 1013904223; return seed; };
    int32_t inside = 0, outside = 0, far = 0, shading[3] = {};
    for (int32_t pass = 0; pass < 12; pass++)
    {
        for (int32_t i = 0; i < gJobCount; i++)
        {
            MeshParts &p = gFeedParts[i];
            p.normals = pass < 10 ? parts[i]->normals : 0;
            p.shades = pass < 10 ? parts[i]->shades : pass == 10 ? (const uint8_t*)p.packed : nullptr;
            DrawXform &x = gFeedXforms[i];
            x = *scene[i];
            int32_t* t = x.m;
            switch (pass)
            {
            case 0: t[11] += 40000 << FIXED_SHIFT; break;
            case 1: t[11] -= 40000 << FIXED_SHIFT; break;
            case 2: t[11] = -t[11]; break;
            case 3: t[3] += int32_t(next() % 4000 + 1) << FIXED_SHIFT; break;
            case 4: t[3] -= int32_t(next() % 4000 + 1) << FIXED_SHIFT; break;
            case 5: t[7] += (int32_t(next() % 3000) - 1500) * (1 << FIXED_SHIFT); t[11] = int32_t(next() % 200) << FIXED_SHIFT; break;
            case 6: x.range = x.ambient + int32_t(next() % 100) + 1; break;
            case 7: x.light[0] = x.light[1] = x.light[2] = 32767; break;
            case 8: x.range = 0; x.ambient = int32_t(next() >> 16) - 0x4000; break;
            case 10: case 11: break;
            default:
                for (int32_t k = 0; k < 12; k++) {
                    x.m[k] = int32_t(next());
                }
                x.ambient = int32_t(next() >> 19);
                x.range = int32_t(next() >> 20);
                for (int32_t k = 0; k < 3; k++) {
                    x.light[k] = int16_t(next() >> 16);
                }
                for (int32_t k = 0; k < 4; k++) {
                    x.shadow[k] = int16_t(next() >> 16);
                }
            }
            gJobs[i].xform = &x;
        }
        bad += feedCompare();
        for (int32_t i = 0; i < gJobCount; i++)
        {
            const MeshJob &job = gJobs[i];
            inside += job.inside;
            outside += job.parts->center && !job.inside;
            far += job.far;
            shading[job.shading]++;
        }
    }
    for (int32_t i = 0; i < gJobCount; i++)
    {
        gJobs[i].xform = scene[i];
        gJobs[i].parts = parts[i];
    }
    verticesFeed();

    // a case never met counts as every job off
    if (!inside || !outside || !far || !shading[MESH_SHADED] || !shading[MESH_SHADES] || !shading[MESH_NORMALS]) {
        bad += gJobCount;
    }
    return bad;
}

uint32_t verticesMeshesDsp()
{
    uint32_t t0 = meshesDspStart();
    while (dspBusy());
    uint32_t cycles = ticksCycles(t0, ticks());
    cachePurge();       // the DSP wrote behind the cache
    return cycles;
}

uint32_t verticesMeshesDsp3()
{
    for (int32_t i = 0; i < gJobCount; i++)
    {
        const MeshJob &job = gJobs[i];
        const MeshDspJob &dsp = gDspJobs[i];
        MeshDspJob3 &v3 = gDspJobs3[i];
        if (!job.parts->center)
        {
            const int16_t* s = job.xform->shadow;
            MeshVertex v[SHADOW_VERTICES];
            shadowVertices(v, s[0], s[1], s[2], s[3]);
            readyVertices(gReady[job.parts - gParts], v, nullptr, SHADOW_VERTICES);
        }
        memcpy(v3.k, dsp.k, sizeof(v3.k));
        v3.k[12] = int32_t(uint32_t(dsp.k[12]) + 128u * uint32_t(dsp.k[13] + dsp.k[14] + dsp.k[15]));
        v3.src = dspAddress(gReady[job.parts - gParts]);
        v3.groups = dsp.groups - 1;
    }
    gDspJobs3[gJobCount].src = gJobCount ? gDspJobs3[0].src : dspAddress(gMeshReady);
    gDspJobs3[gJobCount].groups = -1;

    static const uint32_t md0[3] = { 65536, 0xFFFF, 0x1F00 };
    static const uint32_t one = 1;
    dspLoad(meshlight3_dsp, MESHLIGHT3_DSP_COUNT);
    dspWrite(0, 16, md0, 3);
    dspWrite(3, 16, &one, 1);
    const uint32_t first = dspAddress(gDspJobs3);
    const uint32_t staged[2] = { first + 18, 20 };     // the record after; where the first is staged
    const uint32_t ends[2] = { dspAddress(gMeshVertices), first };
    dspWrite(0, 20, staged, 2);
    dspWrite(0, 23, ends, 2);
    uint32_t t0 = ticks();
    dspStart(MESHLIGHT3_DSP_START);
    while (dspBusy());
    uint32_t cycles = ticksCycles(t0, ticks());
    cachePurge();       // the DSP wrote behind the cache
    return cycles;
}

int32_t verticesMeshesDspCheck()
{
    int32_t bad = 0;
    for (int32_t i = 0; i < gJobCount; i++)
    {
        const MeshJob &job = gJobs[i];
        const DrawXform &x = *job.xform;
        const int32_t* m = x.m;
        int32_t lx = 0, ly = 0, lz = 0;
        if (job.shading == MESH_SHADED && job.parts->normals) {
            meshLight(x, lx, ly, lz);
        }
        int32_t flat = job.shading == MESH_SHADED && !job.parts->normals
                       ? clamp(int16_t(uint32_t(job.parts->intensity + x.ambient) >> 8), 0, 31) << 8 : 0;
        const uint32_t* out = (const uint32_t*)job.out;
        for (int32_t k = 0; k < job.parts->count; k++, out += 2)
        {
            MeshVertex p = jobVertex(job, k);
            int32_t vx = row(m, m[3], p.x, p.y, p.z) >> FIXED_SHIFT;
            int32_t vy = row(m + 4, m[7], p.x, p.y, p.z) >> FIXED_SHIFT;
            int32_t vz = row(m + 8, m[11], p.x, p.y, p.z) >> FIXED_SHIFT;
            int32_t t = flat;
            if (job.shading == MESH_SHADED && job.parts->normals)
            {
                int32_t n[3];
                jobNormal(job, k, n);
                t = x.ambient - (((n[0] * lx + n[1] * ly + n[2] * lz) * x.range) >> 14);
            }
            bad += out[0] != ((uint32_t(vx) << 16) | (vy & 0xFFFF)) ||
                   (job.shading != MESH_SHADED ? (out[1] >> 16) != (uint32_t(vz) & 0xFFFF)
                                               : out[1] != ((uint32_t(vz) << 16) | (t & 0x1F00)));
        }
    }
    return bad;
}

void verticesMeshes()
{
    MeshProject mp;
    mp.divTable = divTable;
    mp.exact = meshExact;
    for (int32_t i = 0; i < gJobCount; i++) {
        meshProject(mp, i);
    }
}

// --- together ------------------------------------------------------------

// Which CPU has each mesh job (tasClaim): the master takes them from the
// first, the slave its rooms done from the last, until they meet.
static uint8_t gClaims[MAX_DRAWS] __attribute__((aligned(16)));

// Mesh job i projected once the DSP has made it, if this CPU claims it
// first: whether it did; the cycles waiting for the DSP.
static inline bool meshAsMade(MeshProject &mp, int32_t i, uint32_t &wait)
{
    if (!tasClaim(uncached(gClaims + i)))
        return false;
    volatile uint32_t* last = jobLast(gJobs[i]);
    uint32_t w0 = ticks();
    while ((*last & 0xFF) == DSP_UNWRITTEN) {
        pause();
    }
    wait += ticksCycles(w0, ticks());
    meshProject(mp, i);
    return true;
}

// The slave's part: each room projected once the DSP has turned it, then
// mesh jobs from the last.
static void roomsAsTurned(void*)
{
    uint32_t t0 = ticks(), wait = 0;
    for (uint32_t i = 0; i < gRoomCount; i++)
    {
        roomTurned(i, wait);
        roomProject(i);
    }
    MeshProject mp;
    mp.divTable = divTable;
    mp.exact = meshExact;
    for (int32_t i = gJobCount - 1; i >= 0 && meshAsMade(mp, i, wait); i--);
    *uncached(&gRoomsDone.work) = ticksCycles(t0, ticks()) - wait;
    *uncached(&gRoomsDone.wait) = wait;
}

// --- stage 2 in pieces, for stages run together ---------------------------

void verticesPoison()
{
    memset(gRoomRotated, 0xA5, sizeof(gRoomRotated));
    memset(gMeshOut, 0xFF, sizeof(gMeshOut));
    for (uint32_t i = 0; i < gRoomCount; i++) {
        memset(roomVertices(gRooms[i]), 0xA5, gRooms[i].verticesCount * sizeof(Vertex));
    }
}

Together verticesTogether()
{
    Together r = {};
    for (int32_t i = 0; i < gJobCount; i++) {
        *uncached(gClaims + i) = 0;
    }
    uint32_t t0 = ticks();
    uint32_t roomsAt = roomsDspStart();
    slaveRun(roomsAsTurned, nullptr);
    verticesFeed();
    for (int32_t i = 0; i < gJobCount; i++) {
        *jobLast(gJobs[i]) = DSP_UNWRITTEN;
    }
    uint32_t w0 = ticks();
    while (dspBusy());
    uint32_t roomsEnd = ticks();
    r.masterWait = ticksCycles(w0, roomsEnd);
    uint32_t meshesAt = meshesDspStart();
    MeshProject mp;
    mp.divTable = divTable;
    mp.exact = meshExact;
    for (int32_t i = 0; i < gJobCount && meshAsMade(mp, i, r.masterWait); i++);
    w0 = ticks();
    while (dspBusy());
    uint32_t meshesEnd = ticks();
    slaveWait();
    uint32_t t1 = ticks();
    r.masterWait += ticksCycles(w0, t1);
    r.wall = ticksCycles(t0, t1);
    r.masterWork = r.wall - r.masterWait;
    r.slaveWork = *uncached(&gRoomsDone.work);
    r.slaveWait = *uncached(&gRoomsDone.wait);
    r.dspRooms = ticksCycles(roomsAt, roomsEnd);
    r.dspGap = ticksCycles(roomsEnd, meshesAt);
    r.dspMeshes = ticksCycles(meshesAt, meshesEnd);
    return r;
}

int32_t verticesMeshesCheck(int32_t &vertices)
{
    int32_t bad = 0;
    vertices = 0;
    for (uint32_t d = 0; d < gDrawCount; d++)
    {
        if (gJobOfDraw[d] < 0)
            continue;
        const MeshJob &job = gJobs[gJobOfDraw[d]];
        const Vertex* game = gSceneVertices + gDraws[d].base;
        vertices += job.parts->count;
        bad += memcmp(job.out, game, job.parts->count * sizeof(Vertex)) != 0 ? job.parts->count : 0;
    }
    return bad;
}

int32_t verticesFallbacksCheck()
{
    int32_t bad = 0;
    MeshProject mp;
    mp.divTable = divTable;
    mp.exact = meshExact;
    mp.inside = 0;
    for (int32_t pass = 0; pass < 2; pass++)
    {
        verticesMeshesDsp();
        for (uint32_t d = 0; d < gDrawCount; d++)
        {
            if (gJobOfDraw[d] < 0)
                continue;
            const MeshJob &job = gJobs[gJobOfDraw[d]];
            const Vertex* game = gSceneVertices + gDraws[d].base;
            if (job.far)
                continue;
            if (pass == 1)
            {
                for (int32_t k = 0; k < job.parts->count; k++) {
                    job.out[k].z = 64;
                }
            }
            mp.job = &job;
            meshProjectAsm(job.out, job.parts->count, &mp);
            for (int32_t k = 0; k < job.parts->count; k++)
            {
                const Vertex &v = job.out[k];
                bad += v.xy != game[k].xy || v.z != game[k].z || v.clip != game[k].clip;
            }
        }
    }
    return bad;
}

int32_t verticesRoomsCheck(int32_t &vertices)
{
    int32_t bad = 0;
    vertices = 0;
    for (uint32_t i = 0; i < gRoomCount; i++)
    {
        const FaceRoom &room = gRooms[i];
        int32_t base = room.vertices - gVertices;
        vertices += room.verticesCount;
        for (int32_t k = base; k < base + room.verticesCount; k++) {
            bad += memcmp(gVertices + k, gSceneVertices + k, sizeof(Vertex)) != 0;
        }
    }
    return bad;
}

