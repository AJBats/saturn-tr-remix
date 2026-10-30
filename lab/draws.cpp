// Stage 1 the new way, the draw list (draws.h): the scene's calls through
// draws.s, and its checks against the game's way (gamedraws.cpp).
#include "draws.h"

#include <stddef.h>
#include <string.h>

static_assert(offsetof(DrawsContext, camera) == 60 && offsetof(DrawsContext, view) == 72 &&
              offsetof(DrawsContext, viewport) == 80 && offsetof(DrawsContext, base) == 96 &&
              offsetof(DrawsContext, faceCount) == 144 && offsetof(DrawsContext, k20) == 148 &&
              offsetof(DrawsContext, frameA) == 156 && offsetof(DrawsContext, viewMin) == 176 &&
              offsetof(DrawsContext, drawCount) == 184, "draws.s's DrawsContext");
static_assert(sizeof(DrawScene) == 24 && sizeof(PoseItem) == 104 && sizeof(PoseLight) == 12 &&
              sizeof(PoseStatic) == 20 && sizeof(PoseShadow) == 8 && sizeof(Face) == 16 && sizeof(Vertex) == 8 &&
              sizeof(Sprite) == 16, "draws.s's records");

#define FACE_TYPE_SHIFT     14
#define FACE_TYPE_SPRITE    6
#define WALL                (-32512)
#define STATIC_MESH_FLAG_VISIBLE    2
#define X_MAX(a, b)         ((a) > (b) ? (a) : (b))
#define X_MIN(a, b)         ((a) < (b) ? (a) : (b))

static DrawsContext gCtx;
static int32_t gView[12];
static DrawList gList;

// Room for what count calls make: a model's meshes, a static, a shadow, a face.
struct DrawsRoom
{
    uint32_t draws, models, statics, shadows, faces;
};

static DrawsRoom roomFor(const CallRecord* calls, uint32_t count, const ItemRecord* items, const GameModel* models)
{
    DrawsRoom r = {};
    for (uint32_t i = 0; i < count; i++)
    {
        const CallRecord &c = calls[i];
        if (c.kind == CALL_MODEL)
        {
            r.models++;
            r.draws += X_MAX(models[items[c.index].item.type].count, 0);
        }
        r.statics += c.kind == CALL_STATIC;
        r.shadows += c.kind == CALL_SHADOW;
        r.faces += c.kind == CALL_SPRITE || c.kind == CALL_RECT;
    }
    r.draws += r.statics + r.shadows + r.faces;
    return r;
}

template <typename T>
static T* highArray(uint32_t count)
{
    return (T*)sceneAlloc(X_MAX(count, 1u) * sizeof(T));
}

template <typename T>
static T* lowArray(uint32_t count)
{
    return (T*)sceneAllocLow(X_MAX(count, 1u) * sizeof(T));
}

// A list's arrays with room r, in high work RAM (low).
static void listArrays(DrawList &l, const DrawsRoom &r, bool low)
{
    l.draws = low ? lowArray<DrawScene>(r.draws) : highArray<DrawScene>(r.draws);
    l.items = low ? lowArray<PoseItem>(r.models) : highArray<PoseItem>(r.models);
    l.lights = low ? lowArray<PoseLight>(r.models) : highArray<PoseLight>(r.models);
    l.statics = low ? lowArray<PoseStatic>(r.statics) : highArray<PoseStatic>(r.statics);
    l.shadows = low ? lowArray<PoseShadow>(r.shadows) : highArray<PoseShadow>(r.shadows);
    l.faces = low ? lowArray<Face>(r.faces) : highArray<Face>(r.faces);
    l.vertices = low ? lowArray<Vertex>(2 * r.faces) : highArray<Vertex>(2 * r.faces);
}

static void contextList(DrawsContext &c, const DrawList &l)
{
    c.draws = l.draws;
    c.poses = l.items;
    c.lights = l.lights;
    c.placed = l.statics;
    c.floors = l.shadows;
    c.faces = l.faces;
    c.vertices = l.vertices;
}

static void run(DrawsContext &c, DrawList &l, int32_t fills)
{
    c.base = 2 * fills;
    drawsAsm(&c);
    l.drawCount = c.drawsEnd - c.draws;
    l.itemCount = c.itemCount;
    l.staticCount = c.staticCount;
    l.shadowCount = c.shadowCount;
    l.faceCount = c.faceCount;
}

void drawsInit()
{
    gCtx.calls = sceneArray<const CallRecord>("CALL", &gCtx.callCount);
    gCtx.items = sceneArray<ItemRecord>("OITM");
    gCtx.anims = sceneArray<const GameAnim>("ANIM");
    gCtx.frames = sceneArray<const uint8_t>("AFRM") - 2 * AFRM_BASE;
    gCtx.models = sceneArray<const GameModel>("MODL");
    gCtx.meshIndex = sceneArray<const uint16_t>("MIDX");
    gCtx.modelNodes = sceneArray<const uint32_t>("MNOD");
    gCtx.nodes = sceneArray<const uint8_t>("PNOD");
    gCtx.statics = sceneArray<const GameStaticMesh>("SMSH");
    gCtx.roomLights = sceneArray<const RoomLights>("RLIT");
    gCtx.level = sceneArray<const RoomLevel>("RLVL");
    gCtx.meshes = sceneArray<const uint32_t>("MESH");
    gCtx.meshGeometry = sceneArray<const uint8_t>("MGEO");
    gCtx.sprites = sceneArray<const Sprite>("SPRS");
    const PoseCamera* camera = sceneArray<const PoseCamera>("CAMR");
    for (int32_t r = 0; r < 3; r++)
    {
        gCtx.camera[r] = camera->pos[r];
        for (int32_t c = 0; c < 3; c++) {
            gView[r * 4 + c] = camera->rotation[r * 3 + c];
        }
        gView[r * 4 + 3] = 0;
    }
    gCtx.view = gView;
    gCtx.divTable = divTable;
    gCtx.k20 = 1 << 20;
    gCtx.k18 = 1 << 18;
    gCtx.viewMin = 64 << 14;
    gCtx.viewMax = (1024 * 20) << 14;
    listArrays(gList, roomFor(gCtx.calls, gCtx.callCount, gCtx.items, gCtx.models), false);
    contextList(gCtx, gList);
}

void draws(int32_t fills)
{
    run(gCtx, gList, fills);
}

void drawsPoison()
{
    DrawList &l = gList;
    memset(l.draws, 0xA5, l.drawCount * sizeof(DrawScene));
    memset(l.items, 0xA5, l.itemCount * sizeof(PoseItem));
    memset(l.lights, 0xA5, l.itemCount * sizeof(PoseLight));
    memset(l.statics, 0xA5, l.staticCount * sizeof(PoseStatic));
    memset(l.shadows, 0xA5, l.shadowCount * sizeof(PoseShadow));
    memset(l.faces, 0xA5, l.faceCount * sizeof(Face));
    memset(l.vertices, 0xA5, 2 * l.faceCount * sizeof(Vertex));
    l.drawCount = l.itemCount = l.staticCount = l.shadowCount = l.faceCount = 0;
}

const DrawList& drawsList()
{
    return gList;
}

// Two lists: their draws, items, lights, statics, shadows and faces that
// differ (a face's two vertices as made: corners, and the sprite's shade or
// the rectangle's colour), or lists of other lengths.
static int32_t listsOff(const DrawList &a, const DrawList &b)
{
    int32_t bad = (a.drawCount != b.drawCount) + (a.itemCount != b.itemCount) + (a.staticCount != b.staticCount) +
                  (a.shadowCount != b.shadowCount) + (a.faceCount != b.faceCount);
    for (uint32_t i = 0; i < X_MIN(a.drawCount, b.drawCount); i++) {
        bad += memcmp(a.draws + i, b.draws + i, sizeof(DrawScene)) != 0;
    }
    for (uint32_t i = 0; i < X_MIN(a.itemCount, b.itemCount); i++) {
        bad += memcmp(a.items + i, b.items + i, sizeof(PoseItem)) || memcmp(a.lights + i, b.lights + i,
                                                                            sizeof(PoseLight));
    }
    for (uint32_t i = 0; i < X_MIN(a.staticCount, b.staticCount); i++) {
        bad += memcmp(a.statics + i, b.statics + i, sizeof(PoseStatic)) != 0;
    }
    for (uint32_t i = 0; i < X_MIN(a.shadowCount, b.shadowCount); i++) {
        bad += memcmp(a.shadows + i, b.shadows + i, sizeof(PoseShadow)) != 0;
    }
    for (uint32_t i = 0; i < X_MIN(a.faceCount, b.faceCount); i++)
    {
        const Vertex* u = a.vertices + 2 * i;
        const Vertex* v = b.vertices + 2 * i;
        bool sprite = (a.faces[i].flags >> FACE_TYPE_SHIFT & 15) == FACE_TYPE_SPRITE;
        bad += memcmp(a.faces + i, b.faces + i, sizeof(Face)) || u[0].xy != v[0].xy || u[1].xy != v[1].xy ||
               (sprite ? u[0].g != v[0].g : u[0].z != v[0].z);
    }
    return bad;
}

int32_t drawsCheck(const DrawList &list)
{
    return listsOff(gList, list);
}

// --- made-up calls ------------------------------------------------------------

static uint32_t gSeed = 0x2545F491;

static uint32_t rnd()
{
    gSeed = gSeed * 1664525 + 1013904223;
    return gSeed >> 8;
}

static int32_t around(int32_t v, int32_t reach)
{
    return v + int32_t(rnd() % uint32_t(2 * reach + 1)) - reach;
}

// An item every mesh of whose model (Lara: of her joints) the scene holds,
// so that drawn it reads no mesh the capture never drew.
static bool drawable(const ItemRecord &it)
{
    const GameModel &model = gCtx.models[it.item.type];
    if (gCtx.modelNodes[it.item.type] == 0xFFFFFFFF)
        return false;
    for (int32_t k = 0; k < model.count; k++)
    {
        uint16_t level = it.item.type == 0 ? ((const GameLara*)it.item.extra)->meshes[k] : model.start + k;
        if (gCtx.meshIndex[level] == 0xFFFF)
            return false;
    }
    return true;
}

// getFrames' frame A's index for a frame relative to the animation's first,
// at a rate (the game's divide for the slow ones, 0 too).
static int32_t frameIndexAt(int32_t frame, int32_t rate)
{
    volatile int32_t divisor = rate;
    if (rate == 1)
        return frame;
    if (rate == 2)
        return frame >> 1;
    if (rate == 4)
        return frame >> 2;
    return frame / divisor;
}

#define SELDOM_PASSES   48
#define SELDOM_CALLS    48
#define SELDOM_MODELS   16

int32_t drawsSeldomCheck()
{
    uint32_t calls, items, anims, sprites;
    const CallRecord* scene = sceneArray<const CallRecord>("CALL", &calls);
    ItemRecord* sceneItems = sceneArray<ItemRecord>("OITM", &items);
    const GameAnim* sceneAnims = sceneArray<const GameAnim>("ANIM", &anims);
    sceneArray<const Sprite>("SPRS", &sprites);
    if (!items || !sprites)
        return -1;

    // the scene's static calls, to move about
    uint32_t staticCalls = 0;
    for (uint32_t i = 0; i < calls; i++) {
        staticCalls += scene[i].kind == CALL_STATIC;
    }

    static CallRecord* made;
    static ItemRecord* madeItems;
    static GameAnim* madeAnims;
    static DrawList mine, game;
    if (!made)
    {
        made = lowArray<CallRecord>(SELDOM_CALLS);
        madeItems = lowArray<ItemRecord>(SELDOM_MODELS);
        madeAnims = lowArray<GameAnim>(anims + SELDOM_MODELS);
        DrawsRoom r = { SELDOM_MODELS * 32 + SELDOM_CALLS, SELDOM_MODELS, SELDOM_CALLS, SELDOM_CALLS, SELDOM_CALLS };
        listArrays(mine, r, true);
        listArrays(game, r, true);
        if (!made || !madeItems || !madeAnims || !mine.vertices || !game.vertices)
            return -1;
    }
    memcpy(madeAnims, sceneAnims, anims * sizeof(GameAnim));    // Lara's arms' own

    DrawsContext ctx = gCtx;
    ctx.calls = made;
    ctx.items = madeItems;
    ctx.anims = madeAnims;
    contextList(ctx, mine);

    static const uint8_t kRates[] = { 0, 1, 2, 4, 3, 5, 6, 8 };
    int32_t bad = 0, models = 0, posed = 0, lerped = 0, stills = 0, lara = 0, extras = 0, walls = 0;
    int32_t statics = 0, staticsDrawn = 0, hidden = 0, spritesMade = 0, spritesDrawn = 0, clamped = 0, unclamped = 0;
    int32_t early[5] = {};      // posed at a frame before their animation's: by rate 0, 1, 2, 4, another
    uint8_t rateOf[SELDOM_MODELS];
    bool earlyOf[SELDOM_MODELS];
    for (int32_t pass = 0; pass < SELDOM_PASSES; pass++)
    {
        int32_t n = 0, m = 0;
        int32_t reach = (pass & 3) == 3 ? 32768 : 4096;
        while (n < SELDOM_CALLS)
        {
            CallRecord &c = made[n++];
            memset(&c, 0, sizeof(c));
            uint32_t kind = rnd() % 8;
            if (kind == 0)
            {
                c.kind = CALL_VIEWPORT;
                bool full = rnd() & 1;
                c.args[0] = full ? 0 : int32_t(rnd() % 200);
                c.args[1] = full ? 0 : int32_t(rnd() % 120);
                c.args[2] = full ? 352 : c.args[0] + int32_t(rnd() % 160);
                c.args[3] = full ? 224 : c.args[1] + int32_t(rnd() % 110);
            }
            else if (kind <= 3 && m < SELDOM_MODELS)
            {
                // an item moved and turned, at any frame of an animation of any
                // rate (or a still)
                // every rate (or a still), a frame before its animation's or
                // in it, at the camera (within its sphere: posed whatever it
                // turns to) or about it: each in turn
                ItemRecord &it = madeItems[m];
                uint32_t g = models;
                bool want = g % 9 != 8 && (g / 9) % 4 == 0;     // a frame before: an animation from 3 on
                for (int32_t tries = 0; tries < 64; tries++)
                {
                    it = sceneItems[rnd() % items];
                    if (drawable(it) && (!want || sceneAnims[it.item.animIndex].frameBegin >= 3))
                        break;
                }
                GameAnim &a = madeAnims[anims + m];
                a = sceneAnims[it.item.animIndex];
                // where the scene has its frame (uint16s past the frames' base)
                int32_t size = 10 + 2 * gCtx.models[it.item.type].count;
                int32_t at = (a.frameOffset >> 1) + (a.frameBegin == a.frameEnd ? 0 :
                             frameIndexAt(it.item.frameIndex - a.frameBegin, a.frameRate)) * size;
                bool still = g % 9 == 8;
                if (still) {
                    a.frameEnd = a.frameBegin;
                } else {
                    a.frameRate = kRates[g % 9];
                }
                bool before = !still && (g / 9) % 4 == 0 && a.frameBegin >= 3;
                int32_t span = a.frameEnd - a.frameBegin;
                if (before)
                {
                    // the animation moved so that frame A lands on the scene's frame
                    int32_t frame = -1 - int32_t((g / 72) % 3);
                    it.item.frameIndex = uint16_t(a.frameBegin + frame);
                    a.frameOffset = uint32_t(2 * (at - frameIndexAt(frame, a.frameRate) * size));
                }
                else
                {
                    it.item.frameIndex = uint16_t(a.frameBegin + int32_t(rnd() % uint32_t(span + 3)));
                }
                it.item.animIndex = anims + m;
                bool near = (g / 36) % 2 == 0;
                for (int32_t k = 0; k < 3; k++) {
                    it.item.pos[k] = near ? gCtx.camera[k] : around(it.item.pos[k], reach);
                }
                it.item.angle[1] = int16_t(rnd());
                if (it.item.type != 0) {
                    it.item.visibleMask = rnd() << 8 | rnd();
                }
                stills += still;
                rateOf[m] = a.frameRate;
                earlyOf[m] = before;
                c.kind = CALL_MODEL;
                c.index = m++;
                c.flags = (rnd() & 1) << 30;
                models++;
                if (rnd() % 2 && n < SELDOM_CALLS)
                {
                    CallRecord &s = made[n++];
                    memset(&s, 0, sizeof(s));
                    s.kind = CALL_SHADOW;
                    s.index = c.index;
                    s.flags = c.flags;
                    bool wall = rnd() % 3 == 0;
                    s.args[0] = wall ? WALL : it.item.pos[1] + int32_t(rnd() % 1024);
                    walls += wall;
                }
            }
            else if (kind == 4 && staticCalls)
            {
                // a static moved, turned, lit, now and then one out of sight
                uint32_t k = rnd() % staticCalls;
                for (uint32_t i = 0; i < calls; i++)
                {
                    if (scene[i].kind == CALL_STATIC && !k--)
                    {
                        c = scene[i];
                        break;
                    }
                }
                uint32_t xy = c.args[0], zf = c.args[1];
                int32_t x = around(int32_t(xy) >> 16, 2048), y = around(int32_t(xy) << 16 >> 16, 512);
                int32_t z = around(int32_t(zf) >> 16, 2048);
                uint32_t id = zf & 0x3F;
                if (rnd() % 4 == 0)
                {
                    uint32_t other = rnd() % 50;
                    const GameStaticMesh &sm = gCtx.statics[other];
                    if (!(sm.flags & STATIC_MESH_FLAG_VISIBLE) || gCtx.meshIndex[sm.meshIndex] != 0xFFFF) {
                        id = other;
                    }
                }
                const GameStaticMesh &sm = gCtx.statics[id];
                if ((sm.flags & STATIC_MESH_FLAG_VISIBLE) && gCtx.meshIndex[sm.meshIndex] == 0xFFFF) {
                    id = zf & 0x3F;     // its mesh not in the scene: the call's own
                }
                hidden += !(gCtx.statics[id].flags & STATIC_MESH_FLAG_VISIBLE);
                c.args[0] = int32_t(uint32_t(x) << 16 | (uint32_t(y) & 0xFFFF));
                c.args[1] = int32_t(uint32_t(z) << 16 | (rnd() & 0xFFC0) | id);
                statics++;
            }
            else if (kind == 5)
            {
                // a sprite about the camera or ahead of it, near or far (in
                // the fog), any shade
                c.kind = CALL_SPRITE;
                c.index = rnd() % sprites;
                int32_t ahead = 512 + int32_t(rnd() % 23500);
                for (int32_t k = 0; k < 3; k++)
                {
                    c.args[k] = (pass & 1) ? around(gCtx.camera[k], 24576) :
                                             around(gCtx.camera[k] + (gView[8 + k] * ahead >> 14), ahead >> 3);
                }
                c.args[3] = int32_t(rnd() % 8192);
                spritesMade++;
            }
            else
            {
                c.kind = CALL_RECT;
                c.index = 7 + rnd() % 3;
                c.args[0] = int32_t(rnd() % 352);
                c.args[1] = int32_t(rnd() % 224);
                c.args[2] = int32_t(rnd() % 64);
                c.args[3] = int32_t(rnd() % 16);
                c.args[4] = int32_t(rnd() & 0xFFFF);
                c.args[5] = int32_t(rnd() % 1024);
            }
        }

        ctx.callCount = n;
        run(ctx, mine, 0);
        gameDrawsRun(made, n, madeItems, madeAnims, 0, game);
        bad += listsOff(mine, game);
        posed += game.itemCount;
        staticsDrawn += game.staticCount;
        for (uint32_t i = 0; i < game.itemCount; i++)
        {
            lerped += game.items[i].frameDelta != 0;
            lara += game.items[i].lara;
            extras += game.items[i].extraCount > 0;
        }
        // the models posed, in the order called: those at a frame before their
        // animation's, by rate
        uint32_t j = 0;
        for (int32_t k = 0; k < n && j < game.itemCount; k++)
        {
            if (made[k].kind != CALL_MODEL)
                continue;
            const GameItem &item = madeItems[made[k].index].item;
            const PoseItem &p = game.items[j];
            if (memcmp(p.pos, item.pos, sizeof(p.pos)) || memcmp(p.angle, item.angle, sizeof(p.angle)))
                continue;
            j++;
            if (earlyOf[made[k].index])
            {
                uint8_t r = rateOf[made[k].index];
                early[r == 0 ? 0 : r == 1 ? 1 : r == 2 ? 2 : r == 4 ? 3 : 4]++;
            }
        }
        for (uint32_t i = 0; i < game.faceCount; i++)
        {
            if ((game.faces[i].flags >> FACE_TYPE_SHIFT & 15) == FACE_TYPE_SPRITE)
            {
                spritesDrawn++;
                clamped += game.vertices[2 * i].g == 31;
                unclamped += game.vertices[2 * i].g < 31;
            }
        }
    }
    bool enemies = false;
    for (uint32_t i = 0; i < items; i++) {
        enemies = enemies || (sceneItems[i].item.type != 0 && sceneItems[i].item.extra);
    }
    return bad + !posed + !(models - posed) + !lerped + !stills + !lara + (enemies && !extras) + !early[0] +
           !early[1] + !early[2] + !early[3] + !early[4] +
           (staticCalls ? !staticsDrawn + !(statics - staticsDrawn) + !hidden : 0) + !spritesDrawn +
           !(spritesMade - spritesDrawn) + !clamped + !unclamped + !walls;
}
