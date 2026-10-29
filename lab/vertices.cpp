// Stage 2 the new way (vertices.h).
#include "vertices.h"
#include "platform.h"
#include "scene.h"

#include "dsp.h"
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
#define MAX_DRAWS       256

static const int8_t kWibble[32] = {
    0, 0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 0,
    0, 0, -1, -1, -1, -2, -2, -2, -2, -2, -2, -2, -1, -1, -1, 0
};

// A mesh's projection and shading, as feeding it to the DSP set them up.
struct MeshJob
{
    Vertex* out;                // its vertices in gVertices
    int32_t count;
    int32_t inside;
    int32_t shading;            // MeshShading
    const uint8_t* shades;      // MESH_SHADES'
    const int8_t* normals;      // MESH_NORMALS'
    int32_t ambient;
    int32_t far;                // might reach past 1 << 29 in z: the DSP's z >> 14 would wrap, so
                                // the vertices are worked out here (meshExact)
    const MeshVertex* vertices; // in mesh space, for exact
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
static const uint8_t* gMeshGeometry;

// A room's job for roomrot.dsp (lab/gen_roomrot.py).
struct RoomDspJob
{
    uint32_t vertices;          // / 4
    uint32_t out;               // / 4
    int32_t groups;             // of 4 vertices: 0 ends the list
    int32_t m[12];              // t m0 m1 m2 a row, m times 4
};

#define MAX_ROOMS   64

// The rooms' rotations, the DSP's and vertexModel's, a room's at its own
// place (whole groups of 4); the rooms' jobs; the meshes' jobs, a shadow's
// vertices made in its job.
static RoomRotated gRoomRotated[MAX_VERTICES + 3 * MAX_ROOMS];
static RoomRotated gRoomModel[MAX_VERTICES + 3 * MAX_ROOMS];
static int32_t gRotatedAt[MAX_ROOMS];
static RoomDspJob gRoomJobs[MAX_ROOMS + 1];
static MeshJob gJobs[MAX_DRAWS];
static int32_t gJobCount;
static MeshVertex gShadows[MAX_DRAWS][SHADOW_VERTICES];

void verticesInit()
{
    gRooms = sceneArray<const RoomScene>("ROOM", &gRoomCount);
    gRoomGeometry = sceneArray<const uint8_t>("RGEO");
    gCaustics = (const int16_t (*)[32])sceneChunk("CAUS");
    gDivTable = sceneArray<const uint16_t>("DIVT");
    gDraws = sceneArray<const DrawScene>("DRAW", &gDrawCount);
    gXforms = sceneArray<const DrawXform>("XFRM");
    gMeshes = sceneArray<const uint32_t>("MESH");
    gMeshGeometry = sceneArray<const uint8_t>("MGEO");
    if (gRoomCount > MAX_ROOMS) {
        gRoomCount = MAX_ROOMS;
    }
    int32_t at = 0;
    for (uint32_t i = 0; i < gRoomCount; i++)
    {
        gRotatedAt[i] = at;
        at += (gRooms[i].verticesCount + 3) & ~3;
    }
}

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

// The game's arithmetic for one mesh vertex but its shade (meshxform.s
// .Lloop), from the job's mesh.
static void meshExact(MeshProject* mp, int32_t index, Vertex* v)
{
    const MeshJob &job = *mp->job;
    const int32_t* m = job.xform->m;
    const MeshVertex &p = job.vertices[index];
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

void vertexModel()
{
    for (uint32_t i = 0; i < gRoomCount; i++)
    {
        const RoomScene &room = gRooms[i];
        const RoomVertex* p = roomPoints(room);
        RoomRotated* out = gRoomModel + gRotatedAt[i];
        for (int32_t k = 0; k < room.verticesCount; k++, p++, out++)
        {
            int32_t x = p->x * 4, y = p->y * 4, z = p->z * 4;
            out->x = row16(room.m, room.tx, x, y, z);
            out->y = row16(room.m + 3, room.ty, x, y, z);
            out->z = row16(room.m + 6, room.tz, x, y, z);
        }
    }

    gJobCount = 0;
    for (uint32_t i = 0; i < gDrawCount && gJobCount < MAX_DRAWS; i++)
    {
        const DrawScene &d = gDraws[i];
        const DrawXform &x = gXforms[i];
        if (d.type != DRAW_MESH && d.type != DRAW_SHADOW)
            continue;
        MeshJob &job = gJobs[gJobCount];
        job.out = gVertices + d.base;
        job.xform = &x;
        job.normals = nullptr;
        job.shades = nullptr;
        job.ambient = x.ambient;
        int32_t intensity = 0;
        const int16_t* center = nullptr;
        int32_t radius = 0;
        if (d.type == DRAW_SHADOW)
        {
            shadowVertices(gShadows[gJobCount], x.shadow[0], x.shadow[1], x.shadow[2], x.shadow[3]);
            job.vertices = gShadows[gJobCount];
            job.count = SHADOW_VERTICES;
        }
        else
        {
            // the mesh's arrays (render.cpp meshParts)
            const uint8_t* mesh = gMeshGeometry + gMeshes[d.mesh];
            job.count = mesh[10];
            job.vertices = (const MeshVertex*)(mesh + 20);
            const uint8_t* after = mesh + 20 + job.count * 6;
            if (mesh[11] == MESH_LIGHT_NORMALS) {
                job.normals = (const int8_t*)after;
            } else if (mesh[11] == MESH_LIGHT_SHADES) {
                job.shades = after;
            }
            intensity = *(const uint16_t*)(mesh + 8);
            center = (const int16_t*)mesh;
            radius = *(const int16_t*)(mesh + 16);
        }
        bool far = false;
        job.inside = center && meshInside(x, center, radius, far);
        job.far = far;

        // the light: the DSP's arithmetic keeps a shade in 0 .. 31 while
        // ambient - range .. ambient + range does (|n . l| < 1 << 14)
        int32_t lx = 0, ly = 0, lz = 0, flat = 0;
        if (job.normals && x.range)
        {
            const int32_t* m = x.m;
            const int16_t* l = x.light;
            lx = (m[0] * l[0] + m[4] * l[1] + m[8] * l[2]) >> (FIXED_SHIFT + 7);
            ly = (m[1] * l[0] + m[5] * l[1] + m[9] * l[2]) >> (FIXED_SHIFT + 7);
            lz = (m[2] * l[0] + m[6] * l[1] + m[10] * l[2]) >> (FIXED_SHIFT + 7);
            bool safe = x.ambient >= x.range && x.ambient + x.range <= 0x1FFF;
            job.shading = safe ? MESH_SHADED : MESH_NORMALS;
        }
        else if (job.shades)
        {
            job.shading = MESH_SHADES;
        }
        else
        {
            job.shading = MESH_SHADED;
            job.normals = nullptr;
            int32_t g = clamp(int16_t(uint32_t(intensity + x.ambient) >> 8), 0, 31);
            flat = g << 8;
        }

        // the vertices rotated and lit, in place
        uint32_t* out = (uint32_t*)job.out;
        for (int32_t k = 0; k < job.count; k++, out += 2)
        {
            const MeshVertex &p = job.vertices[k];
            const int32_t* m = x.m;
            int32_t vx = row(m, m[3], p.x, p.y, p.z) >> FIXED_SHIFT;
            int32_t vy = row(m + 4, m[7], p.x, p.y, p.z) >> FIXED_SHIFT;
            int32_t vz = row(m + 8, m[11], p.x, p.y, p.z) >> FIXED_SHIFT;
            int32_t t = flat;
            if (job.shading == MESH_SHADED && job.normals)
            {
                const int8_t* n = job.normals + 4 * k;
                t = x.ambient - (((n[0] * lx + n[1] * ly + n[2] * lz) * x.range) >> 14);
            }
            out[0] = (uint32_t(vx) << 16) | (vy & 0xFFFF);
            out[1] = (uint32_t(vz) << 16) | (t & 0x1F00);
        }
        gJobCount++;
    }
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
        const RoomRotated* b = gRoomModel + gRotatedAt[i];
        for (int32_t k = 0; k < gRooms[i].verticesCount; k++) {
            bad += a[k].x != b[k].x || a[k].y != b[k].y || a[k].z != b[k].z;
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

// A mesh's shades by its normals, the game's arithmetic (meshxform.s
// .Lnormal), where the DSP's might leave 0 .. 31.
static void meshNormals(const MeshJob &job)
{
    const int32_t* m = job.xform->m;
    const int16_t* l = job.xform->light;
    int32_t lx = (m[0] * l[0] + m[4] * l[1] + m[8] * l[2]) >> (FIXED_SHIFT + 7);
    int32_t ly = (m[1] * l[0] + m[5] * l[1] + m[9] * l[2]) >> (FIXED_SHIFT + 7);
    int32_t lz = (m[2] * l[0] + m[6] * l[1] + m[10] * l[2]) >> (FIXED_SHIFT + 7);
    for (int32_t k = 0; k < job.count; k++)
    {
        const int8_t* n = job.normals + 4 * k;
        int32_t dot = int16_t(n[0] * lx + n[1] * ly + n[2] * lz) * int16_t(job.xform->range);
        job.out[k].g = clamp(int16_t(uint32_t(job.ambient - int16_t(uint32_t(dot) >> 14)) >> 8), 0, 31);
    }
}

int32_t verticesFallbacksCheck(const Vertex* scene)
{
    int32_t bad = 0;
    MeshProject mp;
    mp.divTable = gDivTable;
    mp.exact = meshExact;
    mp.inside = 0;
    for (int32_t pass = 0; pass < 2; pass++)
    {
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
            const Vertex* want = scene + (job.out - gVertices);
            for (int32_t k = 0; k < job.count; k++)
            {
                const Vertex &v = job.out[k];
                bad += v.xy != want[k].xy || v.z != want[k].z || v.clip != want[k].clip;
            }
        }
        vertexModel();
    }
    return bad;
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
            meshShadesAsm(job.out, job.count, job.shades, job.ambient);
        } else if (job.shading == MESH_NORMALS) {
            meshNormals(job);
        }
    }
}
