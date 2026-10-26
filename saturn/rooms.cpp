// Room rendering on the slave SH-2. renderRoom (on the master) queues each
// visible room with its matrix and portal viewport; the slave transforms the
// room's vertices, the SCU DSP applying the matrix a chunk ahead of it, and
// adds the room's faces to its own face list, which flush merges with the
// master's. The master carries on with meshes meanwhile, handing the slave
// rooms' static meshes as long as it keeps up.

#include <stddef.h>

#include "render.h"
#include "detail.h"
#include "dsp.h"
#include "slave.h"
#include "system.h"
#include "xform_dsp.h"

#define MAX_ROOM_JOBS   64
#define MAX_STATIC_JOBS 64
#define STATIC_STEP     32      // static mesh vertices the slave's share moves by a frame
#define DSP_CHUNK       64      // vertices the DSP transforms at a time: 4 of its batches

extern Level level;

// The room matrix for 16x16 multiplies: the rotation fits in 16 bits, and
// room vertex units (256 world units) times 4 keep the products in 32 bits,
// so (m * v + t) >> 8 is the view position in world units.
struct RoomMatrix
{
    int16 m00, m01, m02, m10, m11, m12, m20, m21, m22;
    int32 tx, ty, tz;
};

struct RoomJob
{
    const Room* room;
    RoomMatrix matrix;
    RectMinMax viewport;
    int32 causticsFrame;
};

// Master side. Jobs are written cached (memory is write-through) and read by
// the slave uncached; the counters are read and written uncached.
static RoomJob gJobs[MAX_ROOM_JOBS];
static uint32 gJobCount;
static volatile uint32 gJobsQueued;
static volatile uint32 gJobsClosed;

// Rooms' static meshes, queued the same way. The slave takes as many vertices
// of them a frame as its share, which grows while it has finished every job
// by the time the master is done drawing and shrinks when it hasn't.
static MeshJob gStaticJobs[MAX_STATIC_JOBS];
static uint32 gStaticCount;
static int32 gStaticShare;
static int32 gStaticTaken;
static bool gStaticHeld;    // a mesh was drawn here for want of share
static volatile uint32 gStaticsQueued;
static volatile uint32 gJobsDone;   // rooms and static meshes the slave has drawn

// Slave side.
// The DSP writes one chunk of a room's vertices in view space to one buffer
// while this CPU projects the last from the other.
static int32 gDspOut[2][DSP_CHUNK * 4] __attribute__((aligned(16)));
static int32 gDspJob;       // the job whose chunk the DSP has, -1 none
static int32 gDspStart;     // ... the chunk's first vertex
static int32 gDspBuffer;    // ... and its buffer
static int32 gDspMatrix;    // the job whose matrix the DSP has, -1 none

static Face gSlaveFaces[MAX_FACES];
static Face* gSlaveOT[OT_SIZE];
static Face* gSlaveMeshOT[OT_SIZE];
static RoomStats gSlaveStats;

static FaceList gFaceList;
static FaceList gMeshList;      // static meshes' faces: the same pool, an ordering table of their own
static bool gTurn;              // faces may be turned (gTurnLines)
static uint32 gRoomFaceFlags;   // FACE_WATER for water rooms
static int32 gWibblePhase;      // drifts the ripple of water rooms
static RoomMatrix gMatrix;

// What xform.s needs to transform a room's vertices.
struct RoomXform
{
    int16 v[3];             // scratch: the vertex being transformed, times 4
    int16 rows[9];          // gMatrix's rotation
    int32 tx, ty, tz;       // gMatrix's translation
    int32 x0, y0, x1, y1;   // viewport
    const uint16* divTable;
    int32 zMin, zMax;       // near and far planes << 8
    const int8* wibble;     // gWibble for water rooms, else NULL
    int32 phase;            // ... its phase
    const int16* caustics;  // shade offsets by vertex index, for water rooms
    int32 count;            // vertices
};

static_assert(offsetof(RoomXform, rows) == 6 && offsetof(RoomXform, tx) == 24 &&
              offsetof(RoomXform, x0) == 36 && offsetof(RoomXform, divTable) == 52 &&
              offsetof(RoomXform, zMin) == 56 && offsetof(RoomXform, wibble) == 64 &&
              offsetof(RoomXform, caustics) == 72 && offsetof(RoomXform, count) == 76,
              "RoomXform layout is shared with xform.s");
static_assert(sizeof(Vertex) == 8 && offsetof(Vertex, z) == 4 && offsetof(Vertex, clip) == 7,
              "Vertex layout is shared with xform.s");
static_assert(sizeof(RoomVertex) == 4, "xform.s reads RoomVertex as x, y, z, g bytes");
static_assert(FRAME_WIDTH == 352 && FRAME_HEIGHT == 240 && VDP1_COORD_MAX == 1023 &&
              FOG_MIN == 12288 && FOG_SHIFT == 0 && PROJ_SHIFT == 4 && CLIP_PLANE == 32 &&
              MAX_CAUSTICS == 32 && MAX_RAND_TABLE == 32, "constants repeated in xform.s");

extern "C" void transformRoomAsm(const RoomVertex* src, int32 count, Vertex* dst, RoomXform* xf);
extern "C" void transformRoomDspAsm(const int32* src, int32 count, Vertex* dst, RoomXform* xf);

static RoomXform gXform;
static int16 gRoomCaustics[MAX_RAND_TABLE];
static RectMinMax gViewport;
static const RoomVertex* gRoomVertices;
static Vertex* gNextVertex; // the room's vertices, then those its split faces add

// Water rooms ripple on screen, as in the original (0x06042D34): each
// projected coordinate moves by up to 2 pixels along a sine wave that runs
// across the screen and drifts with time.
static const int8 gWibble[32] = {
    0, 0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 0,
    0, 0, -1, -1, -1, -2, -2, -2, -2, -2, -2, -2, -1, -1, -1, 0
};

X_INLINE void wibble(Vertex* res, int32 phase)
{
    if (res->clip & CLIP_FRAME)
        return;
    int32 x = res->x;
    int32 y = res->y;
    res->x = x + gWibble[(phase + x) & 31];
    res->y = y + gWibble[(phase + y) & 31];
}

// Projects a point in room space (vertex units * 4); sets everything but the
// shade.
X_INLINE void projectRoomPoint(int16 vx, int16 vy, int16 vz, Vertex* res)
{
    const RoomMatrix &m = gMatrix;
    const int16* rows = &m.m00;
    const int16 v[3] = { vx, vy, vz };

    int32 z = dot3(v, rows + 6, m.tz);
    uint32 clip = 0;

    if (z <= (VIEW_MIN_F >> (FIXED_SHIFT - 8))) {
        clip = CLIP_PLANE;
        z = VIEW_MIN_F >> (FIXED_SHIFT - 8);
    }

    if (z >= (VIEW_MAX_F >> (FIXED_SHIFT - 8))) {
        clip = CLIP_PLANE;
        z = VIEW_MAX_F >> (FIXED_SHIFT - 8);
    }

    int32 x = dot3(v, rows, m.tx);
    int32 y = dot3(v, rows + 3, m.ty);

    x = asr16<8>(X_CLAMP(x, -ASR16_MAX(8), ASR16_MAX(8)));
    y = asr16<8>(X_CLAMP(y, -ASR16_MAX(8), ASR16_MAX(8)));

    projectView(x, y, uint32(z) >> 8, clip, gViewport, res);

    if (gRoomFaceFlags & FACE_WATER) {
        wibble(res, gWibblePhase);
    }
}

// Adds one quarter of a split room quad; corners clockwise from the one at
// the texture's top left.
static void faceAddDetailQuad(uint32 flags, const Vertex* v0, const Vertex* v1, const Vertex* v2, const Vertex* v3)
{
    uint32 c0 = v0->clip;
    uint32 c1 = v1->clip;
    uint32 c2 = v2->clip;
    uint32 c3 = v3->clip;

    if (cullFace(c0 & c1 & c2 & c3, c0 | c1 | c2 | c3))
        return;

    int32 depth = X_MAX(v0->z, X_MAX(v1->z, X_MAX(v2->z, v3->z))) >> OT_SHIFT;

    Face* f = faceAdd(gFaceList, depth);
    f->flags = flags;
    f->indices[0] = v0 - gVertices;
    f->indices[1] = v1 - gVertices;
    f->indices[2] = v2 - gVertices;
    f->indices[3] = v3 - gVertices;
}

// Splits a room quad near the camera into four, each drawn with one quadrant
// of its texture. The new vertices are projected from room space, so the
// split also reduces the distortion of affine texture mapping. False when the
// texture can't be cached this frame.
static bool faceAddRoomQuadDetail(const RoomQuad* poly, const Texture* tex,
                                  const Vertex* v0, const Vertex* v1, const Vertex* v2, const Vertex* v3)
{
    if (gNextVertex + 5 > gVertices + MAX_VERTICES || gFaceList.next + 4 > gFaceList.end)
        return false;

    int32 slot = detailSlot(tex->reserved - 1, tex->size, DETAIL_QUADS);
    if (slot < 0)
        return false;

    const RoomVertex* r0 = gRoomVertices + poly->indices[0];
    const RoomVertex* r1 = gRoomVertices + poly->indices[1];
    const RoomVertex* r2 = gRoomVertices + poly->indices[2];
    const RoomVertex* r3 = gRoomVertices + poly->indices[3];

    // edge midpoints and the centre, in room space
    Vertex* m01 = gNextVertex;
    Vertex* m12 = m01 + 1;
    Vertex* m23 = m01 + 2;
    Vertex* m30 = m01 + 3;
    Vertex* mc  = m01 + 4;
    gNextVertex += 5;

    projectRoomPoint((r0->x + r1->x) << 1, (r0->y + r1->y) << 1, (r0->z + r1->z) << 1, m01);
    projectRoomPoint((r1->x + r2->x) << 1, (r1->y + r2->y) << 1, (r1->z + r2->z) << 1, m12);
    projectRoomPoint((r2->x + r3->x) << 1, (r2->y + r3->y) << 1, (r2->z + r3->z) << 1, m23);
    projectRoomPoint((r3->x + r0->x) << 1, (r3->y + r0->y) << 1, (r3->z + r0->z) << 1, m30);
    projectRoomPoint(r0->x + r1->x + r2->x + r3->x,
                     r0->y + r1->y + r2->y + r3->y,
                     r0->z + r1->z + r2->z + r3->z, mc);

    m01->g = (v0->g + v1->g) >> 1;
    m12->g = (v1->g + v2->g) >> 1;
    m23->g = (v2->g + v3->g) >> 1;
    m30->g = (v3->g + v0->g) >> 1;
    mc->g = (v0->g + v1->g + v2->g + v3->g) >> 2;

    uint32 flags = poly->flags | gRoomFaceFlags | FACE_DETAIL | (slot << FACE_DETAIL_SLOT_SHIFT);
    faceAddDetailQuad(flags | (0 << FACE_DETAIL_QUAD_SHIFT), v0, m01, mc, m30);
    faceAddDetailQuad(flags | (1 << FACE_DETAIL_QUAD_SHIFT), m01, v1, m12, mc);
    faceAddDetailQuad(flags | (2 << FACE_DETAIL_QUAD_SHIFT), mc, m12, v2, m23);
    faceAddDetailQuad(flags | (3 << FACE_DETAIL_QUAD_SHIFT), m30, mc, m23, v3);

    gSlaveStats.detailFaces++;
    return true;
}

// A face drawn with its texture whole, if the cache has room for it, and
// turned if that takes fewer lines; in its mean colour if not.
static uint32 wholeTexture(uint32 flags, const Vertex* v0, const Vertex* v1, const Vertex* v2, const Vertex* v3)
{
    const Texture* tex = level.textures + (flags & FACE_TEXTURE);
    if (!tex->reserved)
        return flags;

    bool turned = gTurn && faceTurns(v0, v1, v2, v3);
    int32 slot = detailSlot(tex->reserved - 1, tex->size, turned ? DETAIL_TURNED : DETAIL_WHOLE);
    if (slot < 0)
    {
        gSlaveStats.detailMisses++;
        return flags;
    }

    gSlaveStats.detailFaces++;
    flags |= FACE_DETAIL | FACE_DETAIL_WHOLE | (slot << FACE_DETAIL_SLOT_SHIFT);
    return turned ? flags | FACE_DETAIL_TURNED : flags;
}

static void faceAddRoomQuads(const Vertex* v, const RoomQuad* polys, int32 count)
{
    for (int32 i = 0; i < count; i++, polys++)
    {
        if (gFaceList.next == gFaceList.end)
            return;

        uint32 flags = polys->flags | gRoomFaceFlags;
        const Vertex* v0 = v + polys->indices[0];
        const Vertex* v1 = v + polys->indices[1];
        const Vertex* v2 = v + polys->indices[2];
        const Vertex* v3 = v + polys->indices[3];

        uint32 c0 = v0->clip;
        uint32 c1 = v1->clip;
        uint32 c2 = v2->clip;
        uint32 c3 = v3->clip;

        // all four corners beyond one frame edge
        if (c0 & c1 & c2 & c3 & CLIP_DISCARD)
            continue;

        if (checkBackface(v0, v1, v2))
            continue;

        int32 zMin = X_MIN(v0->z, X_MIN(v1->z, X_MIN(v2->z, v3->z)));
        int32 zMax = X_MAX(v0->z, X_MAX(v1->z, X_MAX(v2->z, v3->z)));
        const Texture* tex = level.textures + (flags & FACE_TEXTURE);

        if (faceFlat(flags, zMin, tex))
        {
            if (cullFace(c0 & c1 & c2 & c3, c0 | c1 | c2 | c3))
                continue;
            flags = flatFlags(flags);
            gSlaveStats.flatFaces++;
        }
        else
        {
            // crossing the near plane: split, so the quarters in front of it
            // are drawn rather than the whole face culled
            if (zMin < DETAIL_DIST && ((c0 | c1 | c2 | c3) & CLIP_PLANE) && tex->reserved &&
                faceAddRoomQuadDetail(polys, tex, v0, v1, v2, v3))
                continue;

            if (cullFace(c0 & c1 & c2 & c3, c0 | c1 | c2 | c3))
                continue;
            flags = wholeTexture(flags, v0, v1, v2, v3);
        }

        Face* f = faceAdd(gFaceList, zMax >> OT_SHIFT);
        f->flags = flags;
        f->indices[0] = v0 - gVertices;
        f->indices[1] = v1 - gVertices;
        f->indices[2] = v2 - gVertices;
        f->indices[3] = v3 - gVertices;
    }
}

static void faceAddRoomTriangles(const Vertex* v, const RoomTriangle* polys, int32 count)
{
    for (int32 i = 0; i < count; i++, polys++)
    {
        if (gFaceList.next == gFaceList.end)
            return;

        uint32 flags = polys->flags | gRoomFaceFlags;
        const Vertex* v0 = v + polys->indices[0];
        const Vertex* v1 = v + polys->indices[1];
        const Vertex* v2 = v + polys->indices[2];

        uint32 c0 = v0->clip;
        uint32 c1 = v1->clip;
        uint32 c2 = v2->clip;

        if (cullFace(c0 & c1 & c2, c0 | c1 | c2))
            continue;


        if (checkBackface(v0, v1, v2))
            continue;

        flags |= FACE_TRIANGLE;

        int32 zMax = X_MAX(v0->z, X_MAX(v1->z, v2->z));
        if (faceFlat(flags, X_MIN(v0->z, X_MIN(v1->z, v2->z)), level.textures + (flags & FACE_TEXTURE))) {
            flags = flatFlags(flags);
            gSlaveStats.flatFaces++;
        } else {
            flags = wholeTexture(flags, v0, v1, v2, v2);
        }
        int32 depth = zMax >> OT_SHIFT;

        Face* f = faceAdd(gFaceList, depth);
        f->flags = flags;
        f->indices[0] = v0 - gVertices;
        f->indices[1] = v1 - gVertices;
        f->indices[2] = v2 - gVertices;
        f->indices[3] = v2 - gVertices;
    }
}

// What faces.s needs to add a room's faces.
struct RoomFaces
{
    const Vertex* vertices;
    uint32 base;            // the room's first vertex's index in gVertices, in both halves
    FaceList* list;
    uint32 flags;           // gRoomFaceFlags
    const Texture* textures;
    int32 detailFaces;      // counted up
    int32 detailMisses;
    Face* end;              // the list's
    const uint8* keys;      // gDetailKeys[0]
    DetailSlot* slots;
    uint32 frame;           // gDetailFrame
    void (*quad)(const Vertex* v, const RoomQuad* polys, int32 count);
    int32_t (*miss)(uint32_t key, uint32_t texSize, uint32_t variant);
    int32 turn;             // gTurn
    int32 across;           // faces.s's
    int32 lod;              // gTextureLod, 0 without mip levels
};

static_assert(offsetof(RoomFaces, textures) == 16 && offsetof(RoomFaces, end) == 28 &&
              offsetof(RoomFaces, frame) == 40 && offsetof(RoomFaces, miss) == 48 &&
              offsetof(RoomFaces, turn) == 52 && offsetof(RoomFaces, lod) == 60 &&
              sizeof(RoomQuad) == 12 && sizeof(RoomTriangle) == 12 && sizeof(DetailSlot) == 8 &&
              offsetof(DetailSlot, frame) == 4 && DETAIL_MAX_KEYS == 1088 && sizeof(Texture) == 16 &&
              offsetof(Texture, size) == 4 && offsetof(Texture, reserved) == 10 &&
              (FACE_DETAIL | FACE_DETAIL_WHOLE) == 0x28000000 && FACE_DETAIL_SLOT_SHIFT == 20 &&
              FACE_DETAIL_TURNED == (1 << 19) && DETAIL_WHOLE == 0 && DETAIL_TURNED == 2 &&
              FACE_DETAIL_MIP == (1 << 18) && FACE_DETAIL_MIP2 == (1 << 26) && DETAIL_MIP1 == 3 &&
              DETAIL_MIP1_TURNED == 4 && DETAIL_MIP2 == 5 && DETAIL_MIP2_TURNED == 6 &&
              DETAIL_FULL_SLOTS == 128 && DETAIL_MIP_SLOTS == 64 &&
              sizeof(gDetailKeys[0]) == DETAIL_MAX_KEYS, "layouts and constants shared with faces.s");

extern "C" void faceAddRoomQuadsAsm(const RoomQuad* polys, int32 count, RoomFaces* rf);
extern "C" void faceAddRoomTrianglesAsm(const RoomTriangle* polys, int32 count, RoomFaces* rf);

// The DSP's DMA reaches high work RAM, not low.
static bool dspRoom(const RoomJob &job)
{
    return gBenchMode != BENCH_NO_DSP && (uint32(job.room->data.vertices) & 0x0FF00000) == 0x06000000;
}

// Starts the DSP on the chunk of job's vertices from start, into buffer.
static void dspChunk(int32 index, const RoomJob &job, int32 start, int32 buffer)
{
    if (gDspMatrix != index)
    {
        const RoomMatrix &m = job.matrix;
        const int32_t md0[12] = { m.m20 * 4, m.m21 * 4, m.m22 * 4, m.tz, m.m00 * 4, m.m01 * 4, m.m02 * 4, m.tx,
                                  m.m10 * 4, m.m11 * 4, m.m12 * 4, m.ty };
        dspWrite(0, 0, (const uint32_t*)md0, 12);
        gDspMatrix = index;
    }

    int32 count = X_MIN(job.room->info->verticesCount - start, DSP_CHUNK);
    const uint32_t md3[5] = { uint32_t(count + 15) >> 4, 1, dspAddress(job.room->data.vertices + start),
                              dspAddress(gDspOut[buffer]), 0xFF };
    dspWrite(3, 0, md3, 5);
    dspStart(0);

    gDspJob = index;
    gDspStart = start;
    gDspBuffer = buffer;
}

// Transforms job's vertices to dst a chunk at a time: from the DSP's buffer
// when it has the chunk, else here, the DSP taking the next chunk meanwhile,
// of this room or the next queued.
static void transformRoom(int32 index, const RoomJob &job, Vertex* dst)
{
    const RoomVertex* src = job.room->data.vertices;
    int32 count = job.room->info->verticesCount;
    if (!dspRoom(job))
    {
        gXform.count = count;
        transformRoomAsm(src, count, dst, &gXform);
        return;
    }

    for (int32 start = 0; start < count; start += DSP_CHUNK)
    {
        bool ready = gDspJob == index && gDspStart == start;
        int32 buffer = gDspBuffer;
        while (dspBusy());
        gDspJob = -1;

        if (start + DSP_CHUNK < count) {
            dspChunk(index, job, start + DSP_CHUNK, buffer ^ 1);
        }
        else if (int32(*uncached(&gJobsQueued)) > index + 1)
        {
            asm volatile("" : : : "memory"); // read the job only after its count
            const RoomJob &next = *uncached(gJobs + index + 1);
            if (dspRoom(next)) {
                dspChunk(index + 1, next, 0, buffer ^ 1);
            }
        }

        int32 n = X_MIN(count - start, DSP_CHUNK);
        gXform.count = start + n;
        if (ready) {
            transformRoomDspAsm(gDspOut[buffer], n, dst + start, &gXform);
        } else {
            transformRoomAsm(src + start, n, dst + start, &gXform);
        }
    }
}

static void renderRoomJob(int32 index, const RoomJob &job)
{
    const RoomInfo* info = job.room->info;
    const RoomData &data = job.room->data;
    int32 vCount = info->verticesCount;

    if (gNextVertex + vCount > gVertices + MAX_VERTICES)
        return;

    bool water = ROOM_FLAG_WATER(info->flags);

    gMatrix = job.matrix;
    gViewport = job.viewport;
    gRoomVertices = data.vertices;
    gRoomFaceFlags = water ? FACE_WATER : 0;
    gWibblePhase = job.causticsFrame;
    gTurn = gTurnLines && gBenchMode != BENCH_FIXED_LINES;

    uint32 t0 = timerTicks();
    Vertex* roomBase = gNextVertex;

    RoomXform &xf = gXform;
    const int16* rows = &gMatrix.m00;
    for (int32 i = 0; i < 9; i++) {
        xf.rows[i] = rows[i];
    }
    xf.tx = gMatrix.tx;
    xf.ty = gMatrix.ty;
    xf.tz = gMatrix.tz;
    xf.x0 = gViewport.x0;
    xf.y0 = gViewport.y0;
    xf.x1 = gViewport.x1;
    xf.y1 = gViewport.y1;
    xf.divTable = divTable;
    xf.zMin = VIEW_MIN_F >> (FIXED_SHIFT - 8);
    xf.zMax = VIEW_MAX_F >> (FIXED_SHIFT - 8);
    xf.wibble = water ? gWibble : NULL;
    xf.phase = job.causticsFrame;
    xf.caustics = gRoomCaustics;

    // caustics shimmer under water: each vertex's shade moves along the
    // caustics wave from its own random start
    if (water)
    {
        for (int32 i = 0; i < MAX_RAND_TABLE; i++) {
            gRoomCaustics[i] = gCaustics[(gRandTable[i] + job.causticsFrame) & (MAX_CAUSTICS - 1)];
        }
    }

    transformRoom(index, job, roomBase);
    gNextVertex += vCount;
    gSlaveStats.vertices += vCount;

    uint32 t1 = timerTicks();
    if (gFlatDist)
    {
        if (info->quadsCount > 0) {
            faceAddRoomQuads(roomBase, data.quads, info->quadsCount);
        }
        if (info->trianglesCount > 0) {
            faceAddRoomTriangles(roomBase, data.triangles, info->trianglesCount);
        }
    }
    else
    {
        uint32 base = roomBase - gVertices;
        RoomFaces rf = { roomBase, base * 0x10001, &gFaceList, gRoomFaceFlags, level.textures, 0, 0,
                         gFaceList.end, gDetailKeys[0], gDetailSlots, gDetailFrame, faceAddRoomQuads, detailSlotMiss,
                         gTurn, 0, detailMips() && gBenchMode != BENCH_NO_LOD ? gTextureLod : 0 };
        if (info->quadsCount > 0) {
            faceAddRoomQuadsAsm(data.quads, info->quadsCount, &rf);
        }
        if (info->trianglesCount > 0) {
            faceAddRoomTrianglesAsm(data.triangles, info->trianglesCount, &rf);
        }
        gSlaveStats.detailFaces += rf.detailFaces;
        gSlaveStats.detailMisses += rf.detailMisses;
    }
    uint32 t2 = timerTicks();

    gSlaveStats.xformTicks += (t1 - t0) & 0xFFFF;
    gSlaveStats.faceTicks += (t2 - t1) & 0xFFFF;
}

static void renderStaticJob(const MeshJob &job)
{
    const Mesh* mesh = job.mesh;
    int32 count = mesh->vCount;
    if (gNextVertex + count > gVertices + MAX_VERTICES || gFaceList.next + mesh->rCount + mesh->tCount > gFaceList.end)
        return;

    uint32 t0 = timerTicks();
    gMeshList.next = gFaceList.next;
    meshJobDraw(job, gNextVertex, gMeshList);
    gFaceList.next = gMeshList.next;
    gNextVertex += count;
    gSlaveStats.vertices += count;
    gSlaveStats.meshTicks += (timerTicks() - t0) & 0xFFFF;
}

// Static meshes' faces after the rooms' in each bucket, as the master draws
// what stands in rooms.
static void mergeStatics()
{
    for (int32 i = 0; i < OT_SIZE; i++)
    {
        Face* faces = gSlaveMeshOT[i];
        if (!faces)
            continue;
        gSlaveMeshOT[i] = NULL;

        Face** link = gSlaveOT + i;
        while (*link) {
            link = &(*link)->next;
        }
        *link = faces;
    }
    for (int32 i = 0; i < OT_GROUPS; i++) {
        gFaceList.groups[i] += gMeshList.groups[i];
    }
}

// The slave's task for a frame: draw rooms and static meshes as they are
// queued until the master closes the queue.
static void roomBatch()
{
    gFaceList.next = gSlaveFaces;
    gFaceList.end = gSlaveFaces + MAX_FACES;
    gFaceList.ot = gSlaveOT;
    memset(gFaceList.groups, 0, sizeof(gFaceList.groups));
    gMeshList.end = gFaceList.end;
    gMeshList.ot = gSlaveMeshOT;
    memset(gMeshList.groups, 0, sizeof(gMeshList.groups));
    gNextVertex = gVertices + MASTER_VERTICES;
    gSlaveStats = RoomStats();
    gDspJob = -1;
    gDspMatrix = -1;

    uint32 done = 0;
    uint32 staticsDone = 0;
    uint32 idleStart = timerTicks();
    while (1)
    {
        uint32 queued = *uncached(&gJobsQueued);
        uint32 statics = *uncached(&gStaticsQueued);
        if (done == queued && staticsDone == statics)
        {
            if (!*uncached(&gJobsClosed))
            {
                busPause();
                continue;
            }
            // the last jobs may have been queued just before the queue closed
            if (done == *uncached(&gJobsQueued) && staticsDone == *uncached(&gStaticsQueued))
                break;
            continue;
        }

        asm volatile("" : : : "memory"); // read the job only after its count
        uint32 t = timerTicks();
        gSlaveStats.idleTicks += (t - idleStart) & 0xFFFF;
        if (done < queued)
        {
            renderRoomJob(done, *uncached(gJobs + done));
            done++;
        }
        else
        {
            MeshJob job = *uncached(gStaticJobs + staticsDone);
            renderStaticJob(job);
            staticsDone++;
        }
        *uncached(&gJobsDone) = done + staticsDone;
        idleStart = timerTicks();
    }

    if (staticsDone) {
        mergeStatics();
    }
}

void roomsInit()
{
    dspLoad(xform_dsp, XFORM_DSP_COUNT);
}

void roomsQueue(const Room* room)
{
    if (room->info->verticesCount <= 0 || gJobCount == MAX_ROOM_JOBS)
        return;

    const Matrix &m = matrixGet();
    RoomJob &job = gJobs[gJobCount];
    job.room = room;
    job.matrix.m00 = m.e00; job.matrix.m01 = m.e01; job.matrix.m02 = m.e02;
    job.matrix.m10 = m.e10; job.matrix.m11 = m.e11; job.matrix.m12 = m.e12;
    job.matrix.m20 = m.e20; job.matrix.m21 = m.e21; job.matrix.m22 = m.e22;
    job.matrix.tx = m.e03 >> (FIXED_SHIFT - 8);
    job.matrix.ty = m.e13 >> (FIXED_SHIFT - 8);
    job.matrix.tz = m.e23 >> (FIXED_SHIFT - 8);
    job.viewport = viewport;
    job.causticsFrame = gCausticsFrame;

    if (gJobCount == 0) {
        slaveRun(roomBatch);
    }

    gJobCount++;
    *uncached(&gJobsQueued) = gJobCount;
}

bool roomsQueueStatic(const Mesh* mesh)
{
    if (gJobCount == 0 || gStaticCount == MAX_STATIC_JOBS)
        return false;
    if (gStaticTaken + mesh->vCount > gStaticShare)
    {
        gStaticHeld = true;
        return false;
    }

    meshJobSetup(gStaticJobs[gStaticCount], mesh);
    gStaticTaken += mesh->vCount;
    gStaticCount++;
    *uncached(&gStaticsQueued) = gStaticCount;
    return true;
}

Face** roomsFinish(RoomStats &stats, const uint16* &groups)
{
    if (gJobCount == 0)
        return NULL;

    bool idle = *uncached(&gJobsDone) == gJobCount + gStaticCount;
    *uncached(&gJobsClosed) = 1;
    slaveWait();
    cachePurge();

    // it grows only while it holds meshes back, not to slow its shrinking when
    // the view gets busier
    if (!idle) {
        gStaticShare = X_MAX(gStaticShare - STATIC_STEP, 0);
    } else if (gStaticHeld) {
        gStaticShare += STATIC_STEP;
    }
    gStaticTaken = 0;
    gStaticHeld = false;

    gJobCount = 0;
    gStaticCount = 0;
    *uncached(&gJobsQueued) = 0;
    *uncached(&gJobsClosed) = 0;
    *uncached(&gStaticsQueued) = 0;
    *uncached(&gJobsDone) = 0;

    stats = gSlaveStats;
    groups = gFaceList.groups;
    return gSlaveOT;
}
