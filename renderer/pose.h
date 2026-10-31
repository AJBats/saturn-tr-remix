// Pose: the items' light and node matrices, each exactly the game's
// (openlara draw.h drawModel, drawNodesLerp and drawLaraNodesLerp on
// common.cpp's matrix arithmetic, calcLightingDynamic), every item in one
// call (pose.s): no call an operation, a rotation's >> 14 in three
// instructions where the game calls libgcc's shift of one bit at a time; and
// the statics' and shadows' transforms.
#ifndef RENDER_POSE_H
#define RENDER_POSE_H

#include <stdint.h>

#include "types.h"

namespace render {

// A matrix as the game keeps it: rows m0 m1 m2 t, int32 each (DrawXform.m).
struct PoseMatrix
{
    int32_t m[12];
};

#define POSE_STACK  9       // the game's 8, and the lerp's copy above the top

// What pose.s reads, at GBR; the fields from op on are its own, an item's.
struct PoseContext
{
    const uint32_t* sines;      // +0  gSinCosTable: sin << 16 | cos (low halves)
    const uint16_t* divTable;   // +4
    const uint8_t* nodes;       // +8  the models' nodes
    const uint8_t* frames;      // +12 where the items' frames lie (none: their places are pointers)
    int32_t camera[3];          // +16
    DrawXform* out;             // +28 the draws' matrices and light, by draw
    PoseMatrix* stack;          // +32 POSE_STACK; the first the view's
    void* op;                   // +36 a node's frame: still or lerped
    int32_t kind;               // +40 the lerp: POSE_*
    int32_t t;                  // +44 POSE_SLOW's step
    const uint32_t* anglesA;    // +48 the next node's packed angles
    const uint32_t* anglesB;    // +52
    const int16_t* extra;       // +56 the next extra angle, or none
    const int16_t* first;       // +60 the first node's place
    int32_t left;               // +64 items
    int32_t torso[3];           // +68 Lara's torso's sine offsets: y, x, z
    const uint32_t* armA[2];    // +80 her arms' angles
    const uint32_t* armB[2];    // +88
    int32_t armLerp[2];         // +96
    int16_t posLerp[4];         // +104
    int32_t light[16];          // +112 the light's scratch
    const PoseLight* rooms;     // +176 what lights each item
    const void* lights;         // +180 the rooms' lights
    struct PoseLit* lit;        // +184 the items' light, by item
    const struct PoseLit* litNow;   // +188 pose.s's own
    const PoseItem* items;      // +192 the shadows' items
    const PoseMatrix* matrices; // +196 the matrices items are drawn under (PoseItem.matrix)
    int32_t pad[2];
};

// An item's light as drawModel leaves it for its meshes (DrawXform's ambient,
// range, light).
struct PoseLit
{
    int32_t ambient, range;
    int16_t view[3];
    int16_t pad;
};

enum { POSE_STILL, POSE_HALF, POSE_QUARTER, POSE_THREE, POSE_SLOW };

extern "C" void poseAsm(const PoseItem* items, int32_t count, PoseContext* ctx);
extern "C" void poseLightAsm(const PoseItem* items, int32_t count, PoseContext* ctx);
extern "C" void poseStaticsAsm(const PoseStatic* statics, int32_t count, PoseContext* ctx);
extern "C" void poseShadowsAsm(const PoseShadow* shadows, int32_t count, PoseContext* ctx);

// The level's: the game's sines and cosines (gSinCosTable), its models'
// nodes, its rooms' lights; the frame's view: the camera and the view's
// matrix (12 int32, no translation).
struct PoseLevel
{
    const uint32_t* sines;
    const uint8_t* nodes;
    const void* lights;
};
void poseInit(const PoseLevel &level);
void poseView(const int32_t camera[3], const int32_t view[12]);

// The frame's matrices items are drawn under (PoseItem.matrix), or none.
void poseMatrices(const PoseMatrix* matrices);

// The items, lights, statics and shadows to pose: a draw list's (draws.h:
// its frames as places), or others with their frames in frames; and the
// draws' transforms pose writes, by draw.
void poseUse(const DrawList &list);
void poseSet(const PoseItem* items, uint32_t count, const PoseLight* rooms, const PoseStatic* statics,
             uint32_t staticCount, const PoseShadow* shadows, uint32_t shadowCount, const uint8_t* frames);
DrawXform* poseXforms();

// For a frame shared between the CPUs: items first .. first + count - 1 lit
// and posed on a CPU (0 the master, 1 the slave, each with its own context),
// and their shadows placed;
// the items; an item's price in cycles, the plan's (its light, its drawn
// matrices); its first draw, and its draws (none if no mesh shows); the
// master's cached copies of those items' transforms dropped (the slave made
// them).
void poseItems(int32_t cpu, uint32_t first, uint32_t count);

// A CPU's share of the items lit and posed, run by run (slave: an item's 1 if
// the slave poses it); the master's cached copies of the slave's dropped.
void poseShare(int32_t cpu, const uint8_t* slave);
void poseDropShare(const uint8_t* slave);
uint32_t poseCount();
int32_t posePrice(uint32_t item);
uint32_t poseFirstDraw(uint32_t item);
int32_t poseDraws(uint32_t item);
void poseDrop(uint32_t first, uint32_t count);

// The meshes an item draws: Lara's all, another's those its mask shows.
int32_t poseDrawn(const PoseItem &item);

// The static meshes' and the shadows' transforms (pose.s), into poseXforms
// by draw (the shadows' items lit).
void poseStatics();
void poseShadows();

// A CPU's context (0 the master's, 1 the slave's), for checks.
PoseContext& poseContext(int32_t cpu);

}

#endif
