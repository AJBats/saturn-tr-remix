// Pose (pose.h): the items through pose.s.
#include "pose.h"
#include "draws.h"
#include "platform.h"

#include <stddef.h>
#include <string.h>

namespace render {

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

// The plan's prices, cycles at the ledge (Mednafen): an item's light, a drawn
// matrix.
#define PRICE_LIGHT     1150
#define PRICE_MATRIX    430

static PoseContext gCtx;
static PoseContext gSlaveCtx __attribute__((aligned(16)));
static PoseMatrix gStack[POSE_STACK] __attribute__((aligned(16)));
static PoseMatrix gSlaveStack[POSE_STACK] __attribute__((aligned(16)));
static DrawXform gOut[MAX_DRAWS];
static PoseLit gLit[MAX_DRAW_ITEMS];
static const PoseItem* gItems;
static uint32_t gCount;
static const PoseLight* gRooms;
static const PoseStatic* gStatics;
static uint32_t gStaticCount;
static const PoseShadow* gShadows;
static uint32_t gShadowCount;

int32_t poseDrawn(const PoseItem &item)
{
    int32_t drawn = 0;
    for (int32_t k = 0; k < item.count; k++) {
        drawn += item.lara || (item.visibleMask >> k & 1);
    }
    return drawn;
}

// The shadows of items first .. end - 1: their run in the shadows (in draw
// order, as their items are).
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

void poseInit(const PoseLevel &level)
{
    gCtx.sines = level.sines;
    gCtx.divTable = divTable;
    gCtx.nodes = level.nodes;
    gCtx.out = gOut;
    gCtx.stack = gStack;
    gCtx.lights = level.lights;
    gCtx.lit = gLit;
    // the slave's: its own stack and scratch
    gSlaveCtx = gCtx;
    gSlaveCtx.stack = gSlaveStack;
}

void poseView(const int32_t camera[3], const int32_t view[12])
{
    for (int32_t i = 0; i < 3; i++) {
        gCtx.camera[i] = gSlaveCtx.camera[i] = camera[i];
    }
    for (int32_t k = 0; k < 12; k++) {
        gStack[0].m[k] = gSlaveStack[0].m[k] = view[k];
    }
}

void poseSet(const PoseItem* items, uint32_t count, const PoseLight* rooms, const PoseStatic* statics,
             uint32_t staticCount, const PoseShadow* shadows, uint32_t shadowCount, const uint8_t* frames)
{
    gItems = items;
    gCount = count;
    gRooms = rooms;
    gStatics = statics;
    gStaticCount = staticCount;
    gShadows = shadows;
    gShadowCount = shadowCount;
    PoseContext* const contexts[2] = { &gCtx, &gSlaveCtx };
    for (PoseContext* ctx : contexts)
    {
        ctx->items = items;
        ctx->rooms = rooms;
        ctx->frames = frames;
    }
}

void poseUse(const DrawList &list)
{
    poseSet(list.items, list.itemCount, list.lights, list.statics, list.staticCount, list.shadows, list.shadowCount,
            nullptr);
}

DrawXform* poseXforms()
{
    return gOut;
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
    return PRICE_LIGHT + PRICE_MATRIX * poseDrawn(gItems[item]);
}

uint32_t poseFirstDraw(uint32_t item)
{
    return gItems[item].firstDraw;
}

int32_t poseDraws(uint32_t item)
{
    return poseDrawn(gItems[item]);
}

void poseDrop(uint32_t first, uint32_t count)
{
    for (uint32_t i = first; i < first + count; i++)
    {
        cacheDrop(gCtx.out + gItems[i].firstDraw, poseDrawn(gItems[i]) * sizeof(DrawXform));
        cacheDrop(gLit + i, sizeof(PoseLit));
    }
    uint32_t from, to;
    shadowsOf(first, first + count, from, to);
    for (uint32_t s = from; s < to; s++) {
        cacheDrop(gCtx.out + gShadows[s].draw, sizeof(DrawXform));
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

PoseContext& poseContext(int32_t cpu)
{
    return cpu ? gSlaveCtx : gCtx;
}

}
