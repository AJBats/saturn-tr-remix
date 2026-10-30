// The game's way (game.h): the C around faces.s and flush.s copied from
// saturn/render.cpp and rooms.cpp (4174aaf), cut down to the game's defaults,
// with the scene standing in for the game's level and caches.

#include "game.h"
#include "faces.h"
#include "platform.h"
#include "scene.h"
#include "hw.h"

#include <stddef.h>
#include <string.h>

#define X_CLAMP(x, a, b) ((x) < (a) ? (a) : ((x) > (b) ? (b) : (x)))
#define X_MIN(a, b)     ((a) < (b) ? (a) : (b))
#define X_MAX(a, b)     ((a) > (b) ? (a) : (b))

#define FRAME_WIDTH     352
#define FRAME_HEIGHT    224
#define FIXED_SHIFT     14
#define VIEW_MIN_F      (64 << FIXED_SHIFT)
#define VIEW_MAX_F      ((1024 * 20) << FIXED_SHIFT)
#define PROJ_SHIFT      4
#define DIV_TABLE_SIZE  1281
#define VDP1_COORD_MIN  -1024
#define VDP1_COORD_MAX  1023

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
    FACE_TYPE_AVG,
    FACE_TYPE_VOID,
};

#define FACE_TRIANGLE       (1u << 31)
#define FACE_WATER          (1u << 30)
#define FACE_DETAIL         (1u << 29)
#define FACE_PARALLELOGRAM  (1u << 28)
#define FACE_V0_AT_D        (1u << 27)
#define FACE_DETAIL_WHOLE   (1u << 27)
#define FACE_DETAIL_SLOT_SHIFT  20
#define FACE_DETAIL_QUAD_SHIFT  18
#define FACE_DETAIL_TURNED  (1u << 19)
#define FACE_DETAIL_MIP     (1u << 18)
#define FACE_DETAIL_MIP2    (1u << 26)
#define FACE_TYPE_SHIFT     14
#define FACE_TYPE_MASK      15
#define FACE_TEXTURE        0x3FFF

enum ClipFlags {
    CLIP_FRAME   = 1 << 0,
    CLIP_LEFT    = 1 << 1,
    CLIP_RIGHT   = 1 << 2,
    CLIP_TOP     = 1 << 3,
    CLIP_BOTTOM  = 1 << 4,
    CLIP_PLANE   = 1 << 5,
    CLIP_DISCARD = (CLIP_LEFT | CLIP_RIGHT | CLIP_TOP | CLIP_BOTTOM | CLIP_PLANE)
};

#define TEX_ATTR_AKILL  0x0001
#define TEX_ATTR_DIR    0x0030  // VDP1 flip bits, stored in CMDCTRL layout
#define CMD_FLIP_H      0x0010

#define ROOM_QUAD_FLAGS 0x00FFFFFF
#define DETAIL_DIST     4096    // render.h: room quads crossing the near plane nearer split
#define NEAR_SPLIT_DIST 2048    // rooms.cpp: quads whose corner A is nearer go to C



struct RoomFaces
{
    const Vertex* vertices;
    uint32_t base;
    FaceList* list;
    uint32_t flags;
    const Texture* textures;
    int32_t detailFaces;
    int32_t detailMisses;
    Face* end;
    const uint8_t* keys;
    DetailSlot* slots;
    uint32_t frame;
    void (*quad)(const Vertex* v, const RoomQuad* polys, int32_t count);
    int32_t (*miss)(uint32_t key, uint32_t texSize, uint32_t variant);
    int32_t turn;
    int32_t across;
    int32_t lod;
    int32_t parity;
    int32_t cached;
    int32_t facing;
};

struct MeshFaces
{
    const Vertex* vertices;
    int32_t base;
    FaceList* list;
    uint32_t flags;
    int32_t inside;
};

struct FlushXform
{
    Face** roomOT;
    Face** meshOT;
    int32_t buckets;
    const Vertex* vertices;
    uint32_t* cmd;
    uint32_t* cmdEnd;
    uint32_t* grd;
    uint32_t grdLink;
    const Texture* textures;
    const uint16_t* palette;
    const uint16_t* shadeGouraud;
    const uint8_t* shadeLevel;
    const uint8_t* halfLevel;
    int32_t dark;
    uint32_t detailVram;
    void (*slow)(const Face* face, uint32_t* w, uint32_t* grd, int32_t room);
    uint32_t mipVram[2];
    ShadeCache* shade;
    int32_t farShade;
};

static_assert(sizeof(Vertex) == 8 && sizeof(Face) == 16 && sizeof(RoomQuad) == 12 && sizeof(MeshQuad) == 6 &&
              sizeof(Texture) == 16 && sizeof(Sprite) == 16 && sizeof(DetailSlot) == 8 &&
              offsetof(FaceList, groups) == 12 && sizeof(ShadeCache) == 2064 &&
              offsetof(RoomFaces, facing) == 72 && offsetof(MeshFaces, inside) == 16 &&
              offsetof(FlushXform, farShade) == 76, "the game's layouts, which its assembly reads");

extern "C" void faceAddMeshQuadsAsm(const MeshQuad* polys, int32_t count, MeshFaces* mf);
extern "C" void faceAddMeshTrianglesAsm(const MeshQuad* polys, int32_t count, MeshFaces* mf);
extern "C" void faceAddRoomQuadsAsm(const RoomQuad* polys, int32_t count, RoomFaces* rf);
extern "C" void faceAddRoomTrianglesAsm(const RoomQuad* polys, int32_t count, RoomFaces* rf);
extern "C" void flushAsm(FlushXform* xf);

ALIGN16 Vertex gVertices[MAX_VERTICES];
ALIGN16 Command gCmds[MAX_CMDS];
ALIGN16 uint32_t gGouraud[MAX_CMDS][2];
extern "C" uint32_t gTexturePmod;
uint32_t gTexturePmod;

// the master's faces and the slave's, as the game's lists
static ALIGN16 Face gFaces[MAX_FACES];
static Face* gOT[OT_SIZE];
static FaceList gFaceList = { gFaces, gFaces + MAX_FACES, gOT, {} };
static ALIGN16 Face gSlaveFaces[MAX_FACES];
static Face* gSlaveOT[OT_SIZE];
static FaceList gSlaveList = { gSlaveFaces, gSlaveFaces + MAX_FACES, gSlaveOT, {} };

// the scene's
static const SceneInfo* gInfo;
static const Vertex* gSceneVertices;
static Face* gRefFaces;         // MPOL, SPOL: their next made pointers
static Face* gRefSlaveFaces;
static const uint16_t* gOrder;  // ORDR
static Face* gRefRoomOT[OT_SIZE];   // flush's tables, made from ORDR (or the faces made)
static Face* gRefMeshOT[OT_SIZE];
static const uint16_t* gFlushGroups[2];     // ... the rooms' faces and the master's by groups of buckets
static int16_t (*gRefBucket)[MAX_FACES];    // each face's bucket there: the master's, the slave's (low)
static const Command* gRefCmds;    // CMDS, GRDS (low work RAM: the checks')
static const uint32_t (*gRefGrd)[2];
static Command gClearCommand;       // the game's clear, its first command
static const Texture* gTextures;
static const uint16_t* gPalette;
static const Sprite* gSprites;
static const uint8_t* gShadeLevel;
static const uint8_t* gHalfLevel;
static const uint16_t* gShadeGouraud;
static ShadeCache* gShadeCaches;
static ShadeCache* gRefShadeCaches;     // as the game left them: its list's copies (2, low)
static const Command* gCheckCmds;       // the list gameListCheck checks against
static const uint32_t (*gCheckGrd)[2];
static int32_t gCheckEnd;
static const ShadeCache* gCheckCaches;
static uint8_t (*gDetailKeys)[DETAIL_MAX_KEYS];
static DetailSlot* gDetailSlots;
static uint32_t gDetailFrame;
static const uint16_t* gDivTable;
static const RoomScene* gRooms;
static const uint8_t* gRoomGeometry;
static const DrawScene* gDraws;
static const uint32_t* gMeshes;
static const uint8_t* gMeshGeometry;
static const MoveScene* gMoves;

static inline uint32_t pair(uint32_t hi, uint32_t lo)
{
    return (hi << 16) | (lo & 0xFFFF);
}

static inline int16_t cornerOffset(const Vertex* v, const Vertex* half)
{
    return int16_t((const uint8_t*)v - (const uint8_t*)half);
}

static inline const Vertex* cornerVertex(int32_t offset, const Vertex* half)
{
    return (const Vertex*)((const uint8_t*)half + offset);
}

template <int N>
static inline int32_t asr16(int32_t v)
{
    return int16_t(uint32_t(v) >> N);
}

#define ASR16_MAX(N)    (0x7FFF << (N))

static inline Face* faceAdd(FaceList &list, int32_t depth)
{
    Face* face = list.next++;
    face->next = list.ot[depth];
    list.ot[depth] = face;
    list.groups[depth >> OT_GROUP_SHIFT]++;
    return face;
}

// t + a . b for three 16-bit values each, on the multiply-accumulate unit.
static inline int32_t dot3(const int16_t* a, const int16_t* b, int32_t t)
{
    int32_t r;
    asm("lds    %3, macl\n\t"
        "mac.w  @%1+, @%2+\n\t"
        "mac.w  @%1+, @%2+\n\t"
        "mac.w  @%1+, @%2+\n\t"
        "sts    macl, %0"
        : "=r"(r), "+r"(a), "+r"(b)
        : "r"(t), "m"(*(const int16_t (*)[3])a), "m"(*(const int16_t (*)[3])b)
        : "mach", "macl");
    return r;
}

static inline bool cullFace(uint32_t clipAnd, uint32_t clipOr)
{
    return (clipAnd & CLIP_DISCARD) || (clipOr & CLIP_PLANE);
}

static inline bool checkBackface(const Vertex* a, const Vertex* b, const Vertex* c)
{
    return (b->x - a->x) * (c->y - a->y) <= (c->x - a->x) * (b->y - a->y);
}

static inline int32_t vertexSpan(const Vertex* a, const Vertex* b)
{
    int32_t dx = a->x - b->x, dy = a->y - b->y;
    return X_MAX(dx < 0 ? -dx : dx, dy < 0 ? -dy : dy);
}

static inline bool faceTurns(const Vertex* a, const Vertex* b, const Vertex* c, const Vertex* d)
{
    return X_MAX(vertexSpan(a, b), vertexSpan(d, c)) < X_MAX(vertexSpan(a, d), vertexSpan(b, c));
}

static inline bool faceTurned(uint32_t flags)
{
    const uint32_t turned = FACE_DETAIL | FACE_DETAIL_WHOLE | FACE_DETAIL_TURNED;
    return (flags & turned) == turned;
}

struct RectMinMax
{
    int32_t x0, y0, x1, y1;
};

// Projects a view-space point and fills in everything but the shade.
static inline void projectView(int16_t x, int16_t y, int32_t z, uint32_t clip, const RectMinMax &vp, Vertex* res)
{
    uint32_t dz = uint32_t(z) >> PROJ_SHIFT;
    if (dz >= DIV_TABLE_SIZE) {
        dz = DIV_TABLE_SIZE - 1;
    }
    int16_t d = gDivTable[dz];

    const int32_t limit = 2047 << (16 - PROJ_SHIFT);
    int32_t sx = asr16<16 - PROJ_SHIFT>(X_CLAMP(x * d, -limit, limit)) + (FRAME_WIDTH >> 1);
    int32_t sy = asr16<16 - PROJ_SHIFT>(X_CLAMP(y * d, -limit, limit)) + (FRAME_HEIGHT >> 1);

    if (uint32_t(sx) > FRAME_WIDTH || uint32_t(sy) > FRAME_HEIGHT)
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

// --- the room texture cache, as the scene holds it (saturn/detail.h) ------

static inline uint32_t detailLevel(uint32_t variant)
{
    return variant < DETAIL_MIP1 ? 0 : (variant - DETAIL_MIP1) / 2 + 1;
}

static inline uint32_t detailPoolStart(uint32_t level)
{
    return level ? DETAIL_FULL_SLOTS + (level - 1) * DETAIL_MIP_SLOTS : 0;
}

// A texture the cache hasn't: into it, its upload queued (textures.h, as
// the game's detail.cpp), from the cache as the frame found it.
static TextureQueue gTextureQueue;

static int32_t detailSlotMiss(uint32_t key, uint32_t texSize, uint32_t variant)
{
    return texturesMiss(gTextureQueue, gDetailFrame, key, texSize, variant);
}

const TextureQueue& gameTextureQueue()
{
    return gTextureQueue;
}

static inline int32_t detailSlot(uint32_t key, uint32_t texSize, uint32_t variant)
{
    if (key < DETAIL_MAX_KEYS)
    {
        uint32_t cached = gDetailKeys[variant][key];
        if (cached)
        {
            gDetailSlots[detailPoolStart(detailLevel(variant)) + cached - 1].frame = gDetailFrame;
            return cached - 1;
        }
    }
    return detailSlotMiss(key, texSize, variant);
}

// --- the slave's room faces (saturn/rooms.cpp) ----------------------------

struct RoomMatrix
{
    int16_t m00, m01, m02, m10, m11, m12, m20, m21, m22;
    int32_t tx, ty, tz;
};

static RoomMatrix gMatrix;
static RectMinMax gViewport;
static const RoomVertex* gRoomVertices;
static uint32_t gRoomFaceFlags;
static bool gRipple;
static int32_t gWibblePhase;
static int32_t gBottom, gTop;

static const int8_t gWibble[32] = {
    0, 0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 0,
    0, 0, -1, -1, -1, -2, -2, -2, -2, -2, -2, -2, -1, -1, -1, 0
};

static inline void wibble(Vertex* res, int32_t phase)
{
    if (res->clip & CLIP_FRAME)
        return;
    int32_t x = res->x;
    int32_t y = res->y;
    res->x = x + gWibble[(phase + x) & 31];
    res->y = y + gWibble[(phase + y) & 31];
}

static inline void projectRoomPoint(int16_t vx, int16_t vy, int16_t vz, Vertex* res)
{
    const RoomMatrix &m = gMatrix;
    const int16_t* rows = &m.m00;
    const int16_t v[3] = { vx, vy, vz };

    int32_t z = dot3(v, rows + 6, m.tz);
    uint32_t clip = 0;

    if (z <= (VIEW_MIN_F >> (FIXED_SHIFT - 8))) {
        clip = CLIP_PLANE;
        z = VIEW_MIN_F >> (FIXED_SHIFT - 8);
    }

    if (z >= (VIEW_MAX_F >> (FIXED_SHIFT - 8))) {
        clip = CLIP_PLANE;
        z = VIEW_MAX_F >> (FIXED_SHIFT - 8);
    }

    int32_t x = dot3(v, rows, m.tx);
    int32_t y = dot3(v, rows + 3, m.ty);

    x = asr16<8>(X_CLAMP(x, -ASR16_MAX(8), ASR16_MAX(8)));
    y = asr16<8>(X_CLAMP(y, -ASR16_MAX(8), ASR16_MAX(8)));

    projectView(x, y, uint32_t(z) >> 8, clip, gViewport, res);

    if (gRipple) {
        wibble(res, gWibblePhase);
    }
}

static void faceAddRoomVoid(const Vertex* v0, const Vertex* v1, const Vertex* v2, const Vertex* v3)
{
    Face* f = faceAdd(gSlaveList, OT_SIZE - 1);
    f->flags = FACE_TYPE_VOID << FACE_TYPE_SHIFT;
    f->indices[0] = cornerOffset(v0, gSlaveVertices);
    f->indices[1] = cornerOffset(v1, gSlaveVertices);
    f->indices[2] = cornerOffset(v2, gSlaveVertices);
    f->indices[3] = cornerOffset(v3, gSlaveVertices);
}

static inline bool roomFaceGone(uint32_t clipAnd, const Vertex* v0)
{
    return (clipAnd & (CLIP_DISCARD & ~CLIP_PLANE)) ||
           ((clipAnd & CLIP_PLANE) && v0->z < (VIEW_MAX_F >> FIXED_SHIFT));
}

static void faceAddDetailQuad(uint32_t flags, const Vertex* v0, const Vertex* v1, const Vertex* v2, const Vertex* v3)
{
    uint32_t c0 = v0->clip;
    uint32_t c1 = v1->clip;
    uint32_t c2 = v2->clip;
    uint32_t c3 = v3->clip;

    if (cullFace(c0 & c1 & c2 & c3, c0 | c1 | c2 | c3))
    {
        if (!(c0 & c1 & c2 & c3 & CLIP_DISCARD)) {
            faceAddRoomVoid(v0, v1, v2, v3);
        }
        return;
    }

    int32_t depth = X_MAX(v0->z, X_MAX(v1->z, X_MAX(v2->z, v3->z))) >> OT_SHIFT;

    Face* f = faceAdd(gSlaveList, depth);
    f->flags = flags;
    f->indices[0] = cornerOffset(v0, gSlaveVertices);
    f->indices[1] = cornerOffset(v1, gSlaveVertices);
    f->indices[2] = cornerOffset(v2, gSlaveVertices);
    f->indices[3] = cornerOffset(v3, gSlaveVertices);
}

static bool faceAddRoomQuadDetail(const RoomQuad* poly, const Texture* tex,
                                  const Vertex* v0, const Vertex* v1, const Vertex* v2, const Vertex* v3)
{
    if (gTop - 5 < gBottom || gSlaveList.next + 4 > gSlaveList.end)
        return false;

    int32_t slot = detailSlot(tex->reserved - 1, tex->size, DETAIL_QUADS);
    if (slot < 0)
        return false;

    const RoomVertex* r0 = gRoomVertices + poly->indices[0] / sizeof(Vertex);
    const RoomVertex* r1 = gRoomVertices + poly->indices[1] / sizeof(Vertex);
    const RoomVertex* r2 = gRoomVertices + poly->indices[2] / sizeof(Vertex);
    const RoomVertex* r3 = gRoomVertices + poly->indices[3] / sizeof(Vertex);

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

    uint32_t flags = (poly->flags & ROOM_QUAD_FLAGS) | gRoomFaceFlags | FACE_DETAIL | (slot << FACE_DETAIL_SLOT_SHIFT);
    faceAddDetailQuad(flags | (0 << FACE_DETAIL_QUAD_SHIFT), v0, m01, mc, m30);
    faceAddDetailQuad(flags | (1 << FACE_DETAIL_QUAD_SHIFT), m01, v1, m12, mc);
    faceAddDetailQuad(flags | (2 << FACE_DETAIL_QUAD_SHIFT), mc, m12, v2, m23);
    faceAddDetailQuad(flags | (3 << FACE_DETAIL_QUAD_SHIFT), m30, mc, m23, v3);
    return true;
}

static uint32_t wholeTexture(uint32_t flags, const Vertex* v0, const Vertex* v1, const Vertex* v2, const Vertex* v3)
{
    const Texture* tex = gTextures + (flags & FACE_TEXTURE);
    if (!tex->reserved)
        return flags;

    bool turned = faceTurns(v0, v1, v2, v3);
    int32_t slot = detailSlot(tex->reserved - 1, tex->size, turned ? DETAIL_TURNED : DETAIL_WHOLE);
    if (slot < 0)
        return flags;

    flags |= FACE_DETAIL | FACE_DETAIL_WHOLE | (slot << FACE_DETAIL_SLOT_SHIFT);
    return turned ? flags | FACE_DETAIL_TURNED : flags;
}

// faces.s's quads cut by a plane, or near the camera
static void faceAddRoomQuads(const Vertex* v, const RoomQuad* polys, int32_t count)
{
    for (int32_t i = 0; i < count; i++, polys++)
    {
        if (gSlaveList.next == gSlaveList.end)
            return;

        uint32_t flags = (polys->flags & ROOM_QUAD_FLAGS) | gRoomFaceFlags;
        const Vertex* v0 = cornerVertex(polys->indices[0], v);
        const Vertex* v1 = cornerVertex(polys->indices[1], v);
        const Vertex* v2 = cornerVertex(polys->indices[2], v);
        const Vertex* v3 = cornerVertex(polys->indices[3], v);

        uint32_t c0 = v0->clip;
        uint32_t c1 = v1->clip;
        uint32_t c2 = v2->clip;
        uint32_t c3 = v3->clip;

        if (roomFaceGone(c0 & c1 & c2 & c3, v0))
            continue;

        if (checkBackface(v0, v1, v2))
            continue;

        int32_t zMin = X_MIN(v0->z, X_MIN(v1->z, X_MIN(v2->z, v3->z)));
        int32_t zMax = X_MAX(v0->z, X_MAX(v1->z, X_MAX(v2->z, v3->z)));
        const Texture* tex = gTextures + (flags & FACE_TEXTURE);

        if ((c0 | c1 | c2 | c3) & CLIP_PLANE)
        {
            if (!(zMin < DETAIL_DIST && tex->reserved && faceAddRoomQuadDetail(polys, tex, v0, v1, v2, v3))) {
                faceAddRoomVoid(v0, v1, v2, v3);
            }
            continue;
        }

        flags = wholeTexture(flags, v0, v1, v2, v3);

        Face* f = faceAdd(gSlaveList, zMax >> OT_SHIFT);
        f->flags = flags;
        f->indices[0] = cornerOffset(v0, gSlaveVertices);
        f->indices[1] = cornerOffset(v1, gSlaveVertices);
        f->indices[2] = cornerOffset(v2, gSlaveVertices);
        f->indices[3] = cornerOffset(v3, gSlaveVertices);
    }
}

static int32_t facingQuads(const RoomQuad* quads, int32_t count, int32_t run, const int32_t* camera, int32_t &first)
{
    int32_t axis = run >> 1;
    int32_t c = camera[axis] + ((run & 1) ? 0 : 1);
    int32_t lo = 0, hi = count;
    while (lo < hi)
    {
        int32_t mid = (lo + hi) >> 1;
        const uint8_t* v = (const uint8_t*)(gRoomVertices + quads[mid].indices[0] / sizeof(Vertex));
        if ((int32_t(v[axis]) << 8) < c) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (run & 1)
    {
        first = 0;
        return lo;
    }
    first = lo;
    return count - lo;
}

static void roomFaces(const RoomScene &room)
{
    const RoomQuad* quads = (const RoomQuad*)(gRoomGeometry + room.quads);
    const RoomQuad* triangles = (const RoomQuad*)(gRoomGeometry + room.triangles);
    int16_t* matrix = &gMatrix.m00;
    for (int32_t i = 0; i < 9; i++) {
        matrix[i] = room.m[i];
    }
    gMatrix.tx = room.tx;
    gMatrix.ty = room.ty;
    gMatrix.tz = room.tz;
    gViewport = { room.clip[0], room.clip[1], room.clip[2], room.clip[3] };
    gRoomVertices = (const RoomVertex*)(gRoomGeometry + room.vertices);
    gRoomFaceFlags = room.wet ? FACE_WATER : 0;
    gRipple = room.ripple;
    gWibblePhase = room.causticsFrame;

    Vertex* roomBase = gVertices + room.base;
    if (room.dsp) {
        cacheDrop(roomBase, room.verticesCount * sizeof(Vertex));  // as the game: the DSP wrote some
    }

    // the runs of quads square to an axis, then the rest: of the runs, only
    // the quads facing the camera, without a backface test
    uint32_t base = uint16_t(cornerOffset(roomBase, gSlaveVertices));
    RoomFaces rf = { roomBase, base * 0x10001, &gSlaveList, gRoomFaceFlags, gTextures, 0, 0,
                     gSlaveList.end, gDetailKeys[0], gDetailSlots, gDetailFrame, faceAddRoomQuads, detailSlotMiss,
                     1, 0, gInfo->lod, 0, -1, 0 };
    int32_t offset = 0;
    for (int32_t run = 0; run <= 6; run++)
    {
        int32_t count = run < 6 ? room.facing[run] : room.quadsCount - offset;
        int32_t first = 0;
        int32_t n = run < 6 ? facingQuads(quads + offset, count, run, room.camera, first) : count;
        if (n > 0)
        {
            rf.facing = run < 6;
            rf.parity = int32_t(gDetailFrame & 1) ^ ((room.quadsCount ^ (offset + first + n)) & 1);
            faceAddRoomQuadsAsm(quads + offset + first, n, &rf);
        }
        offset += count;
    }

    rf.parity = -1;
    rf.cached = -1;
    if (room.trianglesCount > 0) {
        faceAddRoomTrianglesAsm(triangles, room.trianglesCount, &rf);
    }
}

void gameFacesRooms(const RoomScene* rooms, uint32_t count)
{
    gSlaveList.next = gSlaveFaces;
    gTop = MAX_VERTICES;
    gBottom = gInfo->bottom;
    for (uint32_t i = 0; i < count; i++) {
        roomFaces(rooms[i]);
    }
}

void gameFacesRooms()
{
    gameFacesRooms(gRooms, gInfo->rooms);
}

// --- the master's draws (saturn/render.cpp) -------------------------------

static const MeshQuad gShadowQuads[] = {
    { (FACE_TYPE_SHADOW << FACE_TYPE_SHIFT), {0, 1, 2, 7} },
    { (FACE_TYPE_SHADOW << FACE_TYPE_SHIFT), {7, 2, 3, 6} },
    { (FACE_TYPE_SHADOW << FACE_TYPE_SHIFT), {6, 3, 4, 5} }
};

#define MESH_LIGHT_NORMALS  1
#define MESH_LIGHT_SHADES   2

void gameFacesMaster()
{
    gFaceList.next = gFaces;
    for (uint32_t i = 0; i < gInfo->draws; i++)
    {
        const DrawScene &d = gDraws[i];
        if (d.type == DRAW_COPY)
        {
            Face* f = faceAdd(gFaceList, d.bucket);
            const Face &ref = gRefFaces[d.first];
            f->flags = ref.flags;
            memcpy(f->indices, ref.indices, sizeof(f->indices));
            continue;
        }

        MeshFaces mf = { gVertices + d.base, -int32_t(gVertices), &gFaceList, d.flags, d.inside };
        if (d.type == DRAW_SHADOW)
        {
            faceAddMeshQuadsAsm(gShadowQuads, 3, &mf);
            continue;
        }

        // the mesh's arrays (render.cpp meshParts)
        const uint8_t* mesh = gMeshGeometry + gMeshes[d.mesh];
        int32_t vCount = mesh[10];
        int32_t rCount = *(const int16_t*)(mesh + 12);
        int32_t tCount = *(const int16_t*)(mesh + 14);
        const uint8_t* ptr = mesh + 20 + vCount * 6;
        if (mesh[11] == MESH_LIGHT_NORMALS) {
            ptr += vCount * 4;
        } else if (mesh[11] == MESH_LIGHT_SHADES) {
            ptr += (vCount + 1) & ~1;
        }
        if (rCount > 0) {
            faceAddMeshQuadsAsm((const MeshQuad*)ptr, rCount, &mf);
        }
        if (tCount > 0) {
            faceAddMeshTrianglesAsm((const MeshQuad*)(ptr + rCount * 6), tCount, &mf);
        }
    }
}

void gameFacesPrepare()
{
    texturesReset();
    gTextureQueue.count = 0;
    memcpy(gVertices, gSceneVertices, sizeof(gVertices));
    memset(gOT, 0, sizeof(gOT));
    memset(gSlaveOT, 0, sizeof(gSlaveOT));
    memset(gFaceList.groups, 0, sizeof(gFaceList.groups));
    memset(gSlaveList.groups, 0, sizeof(gSlaveList.groups));
}

int32_t gameMasterFaces()
{
    return gFaceList.next - gFaces;
}

int32_t gameSlaveFaces()
{
    return gSlaveList.next - gSlaveFaces;
}

// Each face's bucket in an ordering table, -1 for none.
static void bucketsOf(Face* const* ot, const Face* pool, int16_t* bucket)
{
    for (int32_t i = 0; i < MAX_FACES; i++) {
        bucket[i] = -1;
    }
    for (int32_t b = 0; b < OT_SIZE; b++) {
        for (const Face* f = ot[b]; f; f = f->next) {
            bucket[f - pool] = b;
        }
    }
}

int32_t gameFacesCheck()
{
    static int16_t* bucket = (int16_t*)sceneAllocLow(MAX_FACES * sizeof(int16_t));
    static int16_t* want = (int16_t*)sceneAllocLow(MAX_FACES * sizeof(int16_t));
    int32_t bad = 0;

    // the master's: each face the game's, in its bucket but where a draw
    // order hint moved it (MOVE: in the bucket its corners put it)
    int32_t count = gameMasterFaces();
    int32_t ref = gInfo->masterFaces;
    bad += X_MAX(count, ref) - X_MIN(count, ref);
    bucketsOf(gOT, gFaces, bucket);
    memcpy(want, gRefBucket[0], MAX_FACES * sizeof(int16_t));
    for (uint32_t i = 0; i < gInfo->moved; i++) {
        want[gMoves[i].face] = gMoves[i].bucket;
    }
    for (int32_t i = 0; i < X_MIN(count, ref); i++)
    {
        const Face &a = gFaces[i], &b = gRefFaces[i];
        bad += a.flags != b.flags || memcmp(a.indices, b.indices, sizeof(a.indices)) || bucket[i] != want[i];
    }

    // the slave's
    count = gameSlaveFaces();
    ref = gInfo->slaveFaces;
    bad += X_MAX(count, ref) - X_MIN(count, ref);
    bucketsOf(gSlaveOT, gSlaveFaces, bucket);
    for (int32_t i = 0; i < X_MIN(count, ref); i++)
    {
        const Face &a = gSlaveFaces[i], &b = gRefSlaveFaces[i];
        bad += a.flags != b.flags || memcmp(a.indices, b.indices, sizeof(a.indices)) ||
               bucket[i] != gRefBucket[1][i];
    }

    // and the vertices the split quads added
    bad += memcmp(gVertices, gSceneVertices, sizeof(gVertices)) != 0;
    return bad;
}

// --- flush (saturn/render.cpp) ----------------------------------------------

static inline uint32_t vdp1Coord(int32_t v)
{
    return X_CLAMP(v, VDP1_COORD_MIN, VDP1_COORD_MAX);
}

static inline uint32_t shadePair(uint32_t g0, uint32_t g1)
{
    return (gShadeGouraud[g0] << 16) | gShadeGouraud[g1];
}

// Underwater: red loses 2 steps for every 1024 units beyond the first 1024.
static inline uint32_t waterGouraud(const Vertex* v)
{
    int32_t c = gShadeLevel[v->g];
    int32_t r = c;
    if (v->z > 1024)
    {
        r -= X_MIN(31, ((v->z - 1024) >> 10) << 1);
        if (r < 0) {
            r = 0;
        }
    }
    return (c << 10) | (c << 5) | r;
}

static inline uint32_t gouraudPair(uint32_t flags, const Vertex* a, const Vertex* b)
{
    if (flags & FACE_WATER)
        return (waterGouraud(a) << 16) | waterGouraud(b);
    return shadePair(a->g, b->g);
}

static inline void setRect(uint32_t* w, const Vertex* pos, const Vertex* size)
{
    int32_t x0 = pos->x;
    int32_t y0 = pos->y;
    int32_t x1 = x0 + size->x - 1;
    int32_t y1 = y0 + size->y - 1;
    w[3] = pair(x0, y0);
    w[4] = pair(x1, y0);
    w[5] = pair(x1, y1);
    w[6] = pair(x0, y1);
}

static inline int32_t offFrame(const Vertex* v)
{
    int32_t x = v->x;
    int32_t y = v->y;
    return X_MAX(0, -x) + X_MAX(0, x - FRAME_WIDTH) + X_MAX(0, -y) + X_MAX(0, y - FRAME_HEIGHT);
}

uint32_t shadeTableMiss(ShadeCache* cache, uint32_t key, uint32_t index)
{
    uint32_t a = index & ~1;
    uint32_t drawnA = cache->frames[a] >= cache->validFrom ? cache->frames[a] : 0;
    uint32_t drawnB = cache->frames[a + 1] >= cache->validFrom ? cache->frames[a + 1] : 0;
    index = drawnB < drawnA ? a + 1 : a;
    if (X_MIN(drawnA, drawnB) + 2 > cache->frame)
        return 0;
    cache->keys[index] = key;
    cache->frames[index] = cache->frame;

    uint32_t g = key & 0x7FFF;
    int32_t dr = int32_t(g & 31) - 16;
    int32_t dg = int32_t((g >> 5) & 31) - 16;
    int32_t db = int32_t((g >> 10) & 31) - 16;
    const volatile uint16_t* src = (const volatile uint16_t*)(VDP1_VRAM + (key >> 15) * 8);
    uint32_t table = cache->vram + index * SHADE_TABLE_SIZE;
    volatile uint16_t* dst = (volatile uint16_t*)(VDP1_VRAM + table);
    for (int32_t i = 0; i < 16; i++)
    {
        uint32_t c = src[i];
        int32_t r = X_CLAMP(int32_t(c & 31) + dr, 0, 31);
        int32_t gr = X_CLAMP(int32_t((c >> 5) & 31) + dg, 0, 31);
        int32_t b = X_CLAMP(int32_t((c >> 10) & 31) + db, 0, 31);
        dst[i] = (c & 0x8000) | (b << 10) | (gr << 5) | r;
    }
    return table >> 3;
}

static inline uint32_t detailSlotAddr(int32_t slot)
{
    return gInfo->detailVram + slot * DETAIL_SLOT_SIZE;
}

// The command words that depend on the face's colour or texture.
static inline void faceMaterial(uint32_t* w, uint32_t flags)
{
    uint32_t type = (flags >> FACE_TYPE_SHIFT) & FACE_TYPE_MASK;
    uint32_t color;

    if (type == FACE_TYPE_F)
    {
        color = gPalette[flags & FACE_TEXTURE];
    }
    else if (type == FACE_TYPE_AVG)
    {
        color = gTextures[flags & FACE_TEXTURE].color;
    }
    else
    {
        // room textures have no character but in the cache
        const Texture* tex = gTextures + (flags & FACE_TEXTURE);
        if (tex->srca || (flags & FACE_DETAIL))
        {
            uint32_t pmod = gTexturePmod;
            if (!(flags & FACE_PARALLELOGRAM)) {
                pmod |= PMOD_ECD_OFF;   // end codes trim parallelograms to their triangle
            }
            if (!(tex->attr & TEX_ATTR_AKILL)) {
                pmod |= PMOD_SPD_OFF;   // colour 0 is opaque
            }

            uint32_t srca = tex->srca;
            uint32_t size = tex->size;
            if (flags & FACE_DETAIL)
            {
                int32_t slot = (flags >> FACE_DETAIL_SLOT_SHIFT) & 0x7F;
                if (flags & FACE_DETAIL_WHOLE)
                {
                    uint32_t level = 0;
                    srca = detailSlotAddr(slot) >> 3;
                    if (flags & FACE_DETAIL_MIP)
                    {
                        level = flags & FACE_DETAIL_MIP2 ? 2 : 1;
                        srca = (gInfo->poolVram[level] + (slot & 0x3F) * (DETAIL_SLOT_SIZE >> 2 * level)) >> 3;
                    }
                    uint32_t w = (size >> 8) * 16 >> level;
                    uint32_t h = (size & 0xFF) * 2 >> level;
                    if (flags & FACE_DETAIL_TURNED) {
                        size = ((h / 8) << 8) | w;
                    } else {
                        size = ((w / 8) << 8) | h;
                    }
                }
                else
                {
                    uint32_t quad = (flags >> FACE_DETAIL_QUAD_SHIFT) & 3;
                    uint32_t quadSize = (size >> 8) * 4 * (size & 0xFF);
                    srca = (detailSlotAddr(slot) + quad * quadSize) >> 3;
                }
            }

            w[0] = (CMD_DISTORTED_SPRITE | (tex->attr & TEX_ATTR_DIR)) << 16;
            w[1] = pair(pmod, tex->colr);
            w[2] = pair(srca, size);
            return;
        }
        if (tex->attr & TEX_ATTR_AKILL)
        {
            w[0] = CMD_SKIP << 16;  // see-through texels can't be one colour: left out
            return;
        }
        color = tex->color;
    }

    w[0] = CMD_POLYGON << 16;
    w[1] = pair(PMOD_NO_ECD_SPD | PMOD_GOURAUD, color);
}

// A face's VDP1 command, but for its link to its gouraud table (w[7]), and
// that table: the faces flush.s hands to C.
static void faceCommand(const Face* face, uint32_t* w, uint32_t* grd, int32_t room)
{
    uint32_t flags = face->flags;
    uint32_t type = (flags >> FACE_TYPE_SHIFT) & FACE_TYPE_MASK;
    const Vertex* half = room ? gSlaveVertices : gVertices;
    const Vertex* v0 = cornerVertex(face->indices[0], half);

    switch (type)
    {
        case FACE_TYPE_SPRITE:
        {
            const Sprite* sprite = gSprites + (flags & FACE_TEXTURE);
            w[0] = (CMD_SCALED_SPRITE | (sprite->attr & TEX_ATTR_DIR)) << 16;
            w[1] = pair(PMOD_ECD_OFF | PMOD_LUT16 | PMOD_GOURAUD, sprite->colr);
            w[2] = pair(sprite->srca, sprite->size);
            w[3] = v0[0].xy;
            w[5] = v0[1].xy;
            grd[0] = grd[1] = shadePair(v0->g, v0->g);
            break;
        }

        case FACE_TYPE_FILL_S:
            w[0] = CMD_POLYGON << 16;
            w[1] = pair(PMOD_NO_ECD_SPD | PMOD_SHADOW, RGB(0, 0, 0));
            setRect(w, v0, v0 + 1);
            break;

        case FACE_TYPE_LINE_H:
        case FACE_TYPE_LINE_V:
            w[0] = CMD_POLYGON << 16;
            w[1] = pair(PMOD_NO_ECD_SPD, uint16_t(v0->z));
            setRect(w, v0, v0 + 1);
            break;

        case FACE_TYPE_VOID:
        {
            // the corners' box on the frame, transparent, behind everything
            int32_t x0 = v0->x, y0 = v0->y, x1 = x0, y1 = y0;
            for (int32_t i = 1; i < 4; i++)
            {
                const Vertex* v = cornerVertex(face->indices[i], half);
                x0 = X_MIN(x0, int32_t(v->x));
                y0 = X_MIN(y0, int32_t(v->y));
                x1 = X_MAX(x1, int32_t(v->x));
                y1 = X_MAX(y1, int32_t(v->y));
            }
            x0 = X_MAX(x0, 0);
            y0 = X_MAX(y0, 0);
            x1 = X_MIN(x1, FRAME_WIDTH - 1);
            y1 = X_MIN(y1, FRAME_HEIGHT - 1);
            w[0] = (CMD_POLYGON | (x0 > x1 || y0 > y1 ? CMD_SKIP : 0)) << 16;
            w[1] = pair(PMOD_NO_ECD_SPD, 0);
            w[3] = pair(x0, y0);
            w[4] = pair(x1, y0);
            w[5] = pair(x1, y1);
            w[6] = pair(x0, y1);
            break;
        }

        default:
        {
            const Vertex* v1 = cornerVertex(face->indices[1], half);
            const Vertex* v2 = cornerVertex(face->indices[2], half);

            if (type == FACE_TYPE_SHADOW)
            {
                w[0] = CMD_POLYGON << 16;
                w[1] = pair(PMOD_NO_ECD_SPD | PMOD_MESH, RGB(0, 0, 0));
            }
            else
            {
                faceMaterial(w, flags);
            }

            if (flags & FACE_PARALLELOGRAM)
            {
                // v0, x, y; the fourth corner is x + y - v0
                uint32_t p = pair(vdp1Coord(v1->x + v2->x - v0->x), vdp1Coord(v1->y + v2->y - v0->y));
                if (flags & FACE_V0_AT_D)
                {
                    w[3] = v1->xy;
                    w[4] = p;
                    w[5] = v2->xy;
                    w[6] = v0->xy;
                    grd[0] = gouraudPair(flags, v1, v1);
                    grd[1] = gouraudPair(flags, v2, v0);
                }
                else
                {
                    w[3] = v0->xy;
                    w[4] = v1->xy;
                    w[5] = p;
                    w[6] = v2->xy;
                    grd[0] = gouraudPair(flags, v0, v1);
                    grd[1] = gouraudPair(flags, v2, v2);
                }
                break;
            }

            const Vertex* v3 = cornerVertex(face->indices[3], half);

            if (faceTurned(flags))
            {
                const Vertex* t = v1; v1 = v3; v3 = t;
            }

            // VDP1 draws a quad as lines from edge AD to edge BC, each ending
            // at its first pixel past the frame once it has been on it: a quad
            // further off the frame on its AD side is drawn as B A D C,
            // mirrored, so its lines start on it
            if (((v0->clip | v1->clip | v2->clip | v3->clip) & CLIP_FRAME) &&
                offFrame(v0) + offFrame(v3) > offFrame(v1) + offFrame(v2))
            {
                const Vertex* t = v0; v0 = v1; v1 = t;
                t = v2; v2 = v3; v3 = t;
                w[0] ^= CMD_FLIP_H << 16;
            }

            w[3] = v0->xy;
            w[4] = v1->xy;
            w[5] = v2->xy;
            w[6] = v3->xy;
            grd[0] = gouraudPair(flags, v0, v1);
            grd[1] = gouraudPair(flags, v2, v3);
        }
    }
}

// Buckets first .. first + count - 1 of both tables to commands from slot
// cmd on, up to slot end.
static void flushXformInit(FlushXform &xf, int32_t first, int32_t count, int32_t cmd, int32_t end, int32_t cpu)
{
    ShadeCache &shade = gShadeCaches[cpu];
    shade.frame = gInfo->shadeFrame;
    xf.shade = shade.vram ? &shade : nullptr;
    xf.farShade = 2;
    xf.roomOT = gRefRoomOT + first;
    xf.meshOT = gRefMeshOT + first;
    xf.buckets = count;
    xf.vertices = gVertices;
    xf.cmd = gCmds[cmd].w;
    xf.cmdEnd = gCmds[end].w;
    xf.grd = gGouraud[cmd];
    xf.grdLink = pair((VRAM_GOURAUD_BASE >> 3) + cmd, 0);
    xf.textures = gTextures;
    xf.palette = gPalette;
    xf.shadeGouraud = gShadeGouraud;
    xf.shadeLevel = gShadeLevel;
    xf.halfLevel = gHalfLevel;
    xf.dark = 0;
    xf.detailVram = gInfo->detailVram;
    xf.slow = faceCommand;
    xf.mipVram[0] = gInfo->poolVram[1];
    xf.mipVram[1] = gInfo->poolVram[2];
}

ShadeCache* gameShadeCache(int32_t cpu)
{
    return gShadeCaches + cpu;
}

const uint16_t* gameShadeGouraud()
{
    return gShadeGouraud;
}

void gameFlushPrepare(FlushFaces faces)
{
    if (faces == FLUSH_MADE)
    {
        memcpy(gRefRoomOT, gSlaveOT, sizeof(gRefRoomOT));
        memcpy(gRefMeshOT, gOT, sizeof(gRefMeshOT));
        gFlushGroups[0] = gSlaveList.groups;
        gFlushGroups[1] = gFaceList.groups;
        return;
    }
    gFlushGroups[0] = gOrder + 2 * OT_SIZE;
    gFlushGroups[1] = gFlushGroups[0] + OT_GROUPS;
    memcpy(gVertices, gSceneVertices, sizeof(gVertices));
    for (int32_t b = 0; b < OT_SIZE; b++)
    {
        gRefRoomOT[b] = gOrder[b] && faces != FLUSH_MESHES ? gRefSlaveFaces + gOrder[b] - 1 : nullptr;
        gRefMeshOT[b] = gOrder[OT_SIZE + b] && faces != FLUSH_ROOMS ? gRefFaces + gOrder[OT_SIZE + b] - 1
                                                                    : nullptr;
    }
}

static FlushXform gFarXform;
static volatile uint32_t gFarCycles;

static void flushFar(void*)
{
    uint32_t t0 = ticks();
    flushAsm(&gFarXform);
    *uncached(&gFarCycles) = ticksCycles(t0, ticks());
}

int32_t gameFlush(bool split, uint32_t* slaveCycles)
{
    // the far buckets holding up to the game's share of the faces (counted by
    // groups of buckets) are the slave's, with its cache of shaded tables;
    // the rest this CPU's, in the commands after them
    const uint16_t* roomGroups = gFlushGroups[0];
    const uint16_t* meshGroups = gFlushGroups[1];
    int32_t total = 0;
    for (int32_t i = 0; i < OT_GROUPS; i++) {
        total += roomGroups[i] + meshGroups[i];
    }
    int32_t groups = OT_GROUPS;
    int32_t farFaces = 0;
    while (groups > 0)
    {
        int32_t faces = roomGroups[groups - 1] + meshGroups[groups - 1];
        if ((farFaces + faces) * 100 > total * gInfo->farShare)
            break;
        farFaces += faces;
        groups--;
    }
    int32_t splitBucket = X_MIN(groups << OT_GROUP_SHIFT, OT_SIZE);
    int32_t nearStart = X_MIN(1 + farFaces, MAX_CMDS - 1);

    if (splitBucket < OT_SIZE)
    {
        flushXformInit(gFarXform, splitBucket, OT_SIZE - splitBucket, 1, nearStart, 1);
        if (split) {
            slaveRun(flushFar, nullptr);
        } else {
            flushAsm(&gFarXform);
        }
    }
    int32_t end = nearStart;
    if (splitBucket > 0)
    {
        FlushXform xf;
        flushXformInit(xf, 0, splitBucket, nearStart, MAX_CMDS - 1, 0);
        flushAsm(&xf);
        end = (xf.cmd - gCmds[0].w) / 8;
    }
    if (splitBucket < OT_SIZE)
    {
        if (split)
        {
            slaveWait();
            cachePurge();   // the slave emptied buckets this CPU may hold
            if (slaveCycles) {
                *slaveCycles = *uncached(&gFarCycles);
            }
        }
        int32_t farEnd = (gFarXform.cmd - gCmds[0].w) / 8;
        if (farEnd != nearStart) {
            gCmds[farEnd].w[0] = pair(CMD_SKIP_JUMP, (VRAM_FRAME_CMDS + nearStart * sizeof(Command)) >> 3);
        }
    }
    gCmds[0] = gClearCommand;
    gCmds[end].w[0] = CMD_END << 16;
    return end;
}

enum { SAME_NOT, SAME, SAME_PICTURE };

// The key of the shaded copy a colour table address / 8 is in caches, or 0.
static uint32_t copyKey(uint32_t colr, const ShadeCache* caches)
{
    uint32_t address = colr << 3;
    for (int32_t i = 0; i < 2; i++)
    {
        const ShadeCache &cache = caches[i];
        if (cache.vram && address >= cache.vram && address < cache.vram + 256 * SHADE_TABLE_SIZE)
            return cache.keys[(address - cache.vram) / SHADE_TABLE_SIZE];
    }
    return 0;
}

// A command drawn through a shaded copy of a colour table (pmod p, its key)
// and one drawn with gouraud of one value at every corner, from that table:
// the same picture.
static bool shadedAlike(uint32_t copyW1, const ShadeCache* caches, const uint32_t* w1, const uint32_t* grd)
{
    uint32_t key = copyKey(copyW1 & 0xFFFF, caches);
    uint32_t g = grd[0] >> 16;
    const uint32_t calc = PMOD_GOURAUD_HALF << 16;     // the colour calculation's bits
    return key && (w1[0] & calc) == (PMOD_GOURAUD << 16) && (copyW1 & 0xFFFF0000) == (w1[0] & 0xFFFF0000 & ~calc) &&
           grd[0] == ((g << 16) | g) && grd[1] == grd[0] && key == (((w1[0] & 0xFFFF) << 15) | g);
}

static int32_t sameCommand(const uint32_t* a, const uint32_t* ga, const uint32_t* b, const uint32_t* gb)
{
    uint32_t ca = a[0] >> 16, cb = b[0] >> 16;
    const uint32_t jumps = 0x7000;
    if ((ca & ~jumps) != (cb & ~jumps) || (ca & CMD_SKIP) != (cb & CMD_SKIP))
        return SAME_NOT;
    if (cb & CMD_SKIP)
        return SAME;    // VDP1 reads no more of it
    uint32_t kind = cb & 15;
    bool scaled = kind == CMD_SCALED_SPRITE;   // corners A and C alone
    bool texture = kind < CMD_POLYGON;          // a polygon or line has no character
    if ((texture && a[2] != b[2]) || a[3] != b[3] || a[5] != b[5] || (!scaled && (a[4] != b[4] || a[6] != b[6])))
        return SAME_NOT;
    if (a[1] != b[1])
    {
        // a shaded copy of a table as good as another of the same, or as
        // gouraud of the copy's value at every corner (each list's copies in
        // the caches as it left them)
        uint32_t key = copyKey(a[1] & 0xFFFF, gShadeCaches);
        if ((a[1] >> 16) == (b[1] >> 16) && key && key == copyKey(b[1] & 0xFFFF, gCheckCaches))
            return SAME;
        if (shadedAlike(a[1], gShadeCaches, b + 1, gb) || shadedAlike(b[1], gCheckCaches, a + 1, ga))
            return SAME_PICTURE;
        return SAME_NOT;
    }
    return !((b[1] >> 16) & PMOD_GOURAUD) || (ga[0] == gb[0] && ga[1] == gb[1]) ? SAME : SAME_NOT;
}

int32_t gameListCheck(const Command* cmds, const uint32_t (*grd)[2], int32_t &visited, int32_t &shadedOther)
{
    int32_t bad = 0;
    int32_t slot = 0;
    int32_t count = gCheckEnd;
    visited = 0;
    shadedOther = 0;
    for (int32_t k = 0; k <= count; k++)
    {
        if (slot < 0 || slot >= MAX_CMDS)
            return bad + count + 1 - k;
        const uint32_t* a = cmds[slot].w;
        uint32_t ctrl = a[0] >> 16;
        visited++;
        if (k == count) {
            return bad + !(ctrl & CMD_END);
        }
        if (ctrl & CMD_END)
            return bad + count + 1 - k;
        int32_t same = sameCommand(a, grd[slot], gCheckCmds[k].w, gCheckGrd[k]);
        bad += same == SAME_NOT;
        shadedOther += same == SAME_PICTURE;
        uint32_t jump = (ctrl >> 12) & 7;
        slot = jump == 1 || jump == 5 ? facesSlot(a[0] & 0xFFFF) : slot + 1;
    }
    return bad;
}

void gameListMake(const RoomScene* rooms, uint32_t count, GameList &list)
{
    gameFacesPrepare();
    gameFacesMaster();
    gameFacesRooms(rooms, count);
    gameFlushPrepare(FLUSH_MADE);
    list.end = gameFlush(false);
    memcpy(list.cmds, gCmds, (list.end + 1) * sizeof(Command));
    memcpy(list.grd, gGouraud, (list.end + 1) * sizeof(gGouraud[0]));
    list.caches[0] = gShadeCaches[0];
    list.caches[1] = gShadeCaches[1];
    list.textures = gTextureQueue;
}

void gameListReference(const GameList* list)
{
    gCheckCmds = list ? list->cmds : gRefCmds;
    gCheckGrd = list ? list->grd : gRefGrd;
    gCheckEnd = list ? list->end : int32_t(gInfo->commands);
    gCheckCaches = list ? list->caches : gRefShadeCaches;
}

bool gameCommandMatches(const Command &cmd, const uint32_t* grd, int32_t slot)
{
    const uint32_t* a = cmd.w;
    const uint32_t* b = gRefCmds[slot].w;
    uint32_t ctrl = b[0] >> 16;
    if (a[0] != b[0])
        return false;
    if (ctrl & CMD_SKIP)
        return true;    // VDP1 reads no more of it
    uint32_t kind = ctrl & 15;
    bool scaled = kind == CMD_SCALED_SPRITE;   // corners A and C alone
    bool texture = kind < CMD_POLYGON;          // a polygon or line has no character
    if (a[1] != b[1] || (texture && a[2] != b[2]) || a[3] != b[3] || a[5] != b[5] || a[7] != b[7] ||
        (!scaled && (a[4] != b[4] || a[6] != b[6])))
        return false;
    return !((b[1] >> 16) & PMOD_GOURAUD) || (grd[0] == gRefGrd[slot][0] && grd[1] == gRefGrd[slot][1]);
}

int32_t gameCommandsCheck(const Command* cmds, const uint32_t (*grd)[2], int32_t count)
{
    int32_t bad = count != int32_t(gInfo->commands);
    for (int32_t i = 1; i < X_MIN(count, int32_t(gInfo->commands)); i++) {
        bad += !gameCommandMatches(cmds[i], grd[i], i);
    }
    return bad;
}

// --- from the scene -----------------------------------------------------

static void poolPointers(Face* pool, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t next = uint32_t(pool[i].next);
        pool[i].next = next ? pool + next - 1 : nullptr;
    }
}

void gameInit()
{
    gInfo = &sceneInfo();
    gSceneVertices = sceneArray<const Vertex>("VERT");
    memcpy(divTable, sceneChunk("DIVT"), DIV_TABLE_SIZE * sizeof(divTable[0]));
    gRefFaces = sceneArray<Face>("MPOL");
    gRefSlaveFaces = sceneArray<Face>("SPOL");
    poolPointers(gRefFaces, gInfo->masterFaces);
    poolPointers(gRefSlaveFaces, gInfo->slaveFaces);
    gOrder = sceneArray<const uint16_t>("ORDR");
    gameFlushPrepare();
    gRefBucket = (int16_t (*)[MAX_FACES])sceneAllocLow(2 * MAX_FACES * sizeof(int16_t));
    gRefShadeCaches = (ShadeCache*)sceneAllocLow(2 * sizeof(ShadeCache));
    bucketsOf(gRefMeshOT, gRefFaces, gRefBucket[0]);
    bucketsOf(gRefRoomOT, gRefSlaveFaces, gRefBucket[1]);
    gRefCmds = sceneArray<const Command>("CMDS");
    gRefGrd = (const uint32_t (*)[2])sceneChunk("GRDS");
    gClearCommand = gRefCmds[0];
    gameListReference(nullptr);

    gTextures = sceneArray<const Texture>("TEXS");
    gPalette = sceneArray<const uint16_t>("PALT");
    gSprites = sceneArray<const Sprite>("SPRS");
    const uint8_t* shad = sceneArray<const uint8_t>("SHAD");
    gShadeLevel = shad;
    gHalfLevel = shad + 32;
    gShadeGouraud = (const uint16_t*)(shad + 128);
    gShadeCaches = sceneArray<ShadeCache>("SHCA");
    for (int32_t i = 0; i < 2; i++)
    {
        gShadeCaches[i].miss = shadeTableMiss;
        gRefShadeCaches[i] = gShadeCaches[i];
    }
    gDetailKeys = (uint8_t (*)[DETAIL_MAX_KEYS])sceneChunk("DKEY");
    gDetailSlots = sceneArray<DetailSlot>("DSLT");
    gDetailFrame = gInfo->detailFrame;
    gDivTable = sceneArray<const uint16_t>("DIVT");
    gRooms = sceneArray<const RoomScene>("ROOM");
    gRoomGeometry = sceneArray<const uint8_t>("RGEO");
    gDraws = sceneArray<const DrawScene>("DRAW");
    gMeshes = sceneArray<const uint32_t>("MESH");
    gMeshGeometry = sceneArray<const uint8_t>("MGEO");
    gMoves = sceneArray<const MoveScene>("MOVE");
    gTexturePmod = gInfo->texturePmod;
    texturesInit();
}

