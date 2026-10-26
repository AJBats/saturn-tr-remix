// Saturn renderer: OpenLara's transform -> ordering table pipeline (after the
// 32X port), with the ordering table flushed into a VDP1 command list instead
// of a software rasterizer.

#include <stddef.h>

#include "render.h"
#include "detail.h"
#include "hw.h"
#include "palettes.h"
#include "slave.h"
#include "system.h"
#include "vram.h"

static_assert(FRAME_WIDTH == SCREEN_WIDTH && FRAME_HEIGHT == SCREEN_HEIGHT,
              "OpenLara frame size must match the display mode");

// 2D faces keep their rectangle in two vertices: position, then size. Lines
// keep their RGB colour in the first vertex's z.
#define BAR_HEIGHT      5

#define MAX_CMDS            VRAM_MAX_CMDS

extern Level level;

ALIGN16 Vertex gVertices[MAX_VERTICES];

// The master's faces: meshes, sprites, shadows and 2D. Rooms are drawn by the
// slave into its own list (rooms.cpp).
static ALIGN16 Face gFaces[MAX_FACES];
static Face* gOT[OT_SIZE];
static FaceList gFaceList = { gFaces, gFaces + MAX_FACES, gOT };

static Vertex* gVerticesBase = gVertices;

// Built in work RAM while VDP1 draws the previous frame, then DMAed to VRAM.
// Commands are written as 32-bit words (pairs of fields, big-endian): every
// store goes through the write-through cache to memory, so fewer is faster.
union Command
{
    vdp1_cmd_t cmd;
    uint32 w[8];    // ctrl|link, pmod|colr, srca|size, A, B, C, D, grda|0
};

static ALIGN16 Command gCmds[MAX_CMDS];
static ALIGN16 uint32 gGouraud[MAX_CMDS][2];

// TR shade 0 (lit) .. 31 (dark) as a gouraud level (16 = colour unchanged:
// brighter from shade 15 down, darker from 16 up, along the shading curve), as
// a VDP1 gouraud entry, and dimmed, for the scene behind the inventory
static uint8 gShadeLevel[32];
static uint16 gShadeGouraud[32];
static uint16 gDimGouraud[32];

extern Lara* players[MAX_PLAYERS];

// The inventory draws over the paused game: copyBackground returns this
// marker, and renderBackground redraws the scene behind it, dimmed, each
// frame. Its item textures replace the full-res room texture cache meanwhile.
static uint8 gSceneBackground;
static bool gInventoryFrame;    // renderBackground ran this frame
static bool gItemTextures;      // item textures are in VRAM
static Face* gDimFacesEnd;      // the master's faces before this are the scene

int32 gRenderFaceCount; // VDP1 polygons emitted by the last flush

static uint32 gRoomTicks;
static uint32 gMeshTicks;

static volatile vdp1_cmd_t* const gVdp1Cmds = (volatile vdp1_cmd_t*)VDP1_VRAM;

void setViewport(const RectMinMax &vp)
{
    viewport = vp;
}

// Meshes drawn while this is set (items in water rooms) get FACE_WATER.
static uint32 gMeshFaceFlags;
static uint32 gMeshFlatFaces;   // mesh faces drawn flat this frame

// OpenLara selects palette 1 for whatever is in a water room.
void setPaletteIndex(int32 index)
{
    gMeshFaceFlags = index ? FACE_WATER : 0;
}

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

static_assert(offsetof(MeshXform, tx) == 20 && offsetof(MeshXform, divTable) == 32 &&
              offsetof(MeshXform, ambient) == 36 && offsetof(MeshXform, lx) == 40 &&
              offsetof(MeshXform, range) == 52 && offsetof(MeshXform, normals) == 56 &&
              offsetof(MeshXform, shades) == 60 && offsetof(MeshXform, g) == 64 &&
              offsetof(MeshXform, inside) == 68,
              "MeshXform layout is shared with meshxform.s");
static_assert(sizeof(MeshVertex) == 6 && sizeof(divTableInt) == 2 && VIEW_MIN_F == 0x100000 &&
              VIEW_MAX_F == 0x14000000 && FIXED_SHIFT == 14, "constants repeated in meshxform.s");

extern "C" void transformMeshAsm(const MeshVertex* src, int32 count, Vertex* dst, MeshXform* xf);

// Vertices are shaded as TR1 does: by their normals against the brightest
// light (calcLightingDynamic), by their own shades, or all alike. Meshes are
// clipped to the frame, not to their room's portal window, as OpenLara does:
// Lara climbing into a room seen through a small portal would otherwise lose
// every face outside it.
//
// A mesh whose bounding sphere (a centre and radius, or NULL) is wholly in
// view, projected at its nearest, skips the per-vertex clamps and clip flags;
// true for those.
bool transformMesh(const MeshVertex* vertices, int32 count, int32 intensity, const int8* normals, const uint8* shades,
                   const vec3s* center, int32 radius)
{
    const Matrix &m = matrixGet();

    MeshXform xf;
    xf.rows[0] = m.e00; xf.rows[1] = m.e01; xf.rows[2] = m.e02;
    xf.rows[3] = m.e10; xf.rows[4] = m.e11; xf.rows[5] = m.e12;
    xf.rows[6] = m.e20; xf.rows[7] = m.e21; xf.rows[8] = m.e22;
    xf.tx = m.e03;
    xf.ty = m.e13;
    xf.tz = m.e23;
    xf.divTable = divTable;
    xf.ambient = gLightAmbient;
    xf.range = gLightRange;
    xf.normals = NULL;
    xf.shades = shades;
    xf.g = X_CLAMP(asr16<8>(intensity + gLightAmbient), 0, 31);
    xf.inside = 0;

    if (center)
    {
        int32 cx = sar<FIXED_SHIFT>(mul16(m.e00, center->x) + mul16(m.e01, center->y) + mul16(m.e02, center->z) + m.e03);
        int32 cy = sar<FIXED_SHIFT>(mul16(m.e10, center->x) + mul16(m.e11, center->y) + mul16(m.e12, center->z) + m.e13);
        int32 cz = sar<FIXED_SHIFT>(mul16(m.e20, center->x) + mul16(m.e21, center->y) + mul16(m.e22, center->z) + m.e23);
        int32 zNear = cz - radius;
        // a screen coordinate is about 256 * view / z from the centre; a few
        // pixels' margin covers the divide table's rounding
        xf.inside = (zNear > (VIEW_MIN_F >> FIXED_SHIFT)) && (cz + radius < VIEW_DIST) &&
                    ((abs(cx) + radius) * 256 < ((FRAME_WIDTH >> 1) - 4) * zNear) &&
                    ((abs(cy) + radius) * 256 < ((FRAME_HEIGHT >> 1) - 4) * zNear);
    }

    if (normals && gLightRange)
    {
        const vec3i &l = gLightView;
        xf.lx = sar<FIXED_SHIFT + 7>(m.e00 * l.x + m.e10 * l.y + m.e20 * l.z);
        xf.ly = sar<FIXED_SHIFT + 7>(m.e01 * l.x + m.e11 * l.y + m.e21 * l.z);
        xf.lz = sar<FIXED_SHIFT + 7>(m.e02 * l.x + m.e12 * l.y + m.e22 * l.z);
        xf.normals = normals;
        xf.shades = NULL;
    }

    transformMeshAsm(vertices, count, gVerticesBase, &xf);
    return xf.inside;
}

// What meshfaces.s needs to add a mesh's faces.
struct MeshFaces
{
    const Vertex* vertices;
    int32 base;         // minus gVertices' address / 8: a vertex's index is its address / 8 + base
    FaceList* list;
    uint32 flags;       // gMeshFaceFlags
    int32 inside;       // no vertex has clip flags
};

static_assert(offsetof(FaceList, ot) == 8 && offsetof(FaceList, groups) == 12 && OT_GROUP_SHIFT == 5 &&
              OT_SHIFT == 4 && sizeof(MeshQuad) == 6 && sizeof(MeshTriangle) == 6 &&
              offsetof(MeshFaces, inside) == 16 && FACE_TRIANGLE == int32(0x80000000) &&
              CLIP_DISCARD == 0x3E && CLIP_PLANE == 0x20, "layouts and constants shared with meshfaces.s");

extern "C" void faceAddMeshQuadsAsm(const MeshQuad* polys, int32 count, MeshFaces* mf);
extern "C" void faceAddMeshTrianglesAsm(const MeshTriangle* polys, int32 count, MeshFaces* mf);

void faceAddMeshQuads(const MeshQuad* polys, int32 count)
{
    const Vertex* v = gVerticesBase;

    for (int32 i = 0; i < count; i++, polys++)
    {
        uint32 flags = polys->flags | gMeshFaceFlags;
        const Vertex* v0 = v + polys->indices[0];
        const Vertex* v1 = v + polys->indices[1];
        const Vertex* v2 = v + polys->indices[2];
        const Vertex* v3 = v + polys->indices[3];

        if (checkBackface(v0, v1, v2))
            continue;

        uint32 c0 = v0->clip;
        uint32 c1 = v1->clip;
        uint32 c2 = v2->clip;
        uint32 c3 = v3->clip;

        if (cullFace(c0 & c1 & c2 & c3, c0 | c1 | c2 | c3))
            continue;

        if (faceFlat(flags, X_MIN(X_MIN(v0->z, v1->z), X_MIN(v2->z, v3->z)), level.textures + (flags & FACE_TEXTURE)))
        {
            flags = flatFlags(flags);
            gMeshFlatFaces++;
        }

        int32 depth = uint32(v0->z + v1->z + v2->z + v3->z) >> (2 + OT_SHIFT);

        Face* f = faceAdd(gFaceList, depth);
        f->flags = flags;
        f->indices[0] = v0 - gVertices;
        f->indices[1] = v1 - gVertices;
        f->indices[2] = v2 - gVertices;
        f->indices[3] = v3 - gVertices;
    }
}

// Textured triangles are drawn as the original does: the parallelogram v0, x,
// x + y - v0, y, with a texture whose end codes stop each line at the
// triangle's far edge. The fourth byte of the indices says where v0 goes
// (bit 0: corner D, else A) and whether x, y are v2, v1 (bit 1).
void faceAddMeshTriangles(const MeshTriangle* polys, int32 count)
{
    const Vertex* v = gVerticesBase;

    for (int32 i = 0; i < count; i++, polys++)
    {
        uint32 flags = polys->flags | gMeshFaceFlags;
        const Vertex* v0 = v + polys->indices[0];
        const Vertex* v1 = v + polys->indices[1];
        const Vertex* v2 = v + polys->indices[2];

        if (checkBackface(v0, v1, v2))
            continue;

        uint32 c0 = v0->clip;
        uint32 c1 = v1->clip;
        uint32 c2 = v2->clip;

        if (cullFace(c0 & c1 & c2, c0 | c1 | c2))
            continue;

        flags |= FACE_TRIANGLE;

        if (faceFlat(flags, X_MIN(v0->z, X_MIN(v1->z, v2->z)), level.textures + (flags & FACE_TEXTURE)))
        {
            flags = flatFlags(flags);
            gMeshFlatFaces++;
        }

        uint32 arrangement = polys->indices[3];
        uint32 type = (flags >> FACE_TYPE_SHIFT) & FACE_TYPE_MASK;
        if (type != FACE_TYPE_F && type != FACE_TYPE_AVG && !(arrangement & 0x80))
        {
            flags |= FACE_PARALLELOGRAM;
            if (arrangement & 1) {
                flags |= FACE_V0_AT_D;
            }
            if (arrangement & 2) {
                const Vertex* t = v1; v1 = v2; v2 = t;
            }
        }

        int32 depth = uint32(v0->z + v1->z + v2->z + v2->z) >> (2 + OT_SHIFT);

        Face* f = faceAdd(gFaceList, depth);
        f->flags = flags;
        f->indices[0] = v0 - gVertices;
        f->indices[1] = v1 - gVertices;
        f->indices[2] = v2 - gVertices;
        f->indices[3] = v2 - gVertices;
    }
}

int32 sphereIsVisible_c(int32 sx, int32 sy, int32 sz, int32 r)
{
    Matrix &m = matrixGet();

    if (abs(sx) < r && abs(sy) < r && abs(sz) < r)
        return 1;

    int32 z = DP33(m.e20, m.e21, m.e22, sx, sy, sz);

    if (z < 0)
        return 0;

    int32 x = DP33(m.e00, m.e01, m.e02, sx, sy, sz);
    int32 y = DP33(m.e10, m.e11, m.e12, sx, sy, sz);

    x = sar<FIXED_SHIFT>(x);
    y = sar<FIXED_SHIFT>(y);
    z = sar<FIXED_SHIFT>(z);

    z = PERSPECTIVE_DZ(z);
    if (z >= DIV_TABLE_SIZE) z = DIV_TABLE_SIZE - 1;
    int32 d = FixedInvU(z);
    x = sar<12>(x * d);
    y = sar<12>(y * d);
    r = sar<12>(r * d);

    x += (FRAME_WIDTH  / 2);
    y += (FRAME_HEIGHT / 2);

    int32 rMinX = x - r;
    int32 rMinY = y - r;
    int32 rMaxX = x + r;
    int32 rMaxY = y + r;

    if (rMinX > viewport.x1 ||
        rMaxX < viewport.x0 ||
        rMinY > viewport.y1 ||
        rMaxY < viewport.y0) return 0; // not visible

    return 1;
}

#define TEX_ATTR_DIR  0x0030 // VDP1 flip bits, stored in CMDCTRL layout
#define CMD_FLIP_H    0x0010

X_INLINE uint32 pair(uint32 hi, uint32 lo)
{
    return (hi << 16) | (lo & 0xFFFF);
}

X_INLINE int32 vdp1Coord(int32 v)
{
    return X_CLAMP(v, VDP1_COORD_MIN, VDP1_COORD_MAX);
}

X_INLINE uint32 shadePair(uint32 g0, uint32 g1)
{
    return (gShadeGouraud[g0] << 16) | gShadeGouraud[g1];
}

// Underwater, as in the original (0x06042D34): red loses 2 steps for every
// 1024 units beyond the first 1024, so distant things fade to teal.
X_INLINE uint32 waterGouraud(const Vertex* v)
{
    int32 c = gShadeLevel[v->g];
    int32 r = c;
    if (v->z > 1024)
    {
        r -= X_MIN(31, ((v->z - 1024) >> 10) << 1);
        if (r < 0) {
            r = 0;
        }
    }
    return (c << 10) | (c << 5) | r;
}

X_INLINE uint32 gouraudPair(uint32 flags, bool dim, const Vertex* a, const Vertex* b)
{
    if (dim)
        return (gDimGouraud[a->g] << 16) | gDimGouraud[b->g];
    if (flags & FACE_WATER)
        return (waterGouraud(a) << 16) | waterGouraud(b);
    return shadePair(a->g, b->g);
}

int32 gTexelView;
int32 gBenchMode;
uint32 gTexturePmod = PMOD_LUT16 | PMOD_GOURAUD | PMOD_HSS;

void renderSetFastShrink(bool on)
{
    gTexturePmod = PMOD_LUT16 | PMOD_GOURAUD | (on ? PMOD_HSS : 0);
}

bool renderFastShrink()
{
    return gTexturePmod & PMOD_HSS;
}
int32 gDarkFaces = DARK_CONTRAST;
int32 gFlatDist;
int32 gTurnLines = 1;
int32 gTextureLod = 4;
int32 gShadeTables = 1;

// Adds to each channel of a pair of gouraud values, within 0 .. 31.
X_INLINE uint32 tintPair(uint32 pair, int32 dr, int32 dg, int32 db)
{
    uint32 out = 0;
    for (int32 i = 0; i < 2; i++)
    {
        uint32 c = pair >> (16 * i);
        int32 r = X_CLAMP(int32(c & 31) + dr, 0, 31);
        int32 g = X_CLAMP(int32((c >> 5) & 31) + dg, 0, 31);
        int32 b = X_CLAMP(int32((c >> 10) & 31) + db, 0, 31);
        out |= ((b << 10) | (g << 5) | r) << (16 * i);
    }
    return out;
}

X_INLINE int16 cmdX(uint32 w) { return int16(w >> 16); }
X_INLINE int16 cmdY(uint32 w) { return int16(w); }

// Tints a textured face for the debug views, from its finished command words.
static void texelTint(const uint32* w, uint32* grd, uint32 flags, bool room)
{
    int32 dr = -6, dg = -6, db = -6;

    bool flat = ((flags >> FACE_TYPE_SHIFT) & FACE_TYPE_MASK) == FACE_TYPE_AVG;

    if (gTexelView == TEXEL_VIEW_LOD)
    {
        if (!room && !flat)
            return;
        uint32 mip = FACE_DETAIL | FACE_DETAIL_WHOLE | FACE_DETAIL_MIP;
        if (flat) {
            dr = 10; dg = 10;           // flat
        } else if ((flags & mip) == mip) {
            db = 12;                    // a mip level: half size, or a quarter
            dg = flags & FACE_DETAIL_MIP2 ? -6 : 10;
        } else if (flags & FACE_DETAIL) {
            dg = 10;
        } else {
            dr = 12;                    // missed the cache
        }
    }
    else
    {
        if (flat)
            return;

        // twice the face's area on screen against its texels
        int32 area2 = 0;
        for (int32 i = 0; i < 4; i++)
        {
            uint32 a = w[3 + i];
            uint32 b = w[3 + ((i + 1) & 3)];
            area2 += cmdX(a) * cmdY(b) - cmdX(b) * cmdY(a);
        }
        area2 = abs(area2);
        uint32 size = w[2] & 0xFFFF;
        int32 texels = (size >> 8) * 8 * (size & 0xFF);
        if (area2 <= 2 * texels) {
            dg = 10;                    // a texel is at most a pixel
        } else if (area2 <= 8 * texels) {
            dr = 10; dg = 10;           // up to 2 x 2 pixels
        } else if (area2 <= 32 * texels) {
            dr = 12;                    // up to 4 x 4
        } else {
            dr = 12; db = 12;           // bigger
        }
    }

    grd[0] = tintPair(grd[0], dr, dg, db);
    grd[1] = tintPair(grd[1], dr, dg, db);
}

// DARK_HALF: a face darkened by at least a third of its texture's mean is drawn
// at half luminance, which scales its texels as the PS1 shades, and its
// gouraud levels are raised to keep each corner as bright as it was. Halving
// (t + g - 16) leaves t / 2 + (g - 16) / 2, so for corner level L and mean m
// per channel, g = m + 2 (L - 16) + 16 = m + 2L - 16, which gHalfLevel holds
// within 0 .. 31 (indexed by m + 2L). A face with a corner too bright for that
// (m + 2L - 16 past 31 in a channel) is drawn as usual: held at 31, the corner
// would come out darker and tinted, unlike the faces beside it. Texture.attr's
// high byte holds the texture's mean luma (renderLevelInit).
//
// Half luminance halves the texels' distance from the mean: the faces beside
// one drawn as usual show it as a step in contrast. DARK_CONTRAST draws room
// textures through tables of their colours brought towards the mean instead,
// at 7/8 of the contrast for darkening of a twelfth of the mean, by eighths to
// 4/8 at a third (where 4/8 matches half luminance); gouraud goes as usual.
//
// True when the gouraud table is filled; false for the caller to fill it as
// usual (w[1] may have taken a table of less contrast).
static uint8 gHalfLevel[32 + 64];

X_INLINE uint32 halfGouraud(const uint8* r, const uint8* g, const uint8* b, int32 level)
{
    return r[level * 2] | (g[level * 2] << 5) | (b[level * 2] << 10);
}

X_INLINE bool shadeDark(uint32* w, uint32* grd, uint32 flags, const Vertex* a, const Vertex* b, const Vertex* c, const Vertex* d)
{
    const Texture* tex = level.textures + (flags & FACE_TEXTURE);
    int32 la = gShadeLevel[a->g];
    int32 lb = gShadeLevel[b->g];
    int32 lc = gShadeLevel[c->g];
    int32 ld = gShadeLevel[d->g];

    // average darkening (64 - sum) / 4 against a third of the mean luma / 3
    int32 dark = (64 - (la + lb + lc + ld)) * 9;
    int32 third = int32(tex->attr >> 8) * 4;

    // a room face drawn in its mean colour: the tables are for its texels
    if (tex->contrast && ((w[0] >> 16) & 15) == CMD_POLYGON)
        return false;

    if (gDarkFaces == DARK_CONTRAST && tex->contrast)
    {
        int32 k = 0;
        for (int32 step = third; k < 4 && dark * 4 >= step; step += third) {
            k++;
        }
        if (k) {
            w[1] = (w[1] & 0xFFFF0000) | (tex->contrast + (k - 1) * 4);
        }
        return false;
    }

    if (dark < third)
        return false;

    uint32 color = tex->color;
    int32 lmax = X_MAX(X_MAX(la, lb), X_MAX(lc, ld));
    int32 mmax = X_MAX(int32(color & 31), X_MAX(int32((color >> 5) & 31), int32((color >> 10) & 31)));
    if (mmax + 2 * lmax - 16 > 31)
        return false;
    const uint8* r = gHalfLevel + (color & 31);
    const uint8* g = gHalfLevel + ((color >> 5) & 31);
    const uint8* bl = gHalfLevel + ((color >> 10) & 31);

    grd[0] = (halfGouraud(r, g, bl, la) << 16) | halfGouraud(r, g, bl, lb);
    grd[1] = (halfGouraud(r, g, bl, lc) << 16) | halfGouraud(r, g, bl, ld);
    w[1] = (w[1] & ~(7 << 16)) | (PMOD_GOURAUD_HALF << 16);
    return true;
}

X_INLINE void setRect(uint32* w, const Vertex* pos, const Vertex* size)
{
    int32 x0 = pos->x;
    int32 y0 = pos->y;
    int32 x1 = x0 + size->x - 1;
    int32 y1 = y0 + size->y - 1;
    w[3] = pair(x0, y0);
    w[4] = pair(x1, y0);
    w[5] = pair(x1, y1);
    w[6] = pair(x0, y1);
}

// How far a corner lies off the frame, in pixels across and down.
X_INLINE int32 offFrame(const Vertex* v)
{
    int32 x = v->x;
    int32 y = v->y;
    return X_MAX(0, -x) + X_MAX(0, x - FRAME_WIDTH) + X_MAX(0, -y) + X_MAX(0, y - FRAME_HEIGHT);
}

// Shaded copies of colour tables (gShadeTables). VDP1's gouraud adds each
// channel of the gouraud value, less 16, to the texel's, within 0 .. 31; a
// copy of a face's table with that done to its colours draws a face whose
// corners all take that value the same. Each CPU building commands keeps a
// cache of copies in VDP1 VRAM, two-way set associative by table and shade;
// flush.s looks them up and calls shadeTableMiss when one is missing.
#define SHADE_TABLES    256     // each CPU's
#define SHADE_TABLE_SIZE 32

struct ShadeCache
{
    uint32 frame;           // the frame being built (gShadeFrame)
    uint32 validFrom;       // copies drawn before this frame are stale
    uint32 vram;            // this cache's copies, VDP1 VRAM offset; 0: none
    uint32 (*miss)(ShadeCache* cache, uint32 key, uint32 index);
    uint32 keys[SHADE_TABLES];      // table address / 8 << 15 | gouraud value
    uint32 frames[SHADE_TABLES];    // last drawn
};

static_assert(offsetof(ShadeCache, miss) == 12 && offsetof(ShadeCache, keys) == 16 &&
              offsetof(ShadeCache, frames) == 16 + 4 * SHADE_TABLES && SHADE_TABLES == 256 &&
              SHADE_TABLE_SIZE == 32, "ShadeCache layout is shared with flush.s");

static ShadeCache gShadeCaches[2] __attribute__((aligned(16)));    // the master's, the slave's
static uint32 gShadeFrame = 2;

// A copy for the key, in the pair of slots holding index whose copy was drawn
// longest ago, unless both are drawn this frame or the last (VDP1 may be
// drawing that one): its table address / 8, or 0.
extern "C" uint32 shadeTableMiss(ShadeCache* cache, uint32 key, uint32 index)
{
    uint32 a = index & ~1;
    uint32 drawnA = cache->frames[a] >= cache->validFrom ? cache->frames[a] : 0;
    uint32 drawnB = cache->frames[a + 1] >= cache->validFrom ? cache->frames[a + 1] : 0;
    index = drawnB < drawnA ? a + 1 : a;
    if (X_MIN(drawnA, drawnB) + 2 > cache->frame)
        return 0;
    cache->keys[index] = key;
    cache->frames[index] = cache->frame;

    uint32 g = key & 0x7FFF;
    int32 dr = int32(g & 31) - 16;
    int32 dg = int32((g >> 5) & 31) - 16;
    int32 db = int32((g >> 10) & 31) - 16;
    const volatile uint16* src = (const volatile uint16*)(VDP1_VRAM + (key >> 15) * 8);
    uint32 table = cache->vram + index * SHADE_TABLE_SIZE;
    volatile uint16* dst = (volatile uint16*)(VDP1_VRAM + table);
    for (int32 i = 0; i < 16; i++)
    {
        uint32 c = src[i];
        int32 r = X_CLAMP(int32(c & 31) + dr, 0, 31);
        int32 gr = X_CLAMP(int32((c >> 5) & 31) + dg, 0, 31);
        int32 b = X_CLAMP(int32((c >> 10) & 31) + db, 0, 31);
        dst[i] = (c & 0x8000) | (b << 10) | (gr << 5) | r;
    }
    return table >> 3;
}

// Every copy stale: the tables they copy, or the VRAM they live in, changed.
static void shadeTablesReset()
{
    for (int32 i = 0; i < 2; i++) {
        gShadeCaches[i].validFrom = gShadeFrame;
    }
}

// Room textures' colour tables at 7/8, 6/8, 5/8 and 4/8 of their contrast
// about the texture's mean colour (DARK_CONTRAST). A pixel code 0 that is
// transparent stays as it is.
#define CONTRAST_TABLES 4

static void buildContrast()
{
    for (int32 i = 0; i < level.texturesCount; i++)
    {
        const Texture &t = level.textures[i];
        if (!t.contrast)
            continue;

        const volatile uint16* lut = (const volatile uint16*)(VDP1_VRAM + t.colr * 8);
        volatile uint16* out = (volatile uint16*)(VDP1_VRAM + t.contrast * 8);
        uint32 m = t.color;
        int32 mr = m & 31;
        int32 mg = (m >> 5) & 31;
        int32 mb = (m >> 10) & 31;

        for (int32 k = 1; k <= CONTRAST_TABLES; k++)
        {
            int32 c = 8 - k;
            for (int32 j = 0; j < 16; j++)
            {
                uint32 col = lut[j];
                if (j == 0 && (t.attr & TEX_ATTR_AKILL))
                {
                    *out++ = col;
                    continue;
                }
                // m (8 - c) + col c >= 0, eighths
                uint32 r = (mr * (8 - c) + int32(col & 31) * c + 4) >> 3;
                uint32 g = (mg * (8 - c) + int32((col >> 5) & 31) * c + 4) >> 3;
                uint32 b = (mb * (8 - c) + int32((col >> 10) & 31) * c + 4) >> 3;
                *out++ = (col & 0x8000) | (b << 10) | (g << 5) | r;
            }
        }
    }
    shadeTablesReset();
}

// The command words that depend on the face's colour or texture.
X_INLINE void faceMaterial(uint32* w, uint32 flags)
{
    uint32 type = (flags >> FACE_TYPE_SHIFT) & FACE_TYPE_MASK;
    uint32 color;

    if (type == FACE_TYPE_F)
    {
        color = level.palette[flags & FACE_TEXTURE];
    }
    else if (type == FACE_TYPE_AVG)
    {
        color = level.textures[flags & FACE_TEXTURE].color;
    }
    else
    {
        // room textures have no character but in the cache
        const Texture* tex = level.textures + (flags & FACE_TEXTURE);
        if (tex->srca || (flags & FACE_DETAIL))
        {
            uint32 pmod = gTexturePmod;
            if (!(flags & FACE_PARALLELOGRAM)) {
                pmod |= PMOD_ECD_OFF; // end codes trim parallelograms to their triangle
            }
            if (!(tex->attr & TEX_ATTR_AKILL)) {
                pmod |= PMOD_SPD_OFF; // colour 0 is opaque
            }

            uint32 srca = tex->srca;
            uint32 size = tex->size;
            if (flags & FACE_DETAIL)
            {
                int32 slot = (flags >> FACE_DETAIL_SLOT_SHIFT) & 0x7F;
                if (flags & FACE_DETAIL_WHOLE)
                {
                    uint32 level = 0;
                    srca = detailSlotAddr(slot) >> 3;
                    if (flags & FACE_DETAIL_MIP)
                    {
                        level = flags & FACE_DETAIL_MIP2 ? 2 : 1;
                        srca = (gDetailPoolVram[level] + (slot & 0x3F) * (DETAIL_SLOT_SIZE >> 2 * level)) >> 3;
                    }
                    uint32 w = (size >> 8) * 16 >> level;
                    uint32 h = (size & 0xFF) * 2 >> level;
                    if (flags & FACE_DETAIL_TURNED) {
                        size = ((h / 8) << 8) | w;
                    } else {
                        size = ((w / 8) << 8) | h;
                    }
                }
                else
                {
                    uint32 quad = (flags >> FACE_DETAIL_QUAD_SHIFT) & 3;
                    uint32 quadSize = (size >> 8) * 4 * (size & 0xFF);
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
            w[0] = CMD_SKIP << 16; // see-through texels can't be one colour: left out
            return;
        }
        color = tex->color;
    }

    w[0] = CMD_POLYGON << 16;
    w[1] = pair(PMOD_NO_ECD_SPD | PMOD_GOURAUD, color);
}

// A face's VDP1 command, but for its link to its gouraud table (w[7]), and
// that table. 'room' for the slave's faces; 'dim' behind the inventory.
static void faceCommand(const Face* face, uint32* w, uint32* grd, bool room, bool dim)
{
    uint32 flags = face->flags;
    uint32 type = (flags >> FACE_TYPE_SHIFT) & FACE_TYPE_MASK;
    const Vertex* v0 = gVertices + face->indices[0];

    switch (type)
    {
        case FACE_TYPE_SPRITE:
        {
            const Sprite* sprite = level.sprites + (flags & FACE_TEXTURE);
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
            w[1] = pair(PMOD_NO_ECD_SPD, uint16(v0->z));
            setRect(w, v0, v0 + 1);
            break;

        default:
        {
            const Vertex* v1 = gVertices + face->indices[1];
            const Vertex* v2 = gVertices + face->indices[2];

            if (type == FACE_TYPE_SHADOW)
            {
                w[0] = CMD_POLYGON << 16;
                w[1] = pair(PMOD_NO_ECD_SPD | PMOD_SHADOW, RGB(0, 0, 0));
            }
            else
            {
                faceMaterial(w, flags);
            }

            if (flags & FACE_PARALLELOGRAM)
            {
                // v0, x, y; the fourth corner is x + y - v0
                uint32 p = pair(vdp1Coord(v1->x + v2->x - v0->x), vdp1Coord(v1->y + v2->y - v0->y));

                bool dark = gDarkFaces != DARK_ADD && type != FACE_TYPE_F && type != FACE_TYPE_SHADOW && !dim &&
                            !(flags & FACE_WATER);

                if (flags & FACE_V0_AT_D)
                {
                    w[3] = v1->xy;
                    w[4] = p;
                    w[5] = v2->xy;
                    w[6] = v0->xy;
                    if (!(dark && shadeDark(w, grd, flags, v1, v1, v2, v0)))
                    {
                        grd[0] = gouraudPair(flags, dim, v1, v1);
                        grd[1] = gouraudPair(flags, dim, v2, v0);
                    }
                }
                else
                {
                    w[3] = v0->xy;
                    w[4] = v1->xy;
                    w[5] = p;
                    w[6] = v2->xy;
                    if (!(dark && shadeDark(w, grd, flags, v0, v1, v2, v2)))
                    {
                        grd[0] = gouraudPair(flags, dim, v0, v1);
                        grd[1] = gouraudPair(flags, dim, v2, v2);
                    }
                }

                if (gTexelView == TEXEL_VIEW_SIZE && type != FACE_TYPE_F && type != FACE_TYPE_SHADOW) {
                    texelTint(w, grd, flags, false);
                }
                break;
            }

            const Vertex* v3 = gVertices + face->indices[3];

            if (faceTurned(flags))
            {
                const Vertex* t = v1; v1 = v3; v3 = t;
            }

            // VDP1 draws a quad as lines from edge AD to edge BC, each
            // ending at its first pixel past the frame once it has
            // been on it. A quad further off the frame on its AD side
            // is drawn as B A D C, mirrored, so its lines start on it.
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
            bool dark = gDarkFaces != DARK_ADD && type != FACE_TYPE_F && type != FACE_TYPE_SHADOW && !dim &&
                        !(flags & FACE_WATER);
            if (!(dark && shadeDark(w, grd, flags, v0, v1, v2, v3)))
            {
                grd[0] = gouraudPair(flags, dim, v0, v1);
                grd[1] = gouraudPair(flags, dim, v2, v3);
            }
            if (gTexelView && type != FACE_TYPE_F && type != FACE_TYPE_SHADOW) {
                texelTint(w, grd, flags, room);
            }
        }
    }
}

static void faceSlow(const Face* face, uint32* w, uint32* grd, int32 room)
{
    faceCommand(face, w, grd, room, false);
}

// What flush.s needs to build the commands of a frame's faces.
struct FlushXform
{
    Face** roomOT;
    Face** meshOT;
    int32 buckets;
    const Vertex* vertices;
    uint32* cmd;            // the next command; returned
    uint32* cmdEnd;         // the last, kept for the end
    uint32* grd;            // the next gouraud table; returned
    uint32 grdLink;         // the next command's w[7]; returned
    const Texture* textures;
    const uint16* palette;
    const uint16* shadeGouraud;
    const uint8* shadeLevel;
    const uint8* halfLevel;
    int32 dark;             // gDarkFaces
    uint32 detailVram;
    void (*slow)(const Face* face, uint32* w, uint32* grd, int32 room);
    uint32 mipVram[2];      // gDetailPoolVram[1], [2]
    ShadeCache* shade;      // the CPU's shaded colour tables; NULL: none
};

static_assert(offsetof(FlushXform, vertices) == 12 && offsetof(FlushXform, grdLink) == 28 &&
              offsetof(FlushXform, textures) == 32 && offsetof(FlushXform, dark) == 52 &&
              offsetof(FlushXform, slow) == 60 && offsetof(FlushXform, mipVram) == 64 &&
              offsetof(FlushXform, shade) == 72,
              "FlushXform layout is shared with flush.s");
static_assert(sizeof(Face) == 16 && offsetof(Face, next) == 4 && offsetof(Face, indices) == 8 &&
              sizeof(Texture) == 16 && offsetof(Texture, attr) == 6 && offsetof(Texture, color) == 8 &&
              offsetof(Texture, contrast) == 14 && DARK_CONTRAST == 2 &&
              sizeof(Command) == 32 && sizeof(gGouraud[0]) == 8, "layouts read by flush.s");
static_assert(FACE_WATER == (1 << 30) && FACE_DETAIL == (1 << 29) && FACE_PARALLELOGRAM == (1 << 28) &&
              FACE_DETAIL_WHOLE == (1 << 27) && FACE_V0_AT_D == (1 << 27) && FACE_DETAIL_SLOT_SHIFT == 20 &&
              FACE_DETAIL_TURNED == (1 << 19) &&
              FACE_DETAIL_QUAD_SHIFT == 18 && FACE_TYPE_SHIFT == 14 && FACE_TEXTURE == 0x3FFF &&
              FACE_TYPE_F == 1 && FACE_TYPE_FT == 2 && FACE_TYPE_GTA == 5 && FACE_TYPE_AVG == 10 &&
              TEX_ATTR_AKILL == 1 && TEX_ATTR_DIR == 0x30 && CMD_FLIP_H == 0x10 && DETAIL_SLOT_SIZE == 2048 &&
              FRAME_WIDTH == 352 && FRAME_HEIGHT == 240 && VDP1_COORD_MAX == 1023 && VDP1_COORD_MIN == -1024,
              "constants repeated in flush.s");

extern "C" void flushAsm(FlushXform* xf);

// The frame's commands 1 .. count - 1 rebuilt for gBenchMode.
static void benchCommands(int32 count)
{
    if (gBenchMode != BENCH_NO_GOURAUD && gBenchMode != BENCH_NO_TEXTURE)
        return;

    for (int32 i = 1; i < count; i++)
    {
        uint32* w = gCmds[i].w;
        uint32 type = (w[0] >> 16) & 15;
        if (type != CMD_DISTORTED_SPRITE && type != CMD_POLYGON)
            continue;

        if (gBenchMode == BENCH_NO_GOURAUD && (w[1] & (PMOD_GOURAUD << 16))) {
            w[1] &= ~(7 << 16);
        } else if (gBenchMode == BENCH_NO_TEXTURE && type == CMD_DISTORTED_SPRITE) {
            w[0] = (w[0] & ~(15 << 16)) | (CMD_POLYGON << 16);
            w[1] = pair(PMOD_NO_ECD_SPD | PMOD_GOURAUD, RGB(16, 16, 16));
        }
    }
}

// Buckets first .. first + count - 1 of both tables to commands from slot
// cmd on, up to slot end.
static void flushXformInit(FlushXform &xf, Face** roomOT, int32 first, int32 count, int32 cmd, int32 end, int32 cpu)
{
    ShadeCache &shade = gShadeCaches[cpu];
    shade.frame = gShadeFrame;
    xf.shade = shade.vram && gShadeTables && gBenchMode != BENCH_NO_SHADE ? &shade : NULL;
    xf.roomOT = roomOT + first;
    xf.meshOT = gOT + first;
    xf.buckets = count;
    xf.vertices = gVertices;
    xf.cmd = gCmds[cmd].w;
    xf.cmdEnd = gCmds[end].w;
    xf.grd = gGouraud[cmd];
    xf.grdLink = pair((VRAM_GOURAUD_BASE >> 3) + cmd, 0);
    xf.textures = level.textures;
    xf.palette = level.palette;
    xf.shadeGouraud = gShadeGouraud;
    xf.shadeLevel = gShadeLevel;
    xf.halfLevel = gHalfLevel;
    xf.dark = gDarkFaces;
    xf.detailVram = gDetailVramStart;
    xf.slow = faceSlow;
    xf.mipVram[0] = gDetailPoolVram[1];
    xf.mipVram[1] = gDetailPoolVram[2];
}

static FlushXform gFarXform;

static void flushFar()
{
    flushAsm(&gFarXform);
}

// The commands of the faces in both ordering tables, far to near, emptying
// them, from gCmds[count] on. Returns the next free command.
//
// flush.s builds them but behind the inventory or in the texel views, on
// both CPUs: the slave the far buckets holding up to 45% of the faces
// (counted by groups of buckets in the CPUs' face lists; it starts a little
// later, with its cache empty), this CPU the rest, in the commands after them.
static int32 flushFaces(Face** roomOT, const uint16* roomGroups, int32 count)
{
    uint32 grdBase = VRAM_GOURAUD_BASE >> 3;

    if (roomOT && !gInventoryFrame && !gTexelView)
    {
        int32 total = 0;
        for (int32 i = 0; i < OT_GROUPS; i++) {
            total += roomGroups[i] + gFaceList.groups[i];
        }

        int32 split = OT_GROUPS;
        int32 farFaces = 0;
        while (split > 0)
        {
            int32 faces = roomGroups[split - 1] + gFaceList.groups[split - 1];
            if ((farFaces + faces) * 20 > total * 9)
                break;
            farFaces += faces;
            split--;
        }

        int32 splitBucket = X_MIN(split << OT_GROUP_SHIFT, OT_SIZE);
        int32 nearStart = X_MIN(count + farFaces, MAX_CMDS - 1);

        if (splitBucket < OT_SIZE)
        {
            flushXformInit(gFarXform, roomOT, splitBucket, OT_SIZE - splitBucket, count, nearStart, 1);
            slaveRun(flushFar);
        }

        int32 end = nearStart;
        if (splitBucket > 0)
        {
            FlushXform xf;
            flushXformInit(xf, roomOT, 0, splitBucket, nearStart, MAX_CMDS - 1, 0);
            flushAsm(&xf);
            end = (xf.cmd - gCmds[0].w) / 8;
        }

        if (splitBucket < OT_SIZE)
        {
            uint32 waitStart = timerTicks();
            slaveWait();
            gStats.farWaitTicks = (timerTicks() - waitStart) & 0xFFFF;
            cachePurge(); // the slave emptied buckets of gOT that this CPU may hold

            // its faces fill the commands before this CPU's but for a cap
            int32 farEnd = (gFarXform.cmd - gCmds[0].w) / 8;
            if (farEnd != nearStart) {
                gCmds[farEnd].w[0] = pair(CMD_SKIP_JUMP, (VRAM_FRAME_CMDS + nearStart * sizeof(Command)) >> 3);
            }
        }

        return end;
    }

    // far to near; in each bucket rooms (the slave's) before what stands in them
    for (int32 i = OT_SIZE - 1; i >= 0; i--)
    {
        Face* rooms = roomOT ? roomOT[i] : NULL;
        Face* meshes = gOT[i];
        if (!rooms && !meshes)
            continue;

        if (rooms) {
            roomOT[i] = NULL;
        }
        gOT[i] = NULL;

        for (int32 list = 0; list < 2; list++)
        {
            for (Face* face = list ? meshes : rooms; face; face = face->next)
            {
                if (count == MAX_CMDS - 1)
                    break;

                // rooms, and what was drawn with them, behind the inventory
                bool dim = gInventoryFrame && (!list || face < gDimFacesEnd);

                uint32* w = gCmds[count].w;
                w[7] = pair(grdBase + count, 0);
                faceCommand(face, w, gGouraud[count], !list, dim);
                count++;
            }
        }
    }

    return count;
}

void flush_c()
{
    uint32 start = timerTicks();

    RoomStats rooms = {};
    const uint16* roomGroups = NULL;
    Face** roomOT = roomsFinish(rooms, roomGroups);
    gStats.slaveWaitTicks = (timerTicks() - start) & 0xFFFF;

    gStats.vertices = (gVerticesBase - gVertices) + rooms.vertices;
    gStats.roomVertices = rooms.vertices;
    gStats.detailFaces = rooms.detailFaces;
    gStats.detailMisses = rooms.detailMisses;
    gStats.flatFaces = rooms.flatFaces + gMeshFlatFaces;
    gMeshFlatFaces = 0;
    gStats.slaveXformTicks = rooms.xformTicks;
    gStats.slaveFaceTicks = rooms.faceTicks;
    gStats.slaveIdleTicks = rooms.idleTicks;
    gStats.roomTicks = gRoomTicks;
    gStats.meshTicks = gMeshTicks;
    gRoomTicks = gMeshTicks = 0;

    gVerticesBase = gVertices;

    if (gFaceList.next == gFaces && !roomOT)
        return;

    gFaceList.next = gFaces;

    gShadeFrame++;
    int32 count = flushFaces(roomOT, roomGroups, 1); // gCmds[0] clears the frame (renderInit)
    memset(gFaceList.groups, 0, sizeof(gFaceList.groups));

    gCmds[count].w[0] = CMD_END << 16;
    gRenderFaceCount = count;
    gStats.faces = count;
    benchCommands(count);

    uint32 waitStart = timerTicks();
    vdp1WaitDrawn();
    gStats.waitTicks = (timerTicks() - waitStart) & 0xFFFF;

    uint32 dmaStart = timerTicks();
    detailFlush();
    if (gItemTextures && !gInventoryFrame)
    {
        detailResume();
        buildContrast(); // the item textures may have covered the tables
        gItemTextures = false;
    }
    gInventoryFrame = false;
    gDimFacesEnd = gFaces;

    dmaCopy(gCmds, VDP1_VRAM + VRAM_FRAME_CMDS, (count + 1) * sizeof(Command));
    dmaCopy(gGouraud, VDP1_VRAM + VRAM_GOURAUD_BASE, count * sizeof(gGouraud[0]));
    dmaWait();
    vdp1Plot();
    gStats.dmaTicks = (timerTicks() - dmaStart) & 0xFFFF;

    gStats.flushTicks = ((timerTicks() - start) & 0xFFFF) - gStats.waitTicks - gStats.slaveWaitTicks;
}

void renderSetShadeCurve(int32 curve)
{
    // shades 16 .. 31, how many steps darker
    static const uint8 dark[SHADE_CURVE_COUNT][16] = {
        { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 },
        { 0, 1, 2, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13, 15, 16 },    // ^1.4
        { 0, 0, 1, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 13, 14, 16 }     // ^1.8
    };

    for (int32 g = 0; g < 32; g++)
    {
        uint32 c = (g < 16) ? 31 - g : 16 - dark[curve][g - 16];
        gShadeLevel[g] = c;
        gShadeGouraud[g] = (c << 10) | (c << 5) | c;
        uint32 d = X_MAX(int32(c) - 8, 0);
        gDimGouraud[g] = (d << 10) | (d << 5) | d;
    }
}

void renderInit()
{
    // The core's divide table holds 1 .. 1023 (its last entry is 0, past the
    // far plane there); the view reaches further here.
    for (int32 i = 1024; i < DIV_TABLE_SIZE; i++) {
        divTable[i] = (1 << 16) / i;
    }

    renderSetShadeCurve(SHADE_CURVE_SATURN);
    gDimFacesEnd = gFaces;

    // The frame's first command clears it, as the original does: vblank erase
    // can't cover a 352-wide frame in time. A scaled sprite of colour 0 with
    // transparency off writes 0 (VDP2 shows the back colour) everywhere.
    REG32(VDP1_VRAM + VRAM_CLEAR_TEX) = 0;
    REG32(VDP1_VRAM + VRAM_CLEAR_TEX + 4) = 0;
    {
        uint32* w = gCmds[0].w;
        w[0] = CMD_SCALED_SPRITE << 16;
        w[1] = pair(PMOD_SPD_OFF, 0);
        w[2] = pair(VRAM_CLEAR_TEX >> 3, (1 << 8) | 1);
        w[3] = pair(0, 0);
        w[5] = pair(FRAME_WIDTH - 1, FRAME_HEIGHT - 1);
        w[7] = 0;
    }

    // slots 0-2: system clip, user clip, local coordinates; frame commands follow
    for (int32 i = 0; i < 3; i++) {
        gVdp1Cmds[i].link = 0;
        gVdp1Cmds[i].pmod = 0;
    }
    gVdp1Cmds[0].ctrl = CMD_SYSTEM_CLIP;
    gVdp1Cmds[0].xc = FRAME_WIDTH - 1;
    gVdp1Cmds[0].yc = FRAME_HEIGHT - 1;
    gVdp1Cmds[1].ctrl = CMD_USER_CLIP;
    gVdp1Cmds[1].xa = 0;
    gVdp1Cmds[1].ya = 0;
    gVdp1Cmds[1].xc = FRAME_WIDTH - 1;
    gVdp1Cmds[1].yc = FRAME_HEIGHT - 1;
    gVdp1Cmds[2].ctrl = CMD_LOCAL_COORD;
    gVdp1Cmds[2].xa = 0;
    gVdp1Cmds[2].ya = 0;
    ((volatile vdp1_cmd_t*)(VDP1_VRAM + VRAM_FRAME_CMDS))->ctrl = CMD_END;
}

void renderFree()
{
}

// flush() hands each frame to VDP1; frame buffers change in the vblank
// interrupts (system.cpp).
void renderSwap()
{
}

// Each texture's mean luma, for shadeDark.
static void textureLuma()
{
    for (int32 i = 0; i < level.texturesCount; i++)
    {
        Texture &t = level.textures[i];
        uint32 c = t.color;
        t.attr = (t.attr & 0xFF) | (((c & 31) + ((c >> 5) & 31) + ((c >> 10) & 31)) << 8);
    }
}

void renderPalettesSwapped()
{
    for (int32 i = 0; i < level.texturesCount; i++)
    {
        Texture &t = level.textures[i];
        uint16 c = t.color;
        t.color = t.altColor;
        t.altColor = c;
    }
    textureLuma();
    buildContrast();
}

// The level's texture entries, moved from low work RAM to where the CPUs read
// faster.
#define MAX_LEVEL_TEXTURES 1344 // LEVEL4 has 1313
static Texture gTextures[MAX_LEVEL_TEXTURES];

void renderLevelInit()
{
    for (int32 i = 0; i < int32(sizeof(gHalfLevel)); i++) {
        gHalfLevel[i] = X_CLAMP(i - 16, 0, 31);
    }

    if (level.texturesCount <= MAX_LEVEL_TEXTURES)
    {
        memcpy(gTextures, level.textures, level.texturesCount * sizeof(Texture));
        level.textures = gTextures;
    }

    if (palettesRemastered())
    {
        for (int32 i = 0; i < level.texturesCount; i++)
        {
            Texture &t = level.textures[i];
            uint16 c = t.color;
            t.color = t.altColor;
            t.altColor = c;
        }
    }
    textureLuma();

    // room textures' tables of less contrast at the top of VDP1 VRAM
    int32 rooms = 0;
    for (int32 i = 0; i < level.texturesCount; i++) {
        rooms += level.textures[i].reserved != 0;
    }
    uint32 vram = rooms ? detailReserveTop(rooms * CONTRAST_TABLES * 32) : 0;
    for (int32 i = 0; i < level.texturesCount; i++)
    {
        Texture &t = level.textures[i];
        t.contrast = 0;
        if (vram && t.reserved)
        {
            t.contrast = vram >> 3;
            vram += CONTRAST_TABLES * 32;
        }
    }

    vram = detailReserveTop(2 * SHADE_TABLES * SHADE_TABLE_SIZE);
    for (int32 i = 0; i < 2; i++)
    {
        gShadeCaches[i].vram = vram ? vram + i * SHADE_TABLES * SHADE_TABLE_SIZE : 0;
        gShadeCaches[i].miss = shadeTableMiss;
    }
    buildContrast();
}

void renderLevelFree()
{
}

void clear()
{
}

void renderRoom(Room* room)
{
    uint32 start = timerTicks();
    roomsQueue(room);
    gRoomTicks += (timerTicks() - start) & 0xFFFF;
}

void renderMesh(const Mesh* mesh)
{
    int32 vCount = mesh->vCount;
    if (vCount <= 0)
        return;

    if ((gVerticesBase - gVertices) + vCount > MASTER_VERTICES)
    {
        ASSERT(false);
        return;
    }

    int32 fCount = mesh->rCount + mesh->tCount;
    if ((gFaceList.next - gFaces) + fCount > MAX_FACES)
    {
        ASSERT(false);
        return;
    }

    uint32 start = timerTicks();

    const uint8* ptr = (uint8*)mesh + sizeof(Mesh);

    const MeshVertex* vertices = (MeshVertex*)ptr;
    ptr += vCount * sizeof(vertices[0]);

    const int8* normals = NULL;
    const uint8* shades = NULL;
    if (mesh->hasNormals == MESH_LIGHT_NORMALS)
    {
        normals = (const int8*)ptr;
        ptr += vCount * 4;
    }
    else if (mesh->hasNormals == MESH_LIGHT_SHADES)
    {
        shades = ptr;
        ptr += (vCount + 1) & ~1;
    }

    MeshQuad* quads = (MeshQuad*)ptr;
    ptr += mesh->rCount * sizeof(MeshQuad);

    MeshTriangle* triangles = (MeshTriangle*)ptr;

    bool inside = transformMesh(vertices, vCount, mesh->intensity, normals, shades, &mesh->center, mesh->bound);

    if (gFlatDist)
    {
        if (mesh->rCount > 0) {
            faceAddMeshQuads(quads, mesh->rCount);
        }
        if (mesh->tCount > 0) {
            faceAddMeshTriangles(triangles, mesh->tCount);
        }
    }
    else
    {
        MeshFaces mf = { gVerticesBase, -int32(uint32(gVertices) >> 3), &gFaceList, gMeshFaceFlags, inside };
        if (mesh->rCount > 0) {
            faceAddMeshQuadsAsm(quads, mesh->rCount, &mf);
        }
        if (mesh->tCount > 0) {
            faceAddMeshTrianglesAsm(triangles, mesh->tCount, &mf);
        }
    }

    gVerticesBase += vCount;
    gMeshTicks += (timerTicks() - start) & 0xFFFF;
}

// An octagon on the floor under the item, darkening what is drawn below it.
static const MeshQuad gShadowQuads[] = {
    { (FACE_TYPE_SHADOW << FACE_TYPE_SHIFT), {0, 1, 2, 7} },
    { (FACE_TYPE_SHADOW << FACE_TYPE_SHIFT), {7, 2, 3, 6} },
    { (FACE_TYPE_SHADOW << FACE_TYPE_SHIFT), {6, 3, 4, 5} }
};

void renderShadow(int32 x, int32 z, int32 sx, int32 sz)
{
    if (gVerticesBase - gVertices + 8 > MASTER_VERTICES || gFaceList.next - gFaces + 3 > MAX_FACES)
    {
        ASSERT(false);
        return;
    }

    int16 xns1 = x - sx;
    int16 xps1 = x + sx;
    int16 xns2 = xns1 - sx;
    int16 xps2 = xps1 + sx;

    int16 zns1 = z - sz;
    int16 zps1 = z + sz;
    int16 zns2 = zns1 - sz;
    int16 zps2 = zps1 + sz;

    MeshVertex v[8];
    v[0].x = xns1; v[0].y = 0; v[0].z = zps2;
    v[1].x = xps1; v[1].y = 0; v[1].z = zps2;
    v[2].x = xps2; v[2].y = 0; v[2].z = zps1;
    v[3].x = xps2; v[3].y = 0; v[3].z = zns1;
    v[4].x = xps1; v[4].y = 0; v[4].z = zns2;
    v[5].x = xns1; v[5].y = 0; v[5].z = zns2;
    v[6].x = xns2; v[6].y = 0; v[6].z = zns1;
    v[7].x = xns2; v[7].y = 0; v[7].z = zps1;

    transformMesh(v, 8, 0, NULL, NULL, NULL, 0);
    faceAddMeshQuads(gShadowQuads, 3);

    gVerticesBase += 8;
}

// A sprite facing the camera at a world position; the matrix is the view
// rotation.
void renderSprite(int32 vx, int32 vy, int32 vz, int32 vg, int32 index)
{
    if (gVerticesBase - gVertices + 2 > MASTER_VERTICES || gFaceList.next - gFaces + 1 > MAX_FACES)
    {
        ASSERT(false);
        return;
    }

    const Matrix &m = matrixGet();

    vx -= gCameraViewPos.x;
    vy -= gCameraViewPos.y;
    vz -= gCameraViewPos.z;

    int32 z = DP33(m.e20, m.e21, m.e22, vx, vy, vz);

    if (z < VIEW_MIN_F || z >= VIEW_MAX_F)
        return;

    int32 x = DP33(m.e00, m.e01, m.e02, vx, vy, vz) >> FIXED_SHIFT;
    int32 y = DP33(m.e10, m.e11, m.e12, vx, vy, vz) >> FIXED_SHIFT;
    z >>= FIXED_SHIFT;

    const Sprite* sprite = level.sprites + index;

    int32 l = x + sprite->l;
    int32 r = x + sprite->r;
    int32 t = y + sprite->t;
    int32 b = y + sprite->b;

    PERSPECTIVE(l, t, z);
    PERSPECTIVE(r, b, z);

    l += (FRAME_WIDTH >> 1);
    r += (FRAME_WIDTH >> 1);
    t += (FRAME_HEIGHT >> 1);
    b += (FRAME_HEIGHT >> 1);

    if (l >= viewport.x1 || r < viewport.x0 || t >= viewport.y1 || b < viewport.y0)
        return;

    if (l == r || t == b)
        return;

    if (z > FOG_MIN)
    {
        vg += (z - FOG_MIN) << FOG_SHIFT;
        if (vg > 8191) {
            vg = 8191;
        }
    }

    Vertex* v = gVerticesBase;
    v[0].x = vdp1Coord(l);
    v[0].y = vdp1Coord(t);
    v[0].g = vg >> 8;
    v[1].x = vdp1Coord(r);
    v[1].y = vdp1Coord(b);
    gVerticesBase += 2;

    // drawn in front of the geometry it stands on
    Face* f = faceAdd(gFaceList, X_MAX(0, z - 128) >> OT_SHIFT);
    f->flags = (FACE_TYPE_SPRITE << FACE_TYPE_SHIFT) | index;
    f->indices[0] = v - gVertices;
}

void renderGlyph(int32 vx, int32 vy, int32 index)
{
    if (gVerticesBase - gVertices + 2 > MASTER_VERTICES || gFaceList.next - gFaces + 1 > MAX_FACES)
    {
        ASSERT(false);
        return;
    }

    const Sprite* sprite = level.sprites + index;

    int32 l = vx + sprite->l;
    int32 r = vx + sprite->r;
    int32 t = vy + sprite->t;
    int32 b = vy + sprite->b;

    if (l == r || t == b || r < 0 || b < 0 || l >= FRAME_WIDTH || t >= FRAME_HEIGHT)
        return;

    Vertex* v = gVerticesBase;
    v[0].x = l;
    v[0].y = t;
    v[0].g = 15;    // VDP1 gouraud 16: colours unchanged
    v[1].x = r;
    v[1].y = b;
    gVerticesBase += 2;

    Face* f = faceAdd(gFaceList, 0);
    f->flags = (FACE_TYPE_SPRITE << FACE_TYPE_SHIFT) | index;
    f->indices[0] = v - gVertices;
}

static void renderRect(int32 x, int32 y, int32 width, int32 height, uint32 type, uint16 color, int32 z)
{
    if (gVerticesBase - gVertices + 2 > MASTER_VERTICES || gFaceList.next - gFaces + 1 > MAX_FACES)
    {
        ASSERT(false);
        return;
    }

    Vertex* v = gVerticesBase;
    v[0].x = x;
    v[0].y = y;
    v[0].z = int16(color);
    v[1].x = width;
    v[1].y = height;
    gVerticesBase += 2;

    Face* f = faceAdd(gFaceList, z);
    f->flags = type << FACE_TYPE_SHIFT;
    f->indices[0] = v - gVertices;
}

// Darkens the frame under the rectangle (VDP1 halves it; shade is ignored).
void renderFill(int32 x, int32 y, int32 width, int32 height, int32 shade, int32 z)
{
    renderRect(x, y, width, height, FACE_TYPE_FILL_S, 0, z);
}

// Border colours are grey levels 0..31.
void renderBorder(int32 x, int32 y, int32 width, int32 height, int32 color1, int32 color2, int32 z)
{
    uint16 c1 = RGB(color1, color1, color1);
    uint16 c2 = RGB(color2, color2, color2);
    renderRect(x + 1, y, width - 2, 1, FACE_TYPE_LINE_H, c1, z);
    renderRect(x + 1, y + height - 1, width - 2, 1, FACE_TYPE_LINE_H, c2, z);
    renderRect(x, y, 1, height, FACE_TYPE_LINE_V, c1, z);
    renderRect(x + width - 1, y, 1, height, FACE_TYPE_LINE_V, c2, z);
}

// Bar rows top to bottom; health as in the original game.
static const uint16 BAR_COLORS[BAR_MAX][BAR_HEIGHT] = {
    { RGB(10,  6,  0), RGB(15, 11,  2), RGB(10,  6,  0), RGB( 7,  3,  0), RGB( 4,  0,  0) },
    { RGB( 0,  8, 12), RGB( 4, 14, 18), RGB( 0,  8, 12), RGB( 0,  5,  9), RGB( 0,  2,  6) },
    { RGB( 4, 12,  0), RGB( 9, 18,  2), RGB( 4, 12,  0), RGB( 2,  8,  0), RGB( 0,  4,  0) },
    { RGB(14, 14, 14), RGB(20, 20, 20), RGB(14, 14, 14), RGB(10, 10, 10), RGB( 6,  6,  6) },
};

void renderBar(int32 x, int32 y, int32 width, int32 value, BarType type)
{
    int32 w = value * width >> 8;

    if (w > 0)
    {
        for (int32 i = 0; i < BAR_HEIGHT; i++) {
            renderRect(x + 1, y + 1 + i, w, 1, FACE_TYPE_LINE_H, BAR_COLORS[type][i], 0);
        }
    }

    if (w < width)
    {
        renderRect(x + 1 + w, y + 1, width - w, BAR_HEIGHT, FACE_TYPE_LINE_V, RGB(1, 1, 1), 0);
    }

    renderBorder(x, y, width + 2, BAR_HEIGHT + 2, 2, 2, 0);
}

void renderBackground(const void* background)
{
    gInventoryFrame = true;
    if (!gItemTextures)
    {
        detailSuspend();
        gItemTextures = true;
    }

    ItemObj* lara = (ItemObj*)(void*)players[0];
    if (background != &gSceneBackground || !lara)
        return;

    Camera &camera = lara->extraL->camera;
    matrixSetView(camera.view.pos, camera.angle.x, camera.angle.y);
    drawRooms(&camera);
    gDimFacesEnd = gFaceList.next;
}

void* copyBackground()
{
    return &gSceneBackground;
}
