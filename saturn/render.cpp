// Saturn renderer: OpenLara's transform -> ordering table pipeline (after the
// 32X port), with the ordering table flushed into a VDP1 command list instead
// of a software rasterizer.

#include "common.h"
#include "detail.h"
#include "hw.h"
#include "system.h"
#include "vram.h"

static_assert(FRAME_WIDTH == SCREEN_WIDTH && FRAME_HEIGHT == SCREEN_HEIGHT,
              "OpenLara frame size must match the display mode");

struct Vertex
{
    int16 x;
    int16 y;
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
    FACE_TYPE_MAX
};

#define FACE_TRIANGLE   (1 << 31)
#define FACE_CLIPPED    (1 << 30)
#define FACE_DETAIL     (1 << 29)   // full-res quadrant of a cached room texture
#define FACE_PARALLELOGRAM  (1 << 28)   // mesh triangle drawn as a parallelogram, see faceAddMeshTriangles
#define FACE_V0_AT_D        (1 << 27)   // ... with its first vertex at corner D (else A)
#define FACE_DETAIL_SLOT_SHIFT  20
#define FACE_DETAIL_QUAD_SHIFT  18
#define FACE_TYPE_SHIFT 14
#define FACE_TYPE_MASK  15
#define FACE_GOURAUD    (2 << FACE_TYPE_SHIFT)
#define FACE_TEXTURE    0x3FFF

// VDP1 vertex coordinates are 11 bits plus sign
#define VDP1_COORD_MIN  -1024
#define VDP1_COORD_MAX  1023

// 2D faces keep their rectangle in two vertices: position, then size. Lines
// keep their RGB colour in the first vertex's z.
#define BAR_HEIGHT      5

enum ClipFlags {
    CLIP_FRAME   = 1 << 0,
    CLIP_LEFT    = 1 << 1,
    CLIP_RIGHT   = 1 << 2,
    CLIP_TOP     = 1 << 3,
    CLIP_BOTTOM  = 1 << 4,
    CLIP_PLANE   = 1 << 5,
    CLIP_DISCARD = (CLIP_LEFT | CLIP_RIGHT | CLIP_TOP | CLIP_BOTTOM | CLIP_PLANE)
};

#define MAX_CMDS            VRAM_MAX_CMDS

// Room quads whose farthest vertex is nearer than this are split 2x2 and drawn
// with full-res textures, as in the original game.
#define DETAIL_DIST         4096

extern Level level;

Vertex* gVerticesBase;
Face* gFacesBase;

ALIGN16 Vertex gVertices[MAX_VERTICES];
ALIGN16 Face gFaces[MAX_FACES];
Face* gOT[OT_SIZE];

// Built in work RAM while VDP1 draws the previous frame, then DMAed to VRAM.
static ALIGN16 vdp1_cmd_t gCmds[MAX_CMDS];
static ALIGN16 uint16 gGouraud[MAX_CMDS][4];

int32 gRenderFaceCount; // VDP1 polygons emitted by the last flush

static uint32 gRoomTicks;
static uint32 gMeshTicks;

// The room being drawn: its vertices, and where vertices made by splitting
// its faces go (after the room's own).
static const RoomVertex* gRoomVertices;
static Vertex* gExtraVertices;

static volatile vdp1_cmd_t* const gVdp1Cmds = (volatile vdp1_cmd_t*)VDP1_VRAM;

void setViewport(const RectMinMax &vp)
{
    viewport = vp;
}

void setPaletteIndex(int32 index)
{
    // TODO underwater tint via VDP2 color offset
}

X_INLINE Face* faceAdd(int32 depth)
{
    ASSERT(depth >= 0 && depth < OT_SIZE);

    Face* face = gFacesBase++;
    face->next = gOT[depth];
    gOT[depth] = face;

    return face;
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

X_INLINE uint32 clipFlags(int32 x, int32 y)
{
    uint32 clip = 0;

    if ((x < 0 || x > FRAME_WIDTH) || (y < 0 || y > FRAME_HEIGHT)) {
        clip |= CLIP_FRAME;
    }

    if (x < viewport.x0) clip |= CLIP_LEFT;
    if (x > viewport.x1) clip |= CLIP_RIGHT;
    if (y < viewport.y0) clip |= CLIP_TOP;
    if (y > viewport.y1) clip |= CLIP_BOTTOM;

    return clip;
}

// Projects a point in room space (vertex units << 8) with the room's matrix;
// sets everything but the shade.
X_INLINE void projectRoomPoint(int32 vx, int32 vy, int32 vz, Vertex* res)
{
    const Matrix &m = matrixGet();
    int32 x = DP43(m.e00, m.e01, m.e02, m.e03, vx, vy, vz);
    int32 y = DP43(m.e10, m.e11, m.e12, m.e13, vx, vy, vz);
    int32 z = DP43(m.e20, m.e21, m.e22, m.e23, vx, vy, vz);

    uint32 clip = 0;

    if (z <= VIEW_MIN_F) {
        clip = CLIP_PLANE;
        z = VIEW_MIN_F;
    }

    if (z >= VIEW_MAX_F) {
        clip = CLIP_PLANE;
        z = VIEW_MAX_F;
    }

    x >>= FIXED_SHIFT;
    y >>= FIXED_SHIFT;
    z >>= FIXED_SHIFT;

    PERSPECTIVE(x, y, z);

    x += (FRAME_WIDTH  >> 1);
    y += (FRAME_HEIGHT >> 1);

    res->clip = clip | clipFlags(x, y);
    res->x = X_CLAMP(x, VDP1_COORD_MIN, VDP1_COORD_MAX);
    res->y = X_CLAMP(y, VDP1_COORD_MIN, VDP1_COORD_MAX);
    res->z = z;
}

void transformRoom(const RoomVertex* vertices, int32 count, bool underwater)
{
    Vertex* res = gVerticesBase;

    for (int32 i = 0; i < count; i++, res++)
    {
        projectRoomPoint(vertices->x << 8, vertices->y << 8, vertices->z << 8, res);
        int32 vg = vertices->g << 8;
        vertices++;

        if (underwater)
        {
            int32 causticsValue = gCaustics[(gRandTable[i & (MAX_RAND_TABLE - 1)] + gCausticsFrame) & (MAX_CAUSTICS - 1)];
            vg = X_CLAMP(vg + causticsValue, 0, 8191);
        }

        int32 z = res->z;
        if (z > FOG_MIN)
        {
            vg += (z - FOG_MIN) << FOG_SHIFT;
            if (vg > 8191) {
                vg = 8191;
            }
        }

        res->g = vg >> 8;
    }
}

void transformMesh(const MeshVertex* vertices, int32 count, int32 intensity)
{
    Vertex* res = gVerticesBase;

    int32 vg = X_CLAMP((intensity + gLightAmbient) >> 8, 0, 31);

    for (int32 i = 0; i < count; i++, res++)
    {
        int32 vx = vertices->x;
        int32 vy = vertices->y;
        int32 vz = vertices->z;
        vertices++;

        const Matrix &m = matrixGet();

        // int32 matrices keep the translation pre-scaled (MATRIX_FIXED_SHIFT 0)
        int32 x = DP43(m.e00, m.e01, m.e02, m.e03, vx, vy, vz) >> FIXED_SHIFT;
        int32 y = DP43(m.e10, m.e11, m.e12, m.e13, vx, vy, vz) >> FIXED_SHIFT;
        int32 z = DP43(m.e20, m.e21, m.e22, m.e23, vx, vy, vz) >> FIXED_SHIFT;

        uint32 clip = 0;

        if (z <= (VIEW_MIN_F >> FIXED_SHIFT)) {
            clip = CLIP_PLANE;
            z = VIEW_MIN_F >> FIXED_SHIFT;
        }

        if (z >= (VIEW_MAX_F >> FIXED_SHIFT)) {
            clip = CLIP_PLANE;
            z = VIEW_MAX_F >> FIXED_SHIFT;
        }

        PERSPECTIVE(x, y, z);

        x += (FRAME_WIDTH  >> 1);
        y += (FRAME_HEIGHT >> 1);

        res->clip = clip | clipFlags(x, y);
        res->x = X_CLAMP(x, VDP1_COORD_MIN, VDP1_COORD_MAX);
        res->y = X_CLAMP(y, VDP1_COORD_MIN, VDP1_COORD_MAX);
        res->z = z;
        res->g = vg;
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

    Face* f = faceAdd(depth);
    f->flags = flags;
    f->indices[0] = v0 - gVertices;
    f->indices[1] = v1 - gVertices;
    f->indices[2] = v2 - gVertices;
    f->indices[3] = v3 - gVertices;
}

// Splits a room quad near the camera into four, each drawn with one quadrant
// of the full-res texture. The new vertices are projected from room space, so
// the split also reduces the distortion of affine texture mapping. False when
// the texture can't be cached this frame.
static bool faceAddRoomQuadDetail(const RoomQuad* poly, const Texture* tex,
                                  const Vertex* v0, const Vertex* v1, const Vertex* v2, const Vertex* v3)
{
    if ((gExtraVertices - gVertices) + 5 > MAX_VERTICES || (gFacesBase - gFaces) + 4 > MAX_FACES)
        return false;

    uint32 size = tex->size;
    uint32 quadrant = (size >> 8) * 4 * (size & 0xFF);
    int32 slot = detailSlot(tex->reserved - 1, quadrant * 4);
    if (slot < 0)
        return false;

    const RoomVertex* r0 = gRoomVertices + poly->indices[0];
    const RoomVertex* r1 = gRoomVertices + poly->indices[1];
    const RoomVertex* r2 = gRoomVertices + poly->indices[2];
    const RoomVertex* r3 = gRoomVertices + poly->indices[3];

    // edge midpoints and the centre, in room space
    Vertex* m01 = gExtraVertices;
    Vertex* m12 = m01 + 1;
    Vertex* m23 = m01 + 2;
    Vertex* m30 = m01 + 3;
    Vertex* mc  = m01 + 4;
    gExtraVertices += 5;

    projectRoomPoint((r0->x + r1->x) << 7, (r0->y + r1->y) << 7, (r0->z + r1->z) << 7, m01);
    projectRoomPoint((r1->x + r2->x) << 7, (r1->y + r2->y) << 7, (r1->z + r2->z) << 7, m12);
    projectRoomPoint((r2->x + r3->x) << 7, (r2->y + r3->y) << 7, (r2->z + r3->z) << 7, m23);
    projectRoomPoint((r3->x + r0->x) << 7, (r3->y + r0->y) << 7, (r3->z + r0->z) << 7, m30);
    projectRoomPoint((r0->x + r1->x + r2->x + r3->x) << 6,
                     (r0->y + r1->y + r2->y + r3->y) << 6,
                     (r0->z + r1->z + r2->z + r3->z) << 6, mc);

    m01->g = (v0->g + v1->g) >> 1;
    m12->g = (v1->g + v2->g) >> 1;
    m23->g = (v2->g + v3->g) >> 1;
    m30->g = (v3->g + v0->g) >> 1;
    mc->g = (v0->g + v1->g + v2->g + v3->g) >> 2;

    uint32 flags = poly->flags | FACE_DETAIL | (slot << FACE_DETAIL_SLOT_SHIFT);
    faceAddDetailQuad(flags | (0 << FACE_DETAIL_QUAD_SHIFT), v0, m01, mc, m30);
    faceAddDetailQuad(flags | (1 << FACE_DETAIL_QUAD_SHIFT), m01, v1, m12, mc);
    faceAddDetailQuad(flags | (2 << FACE_DETAIL_QUAD_SHIFT), mc, m12, v2, m23);
    faceAddDetailQuad(flags | (3 << FACE_DETAIL_QUAD_SHIFT), m30, mc, m23, v3);

    return true;
}

void faceAddRoomQuads(const RoomQuad* polys, int32 count)
{
    const Vertex* v = gVerticesBase;

    for (int32 i = 0; i < count; i++, polys++)
    {
        uint32 flags = polys->flags;
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

        int32 zMax = X_MAX(v0->z, X_MAX(v1->z, X_MAX(v2->z, v3->z)));

        if (zMax < DETAIL_DIST)
        {
            const Texture* tex = level.textures + (flags & FACE_TEXTURE);
            if (tex->reserved && faceAddRoomQuadDetail(polys, tex, v0, v1, v2, v3))
                continue;
        }

        if (cullFace(c0 & c1 & c2 & c3, c0 | c1 | c2 | c3))
            continue;

        if ((c0 | c1 | c2 | c3) & CLIP_FRAME) {
            flags |= FACE_CLIPPED;
        }

        int32 depth = zMax >> OT_SHIFT;

        Face* f = faceAdd(depth);
        f->flags = flags;
        f->indices[0] = v0 - gVertices;
        f->indices[1] = v1 - gVertices;
        f->indices[2] = v2 - gVertices;
        f->indices[3] = v3 - gVertices;
    }
}

void faceAddRoomTriangles(const RoomTriangle* polys, int32 count)
{
    const Vertex* v = gVerticesBase;

    for (int32 i = 0; i < count; i++, polys++)
    {
        uint32 flags = polys->flags;
        const Vertex* v0 = v + polys->indices[0];
        const Vertex* v1 = v + polys->indices[1];
        const Vertex* v2 = v + polys->indices[2];

        uint32 c0 = v0->clip;
        uint32 c1 = v1->clip;
        uint32 c2 = v2->clip;

        if (cullFace(c0 & c1 & c2, c0 | c1 | c2))
            continue;

        if ((c0 | c1 | c2) & CLIP_FRAME) {
            flags |= FACE_CLIPPED;
        }

        if (checkBackface(v0, v1, v2))
            continue;

        flags |= FACE_TRIANGLE;

        int32 depth = X_MAX(v0->z, X_MAX(v1->z, v2->z)) >> OT_SHIFT;

        Face* f = faceAdd(depth);
        f->flags = flags;
        f->indices[0] = v0 - gVertices;
        f->indices[1] = v1 - gVertices;
        f->indices[2] = v2 - gVertices;
        f->indices[3] = v2 - gVertices;
    }
}

void faceAddMeshQuads(const MeshQuad* polys, int32 count)
{
    const Vertex* v = gVerticesBase;

    for (int32 i = 0; i < count; i++, polys++)
    {
        uint32 flags = polys->flags;
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

        if ((c0 | c1 | c2 | c3) & CLIP_FRAME) {
            flags |= FACE_CLIPPED;
        }

        int32 depth = (v0->z + v1->z + v2->z + v3->z) >> (2 + OT_SHIFT);

        Face* f = faceAdd(depth);
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
        uint32 flags = polys->flags;
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

        if ((c0 | c1 | c2) & CLIP_FRAME) {
            flags |= FACE_CLIPPED;
        }
        flags |= FACE_TRIANGLE;

        uint32 arrangement = polys->indices[3];
        if (((flags >> FACE_TYPE_SHIFT) & FACE_TYPE_MASK) != FACE_TYPE_F)
        {
            flags |= FACE_PARALLELOGRAM;
            if (arrangement & 1) {
                flags |= FACE_V0_AT_D;
            }
            if (arrangement & 2) {
                const Vertex* t = v1; v1 = v2; v2 = t;
            }
        }

        int32 depth = (v0->z + v1->z + v2->z + v2->z) >> (2 + OT_SHIFT);

        Face* f = faceAdd(depth);
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

// TR shade 0 (lit) .. 31 (dark) as a VDP1 gouraud entry (16 = colour unchanged)
X_INLINE uint16 shadeGouraud(uint32 g)
{
    uint32 c = 31 - g;
    return (c << 10) | (c << 5) | c;
}

#define TEX_ATTR_DIR  0x0030 // VDP1 flip bits, stored in CMDCTRL layout

X_INLINE int16 vdp1Coord(int32 v)
{
    return X_CLAMP(v, VDP1_COORD_MIN, VDP1_COORD_MAX);
}

X_INLINE void setRect(vdp1_cmd_t* cmd, const Vertex* pos, const Vertex* size)
{
    int16 x0 = pos->x;
    int16 y0 = pos->y;
    int16 x1 = x0 + size->x - 1;
    int16 y1 = y0 + size->y - 1;
    cmd->xa = x0; cmd->ya = y0;
    cmd->xb = x1; cmd->yb = y0;
    cmd->xc = x1; cmd->yc = y1;
    cmd->xd = x0; cmd->yd = y1;
}

// Fills the command fields that depend on the face's colour or texture.
X_INLINE void faceMaterial(vdp1_cmd_t* cmd, uint32 flags)
{
    uint32 type = (flags >> FACE_TYPE_SHIFT) & FACE_TYPE_MASK;
    const Texture* tex = (type == FACE_TYPE_F) ? NULL : level.textures + (flags & FACE_TEXTURE);

    if (tex && tex->srca)
    {
        cmd->ctrl = CMD_DISTORTED_SPRITE | (tex->attr & TEX_ATTR_DIR);
        cmd->pmod = PMOD_ECD_OFF | PMOD_LUT16 | PMOD_GOURAUD;
        if (!(tex->attr & TEX_ATTR_AKILL)) {
            cmd->pmod |= PMOD_SPD_OFF; // colour 0 is opaque
        }
        cmd->colr = tex->colr;
        cmd->srca = tex->srca;
        cmd->size = tex->size;

        if (flags & FACE_PARALLELOGRAM) {
            cmd->pmod &= ~PMOD_ECD_OFF; // end codes trim the parallelogram to the triangle
        }

        if (flags & FACE_DETAIL)
        {
            int32 slot = (flags >> FACE_DETAIL_SLOT_SHIFT) & 0xFF;
            uint32 quad = (flags >> FACE_DETAIL_QUAD_SHIFT) & 3;
            uint32 quadSize = (tex->size >> 8) * 4 * (tex->size & 0xFF);
            cmd->srca = (detailSlotAddr(slot) + quad * quadSize) >> 3;
        }
    }
    else
    {
        cmd->ctrl = CMD_POLYGON;
        cmd->pmod = PMOD_NO_ECD_SPD | PMOD_GOURAUD;
        cmd->colr = tex ? tex->color : level.palette[flags & FACE_TEXTURE];
    }
}

void flush_c()
{
    uint32 start = timerTicks();

    gStats.vertices = gVerticesBase - gVertices;
    gStats.roomTicks = gRoomTicks;
    gStats.meshTicks = gMeshTicks;
    gRoomTicks = gMeshTicks = 0;

    gVerticesBase = gVertices;

    if (gFacesBase == gFaces)
        return;

    gFacesBase = gFaces;

    uint32 grdBase = VRAM_GOURAUD_BASE >> 3;
    int32 count = 0;

    for (int32 i = OT_SIZE - 1; i >= 0; i--)
    {
        for (Face* face = gOT[i]; face; face = face->next)
        {
            uint32 flags = face->flags;
            uint32 type = (flags >> FACE_TYPE_SHIFT) & FACE_TYPE_MASK;

            if (count == MAX_CMDS - 1)
                break;

            const Vertex* v0 = gVertices + face->indices[0];

            vdp1_cmd_t* cmd = gCmds + count;
            cmd->link = 0;
            cmd->grda = grdBase + count;
            uint16* grd = gGouraud[count];

            switch (type)
            {
                case FACE_TYPE_SPRITE:
                {
                    const Sprite* sprite = level.sprites + (flags & FACE_TEXTURE);
                    cmd->ctrl = CMD_SCALED_SPRITE | (sprite->attr & TEX_ATTR_DIR);
                    cmd->pmod = PMOD_ECD_OFF | PMOD_LUT16 | PMOD_GOURAUD;
                    cmd->colr = sprite->colr;
                    cmd->srca = sprite->srca;
                    cmd->size = sprite->size;
                    cmd->xa = v0[0].x; cmd->ya = v0[0].y;
                    cmd->xc = v0[1].x; cmd->yc = v0[1].y;
                    grd[0] = grd[1] = grd[2] = grd[3] = shadeGouraud(v0->g);
                    break;
                }

                case FACE_TYPE_FILL_S:
                    cmd->ctrl = CMD_POLYGON;
                    cmd->pmod = PMOD_NO_ECD_SPD | PMOD_SHADOW;
                    cmd->colr = RGB(0, 0, 0);
                    setRect(cmd, v0, v0 + 1);
                    break;

                case FACE_TYPE_LINE_H:
                case FACE_TYPE_LINE_V:
                    cmd->ctrl = CMD_POLYGON;
                    cmd->pmod = PMOD_NO_ECD_SPD;
                    cmd->colr = uint16(v0->z);
                    setRect(cmd, v0, v0 + 1);
                    break;

                default:
                {
                    const Vertex* v1 = gVertices + face->indices[1];
                    const Vertex* v2 = gVertices + face->indices[2];
                    const Vertex* v3 = gVertices + face->indices[3];

                    if (type == FACE_TYPE_SHADOW)
                    {
                        cmd->ctrl = CMD_POLYGON;
                        cmd->pmod = PMOD_NO_ECD_SPD | PMOD_SHADOW;
                        cmd->colr = RGB(0, 0, 0);
                    }
                    else
                    {
                        faceMaterial(cmd, flags);
                    }

                    if (flags & FACE_PARALLELOGRAM)
                    {
                        // v0, x, y; the fourth corner is x + y - v0
                        int16 px = vdp1Coord(v1->x + v2->x - v0->x);
                        int16 py = vdp1Coord(v1->y + v2->y - v0->y);
                        uint16 g0 = shadeGouraud(v0->g);
                        uint16 g1 = shadeGouraud(v1->g);
                        uint16 g2 = shadeGouraud(v2->g);

                        if (flags & FACE_V0_AT_D)
                        {
                            cmd->xa = v1->x; cmd->ya = v1->y;
                            cmd->xb = px;    cmd->yb = py;
                            cmd->xc = v2->x; cmd->yc = v2->y;
                            cmd->xd = v0->x; cmd->yd = v0->y;
                            grd[0] = g1; grd[1] = g1; grd[2] = g2; grd[3] = g0;
                        }
                        else
                        {
                            cmd->xa = v0->x; cmd->ya = v0->y;
                            cmd->xb = v1->x; cmd->yb = v1->y;
                            cmd->xc = px;    cmd->yc = py;
                            cmd->xd = v2->x; cmd->yd = v2->y;
                            grd[0] = g0; grd[1] = g1; grd[2] = g2; grd[3] = g2;
                        }
                        break;
                    }

                    cmd->xa = v0->x; cmd->ya = v0->y;
                    cmd->xb = v1->x; cmd->yb = v1->y;
                    cmd->xc = v2->x; cmd->yc = v2->y;
                    cmd->xd = v3->x; cmd->yd = v3->y;

                    grd[0] = shadeGouraud(v0->g);
                    grd[1] = shadeGouraud(v1->g);
                    grd[2] = shadeGouraud(v2->g);
                    grd[3] = shadeGouraud(v3->g);
                }
            }

            count++;
        }
        gOT[i] = NULL;
    }

    gCmds[count].ctrl = CMD_END;
    gRenderFaceCount = count;
    gStats.faces = count;

    uint32 waitStart = timerTicks();
    vdp1WaitIdle();
    gStats.waitTicks = (timerTicks() - waitStart) & 0xFFFF;

    detailFlush();
    dmaCopy(gCmds, VDP1_VRAM + VRAM_FRAME_CMDS, (count + 1) * sizeof(vdp1_cmd_t));
    dmaCopy(gGouraud, VDP1_VRAM + VRAM_GOURAUD_BASE, count * 8);
    dmaWait();
    vdp1Plot();

    gStats.flushTicks = ((timerTicks() - start) & 0xFFFF) - gStats.waitTicks;
}

void renderInit()
{
    gVerticesBase = gVertices;
    gFacesBase = gFaces;

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
}

void renderLevelFree()
{
}

void clear()
{
}

void renderRoom(Room* room)
{
    int32 vCount = room->info->verticesCount;
    if (vCount <= 0)
        return;

    uint32 start = timerTicks();

    if ((gVerticesBase - gVertices) + vCount > MAX_VERTICES)
    {
        ASSERT(false);
        return;
    }

    transformRoom(room->data.vertices, vCount, ROOM_FLAG_WATER(room->info->flags));

    gRoomVertices = room->data.vertices;
    gExtraVertices = gVerticesBase + vCount;

    if (room->info->quadsCount > 0) {
        faceAddRoomQuads(room->data.quads, room->info->quadsCount);
    }

    if (room->info->trianglesCount > 0) {
        faceAddRoomTriangles(room->data.triangles, room->info->trianglesCount);
    }

    gVerticesBase = gExtraVertices;
    gRoomTicks += (timerTicks() - start) & 0xFFFF;
}

void renderMesh(const Mesh* mesh)
{
    int32 vCount = mesh->vCount;
    if (vCount <= 0)
        return;

    if ((gVerticesBase - gVertices) + vCount > MAX_VERTICES)
    {
        ASSERT(false);
        return;
    }

    int32 fCount = mesh->rCount + mesh->tCount;
    if ((gFacesBase - gFaces) + fCount > MAX_FACES)
    {
        ASSERT(false);
        return;
    }

    const uint8* ptr = (uint8*)mesh + sizeof(Mesh);

    const MeshVertex* vertices = (MeshVertex*)ptr;
    ptr += vCount * sizeof(vertices[0]);

    MeshQuad* quads = (MeshQuad*)ptr;
    ptr += mesh->rCount * sizeof(MeshQuad);

    MeshTriangle* triangles = (MeshTriangle*)ptr;

    uint32 start = timerTicks();
    transformMesh(vertices, vCount, mesh->intensity);

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
    if (gVerticesBase - gVertices + 8 > MAX_VERTICES || gFacesBase - gFaces + 3 > MAX_FACES)
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

    transformMesh(v, 8, 0);
    faceAddMeshQuads(gShadowQuads, 3);

    gVerticesBase += 8;
}

// A sprite facing the camera at a world position; the matrix is the view
// rotation.
void renderSprite(int32 vx, int32 vy, int32 vz, int32 vg, int32 index)
{
    if (gVerticesBase - gVertices + 2 > MAX_VERTICES || gFacesBase - gFaces + 1 > MAX_FACES)
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
    Face* f = faceAdd(X_MAX(0, z - 128) >> OT_SHIFT);
    f->flags = (FACE_TYPE_SPRITE << FACE_TYPE_SHIFT) | index;
    f->indices[0] = v - gVertices;
}

void renderGlyph(int32 vx, int32 vy, int32 index)
{
    if (gVerticesBase - gVertices + 2 > MAX_VERTICES || gFacesBase - gFaces + 1 > MAX_FACES)
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

    Face* f = faceAdd(0);
    f->flags = (FACE_TYPE_SPRITE << FACE_TYPE_SHIFT) | index;
    f->indices[0] = v - gVertices;
}

static void renderRect(int32 x, int32 y, int32 width, int32 height, uint32 type, uint16 color, int32 z)
{
    if (gVerticesBase - gVertices + 2 > MAX_VERTICES || gFacesBase - gFaces + 1 > MAX_FACES)
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

    Face* f = faceAdd(z);
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
    // TODO
}

void* copyBackground()
{
    return NULL;
}
