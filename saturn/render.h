// Saturn renderer internals shared by the master's renderer (render.cpp) and
// the room renderer the slave CPU runs (rooms.cpp).
#ifndef RENDER_H
#define RENDER_H

#include "common.h"

struct Vertex
{
    union
    {
        struct
        {
            int16 x;
            int16 y;
        };
        uint32 xy; // both, as a VDP1 command's vertex word (big-endian)
    };
    int16 z;
    uint8 g;
    uint8 clip;
};

enum FaceType {
    FACE_TYPE_SHADOW,
    FACE_TYPE_F,
    FACE_TYPE_FT,
    FACE_TYPE_FTA,
    FACE_TYPE_GT,
    FACE_TYPE_GTA,
    FACE_TYPE_SPRITE,
    FACE_TYPE_FILL_S,
    FACE_TYPE_LINE_H,
    FACE_TYPE_LINE_V,
    FACE_TYPE_AVG,      // a textured face drawn flat in its texture's mean colour (far LOD)
    FACE_TYPE_MAX
};

#define FACE_TRIANGLE   (1 << 31)
#define FACE_WATER      (1 << 30)   // underwater: red fades with distance (see waterGouraud)
#define FACE_DETAIL     (1 << 29)   // a quadrant of a cached room texture
#define FACE_PARALLELOGRAM  (1 << 28)   // mesh triangle drawn as a parallelogram, see faceAddMeshTriangles
#define FACE_V0_AT_D        (1 << 27)   // ... with its first vertex at corner D (else A)
#define FACE_DETAIL_WHOLE   (1 << 27)   // room face: the cached texture whole
#define FACE_DETAIL_SLOT_SHIFT  20      // 7 bits
#define FACE_DETAIL_QUAD_SHIFT  18      // a split face quarter: its quadrant
#define FACE_DETAIL_TURNED  (1 << 19)   // a whole face's: drawn as A D C B, the texture transposed
#define FACE_DETAIL_MIP     (1 << 18)   // a whole face's texture at half size, its slot (6 bits) in that pool
#define FACE_DETAIL_MIP2    (1 << 26)   // ... at a quarter
#define FACE_TYPE_SHIFT 14
#define FACE_TYPE_MASK  15
#define FACE_GOURAUD    (2 << FACE_TYPE_SHIFT)
#define FACE_TEXTURE    0x3FFF

// VDP1 vertex coordinates are 11 bits plus sign
#define VDP1_COORD_MIN  -1024
#define VDP1_COORD_MAX  1023

enum ClipFlags {
    CLIP_FRAME   = 1 << 0,
    CLIP_LEFT    = 1 << 1,
    CLIP_RIGHT   = 1 << 2,
    CLIP_TOP     = 1 << 3,
    CLIP_BOTTOM  = 1 << 4,
    CLIP_PLANE   = 1 << 5,
    CLIP_DISCARD = (CLIP_LEFT | CLIP_RIGHT | CLIP_TOP | CLIP_BOTTOM | CLIP_PLANE)
};

// gVertices is split between the CPUs: the master's meshes, sprites and 2D
// below MASTER_VERTICES, the slave's rooms above. Face indices address all of it.
#define MASTER_VERTICES 2048
extern Vertex gVertices[MAX_VERTICES];

// Ordering table buckets counted together, for flush to share the frame's
// faces between the CPUs.
#define OT_GROUP_SHIFT  5
#define OT_GROUPS       ((OT_SIZE >> OT_GROUP_SHIFT) + 1)

// A CPU's faces: a pool and an ordering table. flush merges the two CPUs'.
struct FaceList
{
    Face* next;
    Face* end;
    Face** ot;
    uint16 groups[OT_GROUPS];   // faces in each group of buckets
};

X_INLINE Face* faceAdd(FaceList &list, int32 depth)
{
    ASSERT(depth >= 0 && depth < OT_SIZE);

    Face* face = list.next++;
    face->next = list.ot[depth];
    list.ot[depth] = face;
    list.groups[depth >> OT_GROUP_SHIFT]++;

    return face;
}

// SH-2 has no barrel shifter, and GCC calls a library routine for most
// arithmetic right shifts by a constant. When the result fits in 16 bits, a
// logical shift (built from the 1, 2, 8 and 16 bit shift instructions) and a
// sign extension give the same value.
template <int N>
X_INLINE int32 asr16(int32 v)
{
    static_assert(N <= 16, "past 16, the logical shift leaves no sign bits in the low half");
    return int16(uint32(v) >> N);
}

// Largest value whose shift right by N fits in 16 bits.
#define ASR16_MAX(N)    (0x7FFF << (N))

// t + a . b for three 16-bit values each, on the multiply-accumulate unit.
X_INLINE int32 dot3(const int16* a, const int16* b, int32 t)
{
    int32 r;
    asm("lds    %3, macl\n\t"
        "mac.w  @%1+, @%2+\n\t"
        "mac.w  @%1+, @%2+\n\t"
        "mac.w  @%1+, @%2+\n\t"
        "sts    macl, %0"
        : "=r"(r), "+r"(a), "+r"(b)
        : "r"(t), "m"(*(const int16 (*)[3])a), "m"(*(const int16 (*)[3])b)
        : "mach", "macl");
    return r;
}

// Faces wholly outside one clip plane are dropped. So are faces crossing the
// near plane for now: a distorted sprite can't be clipped (it always maps the
// whole texture), and a projected vertex just in front of the camera lands
// far outside VDP1's coordinate range.
X_INLINE bool cullFace(uint32 clipAnd, uint32 clipOr)
{
    return (clipAnd & CLIP_DISCARD) || (clipOr & CLIP_PLANE);
}

X_INLINE bool checkBackface(const Vertex *a, const Vertex *b, const Vertex *c)
{
    return (b->x - a->x) * (c->y - a->y) <= (c->x - a->x) * (b->y - a->y);
}

// Projects a view-space point (MODEHW's PERSPECTIVE) and fills in everything
// but the shade. x and y fit in 16 bits; z is within the view range.
X_INLINE void projectView(int16 x, int16 y, int32 z, uint32 clip, const RectMinMax &vp, Vertex* res)
{
    uint32 dz = uint32(z) >> PROJ_SHIFT;
    if (dz >= DIV_TABLE_SIZE) {
        dz = DIV_TABLE_SIZE - 1;
    }
    int16 d = FixedInvU(dz); // at most 1 << 14 from VIEW_MIN on

    const int32 limit = 2047 << (16 - PROJ_SHIFT);
    int32 sx = asr16<16 - PROJ_SHIFT>(X_CLAMP(x * d, -limit, limit)) + (FRAME_WIDTH  >> 1);
    int32 sy = asr16<16 - PROJ_SHIFT>(X_CLAMP(y * d, -limit, limit)) + (FRAME_HEIGHT >> 1);

    if (uint32(sx) > FRAME_WIDTH || uint32(sy) > FRAME_HEIGHT)
    {
        clip |= CLIP_FRAME;
        sx = X_CLAMP(sx, VDP1_COORD_MIN, VDP1_COORD_MAX);
        sy = X_CLAMP(sy, VDP1_COORD_MIN, VDP1_COORD_MAX);
    }

    if (sx < vp.x0) clip |= CLIP_LEFT;
    if (sx > vp.x1) clip |= CLIP_RIGHT;
    if (sy < vp.y0) clip |= CLIP_TOP;
    if (sy > vp.y1) clip |= CLIP_BOTTOM;

    res->clip = clip;
    res->x = sx;
    res->y = sy;
    res->z = z;
}

// Room quads crossing the near plane nearer than this are split 2x2, so the
// quarters in front of it are drawn rather than the whole face culled.
#define DETAIL_DIST     4096

// Debug views (the debug pad's B cycles them).
enum TexelView
{
    TEXEL_VIEW_OFF,
    TEXEL_VIEW_LOD,     // room faces by the texture cache: green cached at full
                        // res, cyan at half size, blue at a quarter, red missed
                        // (drawn in the mean colour), yellow flat
    TEXEL_VIEW_SIZE,    // textured faces by how big a texel is on screen: green
                        // up to a pixel, yellow 2, red 4, magenta more
    TEXEL_VIEW_COUNT
};
extern int32 gTexelView;

// Far LOD, after XL2's Irreel: textured faces wholly beyond this depth are
// drawn as flat polygons in their texture's mean colour (0: never). Faces with
// see-through texels keep their texture.
extern int32 gFlatDist;

// Textured faces' draw mode but for transparency and end codes: 16-colour
// tables and gouraud, with high-speed shrink by default (as PowerSlave draws:
// a reduced texture is read every other texel and line, which on a Saturn
// takes a sixth off VDP1's time in a busy view). flush.s reads it too.
extern "C" uint32 gTexturePmod;
void renderSetFastShrink(bool on);
bool renderFastShrink();

// Hardware measurements (debug.cpp, HW_LOG builds): the frame's commands
// built another way, to see what VDP1's time goes on.
enum BenchMode
{
    BENCH_BASE,
    BENCH_FIXED_LINES,  // no room face turned (gTurnLines)
    BENCH_NO_LOD,       // room textures at full res only (gTextureLod)
    BENCH_NO_SHADE,     // every shaded face with gouraud (gShadeTables)
    BENCH_NO_GOURAUD,
    BENCH_NO_TEXTURE,   // textured faces as grey polygons
    BENCH_NO_DSP,       // rooms projected on the slave alone, without the SCU DSP
    BENCH_MASTER_MESHES,    // every mesh drawn on the master, none by the slave
    BENCH_ALL_MESHES,   // items' meshes and shadows to the slave too, not only rooms' static meshes
    BENCH_FULL_CLEAR,   // the frame cleared by its first command alone (VBLANK_ERASE_LINES 0)
    BENCH_NO_CLEAR,     // the frame not cleared at all: what no face covers keeps an old frame
    BENCH_PROFILE,      // as Base, sampling where the master's time goes
    BENCH_COUNT
};
extern int32 gBenchMode;

// VDP1 draws a quad as lines from edge AD to edge BC, as many as the longer
// of those is long, each through a row of the texture. Room faces whose edges
// AB and DC are the shorter pair are drawn turned (rooms.cpp), as A D C B with
// the texture transposed: fewer, longer lines, each through a column.
extern int32 gTurnLines;

// Room faces small on the frame take a mip level of their texture (see
// detail.h): the first whose texels along the face's lines number at most
// gTextureLod / 4 a pixel. 0: full res only.
extern int32 gTextureLod;

// Textured faces whose corners all take one shade are drawn without gouraud
// (which costs VDP1 most of a cycle a pixel), through copies of their colour
// tables shaded as gouraud would shade them; so are room faces small enough
// for a mip level whose corners are at most two levels apart, at their mean
// (a level off at most). 0: gouraud for every shaded face; 2: those room faces
// however far apart their corners.
extern int32 gShadeTables;

X_INLINE int32 vertexSpan(const Vertex* a, const Vertex* b)
{
    return X_MAX(abs(a->x - b->x), abs(a->y - b->y));
}

X_INLINE bool faceTurns(const Vertex* a, const Vertex* b, const Vertex* c, const Vertex* d)
{
    return X_MAX(vertexSpan(a, b), vertexSpan(d, c)) < X_MAX(vertexSpan(a, d), vertexSpan(b, c));
}

X_INLINE bool faceTurned(uint32 flags)
{
    const uint32 turned = FACE_DETAIL | FACE_DETAIL_WHOLE | FACE_DETAIL_TURNED;
    return (flags & turned) == turned;
}

X_INLINE bool faceFlat(uint32 flags, int32 zMin, const Texture* tex)
{
    return gFlatDist && zMin > gFlatDist && !(tex->attr & TEX_ATTR_AKILL)
        && ((flags >> FACE_TYPE_SHIFT) & FACE_TYPE_MASK) != FACE_TYPE_F;
}

X_INLINE uint32 flatFlags(uint32 flags)
{
    return (flags & ~(FACE_TYPE_MASK << FACE_TYPE_SHIFT)) | (FACE_TYPE_AVG << FACE_TYPE_SHIFT);
}

// Shading options to compare (the debug menu):
// how far shade darkens, SHADE_CURVE_*;
enum ShadeCurve
{
    SHADE_CURVE_SATURN, // in even steps to black, as the original
    SHADE_CURVE_SOFT,   // less in the middle, still to black
    SHADE_CURVE_SOFTER,
    SHADE_CURVE_COUNT
};
void renderSetShadeCurve(int32 curve);
// and how faces darkened by a third of their texture's mean are drawn.
enum DarkFaces
{
    DARK_ADD,       // by gouraud alone, as the original
    DARK_HALF,      // at half luminance, gouraud making up the rest
    DARK_CONTRAST,  // room textures through tables of less contrast as they
                    // darken, by eighths down to a half; the rest as DARK_HALF
    DARK_COUNT
};
extern int32 gDarkFaces;

// The colour tables have swapped between the original and remastered sets
// (palettes.cpp): textures take the other set's mean colours, and the tables
// of less contrast are made again.
void renderPalettesSwapped();

// rooms.cpp: rooms are drawn on the slave CPU.
struct RoomStats
{
    uint32 vertices;    // room vertices transformed
    uint32 detailFaces; // room faces drawn with cached textures
    uint32 detailMisses;  // ... in their mean colour: no cache slot or upload this frame
    uint32 flatFaces;     // room faces drawn flat (gFlatDist)
    uint32 xformTicks;  // slave time transforming vertices (its own FRT)
    uint32 faceTicks;   // ... adding faces
    uint32 meshTicks;   // ... drawing meshes
    uint32 idleTicks;   // ... waiting for rooms to be queued
    uint32 dspVertices; // room vertices the DSP projected
};

// What meshxform.s needs to transform a mesh's vertices.
struct MeshXform
{
    int16 rows[9];          // the rotation, multiplied straight from MeshVertex's int16s
    int16 pad;
    int32 tx, ty, tz;       // the translation, << FIXED_SHIFT
    const divTableInt* divTable;
    int32 ambient;          // gLightAmbient
    int32 lx, ly, lz;       // the light's direction in the mesh's own space, 128 long
    int32 range;            // gLightRange
    const int8* normals;    // or NULL
    const uint8* shades;    // or NULL
    int32 g;                // every vertex's shade when neither
    int32 inside;           // every vertex lands inside the frame, between the planes
};

// A mesh, or an item's shadow, for the slave to draw (rooms.cpp).
struct MeshJob
{
    const Mesh* mesh;       // NULL for the shadow
    int16 vCount, fCount;   // its vertices and faces
    int16 shadow[4];        // the shadow's x, z, sx, sz (renderShadow)
    uint32 flags;           // face flags (water)
    MeshXform xf;           // set up with the matrix and lighting it was queued with
};

// Sets job up to draw mesh as renderMesh would now (render.cpp).
void meshJobSetup(MeshJob &job, const Mesh* mesh);

// ... or the shadow as renderShadow would.
void meshJobShadow(MeshJob &job, int32 x, int32 z, int32 sx, int32 sz);

// Draws job's mesh on the slave: its vertices to dst, its faces into list.
void meshJobDraw(const MeshJob &job, Vertex* dst, FaceList &list);

// Draws on the master a job the slave had no room for.
void meshJobRedraw(const MeshJob &job);

// Loads the rooms' DSP program (project.dsp), which projects whole rooms.
void roomsInit();

// Every room of the frame is queued: the DSP may start on the last.
void roomsAllQueued();

// project.dsp against transformRoomAsm on random vertices (debug.cpp's boot
// self-test): the DSP's ticks with the start, this CPU's, and the vertices
// that differ.
void roomsDspTest(uint32 &dspTicks, uint32 &cpuTicks, int32 &errors);

// Queues a room for the slave, with the current matrix and viewport.
void roomsQueue(const Room* room);

// Queues a mesh for the slave, set up with the current matrix and lighting;
// false when the master had better draw it (render.cpp hands it rooms'
// static meshes).
bool roomsQueueMesh(const Mesh* mesh);

// Queues an item's shadow for the slave, whatever its share; false when it
// takes no meshes (only in BENCH_ALL_MESHES).
bool roomsQueueShadow(int32 x, int32 z, int32 sx, int32 sz);

// Waits for the slave to finish the frame's rooms; returns its ordering
// table (whose lists flush consumes and clears), or NULL if no rooms were
// queued, and its faces in each group of buckets.
Face** roomsFinish(RoomStats &stats, const uint16* &groups);

#endif
