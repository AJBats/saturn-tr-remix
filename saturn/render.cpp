// Saturn renderer: OpenLara's transform -> ordering table pipeline (after the
// 32X port), with the ordering table flushed into a VDP1 command list instead
// of a software rasterizer.

#include "common.h"
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

#define MAX_CMDS            VRAM_MAX_CMDS

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

static volatile vdp1_cmd_t* const gVdp1Cmds = (volatile vdp1_cmd_t*)VDP1_VRAM;

static void dmaToVram(const void* src, uint32 dst, uint32 size)
{
    while (SCU_DSTA & DSTA_D0_BUSY);
    SCU_D0R = uint32(src);
    SCU_D0W = dst;
    SCU_D0C = size;
    SCU_D0AD = 0x101;   // read +4, write +4
    SCU_D0MD = 0x07;    // direct mode, start on CPU request
    SCU_D0EN = 0x101;   // enable + start
}

static void dmaWait()
{
    while (SCU_DSTA & DSTA_D0_BUSY);
}

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

void transformRoom(const RoomVertex* vertices, int32 count, bool underwater)
{
    Vertex* res = gVerticesBase;

    for (int32 i = 0; i < count; i++, res++)
    {
        int32 vx = vertices->x << 8;
        int32 vy = vertices->y << 8;
        int32 vz = vertices->z << 8;
        int32 vg = vertices->g << 8;
        vertices++;

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

        if (underwater)
        {
            int32 causticsValue = gCaustics[(gRandTable[i & (MAX_RAND_TABLE - 1)] + gCausticsFrame) & (MAX_CAUSTICS - 1)];
            vg = X_CLAMP(vg + causticsValue, 0, 8191);
        }

        x >>= FIXED_SHIFT;
        y >>= FIXED_SHIFT;
        z >>= FIXED_SHIFT;

        if (z > FOG_MIN)
        {
            vg += (z - FOG_MIN) << FOG_SHIFT;
            if (vg > 8191) {
                vg = 8191;
            }
        }

        PERSPECTIVE(x, y, z);

        x += (FRAME_WIDTH  >> 1);
        y += (FRAME_HEIGHT >> 1);

        res->clip = clip | clipFlags(x, y);
        res->x = X_CLAMP(x, VDP1_COORD_MIN, VDP1_COORD_MAX);
        res->y = X_CLAMP(y, VDP1_COORD_MIN, VDP1_COORD_MAX);
        res->z = z;
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

        if (cullFace(c0 & c1 & c2 & c3, c0 | c1 | c2 | c3))
            continue;

        if ((c0 | c1 | c2 | c3) & CLIP_FRAME) {
            flags |= FACE_CLIPPED;
        }

        if (checkBackface(v0, v1, v2))
            continue;

        int32 depth = X_MAX(v0->z, X_MAX(v1->z, X_MAX(v2->z, v3->z))) >> OT_SHIFT;

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

            if (type > FACE_TYPE_GTA)
                continue; // TODO sprites, HUD

            if (count == MAX_CMDS - 1)
                break;

            const Vertex* v0 = gVertices + face->indices[0];
            const Vertex* v1 = gVertices + face->indices[1];
            const Vertex* v2 = gVertices + face->indices[2];
            const Vertex* v3 = gVertices + face->indices[3];

            vdp1_cmd_t* cmd = gCmds + count;
            cmd->link = 0;
            faceMaterial(cmd, flags);
            cmd->xa = v0->x; cmd->ya = v0->y;
            cmd->xb = v1->x; cmd->yb = v1->y;
            cmd->xc = v2->x; cmd->yc = v2->y;
            cmd->xd = v3->x; cmd->yd = v3->y;
            cmd->grda = grdBase + count;

            uint16* grd = gGouraud[count];
            grd[0] = shadeGouraud(v0->g);
            grd[1] = shadeGouraud(v1->g);
            grd[2] = shadeGouraud(v2->g);
            grd[3] = shadeGouraud(v3->g);

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

    dmaToVram(gCmds, VDP1_VRAM + VRAM_FRAME_CMDS, (count + 1) * sizeof(vdp1_cmd_t));
    dmaToVram(gGouraud, VDP1_VRAM + VRAM_GOURAUD_BASE, count * 8);
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

    if (room->info->quadsCount > 0) {
        faceAddRoomQuads(room->data.quads, room->info->quadsCount);
    }

    if (room->info->trianglesCount > 0) {
        faceAddRoomTriangles(room->data.triangles, room->info->trianglesCount);
    }

    gVerticesBase += vCount;
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

void renderShadow(int32 x, int32 z, int32 sx, int32 sz)
{
    // TODO
}

void renderSprite(int32 vx, int32 vy, int32 vz, int32 vg, int32 index)
{
    // TODO
}

void renderGlyph(int32 vx, int32 vy, int32 index)
{
    // TODO
}

void renderFill(int32 x, int32 y, int32 width, int32 height, int32 shade, int32 z)
{
    // TODO
}

void renderBorder(int32 x, int32 y, int32 width, int32 height, int32 color1, int32 color2, int32 z)
{
    // TODO
}

void renderBar(int32 x, int32 y, int32 width, int32 value, BarType type)
{
    // TODO
}

void renderBackground(const void* background)
{
    // TODO
}

void* copyBackground()
{
    return NULL;
}
