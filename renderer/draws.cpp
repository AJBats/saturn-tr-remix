// The draw list (draws.h): the frame's calls through draws.s.
#include "draws.h"

#include <stddef.h>
#include <string.h>

namespace render {

static_assert(offsetof(DrawsContext, camera) == 60 && offsetof(DrawsContext, view) == 72 &&
              offsetof(DrawsContext, viewport) == 80 && offsetof(DrawsContext, base) == 96 &&
              offsetof(DrawsContext, faceCount) == 144 && offsetof(DrawsContext, k20) == 148 &&
              offsetof(DrawsContext, frameA) == 156 && offsetof(DrawsContext, viewMin) == 176 &&
              offsetof(DrawsContext, drawCount) == 184 && offsetof(DrawsContext, hintOpen) == 204 &&
              offsetof(DrawsContext, shown) == 208 && offsetof(DrawsContext, flashMax) == 224,
              "draws.s's DrawsContext");
static_assert(sizeof(DrawScene) == 24 && sizeof(PoseItem) == 104 && sizeof(PoseLight) == 12 &&
              sizeof(PoseStatic) == 20 && sizeof(PoseShadow) == 8 && sizeof(Face) == 16 && sizeof(Vertex) == 8 &&
              sizeof(Sprite) == 16 && sizeof(HintedCall) == 8 && offsetof(ItemRecord, index) == 78 &&
              sizeof(PoseFlash) == 12,
              "draws.s's records");

#define X_MAX(a, b)         ((a) > (b) ? (a) : (b))

static DrawsContext gCtx;
static int32_t gView[12];
static DrawList gList;
static DrawScene gDraws[MAX_DRAWS];
static HintedCall gHinted[MAX_DRAW_ITEMS + MAX_DRAW_STATICS];
static PoseItem gItems[MAX_DRAW_ITEMS];
static PoseLight gLights[MAX_DRAW_ITEMS];
static PoseStatic gStatics[MAX_DRAW_STATICS];
static PoseShadow gShadows[MAX_DRAW_SHADOWS];
static Face gFaces[MAX_DRAW_FACES];
static Vertex gFaceVertices[2 * MAX_DRAW_FACES];
static PoseFlash gFlashes[MAX_DRAW_FLASHES];

void drawsOn(DrawsContext &c, const DrawList &l)
{
    c.draws = l.draws;
    c.poses = l.items;
    c.lights = l.lights;
    c.placed = l.statics;
    c.floors = l.shadows;
    c.faces = l.faces;
    c.vertices = l.vertices;
    c.flashes = l.flashes;
}

void drawsRun(DrawsContext &c, DrawList &l, int32_t fills)
{
    c.base = 2 * fills;
    c.hinted = l.hinted;
    drawsAsm(&c);
    l.hintedCount = c.hintedCount;
    l.drawCount = c.drawsEnd - c.draws;
    l.itemCount = c.itemCount;
    l.staticCount = c.staticCount;
    l.shadowCount = c.shadowCount;
    l.faceCount = c.faceCount;
    l.flashCount = c.flashCount;
}

void drawsInit(const DrawsLevel &level)
{
    gCtx.anims = level.anims;
    gCtx.frames = level.frames;
    gCtx.models = level.models;
    gCtx.meshIndex = level.meshIndex;
    gCtx.modelNodes = level.modelNodes;
    gCtx.nodes = level.nodes;
    gCtx.statics = level.statics;
    gCtx.roomLights = level.roomLights;
    gCtx.level = level.level;
    gCtx.meshes = level.meshes;
    gCtx.meshGeometry = level.meshGeometry;
    gCtx.sprites = level.sprites;
    gCtx.flashMesh = level.flashMesh;
    gCtx.flashMax = MAX_DRAW_FLASHES;
    gCtx.view = gView;
    gCtx.divTable = divTable;
    gCtx.k20 = 1 << 20;
    gCtx.k18 = 1 << 18;
    gCtx.viewMin = 64 << 14;
    gCtx.viewMax = (1024 * 20) << 14;
    gList.draws = gDraws;
    gList.hinted = gHinted;
    gList.items = gItems;
    gList.lights = gLights;
    gList.statics = gStatics;
    gList.shadows = gShadows;
    gList.faces = gFaces;
    gList.vertices = gFaceVertices;
    gList.flashes = gFlashes;
    drawsOn(gCtx, gList);
}

void drawsView(const int32_t camera[3], const int32_t view[12])
{
    for (int32_t k = 0; k < 3; k++) {
        gCtx.camera[k] = camera[k];
    }
    for (int32_t k = 0; k < 12; k++) {
        gView[k] = view[k];
    }
}

uint32_t drawsFitting(const CallRecord* calls, uint32_t count, const ItemRecord* items)
{
    uint32_t draws = 0, models = 0, statics = 0, shadows = 0, faces = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        const CallRecord &c = calls[i];
        uint32_t meshes = c.kind == CALL_MODEL ? X_MAX(gCtx.models[items[c.index].item.type].count, 0) : 1;
        models += c.kind == CALL_MODEL;
        statics += c.kind == CALL_STATIC;
        shadows += c.kind == CALL_SHADOW;
        faces += c.kind == CALL_SPRITE || c.kind == CALL_RECT || c.kind == CALL_GLYPH;
        draws += c.kind == CALL_VIEWPORT ? 0 : meshes;
        if (draws > MAX_DRAWS || models > MAX_DRAW_ITEMS || statics > MAX_DRAW_STATICS ||
            shadows > MAX_DRAW_SHADOWS || faces > MAX_DRAW_FACES)
            return i;
    }
    return count;
}

void drawsHints(const uint32_t masks[2])
{
    gCtx.hintItems = masks[0];
    gCtx.hintStatics = masks[1];
}

void draws(const CallRecord* calls, uint32_t count, ItemRecord* items, int32_t fills)
{
    gCtx.calls = calls;
    gCtx.callCount = count;
    gCtx.items = items;
    drawsRun(gCtx, gList, fills);
}

const DrawList& drawsList()
{
    return gList;
}

DrawsContext& drawsContext()
{
    return gCtx;
}

}
