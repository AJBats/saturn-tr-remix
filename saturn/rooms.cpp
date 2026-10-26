// Room rendering on the slave SH-2. renderRoom (on the master) queues each
// visible room with its matrix and portal viewport; the slave projects the
// room's vertices and adds its faces to its own face list, which flush merges
// with the master's. Once every room is queued, the SCU DSP projects rooms
// from the back of the queue (project.dsp) while the slave takes them from
// the front. The master carries on with meshes meanwhile, handing the slave
// rooms' static meshes as long as it keeps up.

#include <stddef.h>

#include "render.h"
#include "detail.h"
#include "dsp.h"
#include "slave.h"
#include "system.h"
#include "project_dsp.h"

#define MAX_ROOM_JOBS   64
#define MAX_MESH_JOBS   128
#define MESH_STEP       32      // mesh vertices the slave's share moves by a frame

// The bits of a room quad's flags that are the packer's; faces.s keeps the
// quad's turn and mip level in the rest (REUSE).
#define ROOM_QUAD_FLAGS 0x00FFFFFF

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
static volatile uint32 gRoomsComplete;  // every room of the frame queued

// Meshes, queued the same way. The slave takes as many vertices of them a
// frame as its share, which grows while it has finished every job by the
// time the master is done drawing and shrinks when it hasn't.
static MeshJob gMeshJobs[MAX_MESH_JOBS];
static uint32 gMeshCount;
static int32 gMeshShare;
static int32 gMeshTaken;
static bool gMeshHeld;      // a mesh was drawn here for want of share
static volatile uint32 gMeshesQueued;
static volatile uint32 gJobsDone;   // rooms and meshes the slave has drawn
static uint8 gMeshSkipped[MAX_MESH_JOBS];   // jobs the slave had no room for, for the master
static uint32 gMeshSkips;

// Slave side.
// Each room's vertices have their place in gVertices from the bottom up, in
// whole batches of the DSP's; what split faces and meshes add comes
// from the top down. They are projected in chunks: this CPU takes chunks
// from the front of the queue, the DSP runs of them from the back, and a
// room's faces follow once all its chunks are done.
#define CHUNK           64      // vertices
#define DSP_RUN         4       // chunks the DSP takes at a time, at most

struct RoomWork
{
    int32 base;         // the room's first vertex in gVertices, -1 if it didn't fit
    int16 lo, hi;       // the chunks not yet taken
    bool dsp;           // the DSP projected some
    bool faced;
};
static RoomWork gWork[MAX_ROOM_JOBS];
static int32 gPlaced;               // jobs given their place
static int32 gBottom, gTop;         // the free vertices between
static int32 gFaced;                // rooms done
static int32 gDspRoom;              // the room whose chunks the DSP projects, -1 none

static Face gSlaveFaces[MAX_FACES];
static Face* gSlaveOT[OT_SIZE];
static Face* gSlaveMeshOT[OT_SIZE];
static RoomStats gSlaveStats;

static FaceList gFaceList;
static FaceList gMeshList;      // meshes' faces: the same pool, an ordering table of their own
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
static_assert(FRAME_WIDTH == 352 && FRAME_HEIGHT == 224 && VDP1_COORD_MAX == 1023 &&
              FOG_MIN == 12288 && FOG_SHIFT == 0 && PROJ_SHIFT == 4 && CLIP_PLANE == 32 &&
              MAX_CAUSTICS == 32 && MAX_RAND_TABLE == 32, "constants repeated in xform.s");

extern "C" void transformRoomAsm(const RoomVertex* src, int32 count, Vertex* dst, RoomXform* xf);

static RoomXform gXform;
static int16 gRoomCaustics[MAX_RAND_TABLE];
static RectMinMax gViewport;
static const RoomVertex* gRoomVertices;

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
    if (gTop - 5 < gBottom || gFaceList.next + 4 > gFaceList.end)
        return false;

    int32 slot = detailSlot(tex->reserved - 1, tex->size, DETAIL_QUADS);
    if (slot < 0)
        return false;

    const RoomVertex* r0 = gRoomVertices + poly->indices[0];
    const RoomVertex* r1 = gRoomVertices + poly->indices[1];
    const RoomVertex* r2 = gRoomVertices + poly->indices[2];
    const RoomVertex* r3 = gRoomVertices + poly->indices[3];

    // edge midpoints and the centre, in room space
    gTop -= 5;
    Vertex* m01 = gVertices + gTop;
    Vertex* m12 = m01 + 1;
    Vertex* m23 = m01 + 2;
    Vertex* m30 = m01 + 3;
    Vertex* mc  = m01 + 4;

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

    uint32 flags = (poly->flags & ROOM_QUAD_FLAGS) | gRoomFaceFlags | FACE_DETAIL | (slot << FACE_DETAIL_SLOT_SHIFT);
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

        uint32 flags = (polys->flags & ROOM_QUAD_FLAGS) | gRoomFaceFlags;
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
    int32 parity;           // the frame's: a quad decides its turn and mip level on frames
                            // of its own parity (faces.s REUSE); -1: every frame
    int32 cached;           // faces.s's
};

static_assert(offsetof(RoomFaces, textures) == 16 && offsetof(RoomFaces, end) == 28 &&
              offsetof(RoomFaces, frame) == 40 && offsetof(RoomFaces, miss) == 48 &&
              offsetof(RoomFaces, turn) == 52 && offsetof(RoomFaces, lod) == 60 &&
              offsetof(RoomFaces, parity) == 64 && offsetof(RoomFaces, cached) == 68 &&
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

// project.dsp's divide table: divTable << 4 as longwords, by z >> 12.
static int32 gDspDivide[DIV_TABLE_SIZE];

// Starts project.dsp on batches of 16 vertices from src to dst.
static void dspProject(const RoomMatrix &m, const RectMinMax &vp, const RoomVertex* src, Vertex* dst, int32 batches)
{
    const int32_t md0[36] = {
        m.m00 * 4, m.m01 * 4, m.m02 * 4, m.tx, m.m10 * 4, m.m11 * 4, m.m12 * 4, m.ty,
        m.m20 * 4, m.m21 * 4, m.m22 * 4, m.tz,
        VIEW_MIN_F >> (FIXED_SHIFT - 8), VIEW_MAX_F >> (FIXED_SHIFT - 8), 256, 16,
        int32_t(dspAddress(gDspDivide)), 0x7FFF00, -0x7FFF00,
        1023 - FRAME_WIDTH / 2, -1024 - FRAME_WIDTH / 2, FRAME_WIDTH / 2,
        1023 - FRAME_HEIGHT / 2, -1024 - FRAME_HEIGHT / 2, FRAME_HEIGHT / 2,
        FRAME_WIDTH, FRAME_HEIGHT, vp.x0, vp.x1, vp.y0, vp.y1,
        FOG_MIN, 8191, 65536, 0xFFFF, 0xFF00
    };
    const uint32_t md3[5] = { uint32_t(batches), 1, dspAddress(src), dspAddress(dst), 0xFF };
    dspWrite(0, 0, (const uint32_t*)md0, 36);
    dspWrite(3, 0, md3, 5);
    dspStart(0);
}

// project.dsp does what transformRoomAsm does for rooms not under water, and
// the DSP's DMA reaches high work RAM, not low.
static bool dspRoom(const RoomJob &job)
{
    return gBenchMode != BENCH_NO_DSP && !ROOM_FLAG_WATER(job.room->info->flags) &&
           (uint32(job.room->data.vertices) & 0x0FF00000) == 0x06000000;
}

// Gives each newly queued job its place in gVertices.
static void placeRooms(int32 queued)
{
    for (; gPlaced < queued; gPlaced++)
    {
        RoomWork &w = gWork[gPlaced];
        int32 count = uncached(gJobs + gPlaced)->room->info->verticesCount;
        int32 size = (count + 15) & ~15;
        w.lo = 0;
        w.hi = (count + CHUNK - 1) / CHUNK;
        w.dsp = false;
        w.faced = false;
        w.base = gBottom;
        if (gBottom + size > gTop)
        {
            w.base = -1;
            w.lo = w.hi;
            w.faced = true;
            gFaced++;
            continue;
        }
        gBottom += size;
    }
}

// Notes the DSP's run done; once every room is queued, gives the idle DSP
// a run of chunks from the back of the highest room it can project, as long
// as one is left for this CPU.
static void dspFeed()
{
    if (gDspRoom >= 0)
    {
        if (dspBusy())
            return;
        gDspRoom = -1;
    }
    if (!*uncached(&gRoomsComplete))
        return;

    int32 left = 0;
    for (int32 i = 0; i < gPlaced; i++) {
        left += gWork[i].hi - gWork[i].lo;
    }
    for (int32 i = gPlaced - 1; i >= 0; i--)
    {
        RoomWork &w = gWork[i];
        const RoomJob &job = *uncached(gJobs + i);
        if (w.lo == w.hi || !dspRoom(job))
            continue;
        int32 run = X_MIN(X_MIN(DSP_RUN, w.hi - w.lo), left - 1);
        if (run <= 0)
            return;
        int32 first = (w.hi - run) * CHUNK;
        int32 count = X_MIN(int32(job.room->info->verticesCount), w.hi * CHUNK) - first;
        dspProject(job.matrix, job.viewport, job.room->data.vertices + first, gVertices + w.base + first,
                   (count + 15) >> 4);
        w.hi -= run;
        w.dsp = true;
        gDspRoom = i;
        gSlaveStats.dspVertices += count;
        return;
    }
}

// Sets up what projecting and adding the faces of job need.
static void roomBegin(const RoomJob &job)
{
    const RoomInfo* info = job.room->info;
    bool water = ROOM_FLAG_WATER(info->flags);

    gMatrix = job.matrix;
    gViewport = job.viewport;
    gRoomVertices = job.room->data.vertices;
    gRoomFaceFlags = water ? FACE_WATER : 0;
    gWibblePhase = job.causticsFrame;
    gTurn = gTurnLines && gBenchMode != BENCH_FIXED_LINES;

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
}

// Projects the front chunk of room index here.
static void roomChunk(int32 index, const RoomJob &job)
{
    uint32 t0 = timerTicks();
    RoomWork &w = gWork[index];
    int32 first = w.lo * CHUNK;
    int32 count = X_MIN(int32(job.room->info->verticesCount) - first, CHUNK);
    w.lo++;
    roomBegin(job);
    gXform.count = first + count;   // count - vertices left: the index in the room
    transformRoomAsm(job.room->data.vertices + first, count, gVertices + w.base + first, &gXform);
    gSlaveStats.xformTicks += (timerTicks() - t0) & 0xFFFF;
}

static void roomFaces(int32 index, const RoomJob &job)
{
    uint32 t0 = timerTicks();
    RoomWork &w = gWork[index];
    const RoomInfo* info = job.room->info;
    const RoomData &data = job.room->data;
    Vertex* roomBase = gVertices + w.base;

    roomBegin(job);
    if (w.dsp) {
        cacheDrop(roomBase, info->verticesCount * sizeof(Vertex));  // this CPU's copies from before the DSP wrote
    }
    w.faced = true;
    gFaced++;
    gSlaveStats.vertices += info->verticesCount;

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
        // A quad decides its turned lines and mip level on every other frame
        // and keeps them in between: a frame behind as the view moves, but the
        // slave's quads cost a Saturn 3 points less at the bench spot.
        uint32 base = w.base;
        RoomFaces rf = { roomBase, base * 0x10001, &gFaceList, gRoomFaceFlags, level.textures, 0, 0,
                         gFaceList.end, gDetailKeys[0], gDetailSlots, gDetailFrame, faceAddRoomQuads, detailSlotMiss,
                         gTurn, 0, detailMips() && gBenchMode != BENCH_NO_LOD ? gTextureLod : 0,
                         int32(gDetailFrame & 1), -1 };
        if (info->quadsCount > 0) {
            faceAddRoomQuadsAsm(data.quads, info->quadsCount, &rf);
        }
        rf.parity = -1;     // triangles decide every frame
        rf.cached = -1;
        if (info->trianglesCount > 0) {
            faceAddRoomTrianglesAsm(data.triangles, info->trianglesCount, &rf);
        }
        gSlaveStats.detailFaces += rf.detailFaces;
        gSlaveStats.detailMisses += rf.detailMisses;
    }
    gSlaveStats.faceTicks += (timerTicks() - t0) & 0xFFFF;
}

static void renderMeshJob(int32 index, const MeshJob &job)
{
    int32 count = job.vCount;
    if (gTop - count < gBottom || gFaceList.next + job.fCount > gFaceList.end)
    {
        gMeshSkipped[gMeshSkips++] = index;
        return;
    }

    uint32 t0 = timerTicks();
    gTop -= count;
    gMeshList.next = gFaceList.next;
    meshJobDraw(job, gVertices + gTop, gMeshList);
    gFaceList.next = gMeshList.next;
    gSlaveStats.vertices += count;
    gSlaveStats.meshTicks += (timerTicks() - t0) & 0xFFFF;
}

// Meshes' faces after the rooms' in each bucket, as the master draws what
// stands in rooms.
static void mergeMeshes()
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

// The slave's task for a frame: draw rooms and meshes as they are
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
    gSlaveStats = RoomStats();
    gPlaced = 0;
    gBottom = MASTER_VERTICES;
    gTop = MAX_VERTICES;
    gFaced = 0;
    gMeshSkips = 0;
    gDspRoom = -1;

    uint32 meshesDone = 0;
    uint32 idleStart = timerTicks();
    while (1)
    {
        uint32 queued = *uncached(&gJobsQueued);
        uint32 meshes = *uncached(&gMeshesQueued);
        asm volatile("" : : : "memory"); // read the jobs only after their count
        placeRooms(queued);
        dspFeed();

        // a room whose chunks are all done, else the front chunk
        int32 ready = -1, front = -1;
        for (int32 i = 0; i < gPlaced && ready < 0; i++)
        {
            const RoomWork &w = gWork[i];
            if (!w.faced && w.lo == w.hi && gDspRoom != i) {
                ready = i;
            } else if (front < 0 && w.lo < w.hi) {
                front = i;
            }
        }

        if (ready < 0 && front < 0 && meshesDone == meshes)
        {
            if (gDspRoom >= 0 || !*uncached(&gJobsClosed))
            {
                busPause();
                continue;
            }
            // the last jobs may have been queued just before the queue closed
            if (queued == *uncached(&gJobsQueued) && meshesDone == *uncached(&gMeshesQueued))
                break;
            continue;
        }

        uint32 t = timerTicks();
        gSlaveStats.idleTicks += (t - idleStart) & 0xFFFF;
        if (ready >= 0)
        {
            roomFaces(ready, *uncached(gJobs + ready));
        }
        else if (front >= 0)
        {
            roomChunk(front, *uncached(gJobs + front));
        }
        else
        {
            MeshJob job = *uncached(gMeshJobs + meshesDone);
            renderMeshJob(meshesDone, job);
            meshesDone++;
        }
        *uncached(&gJobsDone) = gFaced + meshesDone;
        idleStart = timerTicks();
    }

    if (meshesDone) {
        mergeMeshes();
    }
}

void roomsDspTest(uint32 &dspTicks, uint32 &cpuTicks, int32 &errors)
{
    // renderInit's extension of the core's table, which this runs before
    for (int32 i = 1024; i < DIV_TABLE_SIZE; i++) {
        divTable[i] = (1 << 16) / i;
    }
    for (int32 i = 0; i < DIV_TABLE_SIZE; i++) {
        gDspDivide[i] = divTable[i] << 4;
    }

    // vertices and both projections in gVertices, unused before a level
    const int32 runs = 8, batches = 4, count = batches * 16;
    RoomVertex* src = (RoomVertex*)(gVertices + MAX_VERTICES / 2);
    Vertex* ref = gVertices;
    Vertex* out = gVertices + runs * count;

    uint32 seed = 0x2468ACE1;
    for (int32 i = 0; i < runs * count; i++)
    {
        seed = seed * 1664525 + 1013904223;
        memcpy(src + i, &seed, sizeof(RoomVertex));
    }

    // views that put vertices behind the camera, past the far plane, off the
    // frame and beyond the view's 16 bits
    static const int32 moves[runs][3] = {
        { 0, 0, 2000000 }, { -3000000, 1000000, 4000000 }, { 20000000, -20000000, 3000000 },
        { 0, 0, -1000000 }, { 500000, -500000, 6000000 }, { -9000000, 9000000, 10000000 },
        { 1000000, 2000000, 100000 }, { 0, 0, 800000 }
    };
    RoomMatrix m = { 14189, 0, -8192, 4096, 15851, 7094, 7094, -4096, 12288, 0, 0, 0 };
    RectMinMax vp(30, 20, 320, 200);

    RoomXform &xf = gXform;
    const int16* rows = &m.m00;
    xf.x0 = vp.x0;
    xf.y0 = vp.y0;
    xf.x1 = vp.x1;
    xf.y1 = vp.y1;
    xf.divTable = divTable;
    xf.zMin = VIEW_MIN_F >> (FIXED_SHIFT - 8);
    xf.zMax = VIEW_MAX_F >> (FIXED_SHIFT - 8);
    xf.wibble = NULL;
    xf.caustics = gRoomCaustics;
    xf.count = count;
    for (int32 i = 0; i < 9; i++) {
        xf.rows[i] = rows[i];
    }

    dspLoad(project_dsp, PROJECT_DSP_COUNT);
    dspTicks = cpuTicks = 0;
    for (int32 r = 0; r < runs; r++)
    {
        m.tx = xf.tx = moves[r][0];
        m.ty = xf.ty = moves[r][1];
        m.tz = xf.tz = moves[r][2];

        uint32 t0 = timerTicks();
        transformRoomAsm(src + r * count, count, ref + r * count, &xf);
        uint32 t1 = timerTicks();
        dspProject(m, vp, src + r * count, out + r * count, batches);
        while (dspBusy());
        uint32 t2 = timerTicks();
        cpuTicks += (t1 - t0) & 0xFFFF;
        dspTicks += (t2 - t1) & 0xFFFF;
    }
    cachePurge();   // the DSP wrote behind the cache

    errors = 0;
    for (int32 i = 0; i < runs * count; i++) {
        errors += memcmp(ref + i, out + i, sizeof(Vertex)) != 0;
    }
}

void roomsInit()
{
    for (int32 i = 0; i < DIV_TABLE_SIZE; i++) {
        gDspDivide[i] = divTable[i] << 4;
    }
    dspLoad(project_dsp, PROJECT_DSP_COUNT);
}

void roomsAllQueued()
{
    if (gJobCount) {
        *uncached(&gRoomsComplete) = 1;
    }
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

bool roomsQueueMesh(const Mesh* mesh)
{
    if (gJobCount == 0 || gMeshCount == MAX_MESH_JOBS)
        return false;
    if (gMeshTaken + mesh->vCount > gMeshShare)
    {
        gMeshHeld = true;
        return false;
    }

    meshJobSetup(gMeshJobs[gMeshCount], mesh);
    gMeshTaken += mesh->vCount;
    gMeshCount++;
    *uncached(&gMeshesQueued) = gMeshCount;
    return true;
}

bool roomsQueueShadow(int32 x, int32 z, int32 sx, int32 sz)
{
    if (gJobCount == 0 || gMeshCount == MAX_MESH_JOBS)
        return false;

    meshJobShadow(gMeshJobs[gMeshCount], x, z, sx, sz);
    gMeshCount++;
    *uncached(&gMeshesQueued) = gMeshCount;
    return true;
}

Face** roomsFinish(RoomStats &stats, const uint16* &groups)
{
    if (gJobCount == 0)
        return NULL;

    bool idle = *uncached(&gJobsDone) == gJobCount + gMeshCount;
    *uncached(&gJobsClosed) = 1;
    slaveWait();
    cachePurge();

    for (uint32 i = 0; i < gMeshSkips; i++) {
        meshJobRedraw(gMeshJobs[gMeshSkipped[i]]);
    }

    // it grows only while it holds meshes back, not to slow its shrinking when
    // the view gets busier
    if (!idle) {
        gMeshShare = X_MAX(gMeshShare - MESH_STEP, 0);
    } else if (gMeshHeld) {
        gMeshShare += MESH_STEP;
    }
    gMeshTaken = 0;
    gMeshHeld = false;

    gJobCount = 0;
    gMeshCount = 0;
    *uncached(&gJobsQueued) = 0;
    *uncached(&gJobsClosed) = 0;
    *uncached(&gRoomsComplete) = 0;
    *uncached(&gMeshesQueued) = 0;
    *uncached(&gJobsDone) = 0;

    stats = gSlaveStats;
    groups = gFaceList.groups;
    return gSlaveOT;
}
