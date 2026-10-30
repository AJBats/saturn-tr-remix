// Stage 1 the new way, the draw list (draws.s): the renderer's side of the
// frame's calls after the walk, exactly the game's way's (gamedraws.cpp), in
// one call, into a DrawList (game.h): the draws in order, each item posed
// with its frames, extras and light, each static's place, each shadow's
// floor, each sprite's and rectangle's face made.
#ifndef DRAWS_H
#define DRAWS_H

#include <stdint.h>

#include "game.h"
#include "scene.h"

struct DrawsContext
{
    const CallRecord* calls;    // +0
    uint32_t callCount;         // +4
    ItemRecord* items;          // +8
    const GameAnim* anims;      // +12
    const uint8_t* frames;      // +16 bytes: AFRM - 2 AFRM_BASE
    const GameModel* models;    // +20
    const uint16_t* meshIndex;  // +24 MIDX
    const uint32_t* modelNodes; // +28 MNOD
    const uint8_t* nodes;       // +32 PNOD
    const GameStaticMesh* statics;  // +36
    const RoomLights* roomLights;   // +40
    const RoomLevel* level;     // +44
    const uint32_t* meshes;     // +48 MESH
    const uint8_t* meshGeometry;    // +52 MGEO
    const Sprite* sprites;      // +56
    int32_t camera[3];          // +60
    const int32_t* view;        // +72 the view's matrix (12 int32, no translation)
    const uint16_t* divTable;   // +76
    int32_t viewport[4];        // +80
    int32_t base;               // +96 the first draw's first vertex; after, the next's
    DrawScene* draws;           // +100
    DrawScene* drawsEnd;        // +104
    PoseItem* poses;            // +108
    PoseLight* lights;          // +112
    uint32_t itemCount;         // +116
    PoseStatic* placed;         // +120
    uint32_t staticCount;       // +124
    PoseShadow* floors;         // +128
    uint32_t shadowCount;       // +132
    Face* faces;                // +136
    Vertex* vertices;           // +140
    uint32_t faceCount;         // +144
    int32_t k20, k18;           // +148 2^20, 2^18
    uint32_t frameA, frameB;    // +156 drawsAsm's own
    int32_t delta, rate, frameSize;
    int32_t viewMin, viewMax;   // +176 VIEW_MIN_F, VIEW_MAX_F
    uint32_t drawCount;         // +184
    uint32_t hintItems;         // +188 the frame's hints' things' bits (index & 31): items'
    uint32_t hintStatics;       // +192 ... statics'
    HintedCall* hinted;         // +196 the next
    uint32_t hintedCount;       // +200
    uint32_t hintOpen;          // +204 the call's, at hinted, is open
};

extern "C" void drawsAsm(DrawsContext* ctx);

// The scene's calls; the draw list after fills of the walk's (their vertices
// first); against the game's way's (gameDrawList): those that differ.
void drawsInit();
void drawsHints(const uint32_t masks[2]);
void draws(int32_t fills);
const DrawList& drawsList();
int32_t drawsCheck(const DrawList &list);

// What the draw list makes overwritten with what it never makes, and the
// list emptied, so a check after a frame that skipped it fails.
void drawsPoison();

// What the scene seldom gives it: made-up calls (items moved, turned and
// at any frame of animations of every rate, statics moved, turned and out of
// sight, sprites moved and shaded, viewports, rectangles, shadows on a wall)
// against the game's way on them: those that differ (and paths not taken).
int32_t drawsSeldomCheck();

#endif
