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
    const PoseLight* rooms;     // +176 what lights each item (PLIT)
    const void* lights;         // +180 LGHT
    struct PoseLit* lit;        // +184 the items' light, by item
    const struct PoseLit* litNow;   // +188 pose.s's own
    const PoseItem* items;      // +192 the shadows' items
    int32_t pad[3];
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

// The scene's items for the new way: its context, and the draws' transforms
// it writes, the scene's (XFRM) for the draws not an item's mesh.
void poseInit();

// The items, lights, statics and shadows posed: a draw list's (draws.h, its
// frames as places), or (nullptr) the scene's; false: more items than room.
struct DrawList;
bool poseUse(const DrawList* list);
DrawXform* poseXforms();

// The items' meshes' transforms in poseXforms garbled, so a frame that reads
// them before pose writes them shows.
void posePoison();

// The scene's items posed (their light from poseLight); against the game's
// (XFRM): the draws whose matrix or light differs.
void pose();
int32_t poseCheck();

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

// What the scene seldom gives pose: every item again, many times over, on
// made-up frames, lerps of every kind, extra angles, Lara's arms (with and
// without their own frames, turned by her basis or not, the shotgun's
// torso), against the game's way on the same: the matrices that differ.
int32_t poseSeldomCheck();

// The items' light (pose.s poseLightAsm); against the game's (XFRM): the
// draws that differ. Made up: items, rooms and lights the scene seldom gives
// (divisors of 0 and -1, a light on the point lit, own intensities, rooms
// without lights, overflowing radii) against the game's way on the same.
void poseLight();
int32_t poseLightCheck();
int32_t poseLightSeldomCheck();

// The static meshes' and the shadows' transforms (pose.s), into poseXforms
// by draw (the shadows' items lit); against the game's (XFRM): those that
// differ. Made up: quarter turns, intensities, places, the fog, lerped boxes,
// floors and yaws the scene seldom gives, against the game's way on them.
void poseStatics();
void poseShadows();
int32_t poseStaticsCheck();
int32_t poseStaticsSeldomCheck();

#endif
