// The draw list from the scene, and its checks against the game's way
// (labrender.h).
#include "labrender.h"
#include "draws.h"
#include "game.h"

#include <string.h>

#define FACE_TYPE_SHIFT     14
#define FACE_TYPE_SPRITE    6
#define WALL                (-32512)
#define STATIC_MESH_FLAG_VISIBLE    2
#define X_MAX(a, b)         ((a) > (b) ? (a) : (b))
#define X_MIN(a, b)         ((a) < (b) ? (a) : (b))

static DrawsContext &gCtx = drawsContext();
static DrawList &gList = const_cast<DrawList&>(drawsList());
static const CallRecord* gCalls;        // the scene's (CALL)
static uint32_t gCallCount;
static ItemRecord* gItems;              // OITM
static int32_t gView[12];

void drawsLabInit()
{
    DrawsLevel level;
    level.anims = sceneArray<const GameAnim>("ANIM");
    level.frames = sceneArray<const uint8_t>("AFRM") - 2 * AFRM_BASE;
    level.models = sceneArray<const GameModel>("MODL");
    const MeshStore* store = sceneArray<const MeshStore>("MSTO");
    level.meshIndex = (const uint16_t*)((const uint8_t*)store + store->index);
    level.modelNodes = sceneArray<const uint32_t>("MNOD");
    level.nodes = sceneArray<const uint8_t>("PNOD");
    level.statics = sceneArray<const GameStaticMesh>("SMSH");
    level.roomLights = sceneArray<const RoomLights>("RLIT");
    level.level = sceneArray<const RoomLevel>("RLVL");
    level.meshes = (const uint32_t*)((const uint8_t*)store + store->records);
    level.meshGeometry = (const uint8_t*)store;
    level.sprites = sceneArray<const Sprite>("SPRS");
    drawsInit(level);
    const PoseCamera* camera = sceneArray<const PoseCamera>("CAMR");
    for (int32_t r = 0; r < 3; r++)
    {
        for (int32_t c = 0; c < 3; c++) {
            gView[r * 4 + c] = camera->rotation[r * 3 + c];
        }
        gView[r * 4 + 3] = 0;
    }
    drawsView(camera->pos, gView);
    gCalls = sceneArray<const CallRecord>("CALL", &gCallCount);
    gItems = sceneArray<ItemRecord>("OITM");
}

bool drawsLabFits()
{
    return drawsFitting(gCalls, gCallCount, gItems) == gCallCount;
}

void drawsLab(int32_t fills)
{
    draws(gCalls, gCallCount, gItems, fills);
}

template <typename T>
static T* lowArray(uint32_t count)
{
    return (T*)sceneAllocLow(X_MAX(count, 1u) * sizeof(T));
}

// A list's arrays with room for draws, models, statics, shadows and faces, in
// low work RAM.
static void listArrays(DrawList &l, uint32_t draws, uint32_t models, uint32_t statics, uint32_t shadows,
                       uint32_t faces)
{
    l.draws = lowArray<DrawScene>(draws);
    l.hinted = lowArray<HintedCall>(models + statics);
    l.items = lowArray<PoseItem>(models);
    l.lights = lowArray<PoseLight>(models);
    l.statics = lowArray<PoseStatic>(statics);
    l.shadows = lowArray<PoseShadow>(shadows);
    l.faces = lowArray<Face>(faces);
    l.vertices = lowArray<Vertex>(2 * faces);
}

void drawsPoison()
{
    DrawList &l = gList;
    memset(l.draws, 0xA5, l.drawCount * sizeof(DrawScene));
    memset(l.hinted, 0xA5, l.hintedCount * sizeof(HintedCall));
    memset(l.items, 0xA5, l.itemCount * sizeof(PoseItem));
    memset(l.lights, 0xA5, l.itemCount * sizeof(PoseLight));
    memset(l.statics, 0xA5, l.staticCount * sizeof(PoseStatic));
    memset(l.shadows, 0xA5, l.shadowCount * sizeof(PoseShadow));
    memset(l.faces, 0xA5, l.faceCount * sizeof(Face));
    memset(l.vertices, 0xA5, 2 * l.faceCount * sizeof(Vertex));
    l.drawCount = l.itemCount = l.staticCount = l.shadowCount = l.faceCount = l.hintedCount = 0;
}

// Two lists: their draws, items, lights, statics, shadows and faces that
// differ (a face's two vertices as made: corners, and the sprite's shade or
// the rectangle's colour), or lists of other lengths.
static int32_t listsOff(const DrawList &a, const DrawList &b)
{
    int32_t bad = (a.drawCount != b.drawCount) + (a.itemCount != b.itemCount) + (a.staticCount != b.staticCount) +
                  (a.shadowCount != b.shadowCount) + (a.faceCount != b.faceCount) + (a.hintedCount != b.hintedCount);
    for (uint32_t i = 0; i < X_MIN(a.drawCount, b.drawCount); i++) {
        bad += memcmp(a.draws + i, b.draws + i, sizeof(DrawScene)) != 0;
    }
    for (uint32_t i = 0; i < X_MIN(a.hintedCount, b.hintedCount); i++) {
        bad += memcmp(a.hinted + i, b.hinted + i, sizeof(HintedCall)) != 0;
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
        listArrays(mine, SELDOM_MODELS * 32 + SELDOM_CALLS, SELDOM_MODELS, SELDOM_CALLS, SELDOM_CALLS, SELDOM_CALLS);
        listArrays(game, SELDOM_MODELS * 32 + SELDOM_CALLS, SELDOM_MODELS, SELDOM_CALLS, SELDOM_CALLS, SELDOM_CALLS);
        if (!made || !madeItems || !madeAnims || !mine.vertices || !game.vertices)
            return -1;
    }
    memcpy(madeAnims, sceneAnims, anims * sizeof(GameAnim));    // Lara's arms' own

    DrawsContext ctx = gCtx;
    ctx.calls = made;
    ctx.items = madeItems;
    ctx.anims = madeAnims;
    drawsOn(ctx, mine);

    // the hints' masks every other thing's, the others each pass: calls
    // hinted and not, closed with draws and without
    int32_t hintedCalls = 0, hintedStatics = 0;

    static const uint8_t kRates[] = { 0, 1, 2, 4, 3, 5, 6, 8 };
    int32_t bad = 0, models = 0, posed = 0, lerped = 0, stills = 0, lara = 0, extras = 0, walls = 0, given = 0;
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
                bool found = false;
                for (int32_t tries = 0; tries < 64 && !found; tries++)
                {
                    it = sceneItems[rnd() % items];
                    found = drawable(it) && (!want || sceneAnims[it.item.animIndex].frameBegin >= 3);
                }
                if (!found)
                {
                    n--;        // none the scene holds every mesh of: no call
                    continue;
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
                c.args[1] = (g % 5 == 2) ? int32_t(1 + rnd() % 4) : 0;     // now and then under a matrix given
                given += c.args[1] != 0;
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
                const GameStaticMesh &own = gCtx.statics[id];
                if ((own.flags & STATIC_MESH_FLAG_VISIBLE) && gCtx.meshIndex[own.meshIndex] == 0xFFFF)
                {
                    n--;        // nor the call's own (out of sight in the scene): no call
                    continue;
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

        const uint32_t masks[2] = { pass & 1 ? 0xAAAAAAAA : 0x55555555, pass & 1 ? 0x55555555 : 0xAAAAAAAA };
        ctx.hintItems = masks[0];
        ctx.hintStatics = masks[1];
        gameDrawsHints(masks);
        ctx.callCount = n;
        drawsRun(ctx, mine, 0);
        gameDrawsRun(made, n, madeItems, madeAnims, 0, game);
        bad += listsOff(mine, game);
        for (uint32_t i = 0; i < game.hintedCount; i++)
        {
            hintedCalls++;
            hintedStatics += game.hinted[i].kind;
        }
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
    gameDrawsHints(&gCtx.hintItems);
    bool enemies = false;
    for (uint32_t i = 0; i < items; i++) {
        enemies = enemies || (sceneItems[i].item.type != 0 && sceneItems[i].item.extra);
    }
    return bad + !(hintedCalls - hintedStatics) + (staticsDrawn ? !hintedStatics : 0) + !posed + !(models - posed) + !lerped + !stills + !lara + (enemies && !extras) + !early[0] +
           !early[1] + !early[2] + !early[3] + !early[4] +
           (statics ? (statics > hidden ? !staticsDrawn : 0) + !(statics - staticsDrawn) + !hidden : 0) + !spritesDrawn +
           !(spritesMade - spritesDrawn) + !clamped + !unclamped + !walls + !given;
}
