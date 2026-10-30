// Stage 2 the new way (vertices.h).
#include "vertices.h"
#include "platform.h"
#include "scene.h"

#include "dsp.h"
#include "meshlight3_dsp.h"
#include "meshrot_dsp.h"
#include "roomrot_dsp.h"

#include <string.h>

#define FIXED_SHIFT     14
#define VIEW_DIST       (1024 * 20)
#define VIEW_MIN_F      (64 << FIXED_SHIFT)
#define VIEW_MAX_F      (VIEW_DIST << FIXED_SHIFT)
#define FRAME_WIDTH     352
#define FRAME_HEIGHT    224
#define P_MAX           0x007FF000  // projected 2047 << 12
#define CLIP_FRAME      0x01
#define CLIP_LEFT       0x02
#define CLIP_RIGHT      0x04
#define CLIP_TOP        0x08
#define CLIP_BOTTOM     0x10
#define CLIP_PLANE      0x20
#define MESH_LIGHT_NORMALS  1
#define MESH_LIGHT_SHADES   2
#define SHADOW_VERTICES 8
#define MAX_ROOMS       64
#define MAX_DRAWS       256
#define MAX_MESHES      256
#define MAX_ROOM_VERTICES   (MAX_VERTICES - MASTER_VERTICES + 3 * MAX_ROOMS)
#define MAX_MESH_VERTICES   (MASTER_VERTICES + 3 * MAX_DRAWS)

static const int8_t kWibble[32] = {
    0, 0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 0,
    0, 0, -1, -1, -1, -2, -2, -2, -2, -2, -2, -2, -1, -1, -1, 0
};

// A room's job for roomrot.dsp (lab/gen_roomrot.py).
struct RoomDspJob
{
    uint32_t vertices;          // / 4
    uint32_t out;               // / 4
    int32_t groups;             // of 4 vertices: 0 ends the list
    int32_t m[12];              // t m0 m1 m2 a row, m times 4
};

// A mesh's job for meshrot.dsp (lab/gen_meshrot.py): its coefficients ready
// to use, its groups as the level would keep them (packMesh).
struct MeshDspJob
{
    int32_t k[16];              // 4t 4m 4m 4m a row; ambient << 16 | 0xFFFF less 128 times the
                                // rest's sum, then l * range * -4
    uint32_t src;               // its groups / 4
    int32_t groups;             // of 4 vertices; 0 ends the list
    uint32_t entry;             // where the DSP begins a group: at its normals, or its coordinates
    uint32_t place;             // where a group lands in the DSP's data RAM: 0, or 4 without normals
    uint32_t longwords;         // a group's: 10, or 6
};

// A mesh's job for meshlight3.dsp (dspbench/gen_meshlight3.py), the bench's
// v3, run beside for its time: its vertices laid out for it, a longword each.
struct MeshDspJob3
{
    uint32_t src;               // its vertices / 4
    int32_t groups;             // of 4 vertices, - 1; -1 ends the list
    int32_t k[16];              // 4t 4m 4m 4m a row; ambient << 16 | 0xFFFF and l * range * -4
};

// A mesh's projection and shading, as feeding it to the DSP set them up.
struct MeshJob
{
    Vertex* out;                // its vertices: the DSP's output, projected in place (whole groups)
    const Vertex* game;         // the game's, in the scene's
    int32_t count;
    int32_t inside;
    int32_t shading;            // MeshShading
    const uint8_t* shades;      // MESH_SHADES'
    int32_t normals;            // its groups hold its normals
    int32_t intensity;
    int32_t far;                // might reach past 1 << 29 in z: the DSP's z >> 14 would wrap, so
                                // the vertices are worked out here (meshExact)
    const uint32_t* packed;     // its groups (packMesh's)
    const int16_t* center;      // a mesh's bounding sphere, or none (a shadow)
    int32_t radius;
    int32_t* ready;             // in meshlight3.dsp's layout
    const DrawXform* xform;
};

static const RoomScene* gRooms;
static uint32_t gRoomCount;
static const uint8_t* gRoomGeometry;
static const int16_t (*gCaustics)[32];
static const uint16_t* gDivTable;
static const DrawScene* gDraws;
static const DrawXform* gXforms;
static uint32_t gDrawCount;
static const uint32_t* gMeshes;
static uint32_t gMeshCount;
static const uint8_t* gMeshGeometry;
static const Vertex* gSceneVertices;

// The rooms' rotations, the DSP's, a room's at its own place (whole groups
// of 4), and their jobs.
static RoomRotated gRoomRotated[MAX_ROOM_VERTICES];
static int32_t gRotatedAt[MAX_ROOMS];
static RoomDspJob gRoomJobs[MAX_ROOMS + 1];

// The meshes' groups as the level would keep them (a mesh's once, a
// shadow's each frame; then 10 longwords meshrot.dsp reads past the last);
// as meshlight3.dsp takes them (x y z nx ny nz a longword), both in the
// scene's arena; their jobs, the DSP's of each; their vertices, a job's
// after another, whole groups, after a group's room for meshrot.dsp's first
// output, of nothing.
static uint32_t* gMeshPacked;
static uint32_t* gPackedOf[MAX_MESHES];
static int32_t* gMeshReady;
static int32_t* gReadyOf[MAX_MESHES];
static MeshJob gJobs[MAX_DRAWS];
static MeshDspJob gDspJobs[MAX_DRAWS + 1];
static MeshDspJob3 gDspJobs3[MAX_DRAWS + 1];
static int32_t gJobCount;
static Vertex gMeshOut[4 + MAX_MESH_VERTICES];
static Vertex* const gMeshVertices = gMeshOut + 4;

static inline const RoomVertex* roomPoints(const RoomScene &room)
{
    return (const RoomVertex*)(gRoomGeometry + room.vertices);
}

static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

// A row times a vertex plus the translation, wrapping as the
// multiply-accumulate unit does.
static inline int32_t row(const int32_t* m, int32_t t, int32_t x, int32_t y, int32_t z)
{
    return int32_t(uint32_t(t) + uint32_t(m[0] * x) + uint32_t(m[1] * y) + uint32_t(m[2] * z));
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

// A mesh's groups as the level would keep them for meshrot.dsp: lit by its
// normals, a group's 4 normals (bytes nx ny nz each plus 128, and a spare 0)
// then its 4 vertices' x y z; else the vertices alone. Its last group's
// spare vertices 0. Returns its longwords.
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

const char* verticesInit()
{
    gRooms = sceneArray<const RoomScene>("ROOM", &gRoomCount);
    gRoomGeometry = sceneArray<const uint8_t>("RGEO");
    gCaustics = (const int16_t (*)[32])sceneChunk("CAUS");
    gDivTable = sceneArray<const uint16_t>("DIVT");
    gDraws = sceneArray<const DrawScene>("DRAW", &gDrawCount);
    gXforms = sceneArray<const DrawXform>("XFRM");
    gMeshes = sceneArray<const uint32_t>("MESH", &gMeshCount);
    gMeshGeometry = sceneArray<const uint8_t>("MGEO");
    gSceneVertices = sceneArray<const Vertex>("VERT");
    if (gRoomCount > MAX_ROOMS) {
        gRoomCount = MAX_ROOMS;
    }
    int32_t at = 0;
    for (uint32_t i = 0; i < gRoomCount; i++)
    {
        gRotatedAt[i] = at;
        at += (gRooms[i].verticesCount + 3) & ~3;
    }

    // the meshes packed and in meshlight3.dsp's layout, once
    uint32_t packedLongwords = 10, readyGroups = 0;
    for (uint32_t m = 0; m < gMeshCount && m < MAX_MESHES; m++)
    {
        const uint8_t* mesh = gMeshGeometry + gMeshes[m];
        int32_t groups = (mesh[10] + 3) >> 2;
        packedLongwords += groups * (mesh[11] == MESH_LIGHT_NORMALS ? 10 : 6);
        readyGroups += groups;
    }
    for (uint32_t i = 0; i < gDrawCount; i++)
    {
        if (gDraws[i].type == DRAW_SHADOW)
        {
            packedLongwords += SHADOW_VERTICES / 4 * 6;
            readyGroups += SHADOW_VERTICES / 4;
        }
    }
    gMeshPacked = (uint32_t*)sceneAlloc(packedLongwords * 4);
    gMeshReady = (int32_t*)sceneAlloc(readyGroups * 24 * 4);
    if (!gMeshPacked || !gMeshReady)
        return "no room for the meshes' layouts";
    memset(gMeshPacked, 0, packedLongwords * 4);
    uint32_t* packed = gMeshPacked;
    int32_t* ready = gMeshReady;
    for (uint32_t m = 0; m < gMeshCount && m < MAX_MESHES; m++)
    {
        const uint8_t* mesh = gMeshGeometry + gMeshes[m];
        int32_t count = mesh[10];
        const int8_t* normals = mesh[11] == MESH_LIGHT_NORMALS ? (const int8_t*)(mesh + 20 + count * 6) : nullptr;
        gPackedOf[m] = packed;
        packed += packMesh(packed, (const MeshVertex*)(mesh + 20), normals, count);
        gReadyOf[m] = ready;
        readyVertices(ready, (const MeshVertex*)(mesh + 20), normals, count);
        ready += ((count + 3) & ~3) * 6;
    }

    // the draws' jobs: where each one's vertices go, its parts
    gJobCount = 0;
    Vertex* out = gMeshVertices;
    for (uint32_t i = 0; i < gDrawCount && gJobCount < MAX_DRAWS; i++)
    {
        const DrawScene &d = gDraws[i];
        if (d.type != DRAW_MESH && d.type != DRAW_SHADOW)
            continue;
        MeshJob &job = gJobs[gJobCount++];
        job.xform = gXforms + i;
        job.game = gSceneVertices + d.base;
        job.normals = 0;
        job.shades = nullptr;
        job.intensity = 0;
        job.center = nullptr;
        job.radius = 0;
        if (d.type == DRAW_SHADOW)
        {
            job.count = SHADOW_VERTICES;
            job.packed = packed;
            packed += SHADOW_VERTICES / 4 * 6;
            job.ready = ready;
            ready += SHADOW_VERTICES * 6;
        }
        else
        {
            // the mesh's arrays (render.cpp meshParts)
            const uint8_t* mesh = gMeshGeometry + gMeshes[d.mesh];
            job.count = mesh[10];
            job.packed = gPackedOf[d.mesh];
            const uint8_t* after = mesh + 20 + job.count * 6;
            if (mesh[11] == MESH_LIGHT_NORMALS) {
                job.normals = 1;
            } else if (mesh[11] == MESH_LIGHT_SHADES) {
                job.shades = after;
            }
            job.intensity = *(const uint16_t*)(mesh + 8);
            job.center = (const int16_t*)mesh;
            job.radius = *(const int16_t*)(mesh + 16);
            job.ready = gReadyOf[d.mesh];
        }
        job.out = out;
        out += (job.count + 3) & ~3;
    }
    return nullptr;
}

// --- rooms ---------------------------------------------------------------

// xform.s's multiplies for a room's vertex: its bytes times 4 against each
// row, the translation first.
static RoomRotated roomModel(const RoomScene &room, int32_t k)
{
    const RoomVertex* p = roomPoints(room) + k;
    int32_t x = p->x * 4, y = p->y * 4, z = p->z * 4;
    return { row16(room.m, room.tx, x, y, z), row16(room.m + 3, room.ty, x, y, z),
             row16(room.m + 6, room.tz, x, y, z) };
}

uint32_t verticesRoomsDsp()
{
    for (uint32_t i = 0; i < gRoomCount; i++)
    {
        const RoomScene &room = gRooms[i];
        RoomDspJob &job = gRoomJobs[i];
        job.vertices = dspAddress(roomPoints(room));
        job.out = dspAddress(gRoomRotated + gRotatedAt[i]);
        job.groups = (room.verticesCount + 3) >> 2;
        const int32_t t[3] = { room.tx, room.ty, room.tz };
        for (int32_t r = 0; r < 3; r++)
        {
            job.m[4 * r] = t[r];
            for (int32_t k = 0; k < 3; k++) {
                job.m[4 * r + 1 + k] = 4 * room.m[3 * r + k];
            }
        }
    }
    memset(gRoomJobs + gRoomCount, 0, sizeof(RoomDspJob));

    const uint32_t jobs = dspAddress(gRoomJobs);
    const uint32_t mask = 0xFF;
    dspLoad(roomrot_dsp, ROOMROT_DSP_COUNT);
    dspWrite(0, 16, &jobs, 1);
    dspWrite(3, 12, &mask, 1);
    uint32_t t0 = ticks();
    dspStart(0);
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

void verticesRooms()
{
    cachePurge();       // the DSP wrote behind the cache
    RoomProject rp;
    rp.divTable = gDivTable;
    rp.zMin = VIEW_MIN_F >> (FIXED_SHIFT - 8);
    rp.zMax = VIEW_MAX_F >> (FIXED_SHIFT - 8);
    for (uint32_t i = 0; i < gRoomCount; i++)
    {
        const RoomScene &room = gRooms[i];
        rp.points = roomPoints(room);
        rp.x0 = room.clip[0];
        rp.y0 = room.clip[1];
        rp.x1 = room.clip[2];
        rp.y1 = room.clip[3];
        rp.wibble = room.ripple ? kWibble : nullptr;
        rp.phase = room.causticsFrame;
        rp.caustics = room.wet ? gCaustics[i] : nullptr;
        rp.count = room.verticesCount;
        roomProjectAsm(gRoomRotated + gRotatedAt[i], room.verticesCount, gVertices + room.base, &rp);
    }
}

// --- meshes --------------------------------------------------------------

// render.cpp meshXformSetup's test, the bounding sphere wholly in the frame
// and between the planes; and whether the sphere might reach past 1 << 29 in
// z, either way (by over a margin for meshes whose vertices stray past it).
static bool meshInside(const DrawXform &d, const int16_t* center, int32_t radius, bool &far)
{
    const int32_t* m = d.m;
    int32_t cx = (m[0] * center[0] + m[1] * center[1] + m[2] * center[2] + m[3]) >> FIXED_SHIFT;
    int32_t cy = (m[4] * center[0] + m[5] * center[1] + m[6] * center[2] + m[7]) >> FIXED_SHIFT;
    int32_t cz = (m[8] * center[0] + m[9] * center[1] + m[10] * center[2] + m[11]) >> FIXED_SHIFT;
    far = cz + radius > 30000 || cz - radius < -30000;
    int32_t zNear = cz - radius;
    return (zNear > (VIEW_MIN_F >> FIXED_SHIFT)) && (cz + radius < VIEW_DIST) &&
           (((cx < 0 ? -cx : cx) + radius) * 256 < ((FRAME_WIDTH >> 1) - 4) * zNear) &&
           (((cy < 0 ? -cy : cy) + radius) * 256 < ((FRAME_HEIGHT >> 1) - 4) * zNear);
}

// A vertex of the job's in mesh space, and its normal, from its groups.
static MeshVertex jobVertex(const MeshJob &job, int32_t k)
{
    int32_t longwords = job.normals ? 10 : 6;
    const int16_t* c = (const int16_t*)(job.packed + (k >> 2) * longwords + longwords - 6) + 3 * (k & 3);
    return { c[0], c[1], c[2] };
}

static void jobNormal(const MeshJob &job, int32_t k, int32_t n[3])
{
    const uint8_t* b = (const uint8_t*)(job.packed + (k >> 2) * 10 + (k & 3));
    n[0] = b[0] - 128;
    n[1] = b[1] - 128;
    n[2] = b[2] - 128;
}

// The light's direction in the mesh's space (meshXformSetup).
static void meshLight(const DrawXform &x, int32_t &lx, int32_t &ly, int32_t &lz)
{
    const int32_t* m = x.m;
    const int16_t* l = x.light;
    lx = (m[0] * l[0] + m[4] * l[1] + m[8] * l[2]) >> (FIXED_SHIFT + 7);
    ly = (m[1] * l[0] + m[5] * l[1] + m[9] * l[2]) >> (FIXED_SHIFT + 7);
    lz = (m[2] * l[0] + m[6] * l[1] + m[10] * l[2]) >> (FIXED_SHIFT + 7);
}

// The game's arithmetic for one mesh vertex but its shade (meshxform.s
// .Lloop), from the job's mesh.
static void meshExact(MeshProject* mp, int32_t index, Vertex* v)
{
    const MeshJob &job = *mp->job;
    const int32_t* m = job.xform->m;
    MeshVertex p = jobVertex(job, index);
    int32_t z = row(m + 8, m[11], p.x, p.y, p.z);
    uint32_t clip = 0;
    if (!(z > VIEW_MIN_F))
    {
        z = VIEW_MIN_F;
        clip = CLIP_PLANE;
    }
    if (z >= VIEW_MAX_F)
    {
        z = VIEW_MAX_F;
        clip = CLIP_PLANE;
    }
    int32_t x = int16_t(uint32_t(row(m, m[3], p.x, p.y, p.z)) >> FIXED_SHIFT);
    int32_t y = int16_t(uint32_t(row(m + 4, m[7], p.x, p.y, p.z)) >> FIXED_SHIFT);
    int32_t d = mp->divTable[uint32_t(z) >> (FIXED_SHIFT + 4)];
    int32_t sx = int16_t(uint32_t(clamp(x * d, -P_MAX, P_MAX)) >> 12) + (FRAME_WIDTH >> 1);
    int32_t sy = int16_t(uint32_t(clamp(y * d, -P_MAX, P_MAX)) >> 12) + (FRAME_HEIGHT >> 1);
    if (uint32_t(sx) > FRAME_WIDTH || uint32_t(sy) > FRAME_HEIGHT)
    {
        clip += CLIP_FRAME;
        sx = clamp(sx, -1024, 1023);
        sy = clamp(sy, -1024, 1023);
        clip += (sx < 0 ? CLIP_LEFT : 0) + (sx > FRAME_WIDTH ? CLIP_RIGHT : 0);
        clip += (sy < 0 ? CLIP_TOP : 0) + (sy > FRAME_HEIGHT ? CLIP_BOTTOM : 0);
    }
    v->x = sx;
    v->y = sy;
    v->z = uint32_t(z) >> FIXED_SHIFT;
    v->clip = clip;
}

void verticesFeed()
{
    for (int32_t i = 0; i < gJobCount; i++)
    {
        MeshJob &job = gJobs[i];
        const DrawXform &x = *job.xform;
        MeshDspJob &dsp = gDspJobs[i];
        if (!job.center)
        {
            const int16_t* s = x.shadow;
            MeshVertex v[SHADOW_VERTICES];
            shadowVertices(v, s[0], s[1], s[2], s[3]);
            packMesh((uint32_t*)job.packed, v, nullptr, SHADOW_VERTICES);
        }
        bool far = false;
        job.inside = job.center && meshInside(x, job.center, job.radius, far);
        job.far = far;

        // the rows times 4; the light: by its normals, the DSP's arithmetic
        // keeping a shade in 0 .. 31 while ambient - range .. ambient + range
        // does (|n . l| < 1 << 14) and the normals' +128 comes off in int32;
        // else a shade for all, or the vertices' own (meshShadesAsm), or by
        // the normals here
        const int32_t t[3] = { x.m[3], x.m[7], x.m[11] };
        for (int32_t r = 0; r < 3; r++)
        {
            dsp.k[4 * r] = t[r] * 4;
            for (int32_t k = 0; k < 3; k++) {
                dsp.k[4 * r + 1 + k] = x.m[4 * r + k] * 4;
            }
        }
        int32_t range = x.range * -4;
        int32_t ambient = 0, lx = 0, ly = 0, lz = 0, bias = 0;
        if (job.normals && x.range)
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
        else if (job.shades)
        {
            job.shading = MESH_SHADES;
        }
        else
        {
            job.shading = MESH_SHADED;
            ambient = clamp(int16_t(uint32_t(job.intensity + x.ambient) >> 8), 0, 31) << 8;
        }
        dsp.k[12] = int32_t((uint32_t(ambient) << 16 | 0xFFFF) - uint32_t(bias));
        dsp.k[13] = lx * range;
        dsp.k[14] = ly * range;
        dsp.k[15] = lz * range;
        dsp.src = dspAddress(job.packed);
        dsp.groups = (job.count + 3) >> 2;
        dsp.entry = job.normals ? MESHROT_DSP_NORMALS : MESHROT_DSP_COORDS;
        dsp.place = job.normals ? 0 : 4;
        dsp.longwords = job.normals ? 10 : 6;
    }
    gDspJobs[gJobCount].groups = 0;
}

uint32_t verticesMeshesDsp()
{
    static const uint32_t md1[4] = { 65536, 0xFFFF, 0x1F00, 0xFF };
    static const uint32_t one = 1;
    const uint32_t md3[2] = { dspAddress(gDspJobs), dspAddress(gMeshVertices) - 8 };
    dspLoad(meshrot_dsp, MESHROT_DSP_COUNT);
    dspWrite(1, 24, md1, 4);
    dspWrite(2, 10, &one, 1);
    dspWrite(3, 20, md3, 2);
    uint32_t t0 = ticks();
    dspStart(MESHROT_DSP_START);
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
        if (!job.center)
        {
            const int16_t* s = job.xform->shadow;
            MeshVertex v[SHADOW_VERTICES];
            shadowVertices(v, s[0], s[1], s[2], s[3]);
            readyVertices(job.ready, v, nullptr, SHADOW_VERTICES);
        }
        memcpy(v3.k, dsp.k, sizeof(v3.k));
        v3.k[12] = int32_t(uint32_t(dsp.k[12]) + 128u * uint32_t(dsp.k[13] + dsp.k[14] + dsp.k[15]));
        v3.src = dspAddress(job.ready);
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
        if (job.shading == MESH_SHADED && job.normals) {
            meshLight(x, lx, ly, lz);
        }
        int32_t flat = job.shading == MESH_SHADED && !job.normals
                       ? clamp(int16_t(uint32_t(job.intensity + x.ambient) >> 8), 0, 31) << 8 : 0;
        const uint32_t* out = (const uint32_t*)job.out;
        for (int32_t k = 0; k < job.count; k++, out += 2)
        {
            MeshVertex p = jobVertex(job, k);
            int32_t vx = row(m, m[3], p.x, p.y, p.z) >> FIXED_SHIFT;
            int32_t vy = row(m + 4, m[7], p.x, p.y, p.z) >> FIXED_SHIFT;
            int32_t vz = row(m + 8, m[11], p.x, p.y, p.z) >> FIXED_SHIFT;
            int32_t t = flat;
            if (job.shading == MESH_SHADED && job.normals)
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

// A mesh's shades by its normals, the game's arithmetic (meshxform.s
// .Lnormal), where the DSP's might leave 0 .. 31.
static void meshNormals(const MeshJob &job)
{
    int32_t lx, ly, lz;
    meshLight(*job.xform, lx, ly, lz);
    for (int32_t k = 0; k < job.count; k++)
    {
        int32_t n[3];
        jobNormal(job, k, n);
        int32_t dot = int16_t(n[0] * lx + n[1] * ly + n[2] * lz) * int16_t(job.xform->range);
        job.out[k].g = clamp(int16_t(uint32_t(job.xform->ambient - int16_t(uint32_t(dot) >> 14)) >> 8), 0, 31);
    }
}

void verticesMeshes()
{
    MeshProject mp;
    mp.divTable = gDivTable;
    mp.exact = meshExact;
    for (int32_t i = 0; i < gJobCount; i++)
    {
        const MeshJob &job = gJobs[i];
        mp.inside = job.inside;
        mp.job = &job;
        if (job.far)
        {
            for (int32_t k = 0; k < job.count; k++) {
                meshExact(&mp, k, job.out + k);
            }
        }
        else
        {
            meshProjectAsm(job.out, job.count, &mp);
        }
        if (job.shading == MESH_SHADES) {
            meshShadesAsm(job.out, job.count, job.shades, job.xform->ambient);
        } else if (job.shading == MESH_NORMALS) {
            meshNormals(job);
        }
    }
}

int32_t verticesMeshesCheck(int32_t &vertices)
{
    int32_t bad = 0;
    vertices = 0;
    for (int32_t i = 0; i < gJobCount; i++)
    {
        const MeshJob &job = gJobs[i];
        vertices += job.count;
        bad += memcmp(job.out, job.game, job.count * sizeof(Vertex)) != 0 ? job.count : 0;
    }
    return bad;
}

int32_t verticesFallbacksCheck()
{
    int32_t bad = 0;
    MeshProject mp;
    mp.divTable = gDivTable;
    mp.exact = meshExact;
    mp.inside = 0;
    for (int32_t pass = 0; pass < 2; pass++)
    {
        verticesMeshesDsp();
        for (int32_t i = 0; i < gJobCount; i++)
        {
            const MeshJob &job = gJobs[i];
            if (job.far)
                continue;
            if (pass == 1)
            {
                for (int32_t k = 0; k < job.count; k++) {
                    job.out[k].z = 64;
                }
            }
            mp.job = &job;
            meshProjectAsm(job.out, job.count, &mp);
            for (int32_t k = 0; k < job.count; k++)
            {
                const Vertex &v = job.out[k];
                bad += v.xy != job.game[k].xy || v.z != job.game[k].z || v.clip != job.game[k].clip;
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
        const RoomScene &room = gRooms[i];
        vertices += room.verticesCount;
        for (int32_t k = room.base; k < room.base + room.verticesCount; k++) {
            bad += memcmp(gVertices + k, gSceneVertices + k, sizeof(Vertex)) != 0;
        }
    }
    return bad;
}
