// Stage 1 the new way: pose. The items' node matrices, each exactly the
// game's (openlara draw.h drawModel, drawNodesLerp and drawLaraNodesLerp on
// common.cpp's matrix arithmetic), every item in one call (pose.s): no call
// an operation, a rotation's >> 14 in three instructions where the game
// calls libgcc's shift of one bit at a time.
#ifndef POSE_H
#define POSE_H

#include <stdint.h>

#include "scene.h"

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
    const uint8_t* nodes;       // +8  PNOD
    const uint8_t* frames;      // +12 PFRM
    int32_t camera[3];          // +16
    PoseMatrix* out;            // +28 the draws' matrices, by draw
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
};

enum { POSE_STILL, POSE_HALF, POSE_QUARTER, POSE_THREE, POSE_SLOW };

extern "C" void poseAsm(const PoseItem* items, int32_t count, PoseContext* ctx);

// The scene's items for the new way: its context and where the matrices go.
void poseInit();

// The scene's items posed; against the game's (XFRM): the matrices that differ.
void pose();
int32_t poseCheck();

// What the scene seldom gives pose: every item again, many times over, on
// made-up frames, lerps of every kind, extra angles, Lara's arms (with and
// without their own frames, turned by her basis or not, the shotgun's
// torso), against the game's way on the same: the matrices that differ.
int32_t poseSeldomCheck();

#endif
