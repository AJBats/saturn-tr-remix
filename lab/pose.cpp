// Stage 1 the new way (pose.h): the scene's items through pose.s, and its
// checks against the game's.
#include "pose.h"
#include "game.h"

#include <stddef.h>
#include <string.h>

static_assert(offsetof(PoseContext, camera) == 16 && offsetof(PoseContext, op) == 36 &&
              offsetof(PoseContext, anglesA) == 48 && offsetof(PoseContext, left) == 64 &&
              offsetof(PoseContext, torso) == 68 && offsetof(PoseContext, armA) == 80 &&
              offsetof(PoseContext, armB) == 88 && offsetof(PoseContext, armLerp) == 96 &&
              offsetof(PoseContext, posLerp) == 104, "pose.s's PoseContext");
static_assert(sizeof(PoseItem) == 104 && offsetof(PoseItem, extraCount) == 46 && offsetof(PoseItem, torso) == 56 &&
              offsetof(PoseItem, weapon) == 68 && offsetof(PoseItem, arms) == 72 && sizeof(PoseArm) == 16,
              "pose.s's PoseItem");

#define NO_FRAME        0xFFFFFFFF
#define NODE_FLAG_ROTS  (4 | 8 | 16)

static PoseContext gCtx;
static PoseMatrix gStack[POSE_STACK] __attribute__((aligned(16)));
static const PoseItem* gItems;
static uint32_t gCount;
static const uint8_t* gNodes;
static uint32_t gNodesBytes;
static const uint8_t* gFrames;
static uint32_t gFramesBytes;
static const DrawXform* gXforms;

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
    gCtx.out = (PoseMatrix*)sceneAlloc(sceneInfo().draws * sizeof(PoseMatrix));
    gCtx.stack = gStack;
    PoseMatrix &view = gStack[0];
    for (int32_t r = 0; r < 3; r++)
    {
        for (int32_t c = 0; c < 3; c++) {
            view.m[r * 4 + c] = camera->rotation[r * 3 + c];
        }
        view.m[r * 4 + 3] = 0;
    }
}

void pose()
{
    poseAsm(gItems, gCount, &gCtx);
}

// The meshes an item draws: Lara's all, another's those its mask shows.
static int32_t drawnOf(const PoseItem &item)
{
    int32_t drawn = 0;
    for (int32_t k = 0; k < item.count; k++) {
        drawn += item.lara || (item.visibleMask >> k & 1);
    }
    return drawn;
}

int32_t poseCheck()
{
    int32_t bad = 0;
    for (uint32_t i = 0; i < gCount; i++)
    {
        const PoseItem &item = gItems[i];
        int32_t drawn = drawnOf(item);
        for (int32_t k = 0; k < drawn; k++) {
            bad += memcmp(gCtx.out + item.firstDraw + k, gXforms[item.firstDraw + k].m, sizeof(PoseMatrix)) != 0;
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
    static PoseMatrix* outNew;
    if (!frames)
    {
        nodes = (uint8_t*)sceneAllocLow(gNodesBytes + nodeBytes);
        frames = (uint8_t*)sceneAllocLow(gFramesBytes + frameBytes);
        items = (PoseItem*)sceneAllocLow(n * sizeof(PoseItem));
        outGame = (PoseMatrix*)sceneAllocLow(draws * sizeof(PoseMatrix));
        outNew = (PoseMatrix*)sceneAllocLow(draws * sizeof(PoseMatrix));
        if (!nodes || !frames || !items || !outGame || !outNew)
            return -1;
        memcpy(nodes, gNodes, gNodesBytes);
        memcpy(frames, gFrames, gFramesBytes);
    }

    int32_t bad = 0, cover[COVER_MAX] = {};
    PoseContext ctx = gCtx;
    ctx.nodes = nodes;
    ctx.frames = frames;
    ctx.out = outNew;
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
        memset(outNew, 0xAA, first * sizeof(PoseMatrix));
        gamePoseRun(items, n, nodes, frames, outGame);
        poseAsm(items, n, &ctx);
        for (uint32_t k = 0; k < first; k++) {
            bad += memcmp(outGame + k, outNew + k, sizeof(PoseMatrix)) != 0;
        }
    }
    for (int32_t c = 0; c < COVER_MAX; c++) {
        bad += !cover[c];
    }
    return bad;
}
