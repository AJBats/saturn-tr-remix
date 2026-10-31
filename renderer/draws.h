// The draw list (draws.s): the renderer's side of the frame's calls after the
// walk, exactly the game's (draw.h drawModel, drawRoomStatic, drawShadow,
// render.cpp renderSprite, renderRect, renderGlyph), in one call, into a
// DrawList: the draws in order, each item posed with its frames, extras and
// light, each static's place, each shadow's floor, each sprite's,
// rectangle's and glyph's face made.
#ifndef RENDER_DRAWS_H
#define RENDER_DRAWS_H

#include <stdint.h>

#include "types.h"

namespace render {

// The list's limits besides MAX_DRAWS: the items posed, the statics placed,
// the shadows, the sprites' and rectangles' faces.
#define MAX_DRAW_ITEMS      64
#define MAX_DRAW_STATICS    128
#define MAX_DRAW_SHADOWS    32
#define MAX_DRAW_FACES      256
#define MAX_DRAW_FLASHES    4

struct DrawsContext
{
    const CallRecord* calls;    // +0
    uint32_t callCount;         // +4
    ItemRecord* items;          // +8
    const GameAnim* anims;      // +12
    const uint8_t* frames;      // +16 where the animations' frame offsets count from
    const GameModel* models;    // +20
    const uint16_t* meshIndex;  // +24 each level mesh's in the mesh store
    const uint32_t* modelNodes; // +28 each model's nodes' offset in nodes
    const uint8_t* nodes;       // +32
    const GameStaticMesh* statics;  // +36
    const RoomLights* roomLights;   // +40
    const RoomLevel* level;     // +44
    const uint32_t* meshes;     // +48 the mesh store's offsets in meshGeometry
    const uint8_t* meshGeometry;    // +52
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
    uint32_t shown;             // +208 draws.s's own: the ItemRecord of the last item posed
    uint32_t flashMesh;         // +212 the muzzle flash's mesh in the store (NO_MESH: none)
    PoseFlash* flashes;         // +216
    uint32_t flashCount;        // +220
    uint32_t flashMax;          // +224
};

extern "C" void drawsAsm(DrawsContext* ctx);

// The level's tables the calls read, in the game's layouts: the animations
// and where their frame offsets count from, the models, each level mesh's
// index in the mesh store and each model's nodes' offset in nodes, the
// static meshes, each room's ambient and lights, the rooms as the walk reads
// them, the mesh store, the sprites, the muzzle flash's mesh in the store
// (NO_MESH: none).
struct DrawsLevel
{
    const GameAnim* anims;
    const uint8_t* frames;
    const GameModel* models;
    const uint16_t* meshIndex;
    const uint32_t* modelNodes;
    const uint8_t* nodes;
    const GameStaticMesh* statics;
    const RoomLights* roomLights;
    const RoomLevel* level;
    const uint32_t* meshes;
    const uint8_t* meshGeometry;
    const Sprite* sprites;
    uint16_t flashMesh;
};
void drawsInit(const DrawsLevel &level);

// The frame's view: the camera and the view's matrix (12 int32, no
// translation); the things its hints may name (hints.h hintsMasks).
void drawsView(const int32_t camera[3], const int32_t view[12]);
void drawsHints(const uint32_t masks[2]);

// The frame's calls after the walk (a model's and a shadow's index its
// item's, in items) into the draw list, after the walk's fills (their
// vertices first). The calls must fit the list: drawsFitting, those that do.
void draws(const CallRecord* calls, uint32_t count, ItemRecord* items, int32_t fills);
uint32_t drawsFitting(const CallRecord* calls, uint32_t count, const ItemRecord* items);
const DrawList& drawsList();

// draws.s on a context's calls into a list (its arrays put in the context
// first by drawsOn), with room enough; and the draw list's own context.
void drawsOn(DrawsContext &ctx, const DrawList &list);
void drawsRun(DrawsContext &ctx, DrawList &list, int32_t fills);
DrawsContext& drawsContext();

}

#endif
