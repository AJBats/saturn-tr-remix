// Stage 1 the new way (pose.h): the scene's items through pose.s, and its
// checks against the game's.
#include "pose.h"
#include "game.h"
#include "platform.h"

#include <stddef.h>
#include <string.h>

static_assert(offsetof(PoseContext, camera) == 16 && offsetof(PoseContext, op) == 36 &&
              offsetof(PoseContext, anglesA) == 48 && offsetof(PoseContext, left) == 64 &&
              offsetof(PoseContext, torso) == 68 && offsetof(PoseContext, armA) == 80 &&
              offsetof(PoseContext, armB) == 88 && offsetof(PoseContext, armLerp) == 96 &&
              offsetof(PoseContext, posLerp) == 104, "pose.s's PoseContext");
static_assert(offsetof(PoseContext, light) == 112 && offsetof(PoseContext, rooms) == 176 &&
              offsetof(PoseContext, lit) == 184 && sizeof(PoseLit) == 16 && sizeof(PoseLight) == 12,
              "pose.s's light");
static_assert(offsetof(PoseContext, items) == 192 && sizeof(PoseStatic) == 20 && offsetof(PoseStatic, x) == 8 &&
              sizeof(PoseShadow) == 8 && sizeof(DrawXform) == 72 && offsetof(DrawXform, shadow) == 62,
              "pose.s's statics and shadows");
static_assert(sizeof(PoseItem) == 104 && offsetof(PoseItem, extraCount) == 46 && offsetof(PoseItem, torso) == 56 &&
              offsetof(PoseItem, weapon) == 68 && offsetof(PoseItem, arms) == 72 && sizeof(PoseArm) == 16,
              "pose.s's PoseItem");

#define NO_FRAME        0xFFFFFFFF
#define NODE_FLAG_ROTS  (4 | 8 | 16)

static PoseContext gCtx;
static PoseContext gSlaveCtx __attribute__((aligned(16)));
static PoseMatrix gStack[POSE_STACK] __attribute__((aligned(16)));
static PoseLit* gLit;
static const PoseLight* gRooms;
static const PoseStatic* gStatics;
static uint32_t gStaticCount;
static const PoseShadow* gShadows;
static uint32_t gShadowCount;

// The plan's prices, cycles at the ledge (Mednafen): an item's light, a drawn
// matrix.
#define PRICE_LIGHT     1150
#define PRICE_MATRIX    430
static const PoseItem* gItems;
static uint32_t gCount;
static const uint8_t* gNodes;
static uint32_t gNodesBytes;
static const uint8_t* gFrames;
static uint32_t gFramesBytes;
static const DrawXform* gXforms;

// The meshes an item draws: Lara's all, another's those its mask shows.
static int32_t drawnOf(const PoseItem &item)
{
    int32_t drawn = 0;
    for (int32_t k = 0; k < item.count; k++) {
        drawn += item.lara || (item.visibleMask >> k & 1);
    }
    return drawn;
}

// The shadows of items first .. end - 1: their run in SHDW (in draw order,
// as their items are).
static void shadowsOf(uint32_t first, uint32_t end, uint32_t &from, uint32_t &to)
{
    from = 0;
    while (from < gShadowCount && gShadows[from].item < first) {
        from++;
    }
    to = from;
    while (to < gShadowCount && gShadows[to].item < end) {
        to++;
    }
}

void poseInit()
{
    uint32_t bytes;
    gItems = (const PoseItem*)sceneChunk("POSE", &bytes);
    gCount = bytes / sizeof(PoseItem);
    gNodes = (const uint8_t*)sceneChunk("PNOD", &gNodesBytes);
    gFrames = (const uint8_t*)sceneChunk("PFRM", &gFramesBytes);
    gXforms = sceneArray<const DrawXform>("XFRM");
    const PoseCamera* camera = sceneArray<const PoseCamera>("CAMR");

    gCtx.sines = sceneArray<const uint32_t>("SINC");
    gCtx.divTable = divTable;
    gCtx.nodes = gNodes;
    gCtx.frames = gFrames;
    for (int32_t i = 0; i < 3; i++) {
        gCtx.camera[i] = camera->pos[i];
    }
    gCtx.out = (DrawXform*)sceneAlloc(sceneInfo().draws * sizeof(DrawXform));
    memcpy(gCtx.out, gXforms, sceneInfo().draws * sizeof(DrawXform));
    gCtx.stack = gStack;
    gRooms = sceneArray<const PoseLight>("PLIT");
    gCtx.rooms = gRooms;
    gCtx.lights = sceneChunk("LGHT");
    gCtx.lit = (PoseLit*)sceneAlloc(gCount * sizeof(PoseLit));
    gLit = gCtx.lit;
    gCtx.items = gItems;
    gStatics = sceneArray<const PoseStatic>("STAT", &gStaticCount);
    gShadows = sceneArray<const PoseShadow>("SHDW", &gShadowCount);
    PoseMatrix &view = gStack[0];
    for (int32_t r = 0; r < 3; r++)
    {
        for (int32_t c = 0; c < 3; c++) {
            view.m[r * 4 + c] = camera->rotation[r * 3 + c];
        }
        view.m[r * 4 + 3] = 0;
    }
    // the slave's: its own stack and scratch
    gSlaveCtx = gCtx;
    gSlaveCtx.stack = (PoseMatrix*)sceneAlloc(POSE_STACK * sizeof(PoseMatrix));
    gSlaveCtx.stack[0] = view;
}

void pose()
{
    poseAsm(gItems, gCount, &gCtx);
}

DrawXform* poseXforms()
{
    return gCtx.out;
}

void poseItems(int32_t cpu, uint32_t first, uint32_t count)
{
    if (!count)
        return;
    PoseContext* ctx = cpu ? &gSlaveCtx : &gCtx;
    ctx->lit = gLit + first;
    ctx->rooms = gRooms + first;
    poseLightAsm(gItems + first, count, ctx);
    poseAsm(gItems + first, count, ctx);
    ctx->lit = gLit;
    ctx->rooms = gRooms;
    uint32_t from, to;
    shadowsOf(first, first + count, from, to);
    if (from < to) {
        poseShadowsAsm(gShadows + from, to - from, ctx);
    }
}

void poseShare(int32_t cpu, const uint8_t* slave)
{
    uint32_t first = 0;
    while (first < gCount)
    {
        uint32_t end = first + 1;
        while (end < gCount && slave[end] == slave[first]) {
            end++;
        }
        if ((slave[first] != 0) == (cpu != 0)) {
            poseItems(cpu, first, end - first);
        }
        first = end;
    }
}

void poseDropShare(const uint8_t* slave)
{
    for (uint32_t i = 0; i < gCount; i++) {
        if (slave[i]) poseDrop(i, 1);
    }
}

uint32_t poseCount()
{
    return gCount;
}

int32_t posePrice(uint32_t item)
{
    return PRICE_LIGHT + PRICE_MATRIX * drawnOf(gItems[item]);
}

uint32_t poseFirstDraw(uint32_t item)
{
    return gItems[item].firstDraw;
}

int32_t poseDraws(uint32_t item)
{
    return drawnOf(gItems[item]);
}

void poseDrop(uint32_t first, uint32_t count)
{
    for (uint32_t i = first; i < first + count; i++)
    {
        cacheDrop(gCtx.out + gItems[i].firstDraw, drawnOf(gItems[i]) * sizeof(DrawXform));
        cacheDrop(gLit + i, sizeof(PoseLit));
    }
    uint32_t from, to;
    shadowsOf(first, first + count, from, to);
    for (uint32_t s = from; s < to; s++) {
        cacheDrop(gCtx.out + gShadows[s].draw, sizeof(DrawXform));
    }
}

void posePoison()
{
    for (uint32_t i = 0; i < gCount; i++)
    {
        const PoseItem &item = gItems[i];
        memset(gCtx.out + item.firstDraw, 0xA5, drawnOf(item) * sizeof(DrawXform));
    }
    for (uint32_t i = 0; i < gStaticCount; i++) {
        memset(gCtx.out + gStatics[i].draw, 0xA5, sizeof(DrawXform));
    }
    for (uint32_t i = 0; i < gShadowCount; i++) {
        memset(gCtx.out + gShadows[i].draw, 0xA5, sizeof(DrawXform));
    }
}

void poseStatics()
{
    poseStaticsAsm(gStatics, gStaticCount, &gCtx);
}

void poseShadows()
{
    poseShadowsAsm(gShadows, gShadowCount, &gCtx);
}

int32_t poseStaticsCheck()
{
    int32_t statics, shadows;
    return gameStaticsCheck(gCtx.out, statics, shadows);
}

int32_t poseCheck()
{
    int32_t bad = 0;
    for (uint32_t i = 0; i < gCount; i++)
    {
        const PoseItem &item = gItems[i];
        int32_t drawn = drawnOf(item);
        for (int32_t k = 0; k < drawn; k++)
        {
            const DrawXform &made = gCtx.out[item.firstDraw + k], &game = gXforms[item.firstDraw + k];
            bad += memcmp(made.m, game.m, sizeof(made.m)) || made.ambient != game.ambient ||
                   made.range != game.range || (game.range > 0 && memcmp(made.light, game.light, sizeof(made.light)));
        }
    }
    return bad;
}

// --- made-up inputs ----------------------------------------------------------

static uint32_t gSeed = 0x2545F491;

static uint32_t rnd()
{
    gSeed = gSeed * 1664525 + 1013904223;
    return gSeed;
}

// An angle, a fifth of them 0 and a fifth below 16 (no turn either way).
static int16_t rndAngle()
{
    uint32_t r = rnd();
    switch (r >> 29)
    {
        case 0: return 0;
        case 1: return int16_t(r & 15);
        default: return int16_t(r >> 8);
    }
}

// A frame like the one at offset from, its place moved and its packed
// angles made up, some axes 0.
static void rndFrame(uint8_t* dst, const uint8_t* from, int32_t count)
{
    memcpy(dst, from, 20 + 4 * count);
    int16_t* pos = (int16_t*)(dst + 12);
    for (int32_t k = 0; k < 3; k++) {
        pos[k] = int16_t(pos[k] + int32_t(rnd() >> 22) - 512);
    }
    uint32_t* angles = (uint32_t*)(dst + 20);
    for (int32_t k = 0; k < count; k++)
    {
        uint32_t a = rnd(), zeros = rnd() >> 29;
        if (zeros & 1) a &= ~0x3FF0u;                   // x
        if (zeros & 2) a &= ~(0xFu | 0xFC000000u);      // y
        if (zeros & 4) a &= ~0x03FF0000u;               // z
        angles[k] = a;
    }
}

// Lerps of each kind: (delta, rate), the first two still. getFrames gives
// a delta below the rate, so a lerp stays between its frames and a
// rotation's entries inside 2^14, as pose.s's shift takes them.
static const int16_t LERPS[][2] = {
    { 0, 1 }, { 0, 4 }, { 1, 2 }, { 1, 4 }, { 2, 4 }, { 3, 4 }, { 1, 3 }, { 2, 3 },
    { 1, 5 }, { 4, 5 }, { 2, 6 }, { 5, 7 }, { 7, 9 }, { 8, 9 }, { 1, 9 }
};
#define LERPS_COUNT     int32_t(sizeof(LERPS) / sizeof(LERPS[0]))
#define SELDOM_PASSES   (LERPS_COUNT * 4)

enum { COVER_STILL, COVER_HALF, COVER_QUARTER, COVER_THREE, COVER_SLOW, COVER_EXTRA, COVER_ARM_FRAME,
       COVER_ARM_LERP, COVER_BASIS, COVER_SHOTGUN, COVER_PUSH, COVER_MAX };

// The scene's items again, and Lara's as another's: her nodes through the
// walk their flags push and pop, with made-up flags for extra angles.
static uint32_t madeUpOf(uint32_t* of)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < gCount; i++) {
        of[n++] = i;
    }
    for (uint32_t i = 0; i < gCount; i++) {
        if (gItems[i].lara) of[n++] = i | 0x80000000;
    }
    return n;
}

int32_t poseSeldomCheck()
{
    // per item: its frames A and B, an arm's frame for each arm
    static uint32_t of[2 * 256];
    if (2 * gCount > sizeof(of) / sizeof(of[0]))
        return -1;
    uint32_t n = madeUpOf(of);
    uint32_t frameBytes = 0, nodeBytes = 0, draws = 0;
    for (uint32_t m = 0; m < n; m++)
    {
        const PoseItem &from = gItems[of[m] & 0xFFFF];
        frameBytes += 4 * (20 + 4 * from.count);
        nodeBytes += 8 * from.count;
        draws += from.count;
    }
    static uint8_t* nodes;
    static uint8_t* frames;
    static PoseItem* items;
    static PoseMatrix* outGame;
    static DrawXform* outNew;
    static PoseLit* lit;
    if (!frames)
    {
        nodes = (uint8_t*)sceneAllocLow(gNodesBytes + nodeBytes);
        frames = (uint8_t*)sceneAllocLow(gFramesBytes + frameBytes);
        items = (PoseItem*)sceneAllocLow(n * sizeof(PoseItem));
        outGame = (PoseMatrix*)sceneAllocLow(draws * sizeof(PoseMatrix));
        outNew = (DrawXform*)sceneAllocLow(draws * sizeof(DrawXform));
        lit = (PoseLit*)sceneAllocLow(n * sizeof(PoseLit));
        if (!nodes || !frames || !items || !outGame || !outNew || !lit)
            return -1;
        memcpy(nodes, gNodes, gNodesBytes);
        memcpy(frames, gFrames, gFramesBytes);
    }

    int32_t bad = 0, cover[COVER_MAX] = {};
    PoseContext ctx = gCtx;
    ctx.nodes = nodes;
    ctx.frames = frames;
    ctx.out = outNew;
    ctx.lit = lit;
    for (int32_t pass = 0; pass < SELDOM_PASSES; pass++)
    {
        const int16_t* lerp = LERPS[pass % LERPS_COUNT];
        int32_t variant = pass / LERPS_COUNT;
        uint32_t at = gFramesBytes, nodesAt = gNodesBytes, first = 0;
        for (uint32_t m = 0; m < n; m++)
        {
            PoseItem &item = items[m];
            item = gItems[of[m] & 0xFFFF];
            if (of[m] & 0x80000000)
            {
                // her nodes, up to 4 of them taking an extra angle about an axis
                item.lara = 0;
                uint16_t* made = (uint16_t*)(nodes + nodesAt);
                memcpy(made, gNodes + item.nodes, 8 * (item.count - 1));
                for (int32_t k = 0; k + 1 < item.count; k++) {
                    made[k * 4 + 3] &= ~NODE_FLAG_ROTS;
                }
                for (int32_t e = rnd() >> 30; e >= 0; e--) {
                    made[(rnd() >> 16) % (item.count - 1) * 4 + 3] |= 4 << (rnd() >> 16) % 3;
                }
                item.nodes = nodesAt;
                nodesAt += 8 * item.count;
            }
            int32_t size = 20 + 4 * item.count;
            const uint8_t* from = gFrames + item.frameA;
            uint32_t frame[4];
            for (int32_t f = 0; f < 4; f++)
            {
                rndFrame(frames + at, from, item.count);
                frame[f] = at;
                at += size;
            }
            item.frameA = frame[0];
            item.frameB = lerp[0] ? frame[1] : frame[0];
            item.frameDelta = lerp[0];
            item.frameRate = lerp[1];
            item.visibleMask = rnd();
            for (int32_t k = 0; k < 3; k++)
            {
                item.pos[k] = gCtx.camera[k] + int32_t(rnd() >> 17) - 16384;
                item.angle[k] = rndAngle();
            }
            if (variant & 1) {
                item.angle[0] = item.angle[2] = 0;
            }
            // an enemy's extra angles, as many as its nodes' flags take
            int32_t takes = 0;
            for (int32_t k = 0; k + 1 < item.count; k++) {
                takes += __builtin_popcount(((const uint16_t*)(nodes + item.nodes))[k * 4 + 3] & NODE_FLAG_ROTS);
            }
            item.extraCount = !item.lara && takes <= 4 && (variant & 2) ? takes : 0;
            for (int32_t k = 0; k < 4; k++) {
                item.extra[k] = rndAngle();
            }
            if (item.lara)
            {
                for (int32_t k = 0; k < 3; k++)
                {
                    item.torso[k] = rndAngle();
                    item.head[k] = rndAngle();
                }
                item.weapon = pass % 3 == 0 ? 3 : 1;
                for (int32_t a = 0; a < 2; a++)
                {
                    PoseArm &arm = item.arms[a];
                    arm.frame = (pass + variant + a) & 1 ? frame[2 + a] : NO_FRAME;
                    arm.frameRate = (pass >> 2 & 1) + 1;
                    arm.useBasis = rnd() >> 31;
                    for (int32_t k = 0; k < 3; k++) {
                        arm.angle[k] = rndAngle();
                    }
                    cover[COVER_ARM_FRAME] += arm.frame != NO_FRAME;
                    cover[COVER_ARM_LERP] += lerp[0] && (arm.frame == NO_FRAME ? lerp[1] : arm.frameRate) > 1;
                    cover[COVER_BASIS] += arm.useBasis;
                }
                cover[COVER_SHOTGUN] += item.weapon == 3 && item.arms[0].frame != NO_FRAME;
            }
            item.firstDraw = first;
            first += drawnOf(item);
            cover[COVER_EXTRA] += item.extraCount;
            for (int32_t k = 0; k + 1 < item.count && !item.lara; k++) {
                cover[COVER_PUSH] += ((const uint16_t*)(nodes + item.nodes))[k * 4 + 3] & 2 ? 1 : 0;
            }
        }
        int32_t kind = !lerp[0] ? COVER_STILL
                     : lerp[1] == 2 || (lerp[1] == 4 && lerp[0] == 2) ? COVER_HALF
                     : lerp[1] == 4 ? (lerp[0] == 1 ? COVER_QUARTER : COVER_THREE) : COVER_SLOW;
        cover[kind]++;

        memset(outGame, 0x55, first * sizeof(PoseMatrix));
        memset(outNew, 0xAA, first * sizeof(DrawXform));
        gamePoseRun(items, n, nodes, frames, outGame);
        poseAsm(items, n, &ctx);
        for (uint32_t k = 0; k < first; k++) {
            bad += memcmp(outGame[k].m, outNew[k].m, sizeof(outNew[k].m)) != 0;
        }
    }
    for (int32_t c = 0; c < COVER_MAX; c++) {
        bad += !cover[c];
    }
    return bad;
}

// --- the light ---------------------------------------------------------------

void poseLight()
{
    poseLightAsm(gItems, gCount, &gCtx);
}

// Whether a draw's light differs from the game's: ambient, range, and the
// light's direction where the range is above 0 (the game leaves it as it was
// otherwise).
static int32_t litOff(const PoseLit &a, int32_t ambient, int32_t range, const int16_t* view)
{
    return a.ambient != ambient || a.range != range || (range > 0 && memcmp(a.view, view, sizeof(a.view)));
}

int32_t poseLightCheck()
{
    int32_t bad = 0;
    for (uint32_t i = 0; i < gCount; i++)
    {
        const PoseItem &item = gItems[i];
        int32_t drawn = drawnOf(item);
        for (int32_t k = 0; k < drawn; k++)
        {
            const DrawXform &x = gXforms[item.firstDraw + k];
            bad += litOff(gCtx.lit[i], x.ambient, x.range, x.light);
        }
    }
    return bad;
}

struct MadeLight
{
    int16_t x, y, z;
    uint8_t radius, intensity;
};

// A made-up room takes up to 8 lights.
#define MADE_LIGHTS     8
#define LIGHT_PASSES    48

enum { LIT_STILL, LIT_DARK, LIT_ROOM, LIT_RANGE, LIT_ON_POINT, LIT_DIV0, LIT_DIV1, LIT_FOG, LIT_MAX };

// A light at the point plus (dx, dy, dz), placed through its room's place.
static void lightAt(MadeLight &light, PoseLight &room, const int32_t* point, int32_t dx, int32_t dy, int32_t dz)
{
    room.roomX = int16_t((point[0] + dx) >> 8);
    room.roomZ = int16_t((point[2] + dz) >> 8);
    light.x = int16_t(point[0] + dx - (room.roomX << 8));
    light.y = int16_t(point[1] + dy);
    light.z = int16_t(point[2] + dz - (room.roomZ << 8));
}

int32_t poseLightSeldomCheck()
{
    static PoseItem* items;
    static PoseLight* rooms;
    static MadeLight* lights;
    static uint8_t* frames;
    static PoseLit* outGame;
    static PoseLit* outNew;
    uint32_t frameBytes = 0;
    for (uint32_t i = 0; i < gCount; i++) {
        frameBytes += 20 + 4 * gItems[i].count;
    }
    if (!items)
    {
        items = (PoseItem*)sceneAllocLow(gCount * sizeof(PoseItem));
        rooms = (PoseLight*)sceneAllocLow(gCount * sizeof(PoseLight));
        lights = (MadeLight*)sceneAllocLow(gCount * MADE_LIGHTS * sizeof(MadeLight));
        frames = (uint8_t*)sceneAllocLow(frameBytes);
        outGame = (PoseLit*)sceneAllocLow(gCount * sizeof(PoseLit));
        outNew = (PoseLit*)sceneAllocLow(gCount * sizeof(PoseLit));
        if (!items || !rooms || !lights || !frames || !outGame || !outNew)
            return -1;
    }

    int32_t bad = 0, cover[LIT_MAX] = {};
    PoseContext ctx = gCtx;
    ctx.frames = frames;
    ctx.rooms = rooms;
    ctx.lights = lights;
    ctx.lit = outNew;
    for (int32_t pass = 0; pass < LIGHT_PASSES; pass++)
    {
        uint32_t at = 0;
        for (uint32_t i = 0; i < gCount; i++)
        {
            PoseItem &item = items[i];
            item = gItems[i];
            int32_t still = pass & 1;
            for (int32_t k = 0; k < 3; k++)
            {
                item.pos[k] = gCtx.camera[k] + int32_t(rnd() >> 17) - 16384;
                item.angle[k] = still ? 0 : rndAngle();
            }
            // its first frame: a made-up box (a big one now and then)
            int16_t* box = (int16_t*)(frames + at);
            memcpy(box, gFrames + item.frameA, 20 + 4 * item.count);
            int32_t size = (rnd() >> 28) == 0 ? 32767 : 2048;
            for (int32_t k = 0; k < 6; k++) {
                box[k] = int16_t(int32_t(rnd() >> 16) % size);
            }
            item.frameA = at;
            at += 20 + 4 * item.count;

            PoseLight &room = rooms[i];
            room.intensity = (rnd() >> 30) == 0 ? uint8_t(1 + (rnd() >> 24) % 255) : 0;
            room.ambient = uint8_t(rnd() >> 24);
            room.lights = uint8_t(rnd() >> 29);
            room.first = i * MADE_LIGHTS;
            room.roomX = int16_t(int32_t(rnd() >> 25) - 64);
            room.roomZ = int16_t(int32_t(rnd() >> 25) - 64);
            for (int32_t l = 0; l < MADE_LIGHTS; l++)
            {
                MadeLight &light = lights[room.first + l];
                light.x = int16_t(item.pos[0] - (room.roomX << 8) + int32_t(rnd() >> 18) - 8192);
                light.y = int16_t(item.pos[1] + int32_t(rnd() >> 18) - 8192);
                light.z = int16_t(item.pos[2] - (room.roomZ << 8) + int32_t(rnd() >> 18) - 8192);
                static const uint8_t RADII[] = { 255, 200, 182, 181, 100, 40, 1, 0 };
                light.radius = (rnd() >> 30) == 0 ? RADII[rnd() >> 29] : uint8_t(rnd() >> 24);
                light.intensity = uint8_t(rnd() >> 24);
            }
            // the cases the divides and the length turn on, the item unturned so
            // its point is its place plus its box centre
            int32_t forced = (pass >> 1) % 4;
            if (still && !room.intensity && forced)
            {
                int32_t point[3];
                for (int32_t k = 0; k < 3; k++) {
                    point[k] = item.pos[k] + ((box[2 * k + 1] + box[2 * k]) >> 1);
                }
                MadeLight &light = lights[room.first];
                if (forced == 1)
                {
                    // dist + att = 0: att -8176, dist 8176
                    lightAt(light, room, point, 5787, 0, 0);
                    light.radius = 255;
                    cover[LIT_DIV0]++;
                }
                else if (forced == 2)
                {
                    // dist + att = -1
                    lightAt(light, room, point, 5786, 84, 0);
                    light.radius = 255;
                    cover[LIT_DIV1]++;
                }
                else
                {
                    // the only light, on the point: no direction
                    lightAt(light, room, point, 0, 0, 0);
                    light.radius = 100;
                    light.intensity = 255;
                    room.lights = 1;
                    cover[LIT_ON_POINT]++;
                }
                if (!room.lights) {
                    room.lights = 1;
                }
            }
            const int32_t* view = gStack[0].m;
            int32_t depth = (view[8] * (item.pos[0] - gCtx.camera[0]) + view[9] * (item.pos[1] - gCtx.camera[1]) +
                             view[10] * (item.pos[2] - gCtx.camera[2])) >> 14;
            cover[LIT_FOG] += depth > 1024 * 12;
            cover[LIT_STILL] += room.intensity != 0;
            cover[LIT_DARK] += !room.intensity && !room.lights;
            cover[LIT_ROOM] += !room.intensity && room.lights;
        }
        gameLightRun(items, rooms, lights, gCount, frames, (GameLight*)outGame);
        poseLightAsm(items, gCount, &ctx);
        for (uint32_t i = 0; i < gCount; i++)
        {
            bad += litOff(outNew[i], outGame[i].ambient, outGame[i].range, outGame[i].view);
            cover[LIT_RANGE] += outGame[i].range > 0;
        }
    }
    for (int32_t c = 0; c < LIT_MAX; c++) {
        bad += !cover[c];
    }
    return bad;
}

// --- the static meshes and the shadows: made up --------------------------------

#define STATIC_PASSES   32

enum { SH_Q0, SH_Q1, SH_Q2, SH_Q3, SH_FOG, SH_STILL, SH_LERP, SH_MAX };

int32_t poseStaticsSeldomCheck()
{
    // a static mesh and a shadow for every draw slot an item's first mesh
    // has, reusing the scene's items and their light
    static PoseStatic* statics;
    static PoseShadow* shadows;
    static PoseItem* items;
    static uint8_t* frames;
    static DrawXform* outGame;
    static DrawXform* outNew;
    uint32_t frameBytes = 0;
    for (uint32_t i = 0; i < gCount; i++) {
        frameBytes += 2 * (20 + 4 * gItems[i].count);
    }
    uint32_t draws = 2 * gCount;
    if (!statics)
    {
        statics = (PoseStatic*)sceneAllocLow(gCount * sizeof(PoseStatic));
        shadows = (PoseShadow*)sceneAllocLow(gCount * sizeof(PoseShadow));
        items = (PoseItem*)sceneAllocLow(gCount * sizeof(PoseItem));
        frames = (uint8_t*)sceneAllocLow(frameBytes);
        outGame = (DrawXform*)sceneAllocLow(draws * sizeof(DrawXform));
        outNew = (DrawXform*)sceneAllocLow(draws * sizeof(DrawXform));
        if (!statics || !shadows || !items || !frames || !outGame || !outNew)
            return -1;
    }
    int32_t bad = 0, cover[SH_MAX] = {};
    PoseContext ctx = gCtx;
    ctx.frames = frames;
    ctx.out = outNew;
    ctx.items = items;
    const int32_t* view = gStack[0].m;
    for (int32_t pass = 0; pass < STATIC_PASSES; pass++)
    {
        uint32_t at = 0;
        for (uint32_t i = 0; i < gCount; i++)
        {
            PoseStatic &st = statics[i];
            st.draw = 2 * i;
            st.quadrant = (pass + i) & 3;
            st.intensity = int32_t(rnd() >> 19);
            for (int32_t k = 0; k < 3; k++) {
                (&st.x)[k] = gCtx.camera[k] + int32_t(rnd() >> 17) - 16384;
            }
            cover[st.quadrant]++;
            int32_t depth = (view[8] * (st.x - gCtx.camera[0]) + view[9] * (st.y - gCtx.camera[1]) +
                             view[10] * (st.z - gCtx.camera[2])) >> 14;
            cover[SH_FOG] += depth > 1024 * 12;

            // the item with made-up frames and lerp
            PoseItem &item = items[i];
            item = gItems[i];
            uint32_t size = 20 + 4 * item.count;
            for (int32_t f = 0; f < 2; f++)
            {
                rndFrame(frames + at + f * size, gFrames + item.frameA, item.count);
                int16_t* box = (int16_t*)(frames + at + f * size);
                for (int32_t k = 0; k < 6; k++) {
                    box[k] = int16_t(int32_t(rnd() >> 16) % 4096);
                }
            }
            item.frameA = at;
            item.frameB = at + size;
            at += 2 * size;
            static const int16_t LERP[][2] = { { 0, 1 }, { 1, 2 }, { 1, 4 }, { 3, 4 }, { 2, 5 }, { 6, 7 } };
            const int16_t* lerp = LERP[(pass + i) % 6];
            item.frameDelta = lerp[0];
            item.frameRate = lerp[1];
            cover[lerp[0] ? SH_LERP : SH_STILL]++;
            for (int32_t k = 0; k < 3; k++)
            {
                item.pos[k] = gCtx.camera[k] + int32_t(rnd() >> 17) - 16384;
                item.angle[k] = rndAngle();
            }
            PoseShadow &sh = shadows[i];
            sh.draw = 2 * i + 1;
            sh.item = i;
            sh.floor = item.pos[1] + int32_t(rnd() >> 20);
        }
        memset(outGame, 0x55, draws * sizeof(DrawXform));
        memset(outNew, 0xAA, draws * sizeof(DrawXform));
        gameStaticsRun(statics, gCount, shadows, gCount, items, (const GameLight*)gLit, frames, outGame);
        poseStaticsAsm(statics, gCount, &ctx);
        poseShadowsAsm(shadows, gCount, &ctx);
        for (uint32_t i = 0; i < gCount; i++)
        {
            bad += xformOff(outNew[2 * i], outGame[2 * i], false);
            bad += xformOff(outNew[2 * i + 1], outGame[2 * i + 1], true);
        }
    }
    for (int32_t c = 0; c < SH_MAX; c++) {
        bad += !cover[c];
    }
    return bad;
}
