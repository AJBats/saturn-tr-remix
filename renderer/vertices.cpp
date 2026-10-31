// Vertices (vertices.h): the DSP's programs fed and started, the CPUs'
// projection of what they hand over.
#include "vertices.h"
#include "draws.h"
#include "platform.h"

#include "dsp.h"
#include "meshrot_dsp.h"
#include "roomrot_dsp.h"

#include <stddef.h>
#include <string.h>

namespace render {

#define FIXED_SHIFT     14
#define VIEW_DIST       (1024 * 20)
#define VIEW_MIN_F      (64 << FIXED_SHIFT)
#define VIEW_MAX_F      (VIEW_DIST << FIXED_SHIFT)
#define FRAME_WIDTH     352
#define FRAME_HEIGHT    224
#define P_MAX           0x007FF000  // projected 2047 << 12
#define XY_MAX          0x007FFF00  // view x, y whose >> 8 fits in 16 bits
#define COORD_MAX       1023        // VDP1_COORD_MAX
#define FOG_MIN         12288
#define SHADE_MAX       8191        // shade << 8 at its darkest
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
// to use, its groups as the level keeps them.
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

// The plan's prices, SH-2 cycles a unit (Mednafen, the ledge, each stage
// run alone): the work each CPU has as the counts predict it.
#define PLAN_ROOM_VERTEX    125     // projected
#define PLAN_ROOM_FACE      228     // a face given, to commands
#define PLAN_MESH_JOB       456     // fed
#define PLAN_MESH_VERTEX    63      // projected
#define PLAN_MESH_FACE      138     // a face given, to commands
#define PLAN_DSP_VERTEX     46      // a room's, turned on the DSP

// A mesh's parts as its jobs take them, once for the level's mesh (a
// shadow's slot's, its groups made each frame).
struct MeshParts
{
    int32_t count;
    int32_t price;              // its job's, as the plan predicts it
    int32_t normals;            // its groups hold its normals
    const uint8_t* shades;      // MESH_SHADES'
    int32_t intensity;
    const uint32_t* packed;     // its groups (its record's, or a shadow's slot's)
    const int16_t* center;      // a mesh's bounding sphere, or none (a shadow)
    int32_t radius;
    int32_t faces;              // its quads and triangles: its commands at most
};

// A mesh's projection and shading, as feeding it to the DSP set them up.
struct MeshJob
{
    Vertex* out;                // its vertices: the DSP's output, projected in place (whole groups)
    const MeshParts* parts;
    const DrawXform* xform;
    int32_t inside;
    int32_t shading;            // MeshShading
    int32_t far;                // might reach past 1 << 29 in z: the DSP's z >> 14 would wrap, so
                                // the vertices are worked out here (meshExact)
};

// meshfeed.s's offsets
static_assert(offsetof(MeshJob, parts) == 4 && offsetof(MeshJob, xform) == 8 &&
              offsetof(MeshJob, inside) == 12 && offsetof(MeshJob, shading) == 16 &&
              offsetof(MeshJob, far) == 20 && sizeof(MeshJob) == 24, "meshfeed.s's MeshJob");
static_assert(offsetof(MeshParts, count) == 0 && offsetof(MeshParts, normals) == 8 &&
              offsetof(MeshParts, shades) == 12 && offsetof(MeshParts, intensity) == 16 &&
              offsetof(MeshParts, packed) == 20 && offsetof(MeshParts, center) == 24 &&
              offsetof(MeshParts, radius) == 28, "meshfeed.s's MeshParts");
static_assert(offsetof(MeshDspJob, src) == 64 && sizeof(MeshDspJob) == 84, "meshfeed.s's MeshDspJob");
static_assert(offsetof(DrawXform, ambient) == 48 && offsetof(DrawXform, light) == 56 &&
              offsetof(DrawXform, shadow) == 62, "meshfeed.s's DrawXform");

extern "C" void verticesFeedAsm(MeshJob* jobs, int32_t count, MeshDspJob* dsp, const uint32_t* tails);

// A job's last three longwords, without normals then with: where a group
// begins, where it lands in MD2, its longwords.
static const uint32_t kTails[6] = { MESHROT_DSP_COORDS, 4, 6, MESHROT_DSP_NORMALS, 0, 10 };

static const FaceRoom* gRooms;     // the frame's (verticesRoomsSet)
static uint32_t gRoomCount;
static const DrawScene* gDraws;     // ... and its draws (verticesDrawsSet)
static uint32_t gDrawCount;
static const uint8_t* gStore;       // the level's meshes (MeshStore)
static uint32_t gMeshCount;         // ... its records

// The rooms' rotations, the DSP's, a room's at its own place (whole groups
// of 4), and their jobs.
static RoomRotated gRoomRotated[MAX_ROOM_VERTICES] __attribute__((aligned(16)));
static int32_t gRotatedAt[MAX_ROOMS];
static int32_t gRoomsPredicted;     // the plan's prediction of their projection and faces
static int32_t gRoomsFaces;         // their quads and triangles
static RoomDspJob gRoomJobs[MAX_ROOMS + 1];

// The shadows' groups, made each frame (then 10 longwords meshrot.dsp reads
// past the last; a mesh's are its record's); each mesh's parts, then the
// shadows' slots'; the frame's jobs and the plan's prediction of them, the
// DSP's of each; their vertices, a job's after another, whole groups, after
// a group's room for meshrot.dsp's first output, of nothing.
static uint32_t* gMeshPacked;
static MeshParts* gParts;
static const MeshParts* gShadowParts;
static const MeshParts* gShadowPartsEnd;
static MeshJob gJobs[MAX_DRAWS];
static int32_t gJobsPredicted;
static int32_t gJobsFaces;      // the jobs' faces: their commands at most
static MeshDspJob gDspJobs[MAX_DRAWS + 1];
static int32_t gJobCount;
static int32_t gJobOfDraw[MAX_DRAWS];      // a draw's job, or -1
static Vertex gMeshOut[4 + MAX_MESH_VERTICES] __attribute__((aligned(16)));
static Vertex* const gMeshVertices = gMeshOut + 4;

static inline Vertex* roomVertices(const FaceRoom &room)
{
    return (Vertex*)room.vertices;
}

static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

// A row times a vertex plus the translation, wrapping as the
// multiply-accumulate unit does.
static inline int32_t row(const int32_t* m, int32_t t, int32_t x, int32_t y, int32_t z)
{
    return int32_t(uint32_t(t) + uint32_t(m[0]) * uint32_t(x) + uint32_t(m[1]) * uint32_t(y) +
                   uint32_t(m[2]) * uint32_t(z));
}

// The order the DSP turns the rooms in (its k-th job's room), and each
// room's place in it: the rooms done counts in that order.
static uint8_t gRoomOrder[MAX_ROOMS];
static uint8_t gRoomTurn[MAX_ROOMS];

static void roomOrder(const uint8_t* order)
{
    for (uint32_t k = 0; k < gRoomCount; k++)
    {
        gRoomOrder[k] = order[k];
        gRoomTurn[order[k]] = k;
    }
}

// Between polls of what the DSP writes: off the bus a while, so as not to
// hold up the DSP's transfers.
static inline void pause()
{
    for (volatile int32_t i = 0; i < 16; i++);
}

static void chunksLay();

void verticesRoomsSet(const FaceRoom* rooms, uint32_t count)
{
    gRooms = rooms;
    gRoomCount = count < MAX_ROOMS ? count : MAX_ROOMS;
    int32_t at = 0, predicted = 0, faces = 0;
    for (uint32_t i = 0; i < gRoomCount; i++)
    {
        const FaceRoom &r = gRooms[i];
        gRotatedAt[i] = at;
        at += (r.verticesCount + 3) & ~3;
        predicted += PLAN_ROOM_VERTEX * r.verticesCount + PLAN_ROOM_FACE * (r.quadsCount + r.trianglesCount);
        faces += r.quadsCount + r.trianglesCount;
    }
    gRoomsPredicted = predicted;
    gRoomsFaces = faces;
    chunksLay();
}

void verticesDrawsSet(const DrawScene* draws, uint32_t count, const DrawXform* xforms)
{
    gDraws = draws;
    gDrawCount = count;
    // the draws' jobs: where each one's vertices go, its mesh's parts or its
    // shadow's slot's
    int32_t jobs = 0, predicted = 0, faces = 0;
    Vertex* out = gMeshVertices;
    const MeshParts* shadow = gShadowParts;
    for (uint32_t i = 0; i < count && jobs < MAX_DRAWS; i++)
    {
        const DrawScene &d = draws[i];
        gJobOfDraw[i] = -1;
        const MeshParts* parts;
        if (d.type == DRAW_MESH) {
            parts = gParts + d.mesh;
        } else if (d.type == DRAW_SHADOW && shadow < gShadowPartsEnd) {
            parts = shadow++;
        } else {
            continue;
        }
        gJobOfDraw[i] = jobs;
        MeshJob &job = gJobs[jobs++];
        job.out = out;
        job.parts = parts;
        job.xform = xforms + i;
        out += (parts->count + 3) & ~3;
        predicted += parts->price;
        faces += parts->faces;
    }
    gJobCount = jobs;
    gJobsPredicted = predicted;
    gJobsFaces = faces;
}

int32_t verticesFaces()
{
    return gJobsFaces;
}

int32_t verticesRoomFaces()
{
    return gRoomsFaces;
}

int32_t verticesInsideOf(uint32_t draw)
{
    return gJobs[gJobOfDraw[draw]].inside;
}

const char* verticesInit(const VerticesLevel &level)
{
    gStore = (const uint8_t*)level.store;
    gMeshCount = level.store->recordCount;
    const uint32_t* records = (const uint32_t*)(gStore + level.store->records);

    // each mesh's parts from its record; the shadows' slots, their groups
    // made each frame (and room for what meshrot.dsp reads past the last)
    uint32_t packedLongwords = MAX_DRAW_SHADOWS * SHADOW_VERTICES / 4 * 6 + 10;
    gMeshPacked = (uint32_t*)level.alloc(packedLongwords * 4);
    gParts = (MeshParts*)level.alloc((gMeshCount + MAX_DRAW_SHADOWS) * sizeof(MeshParts));
    if (!gMeshPacked || !gParts)
        return "no room for the meshes' parts";
    memset(gMeshPacked, 0, packedLongwords * 4);
    for (uint32_t m = 0; m < gMeshCount; m++)
    {
        const uint8_t* mesh = gStore + records[m];
        int32_t count = mesh[10];
        bool normals = mesh[11] == MESH_LIGHT_NORMALS;
        MeshParts &p = gParts[m];
        p.count = count;
        p.price = PLAN_MESH_JOB + PLAN_MESH_VERTEX * count +
                  PLAN_MESH_FACE * (*(const int16_t*)(mesh + 12) + *(const int16_t*)(mesh + 14));
        p.normals = normals;
        p.packed = (const uint32_t*)(mesh + 20);
        p.shades = mesh[11] == MESH_LIGHT_SHADES ? mesh + 20 + ((count + 3) >> 2) * 24 : nullptr;
        p.intensity = *(const uint16_t*)(mesh + 8);
        p.center = (const int16_t*)mesh;
        p.radius = *(const int16_t*)(mesh + 16);
        p.faces = *(const int16_t*)(mesh + 12) + *(const int16_t*)(mesh + 14);
    }
    // a shadow's: 8 vertices, 3 faces given
    uint32_t* packed = gMeshPacked;
    for (uint32_t s = 0; s < MAX_DRAW_SHADOWS; s++)
    {
        MeshParts &p = gParts[gMeshCount + s];
        p = {};
        p.count = SHADOW_VERTICES;
        p.price = PLAN_MESH_JOB + PLAN_MESH_VERTEX * SHADOW_VERTICES + PLAN_MESH_FACE * 3;
        p.faces = 3;
        p.packed = packed;
        packed += SHADOW_VERTICES / 4 * 6;
    }
    gShadowParts = gParts + gMeshCount;
    gShadowPartsEnd = gShadowParts + MAX_DRAW_SHADOWS;
    return nullptr;
}

// --- rooms ---------------------------------------------------------------

// The rooms the DSP has turned so far, which roomrot.dsp sends after each
// room's output (a cache line of its own); what the slave's part of stage 2
// together measured.
struct RoomsDone
{
    uint32_t rooms;
    uint32_t work, wait;
    uint32_t pad;
} __attribute__((aligned(16)));
static RoomsDone gRoomsDone;

// Room i turned by the DSP (the cycles waiting added to wait).
static void roomTurned(uint32_t i, uint32_t &wait)
{
    volatile uint32_t* done = uncached(&gRoomsDone.rooms);
    uint32_t w0 = ticks();
    while (*done <= gRoomTurn[i]) {
        pause();
    }
    wait += ticksCycles(w0, ticks());
}

// The rooms' jobs, roomrot.dsp loaded and started on them: the tick it
// started at.
// The ticks the last runs started at (verticesDspStarts).
static uint32_t gRoomsDspAt, gMeshesDspAt;

static uint32_t roomsDspStart()
{
    for (uint32_t k = 0; k < gRoomCount; k++)
    {
        uint32_t i = gRoomOrder[k];
        const FaceRoom &room = gRooms[i];
        RoomDspJob &job = gRoomJobs[k];
        job.vertices = dspAddress(room.points);
        job.out = dspAddress(gRoomRotated + gRotatedAt[i]);
        job.groups = (room.verticesCount + 3) >> 2;
        for (int32_t r = 0; r < 3; r++)
        {
            job.m[4 * r] = room.t[r];
            for (int32_t k = 0; k < 3; k++) {
                job.m[4 * r + 1 + k] = 4 * room.m[3 * r + k];
            }
        }
    }
    memset(gRoomJobs + gRoomCount, 0, sizeof(RoomDspJob));

    const uint32_t md0[3] = { dspAddress(gRoomJobs), dspAddress(&gRoomsDone.rooms), 0 };
    const uint32_t mask = 0xFF;
    *uncached(&gRoomsDone.rooms) = 0;
    dspLoad(roomrot_dsp, ROOMROT_DSP_COUNT);
    dspWrite(0, 16, md0, 3);
    dspWrite(3, 12, &mask, 1);
    uint32_t t0 = ticks();
    dspStart(0);
    *uncached(&gRoomsDspAt) = t0;
    return t0;
}

// Room i's vertices projected from the DSP's rotations.
static void roomProject(uint32_t i, int32_t from = 0, int32_t to = -1)
{
    const FaceRoom &room = gRooms[i];
    if (to < 0) {
        to = room.verticesCount;
    }
    RoomProject rp;
    rp.divTable = divTable;
    rp.zMin = VIEW_MIN_F >> (FIXED_SHIFT - 8);
    rp.zMax = VIEW_MAX_F >> (FIXED_SHIFT - 8);
    rp.points = room.points + from;
    rp.x0 = room.clip[0];
    rp.y0 = room.clip[1];
    rp.x1 = room.clip[2];
    rp.y1 = room.clip[3];
    rp.wibble = room.ripple ? kWibble : nullptr;
    rp.phase = room.phase;
    rp.caustics = room.caustics;
    rp.count = to;      // the caustics' index, count - vertices left: from on
    roomProjectAsm(gRoomRotated + gRotatedAt[i] + from, to - from, roomVertices(room) + from, &rp);
}

// --- meshes --------------------------------------------------------------

// A vertex of the job's in mesh space, and its normal, from its groups.
static MeshVertex jobVertex(const MeshJob &job, int32_t k)
{
    int32_t longwords = job.parts->normals ? 10 : 6;
    const int16_t* c = (const int16_t*)(job.parts->packed + (k >> 2) * longwords + longwords - 6) + 3 * (k & 3);
    return { c[0], c[1], c[2] };
}

static void jobNormal(const MeshJob &job, int32_t k, int32_t n[3])
{
    const uint8_t* b = (const uint8_t*)(job.parts->packed + (k >> 2) * 10 + (k & 3));
    n[0] = b[0] - 128;
    n[1] = b[1] - 128;
    n[2] = b[2] - 128;
}

// The light's direction in the mesh's space (meshXformSetup).
static void meshLight(const DrawXform &x, int32_t &lx, int32_t &ly, int32_t &lz)
{
    const int32_t* m = x.m;
    const int16_t* l = x.light;
    const int32_t c0[3] = { m[0], m[4], m[8] }, c1[3] = { m[1], m[5], m[9] }, c2[3] = { m[2], m[6], m[10] };
    lx = row(c0, 0, l[0], l[1], l[2]) >> (FIXED_SHIFT + 7);
    ly = row(c1, 0, l[0], l[1], l[2]) >> (FIXED_SHIFT + 7);
    lz = row(c2, 0, l[0], l[1], l[2]) >> (FIXED_SHIFT + 7);
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

// meshrot.dsp loaded and started on the jobs fed: the tick it started at.
static uint32_t meshesDspStart()
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
    *uncached(&gMeshesDspAt) = t0;
    return t0;
}

void verticesDspStarts(uint32_t &rooms, uint32_t &meshes)
{
    rooms = *uncached(&gRoomsDspAt);
    meshes = *uncached(&gMeshesDspAt);
}

// A mesh's shades by its normals, the game's arithmetic (meshxform.s
// .Lnormal), where the DSP's might leave 0 .. 31.
static void meshNormals(const MeshJob &job)
{
    int32_t lx, ly, lz;
    meshLight(*job.xform, lx, ly, lz);
    for (int32_t k = 0; k < job.parts->count; k++)
    {
        int32_t n[3];
        jobNormal(job, k, n);
        int32_t dot = int16_t(n[0] * lx + n[1] * ly + n[2] * lz) * int16_t(job.xform->range);
        job.out[k].g = clamp(int16_t(uint32_t(job.xform->ambient - int16_t(uint32_t(dot) >> 14)) >> 8), 0, 31);
    }
}

// Job i's vertices projected in place and shaded (mp's divide table and
// exact set).
static inline __attribute__((always_inline)) void meshProject(MeshProject &mp, int32_t i)
{
    const MeshJob &job = gJobs[i];
    mp.inside = job.inside;
    mp.job = &job;
    if (job.far)
    {
        for (int32_t k = 0; k < job.parts->count; k++) {
            meshExact(&mp, k, job.out + k);
        }
    }
    else
    {
        meshProjectAsm(job.out, job.parts->count, &mp);
    }
    if (job.shading == MESH_SHADES) {
        meshShadesAsm(job.out, job.parts->count, job.parts->shades, job.xform->ambient);
    } else if (job.shading == MESH_NORMALS) {
        meshNormals(job);
    }
}

// --- stage 2 in pieces, for a frame's stages run together -----------------

// A job's last output longword (its last vertex's z, shade and clip flags),
// marked before the DSP runs with what its output never holds: a clip byte
// the DSP leaves 0. The mark gone, the job's output is all there.
#define DSP_UNWRITTEN   0xFF

static volatile uint32_t* jobLast(const MeshJob &job)
{
    Vertex* last = job.out + ((job.parts->count + 3) & ~3) - 1;
    return uncached((volatile uint32_t*)last + 1);
}

static MeshProject gPiecesProject;

// The rooms' projection in chunks of CHUNK vertices, each room's after the
// one before: each chunk's room, which CPU has it (tasClaim), whether it's
// done, and whether the plan gives it to the master.
#define CHUNK           64
#define MAX_CHUNKS      (MAX_ROOM_VERTICES / CHUNK + MAX_ROOMS)
static int32_t gChunkAt[MAX_ROOMS + 1];     // a room's first chunk
static uint8_t gChunkRoom[MAX_CHUNKS];
static uint8_t gChunkClaims[MAX_CHUNKS] __attribute__((aligned(16)));
static uint8_t gChunkProjected[MAX_CHUNKS] __attribute__((aligned(16)));
static bool gChunkMaster[MAX_CHUNKS];

static int32_t chunkSize(int32_t c)
{
    const FaceRoom &room = gRooms[gChunkRoom[c]];
    int32_t from = (c - gChunkAt[gChunkRoom[c]]) * CHUNK;
    return room.verticesCount - from < CHUNK ? room.verticesCount - from : CHUNK;
}

void verticesPredicted(int32_t &master, int32_t &slave)
{
    master = gJobsPredicted;
    slave = gRoomsPredicted;
}

int32_t verticesSlaveIdle()
{
    return PLAN_DSP_VERTEX * gRooms[0].verticesCount;
}

int32_t verticesJobOfDraw(uint32_t draw)
{
    return gJobOfDraw[draw];
}

int32_t verticesJobs()
{
    return gJobCount;
}

// The rooms' chunks: each room's first, each chunk's room.
static void chunksLay()
{
    gChunkAt[0] = 0;
    for (uint32_t i = 0; i < gRoomCount; i++)
    {
        gChunkAt[i + 1] = gChunkAt[i] + (gRooms[i].verticesCount + CHUNK - 1) / CHUNK;
        for (int32_t c = gChunkAt[i]; c < gChunkAt[i + 1]; c++) {
            gChunkRoom[c] = i;
        }
    }
}

void verticesPlan(int32_t bias, int32_t ahead, bool started)
{
    // the slave's rooms, the master's draws (its join comes after both), and
    // the bias the frames before found
    int32_t master, slave;
    verticesPredicted(master, slave);
    master += bias;
    // from the last chunk back, each that leaves the master's share no
    // greater than the slave's
    for (int32_t c = gChunkAt[gRoomCount] - 1; c >= 0; c--)
    {
        int32_t p = PLAN_ROOM_VERTEX * chunkSize(c);
        gChunkMaster[c] = master + p <= slave - p;
        if (gChunkMaster[c])
        {
            master += p;
            slave -= p;
        }
    }

    // the DSP turns the slave's first room, then those the master has
    // chunks of (it takes them once fed), then the rest in order; or every
    // room in order, when the master comes to its chunks only after the DSP
    // has turned them all (or the DSP has them already, started)
    if (started)
        return;
    uint8_t order[MAX_ROOMS];
    bool masters[MAX_ROOMS] = {};
    int32_t dsp = 0;
    for (uint32_t i = 0; i < gRoomCount; i++) {
        dsp += PLAN_DSP_VERTEX * gRooms[i].verticesCount;
    }
    bool late = ahead + PLAN_MESH_JOB * gJobCount >= dsp;
    for (int32_t c = 0; c < gChunkAt[gRoomCount] && !late; c++) {
        masters[gChunkRoom[c]] = masters[gChunkRoom[c]] || gChunkMaster[c];
    }
    uint32_t k = 0;
    order[k++] = 0;
    for (uint32_t pass = 0; pass < 2; pass++)
    {
        for (uint32_t i = 1; i < gRoomCount; i++)
        {
            if (masters[i] == !pass) {
                order[k++] = i;
            }
        }
    }
    roomOrder(order);
}

void verticesRoomsStart()
{
    uint8_t order[MAX_ROOMS];
    for (uint32_t i = 0; i < gRoomCount; i++) {
        order[i] = i;
    }
    roomOrder(order);
    verticesRoomsBegin();
}

void verticesRoomsBegin()
{
    for (int32_t c = 0; c < gChunkAt[gRoomCount]; c++)
    {
        *uncached(gChunkClaims + c) = 0;
        *uncached(gChunkProjected + c) = 0;
    }
    roomsDspStart();
}

void verticesClaimMasters()
{
    for (int32_t c = 0; c < gChunkAt[gRoomCount]; c++)
    {
        if (gChunkMaster[c]) {
            tasClaim(uncached(gChunkClaims + c));
        }
    }
}

// Chunk c projected, its room waited for on the DSP; marked done.
static void chunkProject(int32_t c, uint32_t &wait)
{
    uint32_t room = gChunkRoom[c];
    roomTurned(room, wait);
    int32_t from = (c - gChunkAt[room]) * CHUNK;
    roomProject(room, from, from + chunkSize(c));
    *uncached(gChunkProjected + c) = 1;
}

void verticesMasterRooms(uint32_t &wait, bool &meshes)
{
    for (int32_t c = 0; c < gChunkAt[gRoomCount]; c++)
    {
        if (gChunkMaster[c])
        {
            meshes = meshes || verticesMeshesTryStart();
            chunkProject(c, wait);
        }
    }
}

void verticesRoom(uint32_t i, uint32_t &wait)
{
    for (int32_t c = gChunkAt[i]; c < gChunkAt[i + 1]; c++)
    {
        if (tasClaim(uncached(gChunkClaims + c)))
        {
            chunkProject(c, wait);
            continue;
        }
        volatile uint8_t* projected = uncached(gChunkProjected + c);
        uint32_t w0 = ticks();
        while (!*projected) {
            pause();
        }
        wait += ticksCycles(w0, ticks());
        cacheDrop(roomVertices(gRooms[i]) + (c - gChunkAt[i]) * CHUNK, chunkSize(c) * sizeof(Vertex));
    }
}

void verticesRoomWait(uint32_t i, uint32_t &wait)
{
    for (int32_t c = gChunkAt[i]; c < gChunkAt[i + 1]; c++)
    {
        volatile uint8_t* projected = uncached(gChunkProjected + c);
        uint32_t w0 = ticks();
        while (!*projected) {
            pause();
        }
        wait += ticksCycles(w0, ticks());
    }
    cacheDrop(roomVertices(gRooms[i]), gRooms[i].verticesCount * sizeof(Vertex));
}

void verticesMeshesFeed(int32_t from, int32_t to)
{
    if (to < 0) {
        to = gJobCount;
    }
    if (from < to) {
        verticesFeedAsm(gJobs + from, to - from, gDspJobs + from, kTails);
    }
    for (int32_t i = from; i < to; i++) {
        *jobLast(gJobs[i]) = DSP_UNWRITTEN;
    }
    gPiecesProject.divTable = divTable;
    gPiecesProject.exact = meshExact;
}

bool verticesMeshesTryStart()
{
    if (dspBusy())
        return false;
    meshesDspStart();
    return true;
}

void verticesMeshesStart(uint32_t &wait)
{
    uint32_t w0 = ticks();
    while (dspBusy());
    wait += ticksCycles(w0, ticks());
    meshesDspStart();
}

const Vertex* verticesDraw(uint32_t draw, uint32_t &wait)
{
    int32_t j = gJobOfDraw[draw];
    volatile uint32_t* last = jobLast(gJobs[j]);
    uint32_t w0 = ticks();
    while ((*last & 0xFF) == DSP_UNWRITTEN) {
        pause();
    }
    wait += ticksCycles(w0, ticks());
    meshProject(gPiecesProject, j);
    return gJobs[j].out;
}

void verticesEnd()
{
    while (dspBusy());
}

}

#ifdef RENDER_LAB
#include "labvertices.inc"
#endif
