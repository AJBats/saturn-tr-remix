// Stage 1 the game's way, the draw list (game.h): the renderer's side of the
// calls the frame made after the walk (CALL), as openlara draw.h and
// saturn/render.cpp take them: drawModel's frames (ItemObj::getFrames), its
// sphere in view (render.cpp sphereIsVisible_c) and what it hands the pose
// (the item's frames, extras and light, a mesh a node shown, its shadow);
// drawRoomStatic's sphere and place; renderSprite's and renderRect's faces.
// Each draw takes its vertices in gVertices after the one before, as the
// master's do (gVerticesBase), after the walk's fills. Compiled as the game
// compiles draw.h (main.cpp, -Os: the Makefile), on the scene's records in
// the game's own layouts (scene.h).
#include "game.h"
#include "gamematrix.h"
#include "scene.h"

#include <stddef.h>
#include <string.h>

#define FRAME_WIDTH     352
#define FRAME_HEIGHT    224
#define PROJ_SHIFT      4
#define DIV_TABLE_SIZE  1281
#define VIEW_MIN_F      (64 << FIXED_SHIFT)
#define VIEW_MAX_F      ((1024 * 20) << FIXED_SHIFT)
#define FOG_MIN         (1024 * 12)
#define FOG_SHIFT       0
#define OT_SHIFT        4
#define ANGLE_45        0x2000
#define ANGLE_SHIFT_90  14
#define ITEM_LARA       0
#define ITEM_FLAG_SHADOW    0x8000
#define WALL            (-32512)
#define STATIC_MESH_FLAG_VISIBLE    2
#define NODE_FLAG_ROT   (4 | 8 | 16)    // NODE_FLAG_ROTX, ROTY, ROTZ
#define FACE_TYPE_SHIFT 14
#define FACE_TYPE_SPRITE    6
#define SHADOW_VERTICES 8
#define VDP1_COORD_MIN  -1024
#define VDP1_COORD_MAX  1023
#define NONE            0xFFFFFFFF
#define X_MAX(a, b)     ((a) > (b) ? (a) : (b))
#define X_CLAMP(x, a, b)    ((x) < (a) ? (a) : ((x) > (b) ? (b) : (x)))
#define DP33(a0, a1, a2, b0, b1, b2)    ((a0) * (b0) + (a1) * (b1) + (a2) * (b2))

#define PERSPECTIVE(x, y, z) {\
    int32_t dz = (z >> PROJ_SHIFT);\
    if (dz >= DIV_TABLE_SIZE) dz = DIV_TABLE_SIZE - 1;\
    int32_t d = divTable[dz];\
    x = (x * d) >> (16 - PROJ_SHIFT);\
    y = (y * d) >> (16 - PROJ_SHIFT);\
}

struct RectMinMax
{
    int32_t x0, y0, x1, y1;
};

// the game's layouts (tools/gamelayout.py: the game's compiler's offsets)
static_assert(sizeof(GameItem) == 76 && offsetof(GameItem, pos) == 8 && offsetof(GameItem, angle) == 20 &&
              offsetof(GameItem, flags) == 26 && offsetof(GameItem, animIndex) == 32 &&
              offsetof(GameItem, frameIndex) == 34 && offsetof(GameItem, type) == 52 &&
              offsetof(GameItem, intensity) == 53 && offsetof(GameItem, visibleMask) == 60 &&
              offsetof(GameItem, extra) == 64, "ItemObj");
static_assert(sizeof(GameArm) == 32 && offsetof(GameArm, animIndex) == 12 && offsetof(GameArm, frameIndex) == 14 &&
              offsetof(GameArm, flash) == 16 && offsetof(GameArm, useBasis) == 29, "ExtraInfoLara::Arm");
static_assert(offsetof(GameLara, weapon) == 10 && offsetof(GameLara, head) == 12 && offsetof(GameLara, torso) == 18 &&
              offsetof(GameLara, arms) == 24 && offsetof(GameLara, meshes) == 196 && sizeof(GameLara) <= 228,
              "ExtraInfoLara");
static_assert(sizeof(ItemRecord) == 76 + 4 + 228 && sizeof(CallRecord) == 32, "tools/drawpack.py's records");
static_assert(sizeof(GameAnim) == 32 && offsetof(GameAnim, frameRate) == 4 && offsetof(GameAnim, frameBegin) == 16 &&
              offsetof(GameAnim, frameEnd) == 18, "Anim");
static_assert(sizeof(GameModel) == 8 && offsetof(GameModel, count) == 1 && offsetof(GameModel, start) == 2 &&
              offsetof(GameModel, nodeIndex) == 4, "Model");
static_assert(sizeof(GameStaticMesh) == 40 && offsetof(GameStaticMesh, meshIndex) == 2 &&
              offsetof(GameStaticMesh, flags) == 4 && offsetof(GameStaticMesh, vsXY) == 8, "StaticMesh");

// the scene's
static const CallRecord* gCalls;
static uint32_t gCallCount;
static ItemRecord* gItems;
static const GameAnim* gAnims;
static const uint16_t* gFrames;     // AFRM - AFRM_BASE: the level's animFrames as the anims count from it
static const GameModel* gModels;
static const uint16_t* gMeshIndex;
static const uint32_t* gModelNodes;
static const uint8_t* gNodes;
static const GameStaticMesh* gStatics;
static const RoomLights* gRoomLights;
static const RoomLevel* gLevel;
static const uint32_t* gMeshes;
static const uint8_t* gMeshGeometry;
static const Sprite* gSprites;
static const int32_t* gCamera;
static Matrix gView;

static RectMinMax gViewport;
static int32_t gBase;               // the next draw's first vertex
static DrawList gList;

static int32_t sphereIsVisible(int32_t sx, int32_t sy, int32_t sz, int32_t r)
{
    const Matrix &m = gView;

    if (X_MAX(sx, -sx) < r && X_MAX(sy, -sy) < r && X_MAX(sz, -sz) < r)
        return 1;

    int32_t z = DP33(m.e20, m.e21, m.e22, sx, sy, sz);

    if (z < 0)
        return 0;

    int32_t x = DP33(m.e00, m.e01, m.e02, sx, sy, sz);
    int32_t y = DP33(m.e10, m.e11, m.e12, sx, sy, sz);

    x >>= FIXED_SHIFT;
    y >>= FIXED_SHIFT;
    z >>= FIXED_SHIFT;

    z >>= PROJ_SHIFT;
    if (z >= DIV_TABLE_SIZE) z = DIV_TABLE_SIZE - 1;
    int32_t d = divTable[z];
    x = (x * d) >> 12;
    y = (y * d) >> 12;
    r = (r * d) >> 12;

    x += (FRAME_WIDTH / 2);
    y += (FRAME_HEIGHT / 2);

    int32_t rMinX = x - r;
    int32_t rMinY = y - r;
    int32_t rMaxX = x + r;
    int32_t rMaxY = y + r;

    if (rMinX > gViewport.x1 ||
        rMaxX < gViewport.x0 ||
        rMinY > gViewport.y1 ||
        rMaxY < gViewport.y0) return 0;

    return 1;
}

// ItemObj::getFrames
static int32_t getFrames(const GameItem &item, const uint16_t* &frameA, const uint16_t* &frameB,
                         int32_t &animFrameRate)
{
    const GameAnim* anim = gAnims + item.animIndex;

    if (anim->frameBegin == anim->frameEnd)
    {
        frameA = frameB = gFrames + (anim->frameOffset >> 1);
        animFrameRate = 1;
        return 0;
    }

    animFrameRate = anim->frameRate;

    int32_t frameSize = (20 >> 1) + (gModels[item.type].count << 1);

    int32_t frame = item.frameIndex - anim->frameBegin;

    int32_t indexA, indexB;
    int32_t frameDelta;

    if (animFrameRate == 1)
    {
        indexA = frame;
        frameDelta = frame - indexA;
    }
    else if (animFrameRate == 2)
    {
        indexA = frame >> 1;
        frameDelta = frame - (indexA << 1);
    }
    else if (animFrameRate == 4)
    {
        indexA = frame >> 2;
        frameDelta = frame - (indexA << 2);
    }
    else
    {
        indexA = frame / animFrameRate;
        frameDelta = frame - (indexA * animFrameRate);
    }

    indexB = indexA + 1;

    if (indexB * animFrameRate >= anim->frameEnd)
    {
        indexB = indexA;
    }

    frameA = gFrames + (anim->frameOffset >> 1) + indexA * frameSize;
    frameB = gFrames + (anim->frameOffset >> 1) + indexB * frameSize;

    if (!frameDelta || frameA == frameB)
        return 0;

    indexB *= animFrameRate;
    if (indexB > anim->frameEnd) {
        animFrameRate -= indexB - anim->frameEnd;
    }

    return frameDelta;
}

static void addDraw(uint8_t type, uint16_t mesh, uint32_t flags, int32_t vertices)
{
    DrawScene &d = gList.draws[gList.drawCount++];
    d.type = type;
    d.inside = 0;
    d.mesh = mesh;
    d.flags = flags;
    d.base = gBase;
    d.first = d.count = d.bucket = 0;
    gBase += vertices;
}

static int32_t meshVertices(uint16_t mesh)
{
    return gMeshGeometry[gMeshes[mesh] + 10];
}

// drawModel: the pose's item and light, its meshes' draws
static void drawModel(const ItemRecord &record, uint32_t flags)
{
    const GameItem* item = &record.item;
    const uint16_t *frameA, *frameB;

    int32_t frameRate;
    int32_t frameDelta = getFrames(*item, frameA, frameB, frameRate);

    const int16_t* box = (const int16_t*)frameA;    // AABBs: min max x, y, z
    int32_t sx, sy, sz, sr, smin, smax;
    smin = box[0];
    smax = box[1];
    sx = (smax + smin) >> 1;
    sr = (smax - smin);

    smin = box[2];
    smax = box[3];
    sy = (smax + smin) >> 1;
    sr = X_MAX(sr, (smax - smin));

    smin = box[4];
    smax = box[5];
    sz = (smax + smin) >> 1;
    sr = X_MAX(sr, (smax - smin));

    sr = (sr >> 1) + (sr >> 2);

    uint16_t quadrant = uint16_t(item->angle[1] + ANGLE_45) >> ANGLE_SHIFT_90;

    if (quadrant != 0)
    {
        int32_t ix = sx;
        int32_t iz = sz;

        switch (quadrant)
        {
            case 1:
                sx = iz;
                sz = -ix;
                break;
            case 2:
                sx = -ix;
                sz = -iz;
                break;
            case 3:
                sx = -iz;
                sz = ix;
                break;
        }
    }

    sx += item->pos[0] - gCamera[0];
    sy += item->pos[1] - gCamera[1];
    sz += item->pos[2] - gCamera[2];

    if (!sphereIsVisible(sx, sy, sz, sr))
        return;

    const GameModel &model = gModels[item->type];
    PoseItem &p = gList.items[gList.itemCount];
    PoseLight &l = gList.lights[gList.itemCount++];
    memset(&p, 0, sizeof(p));
    p.lara = item->type == ITEM_LARA;
    p.count = model.count;
    p.firstDraw = gList.drawCount;
    p.nodes = gModelNodes[item->type];
    p.frameA = uint32_t(frameA);
    p.frameB = uint32_t(frameB);
    p.frameDelta = frameDelta;
    p.frameRate = frameRate;
    p.visibleMask = item->visibleMask;
    memcpy(p.pos, item->pos, sizeof(p.pos));
    memcpy(p.angle, item->angle, sizeof(p.angle));
    if (p.lara)
    {
        const GameLara &lara = *(const GameLara*)item->extra;
        memcpy(p.torso, lara.torso, sizeof(p.torso));
        memcpy(p.head, lara.head, sizeof(p.head));
        p.weapon = lara.weapon;
        int32_t frameSize = (20 >> 1) + (model.count << 1);
        for (int32_t k = 0; k < 2; k++)
        {
            const GameArm &arm = lara.arms[k];
            PoseArm &a = p.arms[k];
            a.frame = NONE;
            if (arm.animIndex)
            {
                const GameAnim* anim = gAnims + arm.animIndex;
                a.frame = uint32_t(gFrames + (anim->frameOffset >> 1) + arm.frameIndex * frameSize);
                a.frameRate = anim->frameRate;
            }
            memcpy(a.angle, arm.angle, sizeof(a.angle));
            a.useBasis = arm.useBasis;
        }
    }
    else
    {
        for (int32_t k = 0; k < 2; k++) {
            p.arms[k].frame = NONE;
        }
        if (item->extra)
        {
            // an enemy's: as many angles as its nodes' ROT flags take
            const uint8_t* node = gNodes + p.nodes;
            for (int32_t k = 0; k < model.count - 1; k++, node += 8)
            {
                uint32_t rot = ((node[6] << 8) | node[7]) & NODE_FLAG_ROT;
                for (; rot; rot &= rot - 1) {
                    p.extraCount++;
                }
            }
            memcpy(p.extra, item->extra, p.extraCount * sizeof(int16_t));
        }
    }

    const RoomLevel &room = gLevel[record.room];
    const RoomLights &lights = gRoomLights[record.room];
    l.roomX = room.x;
    l.roomZ = room.z;
    l.intensity = item->intensity;
    l.ambient = lights.ambient;
    l.lights = lights.lights;
    l.pad = 0;
    l.first = lights.first;

    // drawLaraNodesLerp: each joint's mesh of hers; drawNodesLerp: the
    // model's, each its visible mask shows
    for (int32_t k = 0; k < model.count; k++)
    {
        if (p.lara || (item->visibleMask & (1 << k)))
        {
            uint16_t mesh = gMeshIndex[p.lara ? ((const GameLara*)item->extra)->meshes[k] : model.start + k];
            addDraw(DRAW_MESH, mesh, flags, meshVertices(mesh));
        }
    }
}

// drawShadow, the floor given: its draw
static void drawShadow(const CallRecord &call)
{
    int32_t floor = call.args[0];

    if (floor == WALL)
        return;

    PoseShadow &s = gList.shadows[gList.shadowCount++];
    s.draw = gList.drawCount;
    s.item = gList.itemCount - 1;
    s.floor = floor;
    addDraw(DRAW_SHADOW, 0, call.flags, SHADOW_VERTICES);
}

// drawRoomStatic: its place, quarter turn and light, its draw
static void drawRoomStatic(const CallRecord &call)
{
    uint32_t xy = call.args[0], zf = call.args[1];
    int32_t rx = call.args[2], ry = call.args[3], rz = call.args[4];

    const GameStaticMesh* staticMesh = gStatics + (zf & 0x3F);

    if (!(staticMesh->flags & STATIC_MESH_FLAG_VISIBLE))
        return; // invisible

    int32_t px = rx + (int32_t(xy) >> 16);
    int32_t py = ry + (int32_t(xy) << 16 >> 16);
    int32_t pz = rz + (int32_t(zf) >> 16);

    int32_t sx = (int32_t(staticMesh->vsXY) >> 16);
    int32_t sy = (int32_t(staticMesh->vsXY) << 16 >> 16);
    int32_t sz = (int32_t(staticMesh->vsZR) >> 16);
    int32_t sr = (int32_t(staticMesh->vsZR) << 16 >> 16);

    // rotate visible sphere offset
    int32_t q = (zf >> 6) & 3;
    if (q == 0) {
        sx = -sx;
        sz = -sz;
    } else if (q == 1) {
        int32_t t = sx;
        sx = -sz;
        sz = t;
    } else if (q == 3) {
        int32_t t = sz;
        sz = -sx;
        sx = t;
    }

    sx += px;
    sy += py;
    sz += pz;

    if (!sphereIsVisible(sx, sy, sz, sr))
        return;

    PoseStatic &s = gList.statics[gList.staticCount++];
    s.draw = gList.drawCount;
    s.quadrant = q;
    s.pad = 0;
    s.intensity = ((zf >> 8) & 0xFF) << 5;
    s.x = px + gCamera[0];
    s.y = py + gCamera[1];
    s.z = pz + gCamera[2];
    uint16_t mesh = gMeshIndex[staticMesh->meshIndex];
    addDraw(DRAW_MESH, mesh, call.flags, meshVertices(mesh));
}

static Vertex* addCopy(uint32_t flags, int32_t depth)
{
    Face &f = gList.faces[gList.faceCount];
    f.flags = flags;
    f.next = nullptr;
    memset(f.indices, 0, sizeof(f.indices));
    addDraw(DRAW_COPY, 0, 0, 2);
    DrawScene &d = gList.draws[gList.drawCount - 1];
    d.first = gList.faceCount;
    d.count = 1;
    d.bucket = depth;
    return gList.vertices + 2 * gList.faceCount++;
}

static inline int32_t vdp1Coord(int32_t v)
{
    return X_CLAMP(v, VDP1_COORD_MIN, VDP1_COORD_MAX);
}

// renderSprite
static void renderSprite(int32_t vx, int32_t vy, int32_t vz, int32_t vg, int32_t index)
{
    const Matrix &m = gView;

    vx -= gCamera[0];
    vy -= gCamera[1];
    vz -= gCamera[2];

    int32_t z = DP33(m.e20, m.e21, m.e22, vx, vy, vz);

    if (z < VIEW_MIN_F || z >= VIEW_MAX_F)
        return;

    int32_t x = DP33(m.e00, m.e01, m.e02, vx, vy, vz) >> FIXED_SHIFT;
    int32_t y = DP33(m.e10, m.e11, m.e12, vx, vy, vz) >> FIXED_SHIFT;
    z >>= FIXED_SHIFT;

    const Sprite* sprite = gSprites + index;

    int32_t l = x + sprite->l;
    int32_t r = x + sprite->r;
    int32_t t = y + sprite->t;
    int32_t b = y + sprite->b;

    PERSPECTIVE(l, t, z);
    PERSPECTIVE(r, b, z);

    l += (FRAME_WIDTH >> 1);
    r += (FRAME_WIDTH >> 1);
    t += (FRAME_HEIGHT >> 1);
    b += (FRAME_HEIGHT >> 1);

    if (l >= gViewport.x1 || r < gViewport.x0 || t >= gViewport.y1 || b < gViewport.y0)
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

    // drawn in front of the geometry it stands on
    Vertex* v = addCopy((FACE_TYPE_SPRITE << FACE_TYPE_SHIFT) | index, X_MAX(0, z - 128) >> OT_SHIFT);
    v[0].x = vdp1Coord(l);
    v[0].y = vdp1Coord(t);
    v[0].g = vg >> 8;
    v[1].x = vdp1Coord(r);
    v[1].y = vdp1Coord(b);
}

// renderRect
static void renderRect(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t type, uint16_t color,
                       int32_t z)
{
    Vertex* v = addCopy(type << FACE_TYPE_SHIFT, z);
    v[0].x = x;
    v[0].y = y;
    v[0].z = int16_t(color);
    v[1].x = width;
    v[1].y = height;
}

template <typename T>
static T* alloc(uint32_t count)
{
    return (T*)sceneAlloc(X_MAX(count, 1u) * sizeof(T));
}

int32_t gameDrawsInit()
{
    gCalls = sceneArray<const CallRecord>("CALL", &gCallCount);
    uint32_t items;
    gItems = sceneArray<ItemRecord>("OITM", &items);
    for (uint32_t i = 0; i < items; i++)
    {
        if (gItems[i].item.extra) {
            gItems[i].item.extra = gItems[i].extra;
        }
    }
    gAnims = sceneArray<const GameAnim>("ANIM");
    gFrames = sceneArray<const uint16_t>("AFRM") - AFRM_BASE;
    gModels = sceneArray<const GameModel>("MODL");
    gMeshIndex = sceneArray<const uint16_t>("MIDX");
    gModelNodes = sceneArray<const uint32_t>("MNOD");
    gNodes = sceneArray<const uint8_t>("PNOD");
    gStatics = sceneArray<const GameStaticMesh>("SMSH");
    gRoomLights = sceneArray<const RoomLights>("RLIT");
    gLevel = sceneArray<const RoomLevel>("RLVL");
    gMeshes = sceneArray<const uint32_t>("MESH");
    gMeshGeometry = sceneArray<const uint8_t>("MGEO");
    gSprites = sceneArray<const Sprite>("SPRS");
    const PoseCamera* camera = sceneArray<const PoseCamera>("CAMR");
    gCamera = camera->pos;
    int32_t* e = &gView.e00;
    for (int32_t r = 0; r < 3; r++)
    {
        for (int32_t c = 0; c < 3; c++) {
            e[r * 4 + c] = camera->rotation[r * 3 + c];
        }
        e[r * 4 + 3] = 0;
    }

    // room for every call's draws: a model's meshes, a shadow, a static, a face
    uint32_t draws = 0, models = 0, statics = 0, shadows = 0, faces = 0;
    for (uint32_t i = 0; i < gCallCount; i++)
    {
        const CallRecord &c = gCalls[i];
        if (c.kind == CALL_MODEL)
        {
            models++;
            draws += gModels[gItems[c.index].item.type].count;
        }
        statics += c.kind == CALL_STATIC;
        shadows += c.kind == CALL_SHADOW;
        faces += c.kind == CALL_SPRITE || c.kind == CALL_RECT;
    }
    draws += statics + shadows + faces;
    gList.draws = alloc<DrawScene>(draws);
    gList.items = alloc<PoseItem>(models);
    gList.lights = alloc<PoseLight>(models);
    gList.statics = alloc<PoseStatic>(statics);
    gList.shadows = alloc<PoseShadow>(shadows);
    gList.faces = alloc<Face>(faces);
    gList.vertices = alloc<Vertex>(2 * faces);
    return gCallCount;
}

void gameDraws(int32_t fills)
{
    gList.drawCount = gList.itemCount = gList.staticCount = gList.shadowCount = gList.faceCount = 0;
    gBase = 2 * fills;
    for (uint32_t i = 0; i < gCallCount; i++)
    {
        const CallRecord &c = gCalls[i];
        switch (c.kind)
        {
            case CALL_VIEWPORT:
                gViewport.x0 = c.args[0];
                gViewport.y0 = c.args[1];
                gViewport.x1 = c.args[2];
                gViewport.y1 = c.args[3];
                break;
            case CALL_MODEL:
                drawModel(gItems[c.index], c.flags);
                break;
            case CALL_SHADOW:
                drawShadow(c);
                break;
            case CALL_STATIC:
                drawRoomStatic(c);
                break;
            case CALL_SPRITE:
                renderSprite(c.args[0], c.args[1], c.args[2], c.args[3], c.index);
                break;
            case CALL_RECT:
                renderRect(c.args[0], c.args[1], c.args[2], c.args[3], c.index, c.args[4], c.args[5]);
                break;
        }
    }
}

const DrawList& gameDrawList()
{
    return gList;
}

void gameDrawsRun(const CallRecord* calls, uint32_t count, ItemRecord* items, const GameAnim* anims, int32_t fills,
                  DrawList &out)
{
    const CallRecord* sceneCalls = gCalls;
    uint32_t sceneCount = gCallCount;
    ItemRecord* sceneItems = gItems;
    const GameAnim* sceneAnims = gAnims;
    DrawList sceneList = gList;
    gCalls = calls;
    gCallCount = count;
    gItems = items;
    gAnims = anims;
    gList = out;
    gameDraws(fills);
    out = gList;
    gCalls = sceneCalls;
    gCallCount = sceneCount;
    gItems = sceneItems;
    gAnims = sceneAnims;
    gList = sceneList;
}

// A frame's bytes (its box, place and packed angles) against another's.
static bool sameFrame(uint32_t a, const uint8_t* b, int32_t count)
{
    return !memcmp((const void*)a, b, 20 + 4 * count);
}

static bool sameArm(const PoseArm &a, const PoseArm &b, const uint8_t* frames, int32_t count)
{
    if ((a.frame == NONE) != (b.frame == NONE))
        return false;
    return (a.frame == NONE || (sameFrame(a.frame, frames + b.frame, count) && a.frameRate == b.frameRate)) &&
           !memcmp(a.angle, b.angle, sizeof(a.angle)) && a.useBasis == b.useBasis;
}

int32_t gameDrawsCheck(const DrawList &list, int32_t fills)
{
    const SceneInfo &info = sceneInfo();
    const DrawScene* draws = sceneArray<const DrawScene>("DRAW");
    const Face* faces = sceneArray<const Face>("MPOL");
    const uint8_t* vertices = sceneArray<const uint8_t>("VERT");
    uint32_t items, statics, shadows;
    const PoseItem* poses = sceneArray<const PoseItem>("POSE", &items);
    const PoseLight* lights = sceneArray<const PoseLight>("PLIT");
    const uint8_t* frames = sceneArray<const uint8_t>("PFRM");
    const PoseStatic* placed = sceneArray<const PoseStatic>("STAT", &statics);
    const PoseShadow* floors = sceneArray<const PoseShadow>("SHDW", &shadows);

    int32_t bad = list.drawCount + fills != info.draws;
    for (uint32_t i = 0; i < list.drawCount && i + fills < info.draws; i++)
    {
        const DrawScene &a = list.draws[i], &b = draws[i + fills];
        if (a.type != b.type)
        {
            bad++;
            continue;
        }
        if (a.type == DRAW_COPY)
        {
            const Face &f = list.faces[a.first], &g = faces[b.first];
            const Vertex* u = list.vertices + 2 * a.first;
            const Vertex* v = (const Vertex*)(vertices + g.indices[0]);
            bool sprite = (f.flags >> FACE_TYPE_SHIFT & 15) == FACE_TYPE_SPRITE;
            bad += a.bucket != b.bucket || f.flags != g.flags || u[0].xy != v[0].xy || u[1].xy != v[1].xy ||
                   (sprite ? u[0].g != v[0].g : u[0].z != v[0].z);
            continue;
        }
        bad += (a.type == DRAW_MESH && a.mesh != b.mesh) || a.flags != b.flags || a.base != b.base;
    }

    bad += list.itemCount != items;
    for (uint32_t i = 0; i < list.itemCount && i < items; i++)
    {
        const PoseItem &a = list.items[i], &b = poses[i];
        const PoseLight &l = list.lights[i], &m = lights[i];
        bad += a.lara != b.lara || a.count != b.count || a.firstDraw + fills != b.firstDraw || a.nodes != b.nodes ||
               !sameFrame(a.frameA, frames + b.frameA, a.count) || !sameFrame(a.frameB, frames + b.frameB, a.count) ||
               a.frameDelta != b.frameDelta || a.frameRate != b.frameRate || a.visibleMask != b.visibleMask ||
               memcmp(a.pos, b.pos, sizeof(a.pos)) || memcmp(a.angle, b.angle, sizeof(a.angle)) ||
               a.extraCount != b.extraCount || memcmp(a.extra, b.extra, a.extraCount * sizeof(int16_t)) ||
               memcmp(a.torso, b.torso, sizeof(a.torso)) || memcmp(a.head, b.head, sizeof(a.head)) ||
               a.weapon != b.weapon || !sameArm(a.arms[0], b.arms[0], frames, a.count) ||
               !sameArm(a.arms[1], b.arms[1], frames, a.count) ||
               l.roomX != m.roomX || l.roomZ != m.roomZ || l.intensity != m.intensity || l.ambient != m.ambient ||
               l.lights != m.lights || l.first != m.first;
    }

    bad += list.staticCount != statics;
    for (uint32_t i = 0; i < list.staticCount && i < statics; i++)
    {
        const PoseStatic &a = list.statics[i], &b = placed[i];
        bad += a.draw + fills != b.draw || a.quadrant != b.quadrant || a.intensity != b.intensity || a.x != b.x ||
               a.y != b.y || a.z != b.z;
    }

    bad += list.shadowCount != shadows;
    for (uint32_t i = 0; i < list.shadowCount && i < shadows; i++)
    {
        const PoseShadow &a = list.shadows[i], &b = floors[i];
        bad += a.draw + fills != b.draw || a.item != b.item || a.floor != b.floor;
    }
    return bad;
}
