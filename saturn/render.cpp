// Saturn renderer: OpenLara's transform -> ordering table pipeline (after the
// 32X port), with the ordering table flushed into a VDP1 command list instead
// of a software rasterizer.

#include "render.h"
#include "detail.h"
#include "hw.h"
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

// Mesh vertices are 16-bit, so the rotation multiplies are 16x16 too; the
// translation stays pre-scaled by 1 << FIXED_SHIFT. Vertices are shaded as
// TR1 does: by their normals against the brightest light (calcLightingDynamic),
// by their own shades, or all alike.
void transformMesh(const MeshVertex* vertices, int32 count, int32 intensity, const int8* normals, const uint8* shades)
{
    // the rotation's rows as 16-bit values, multiplied straight from the
    // vertex array (MeshVertex is three int16); the translation stays
    // pre-scaled by 1 << FIXED_SHIFT
    const Matrix &m = matrixGet();
    const int16 rows[9] = {
        int16(m.e00), int16(m.e01), int16(m.e02),
        int16(m.e10), int16(m.e11), int16(m.e12),
        int16(m.e20), int16(m.e21), int16(m.e22)
    };
    const int32 tx = m.e03, ty = m.e13, tz = m.e23;

    int32 vg = X_CLAMP((intensity + gLightAmbient) >> 8, 0, 31);

    // the light's direction in the mesh's own space, 128 long
    int32 lx = 0, ly = 0, lz = 0;
    if (normals && gLightRange)
    {
        const vec3i &l = gLightView;
        lx = (m.e00 * l.x + m.e10 * l.y + m.e20 * l.z) >> (FIXED_SHIFT + 7);
        ly = (m.e01 * l.x + m.e11 * l.y + m.e21 * l.z) >> (FIXED_SHIFT + 7);
        lz = (m.e02 * l.x + m.e12 * l.y + m.e22 * l.z) >> (FIXED_SHIFT + 7);
    } else {
        normals = NULL;
    }

    // Meshes are clipped to the frame, not to their room's portal window, as
    // OpenLara does: Lara climbing into a room seen through a small portal
    // would otherwise lose every face outside it.
    static const RectMinMax frame(0, 0, FRAME_WIDTH, FRAME_HEIGHT);

    Vertex* res = gVerticesBase;

    for (int32 i = 0; i < count; i++, res++, vertices++)
    {
        const int16* v = &vertices->x;

        int32 z = dot3(v, rows + 6, tz);
        uint32 clip = 0;

        if (z <= VIEW_MIN_F) {
            clip = CLIP_PLANE;
            z = VIEW_MIN_F;
        }

        if (z >= VIEW_MAX_F) {
            clip = CLIP_PLANE;
            z = VIEW_MAX_F;
        }

        // meshes reach the transform only when their sphere is in view, so
        // view x and y fit in 16 bits
        int32 x = asr16<FIXED_SHIFT>(dot3(v, rows, tx));
        int32 y = asr16<FIXED_SHIFT>(dot3(v, rows + 3, ty));

        projectView(x, y, uint32(z) >> FIXED_SHIFT, clip, frame, res);

        if (normals)
        {
            int32 d = normals[0] * lx + normals[1] * ly + normals[2] * lz;
            normals += 4;
            int32 shade = gLightAmbient - ((d * gLightRange) >> 14);
            res->g = X_CLAMP(shade >> 8, 0, 31);
        }
        else if (shades)
        {
            res->g = X_CLAMP((gLightAmbient + (*shades++ << 5)) >> 8, 0, 31);
        }
        else
        {
            res->g = vg;
        }
    }
}

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

    x >>= FIXED_SHIFT;
    y >>= FIXED_SHIFT;
    z >>= FIXED_SHIFT;

    z = PERSPECTIVE_DZ(z);
    if (z >= DIV_TABLE_SIZE) z = DIV_TABLE_SIZE - 1;
    int32 d = FixedInvU(z);
    x = (x * d) >> 12;
    y = (y * d) >> 12;
    r = (r * d) >> 12;

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
bool gSaturnLod;
bool gShadeHalf = true;
int32 gFlatDist;

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
static void texelTint(const uint32* w, uint32* grd, uint32 flags, bool room, int32 zMax)
{
    int32 dr = -6, dg = -6, db = -6;

    bool flat = ((flags >> FACE_TYPE_SHIFT) & FACE_TYPE_MASK) == FACE_TYPE_AVG;

    if (gTexelView == TEXEL_VIEW_LOD)
    {
        if (!room && !flat)
            return;
        if (flat) {
            dr = 10; dg = 10;           // flat
        } else if (flags & FACE_DETAIL) {
            dg = 10;
        } else if (zMax < DETAIL_DIST) {
            dr = 12;
        } else {
            db = 10;
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

// gShadeHalf: a face darkened by at least a third of its texture's mean is drawn
// at half luminance, which scales its texels as the PS1 shades, and its
// gouraud levels are raised to keep each corner as bright as it was. Halving
// (t + g - 16) leaves t / 2 + (g - 16) / 2, so for corner level L and mean m
// per channel, g = m + 2 (L - 16) + 16 = m + 2L - 16, which gHalfLevel holds
// within 0 .. 31 (indexed by m + 2L). Texture.attr's high byte holds the
// texture's mean luma (renderLevelInit). False for a face drawn as usual.
static uint8 gHalfLevel[32 + 64];

X_INLINE uint32 halfGouraud(const uint8* r, const uint8* g, const uint8* b, int32 level)
{
    return r[level * 2] | (g[level * 2] << 5) | (b[level * 2] << 10);
}

X_INLINE bool shadeHalf(uint32* w, uint32* grd, uint32 flags, const Vertex* a, const Vertex* b, const Vertex* c, const Vertex* d)
{
    const Texture* tex = level.textures + (flags & FACE_TEXTURE);
    int32 la = gShadeLevel[a->g];
    int32 lb = gShadeLevel[b->g];
    int32 lc = gShadeLevel[c->g];
    int32 ld = gShadeLevel[d->g];

    // average darkening (64 - sum) / 4 against a third of the mean luma / 3
    if ((64 - (la + lb + lc + ld)) * 9 < int32(tex->attr >> 8) * 4)
        return false;

    uint32 color = tex->color;
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
        const Texture* tex = level.textures + (flags & FACE_TEXTURE);
        if (tex->srca)
        {
            uint32 pmod = PMOD_LUT16 | PMOD_GOURAUD;
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
                    srca = detailSlotAddr(slot) >> 3;
                    size = ((size >> 8) << 9) | ((size & 0xFF) << 1);
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
        color = tex->color;
    }

    w[0] = CMD_POLYGON << 16;
    w[1] = pair(PMOD_NO_ECD_SPD | PMOD_GOURAUD, color);
}

void flush_c()
{
    uint32 start = timerTicks();

    RoomStats rooms = {};
    Face** roomOT = roomsFinish(rooms);
    gStats.slaveWaitTicks = (timerTicks() - start) & 0xFFFF;

    gStats.vertices = (gVerticesBase - gVertices) + rooms.vertices;
    gStats.roomVertices = rooms.vertices;
    gStats.detailFaces = rooms.detailFaces;
    gStats.detailMisses = rooms.detailMisses;
    gStats.nearNoDetail = rooms.nearNoDetail;
    gStats.nearTriangles = rooms.nearTriangles;
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

    uint32 grdBase = VRAM_GOURAUD_BASE >> 3;
    int32 count = 1; // gCmds[0] clears the frame (renderInit)

    // far to near; in each bucket rooms (the slave's) before what stands in them
    for (int32 i = OT_SIZE - 1; i >= 0; i--)
    for (int32 list = 0; list < 2; list++)
    {
        Face** ot = list ? gOT : roomOT;
        if (!ot)
            continue;

        for (Face* face = ot[i]; face; face = face->next)
        {
            uint32 flags = face->flags;
            uint32 type = (flags >> FACE_TYPE_SHIFT) & FACE_TYPE_MASK;

            // rooms, and what was drawn with them, behind the inventory
            bool dim = gInventoryFrame && (!list || face < gDimFacesEnd);

            if (count == MAX_CMDS - 1)
                break;

            const Vertex* v0 = gVertices + face->indices[0];

            uint32* w = gCmds[count].w;
            uint32* grd = gGouraud[count];
            w[7] = pair(grdBase + count, 0);

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

                        bool half = gShadeHalf && type != FACE_TYPE_F && type != FACE_TYPE_SHADOW && !dim && !(flags & FACE_WATER);

                        if (flags & FACE_V0_AT_D)
                        {
                            w[3] = v1->xy;
                            w[4] = p;
                            w[5] = v2->xy;
                            w[6] = v0->xy;
                            if (!(half && shadeHalf(w, grd, flags, v1, v1, v2, v0)))
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
                            if (!(half && shadeHalf(w, grd, flags, v0, v1, v2, v2)))
                            {
                                grd[0] = gouraudPair(flags, dim, v0, v1);
                                grd[1] = gouraudPair(flags, dim, v2, v2);
                            }
                        }

                        if (gTexelView == TEXEL_VIEW_SIZE && type != FACE_TYPE_F && type != FACE_TYPE_SHADOW) {
                            texelTint(w, grd, flags, false, 0);
                        }
                        break;
                    }

                    const Vertex* v3 = gVertices + face->indices[3];
                    w[3] = v0->xy;
                    w[4] = v1->xy;
                    w[5] = v2->xy;
                    w[6] = v3->xy;
                    bool half = gShadeHalf && type != FACE_TYPE_F && type != FACE_TYPE_SHADOW && !dim && !(flags & FACE_WATER);
                    if (!(half && shadeHalf(w, grd, flags, v0, v1, v2, v3)))
                    {
                        grd[0] = gouraudPair(flags, dim, v0, v1);
                        grd[1] = gouraudPair(flags, dim, v2, v3);
                    }
                    if (gTexelView && type != FACE_TYPE_F && type != FACE_TYPE_SHADOW) {
                        texelTint(w, grd, flags, !list, X_MAX(X_MAX(v0->z, v1->z), X_MAX(v2->z, v3->z)));
                    }
                }
            }

            count++;
        }
        ot[i] = NULL;
    }

    gCmds[count].w[0] = CMD_END << 16;
    gRenderFaceCount = count;
    gStats.faces = count;

    uint32 waitStart = timerTicks();
    vdp1WaitIdle();
    gStats.waitTicks = (timerTicks() - waitStart) & 0xFFFF;

    detailFlush();
    if (gItemTextures && !gInventoryFrame)
    {
        detailResume();
        gItemTextures = false;
    }
    gInventoryFrame = false;
    gDimFacesEnd = gFaces;

    dmaCopy(gCmds, VDP1_VRAM + VRAM_FRAME_CMDS, (count + 1) * sizeof(Command));
    dmaCopy(gGouraud, VDP1_VRAM + VRAM_GOURAUD_BASE, count * sizeof(gGouraud[0]));
    dmaWait();
    vdp1Plot();

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

void renderLevelInit()
{
    for (int32 i = 0; i < int32(sizeof(gHalfLevel)); i++) {
        gHalfLevel[i] = X_CLAMP(i - 16, 0, 31);
    }

    // each texture's mean luma, for shadeHalf
    for (int32 i = 0; i < level.texturesCount; i++)
    {
        Texture &t = level.textures[i];
        uint32 c = t.color;
        t.attr = (t.attr & 0xFF) | (((c & 31) + ((c >> 5) & 31) + ((c >> 10) & 31)) << 8);
    }
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

    uint32 start = timerTicks();
    transformMesh(vertices, vCount, mesh->intensity, normals, shades);

    if (mesh->rCount > 0) {
        faceAddMeshQuads(quads, mesh->rCount);
    }

    if (mesh->tCount > 0) {
        faceAddMeshTriangles(triangles, mesh->tCount);
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

    transformMesh(v, 8, 0, NULL, NULL);
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
