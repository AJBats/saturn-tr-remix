// The game's side of the renderer (render.h): the frame's call records, the
// view, the level's tables, and the list's way to VDP1.

// The renderer's headers before the game's: common.h's viewport macro would
// rename their fields of that name.
#include "draws.h"
#include "frame.h"
#include "hints.h"
#include "platform.h"
#include "pose.h"
#include "rooms.h"
#include "textures.h"
#include "vertices.h"

#include "render.h"
#include "detail.h"
#include "crash.h"
#include "hw.h"
#include "palettes.h"
#include "slave.h"
#include "system.h"
#include "trace.h"
#include "vram.h"

static_assert(FRAME_WIDTH == SCREEN_WIDTH && FRAME_HEIGHT == SCREEN_HEIGHT,
              "OpenLara frame size must match the display mode");
static_assert(((VIEW_MAX_F >> (FIXED_SHIFT + OT_SHIFT)) + 1) == OT_SIZE && VRAM_MAX_CMDS == MAX_CMDS,
              "the renderer's limits are the game's");
static_assert(sizeof(ItemObj) == sizeof(render::GameItem) && sizeof(Texture) == sizeof(render::Texture) &&
              sizeof(Sprite) == sizeof(render::Sprite) && sizeof(Anim) == sizeof(render::GameAnim) &&
              sizeof(Model) == sizeof(render::GameModel) && sizeof(StaticMesh) == sizeof(render::GameStaticMesh) &&
              sizeof(Portal) == 32 && sizeof(Light) == 8 && sizeof(RoomQuad) == sizeof(render::RoomQuad) &&
              sizeof(RoomTriangle) == sizeof(render::RoomQuad) && sizeof(RoomVertex) == sizeof(render::RoomVertex),
              "the renderer reads the game's layouts");

#define FACE_WATER      (1u << 30)  // the faces' flags: under water
#define FACE_TYPE_FILL_S    7       // render::facesOther's 2D faces
#define FACE_TYPE_LINE_H    8
#define FACE_TYPE_LINE_V    9
#define BAR_HEIGHT      5
#define TEXTURE_LOD     4           // room faces' mip level: 1 texel a pixel
#define MAX_CALLS       384         // a frame's: its draws, and a viewport for each room seen
#define NO_INDEX        0xFFFFFFFF

extern Lara* players[MAX_PLAYERS];

// --- the frame's calls --------------------------------------------------------

static render::CallRecord gCalls[MAX_CALLS];
static uint32 gCallCount;
static render::ItemRecord gItems[MAX_DRAW_ITEMS];
static uint32 gItemCount;
static bool gWalked;            // the rooms are walked this frame
#define MAX_GIVEN       16          // the inventory's items a frame
static int32_t gGiven[MAX_GIVEN][12];   // the matrices models are drawn under
static uint32 gGivenCount;
static uint32 gDrawStart;       // the frame's drawing began (clear)

// Meshes drawn while this is set (items in water rooms, or everything while
// the camera is under water) get FACE_WATER.
#define gMeshFaceFlags  (cpuState()->meshFaceFlags)
static bool gCameraWater;
static bool gSceneClear;        // the scene's frame cleared all the same (setSceneClear)

static render::CallRecord* call(uint8 kind, uint32 index)
{
    if (gCallCount == MAX_CALLS)
        return nullptr;
    render::CallRecord &c = gCalls[gCallCount++];
    c.kind = kind;
    c.pad = 0;
    c.index = index;
    c.flags = gMeshFaceFlags;
    memset(c.args, 0, sizeof(c.args));
    return &c;
}

void setViewport(const RectMinMax &vp)
{
    viewport = vp;
    if (render::CallRecord* c = call(render::CALL_VIEWPORT, 0))
    {
        c->args[0] = vp.x0;
        c->args[1] = vp.y0;
        c->args[2] = vp.x1;
        c->args[3] = vp.y1;
    }
}

// OpenLara selects palette 1 for whatever is in a water room.
void setPaletteIndex(int32 index)
{
    gMeshFaceFlags = index || gCameraWater ? FACE_WATER : 0;
}

void setCameraWater(bool water)
{
    gCameraWater = water;
}

void setSceneClear(bool clear)
{
    gSceneClear = clear;
}

void renderModel(const ItemObj* item)
{
    if (gItemCount == MAX_DRAW_ITEMS)
        return;
    render::CallRecord* c = call(render::CALL_MODEL, gItemCount);
    if (!c)
        return;
    render::ItemRecord &r = gItems[gItemCount++];
    memcpy(&r.item, item, sizeof(r.item));
    r.room = uint16(item->room - rooms);
    r.index = uint16(item - items);
    c->args[0] = item - items;
}

void renderModelUnder(const ItemObj* item)
{
    if (gItemCount == MAX_DRAW_ITEMS || gGivenCount == MAX_GIVEN)
        return;
    render::CallRecord* c = call(render::CALL_MODEL, gItemCount);
    if (!c)
        return;
    render::ItemRecord &r = gItems[gItemCount++];
    memcpy(&r.item, item, sizeof(r.item));
    r.room = 0;
    r.index = 0xFFFF;
    memcpy(gGiven[gGivenCount], &matrixGet(), sizeof(gGiven[0]));
    c->args[0] = -1;
    c->args[1] = ++gGivenCount;
}

void renderShadowCall(const ItemObj* item, int32 floor)
{
    // its model's call is the last
    if (!gItemCount || gItems[gItemCount - 1].index != item - items)
        return;
    if (render::CallRecord* c = call(render::CALL_SHADOW, gItemCount - 1)) {
        c->args[0] = floor;
    }
}

void renderStatic(const RoomMesh* mesh, int32 rx, int32 ry, int32 rz, int32 room, int32 index)
{
    if (render::CallRecord* c = call(render::CALL_STATIC, room))
    {
        c->args[0] = int32(mesh->xy);
        c->args[1] = int32(mesh->zf);
        c->args[2] = rx;
        c->args[3] = ry;
        c->args[4] = rz;
        c->args[5] = index;
    }
}

void renderSprite(int32 vx, int32 vy, int32 vz, int32 vg, int32 index)
{
    if (render::CallRecord* c = call(render::CALL_SPRITE, index))
    {
        c->args[0] = vx;
        c->args[1] = vy;
        c->args[2] = vz;
        c->args[3] = vg;
    }
}

void renderGlyph(int32 vx, int32 vy, int32 index)
{
    const Sprite* sprite = level.sprites + index;
    int32 l = vx + sprite->l;
    int32 r = vx + sprite->r;
    int32 t = vy + sprite->t;
    int32 b = vy + sprite->b;
    if (l == r || t == b || r < 0 || b < 0 || l >= FRAME_WIDTH || t >= FRAME_HEIGHT)
        return;
    if (render::CallRecord* c = call(render::CALL_GLYPH, index))
    {
        c->args[0] = l;
        c->args[1] = t;
        c->args[2] = r;
        c->args[3] = b;
    }
}

static void renderRect(int32 x, int32 y, int32 width, int32 height, uint32 type, uint16 color, int32 z)
{
    if (render::CallRecord* c = call(render::CALL_RECT, type))
    {
        c->args[0] = x;
        c->args[1] = y;
        c->args[2] = width;
        c->args[3] = height;
        c->args[4] = color;
        c->args[5] = z;
    }
}

// Darkens the frame under the rectangle (VDP1 halves it; shade is ignored).
void renderFill(int32 x, int32 y, int32 width, int32 height, int32 shade, int32 z)
{
    renderRect(x, y, width, height, FACE_TYPE_FILL_S, 0, z);
}

// Border colours are grey levels 0..31.
void renderBorder(int32 x, int32 y, int32 width, int32 height, int32 color1, int32 color2, int32 z)
{
    uint16 c1 = RGB(color1, color1, color1);
    uint16 c2 = RGB(color2, color2, color2);
    renderRect(x + 1, y, width - 2, 1, FACE_TYPE_LINE_H, c1, z);
    renderRect(x + 1, y + height - 1, width - 2, 1, FACE_TYPE_LINE_H, c2, z);
    renderRect(x, y, 1, height, FACE_TYPE_LINE_V, c1, z);
    renderRect(x + width - 1, y, 1, height, FACE_TYPE_LINE_V, c2, z);
}

// Bar rows top to bottom; health as in the original game.
static const uint16 BAR_COLORS[BAR_MAX][BAR_HEIGHT] = {
    { RGB(10,  6,  0), RGB(15, 11,  2), RGB(10,  6,  0), RGB( 7,  3,  0), RGB( 4,  0,  0) },
    { RGB( 0,  8, 12), RGB( 4, 14, 18), RGB( 0,  8, 12), RGB( 0,  5,  9), RGB( 0,  2,  6) },
    { RGB( 4, 12,  0), RGB( 9, 18,  2), RGB( 4, 12,  0), RGB( 2,  8,  0), RGB( 0,  4,  0) },
    { RGB(14, 14, 14), RGB(20, 20, 20), RGB(14, 14, 14), RGB(10, 10, 10), RGB( 6,  6,  6) },
};

void renderBar(int32 x, int32 y, int32 width, int32 value, BarType type)
{
    int32 w = value * width >> 8;

    if (w > 0)
    {
        for (int32 i = 0; i < BAR_HEIGHT; i++) {
            renderRect(x + 1, y + 1 + i, w, 1, FACE_TYPE_LINE_H, BAR_COLORS[type][i], 0);
        }
    }

    if (w < width)
    {
        renderRect(x + 1 + w, y + 1, width - w, BAR_HEIGHT, FACE_TYPE_LINE_V, RGB(1, 1, 1), 0);
    }

    renderBorder(x, y, width + 2, BAR_HEIGHT + 2, 2, 2, 0);
}

// --- the view -----------------------------------------------------------------

static render::RoomCaustics gRoomCaustics;

// The camera and the view's matrix (no translation), as matrixSetView left them.
static void view(int32_t camera[3], int32_t m[12])
{
    camera[0] = gCameraViewPos.x;
    camera[1] = gCameraViewPos.y;
    camera[2] = gCameraViewPos.z;
    for (int32 r = 0; r < 3; r++)
    {
        for (int32 c = 0; c < 3; c++) {
            m[r * 4 + c] = gViewRotation[r * 3 + c];
        }
        m[r * 4 + 3] = 0;
    }
}

// The view the frame's scene is drawn from: the walk's (the inventory sets up
// its own after the scene behind it).
static int32_t gSceneCamera[3], gSceneView[12];

Room** renderWalk(Room* cameraRoom)
{
    static Room* visible[ROOMS_LISTED + 1];
    view(gSceneCamera, gSceneView);
    int32_t vp[4] = { viewport.x0, viewport.y0, viewport.x1, viewport.y1 };
    gRoomCaustics.phase = gCausticsFrame;
    render::roomsView(gSceneCamera, gSceneView, vp, uint32(cameraRoom - rooms));
    render::roomsSeen();
    gWalked = true;

    const render::RoomsContext &walk = render::roomsContext();
    int32 n = 0;
    for (int32 i = 0; i < walk.listed; i++)
    {
        const render::RoomSeen &s = walk.seen[i];
        Room* room = rooms + s.room;
        room->clip = RectMinMax(s.window[0], s.window[1], s.window[2], s.window[3]);
        visible[n++] = room;
    }
    visible[n] = nullptr;
    return visible;
}

// --- the shaded copies of colour tables ----------------------------------------

// TR shade 0 (lit) .. 31 (dark) as a gouraud level (16 = colour unchanged:
// brighter from shade 15 down, darker from 16 up, in even steps to black, as
// the original), and as a VDP1 gouraud entry; and dimmed by 8 levels, for
// the scene behind the inventory.
static uint8 gShadeLevel[32];
static uint16 gShadeGouraud[32];
static uint8 gDimLevel[32];
static uint16 gDimGouraud[32];

static render::ShadeCache gShadeCaches[2] __attribute__((aligned(16)));    // the master's, the slave's

// A copy for the key, in the pair of slots holding index whose copy was drawn
// longest ago, unless both are drawn this frame or the last (VDP1 may be
// drawing that one): its table address / 8, or 0.
static uint32_t shadeTableMiss(render::ShadeCache* cache, uint32_t key, uint32_t index)
{
    uint32 a = index & ~1;
    uint32 drawnA = cache->frames[a] >= cache->validFrom ? cache->frames[a] : 0;
    uint32 drawnB = cache->frames[a + 1] >= cache->validFrom ? cache->frames[a + 1] : 0;
    index = drawnB < drawnA ? a + 1 : a;
    if (X_MIN(drawnA, drawnB) + 2 > cache->frame)
        return 0;
    cache->keys[index] = key;
    cache->frames[index] = cache->frame;

    uint32 g = key & 0x7FFF;
    int32 dr = int32(g & 31) - 16;
    int32 dg = int32((g >> 5) & 31) - 16;
    int32 db = int32((g >> 10) & 31) - 16;
    const volatile uint16* src = (const volatile uint16*)(VDP1_VRAM + (key >> 15) * 8);
    uint32 table = cache->vram + index * SHADE_TABLE_SIZE;
    volatile uint16* dst = (volatile uint16*)(VDP1_VRAM + table);
    for (int32 i = 0; i < 16; i++)
    {
        uint32 c = src[i];
        int32 r = X_CLAMP(int32(c & 31) + dr, 0, 31);
        int32 gr = X_CLAMP(int32((c >> 5) & 31) + dg, 0, 31);
        int32 b = X_CLAMP(int32((c >> 10) & 31) + db, 0, 31);
        dst[i] = (c & 0x8000) | (b << 10) | (gr << 5) | r;
    }
    return table >> 3;
}

// Every copy stale: the tables they copy, or the VRAM they live in, changed.
static void shadeTablesReset()
{
    for (int32 i = 0; i < 2; i++) {
        gShadeCaches[i].validFrom = gShadeCaches[i].frame + 1;
    }
}

// --- the level --------------------------------------------------------------

// The draw order hints (saturn/depth_hints.txt), the level's in hints.h's
// layout.
typedef render::DepthHint DepthHint;
enum DepthHintKind { DEPTH_HINT_ITEM, DEPTH_HINT_STATIC };
#include "depth_hints.h"

#define MAX_LEVEL_HINTS 32
static DepthHint gLevelHints[MAX_LEVEL_HINTS];

// The walk's and the pose's tables of the level's rooms, in high work RAM:
// each room as the walk reads it, the portals and lights gathered (each
// room's from its first), each room's geometry (its faces' and vertices'
// addresses: rgeo is none), each room's ambient and lights.
static render::RoomLevel* gRoomLevel;
static Portal* gPortals;
static Light* gLights;
static render::RoomGeometry* gGeometry;
static render::RoomLights* gRoomLights;
static uint32 gModelNodes[MAX_MODELS];

static render::Vertex gVertices[MAX_VERTICES] __attribute__((aligned(16)));
static render::FrameLevel gFrameLevel;

static void* highAlloc(uint32 bytes)
{
    void* p = osHighAlloc(bytes);
    if (!p)
    {
        crashLog("HIGHRAM", bytes);
        while (1);  // TODO error screen
    }
    return p;
}

static void* highAllocDsp(uint32_t bytes)
{
    return osHighAlloc(bytes);
}

// The rooms' tables from rooms[] as they stand (again after a flip map).
static void roomTables()
{
    uint32 portals = 0, lights = 0;
    for (int32 i = 0; i < level.roomsCount; i++)
    {
        const Room &room = rooms[i];
        const RoomInfo* info = room.info;
        render::RoomLevel &r = gRoomLevel[i];
        r.x = info->x;
        r.z = info->z;
        r.yTop = info->yTop;
        r.flags = info->flags;
        r.portals = info->portalsCount;
        r.vertices = info->verticesCount;
        r.pad = 0;
        r.first = portals;
        memcpy(gPortals + portals, room.data.portals, info->portalsCount * sizeof(Portal));
        portals += info->portalsCount;

        render::RoomLights &l = gRoomLights[i];
        l.ambient = info->ambient;
        l.lights = info->lightsCount;
        l.pad = 0;
        l.first = lights;
        memcpy(gLights + lights, room.data.lights, info->lightsCount * sizeof(Light));
        lights += info->lightsCount;

        render::RoomGeometry &g = gGeometry[i];
        g.quads = uint32(room.data.quads);
        g.triangles = uint32(room.data.triangles);
        g.points = uint32(room.data.vertices);
        g.quadsCount = info->quadsCount;
        g.trianglesCount = info->trianglesCount;
        for (int32 k = 0; k < 6; k++) {
            g.facing[k] = info->facing[k];
        }
        g.pad = 0;
    }
}

// (before the level's tables are made, they are made flipped)
void renderRoomsFlipped()
{
    if (gRoomLevel) {
        roomTables();
    }
}

// The clear: a scaled sprite of colour 0 over the frame, transparency off,
// which writes 0 (VDP2 shows the back colour); skipped where rooms cover the
// frame (what neither a face nor a portal of theirs would draw is filled
// transparent behind everything then).
static render::Command clearCommand()
{
    render::Command c = {};
    c.w[0] = CMD_SCALED_SPRITE << 16;
    c.w[1] = PMOD_SPD_OFF << 16;
    c.w[2] = ((VRAM_CLEAR_TEX >> 3) << 16) | (1 << 8) | 1;
    c.w[5] = ((FRAME_WIDTH - 1) << 16) | (FRAME_HEIGHT - 1);
    return c;
}

void renderLevelInit()
{
    // the textures' mean colours under the colour tables VRAM holds
    if (palettesRemastered())
    {
        for (int32 i = 0; i < level.texturesCount; i++)
        {
            Texture &t = level.textures[i];
            X_SWAP(t.color, t.altColor);
        }
    }

    // the shaded copies, at the top of the cache, if it can spare them
    uint32 shadeBytes = 2 * 256 * SHADE_TABLE_SIZE;
    uint32 vram = detailSpareBytes() >= shadeBytes ? detailReserveTop(shadeBytes) : 0;
    for (int32 i = 0; i < 2; i++)
    {
        render::ShadeCache &c = gShadeCaches[i];
        memset(&c, 0, sizeof(c));
        c.frame = 2;
        c.validFrom = 3;
        c.vram = vram ? vram + i * 256 * SHADE_TABLE_SIZE : 0;
        c.miss = shadeTableMiss;
    }

    // the rooms' tables
    uint32 portals = 0, lights = 0;
    for (int32 i = 0; i < level.roomsCount; i++)
    {
        portals += rooms[i].info->portalsCount;
        lights += rooms[i].info->lightsCount;
    }
    gRoomLevel = (render::RoomLevel*)highAlloc(level.roomsCount * sizeof(render::RoomLevel));
    gPortals = (Portal*)highAlloc(X_MAX(portals, 1u) * sizeof(Portal));
    gLights = (Light*)highAlloc(X_MAX(lights, 1u) * sizeof(Light));
    gGeometry = (render::RoomGeometry*)highAlloc(level.roomsCount * sizeof(render::RoomGeometry));
    gRoomLights = (render::RoomLights*)highAlloc(level.roomsCount * sizeof(render::RoomLights));
    roomTables();

    for (int32 i = 0; i < 32; i++)
    {
        gRoomCaustics.wave[i] = int16(gCaustics[i]);
        gRoomCaustics.starts[i] = uint8(gRandTable[i] & 31);
    }

    // the models' nodes, each model's from its first
    for (int32 m = 0; m < MAX_MODELS; m++) {
        gModelNodes[m] = level.models[m].count > 0 ? level.models[m].nodeIndex * sizeof(ModelNode) : NO_INDEX;
    }

    const render::MeshStore* store = (const render::MeshStore*)osMeshStore();
    if (!store || store->magic != MESH_STORE_MAGIC || store->meshCount != level.meshesCount)
    {
        crashLog("MSH", store ? store->meshCount : 0);
        while (1);  // TODO error screen
    }
    const uint8* storeBytes = (const uint8*)store;

    render::DrawsLevel d;
    d.anims = (const render::GameAnim*)level.anims;
    d.frames = (const uint8_t*)level.animFrames;
    d.models = (const render::GameModel*)level.models;
    d.meshIndex = (const uint16_t*)(storeBytes + store->index);
    d.modelNodes = (const uint32_t*)gModelNodes;
    d.nodes = (const uint8_t*)level.nodes;
    d.statics = (const render::GameStaticMesh*)level.staticMeshes;
    d.roomLights = gRoomLights;
    d.level = gRoomLevel;
    d.meshes = (const uint32_t*)(storeBytes + store->records);
    d.meshGeometry = storeBytes;
    d.sprites = (const render::Sprite*)level.sprites;
    const Model &flash = level.models[ITEM_MUZZLE_FLASH];
    d.flashMesh = flash.count > 0 ? d.meshIndex[flash.start] : NO_MESH;
    render::drawsInit(d);

    render::PoseLevel p;
    p.sines = (const uint32_t*)gSinCosTable;
    p.nodes = (const uint8_t*)level.nodes;
    p.lights = gLights;
    render::poseInit(p);

    render::RoomsLevel r;
    r.level = gRoomLevel;
    r.count = level.roomsCount;
    r.portals = gPortals;
    r.geometry = gGeometry;
    r.rgeo = nullptr;
    r.caustics = &gRoomCaustics;
    r.vertices = gVertices;
    render::roomsInit(r);

    render::VerticesLevel v;
    v.store = store;
    v.alloc = highAllocDsp;
    if (const char* failed = render::verticesInit(v))
    {
        crashLog("VERTS", uint32(failed));
        while (1);  // TODO error screen
    }

    uint32 hints = 0;
    for (uint32 i = 0; i < sizeof(gDepthHints) / sizeof(gDepthHints[0]) && hints < MAX_LEVEL_HINTS; i++)
    {
        if (gDepthHints[i].level == gLevelID) {
            gLevelHints[hints++] = gDepthHints[i];
        }
    }
    render::hintsInit(gLevelHints, hints);

    // the room textures: those the rooms' faces take
    uint32 roomTextures = 0;
    for (int32 i = 0; i < level.roomsCount; i++)
    {
        const Room &room = rooms[i];
        for (int32 k = 0; k < room.info->quadsCount; k++) {
            roomTextures = X_MAX(roomTextures, (room.data.quads[k].flags & 0x3FFF) + 1);
        }
        for (int32 k = 0; k < room.info->trianglesCount; k++) {
            roomTextures = X_MAX(roomTextures, (room.data.triangles[k].flags & 0x3FFF) + 1);
        }
    }
    crashLog("ROOMTEX", roomTextures);

    render::FrameLevel &f = gFrameLevel;
    f.faceSets = storeBytes + store->faceSets;
    f.materials = (const render::Material*)(storeBytes + store->materials);
    f.textures = (const render::Texture*)level.textures;
    f.palette = level.palette;
    f.sprites = (const render::Sprite*)level.sprites;
    f.shadeGouraud = gShadeGouraud;
    f.shadeLevel = gShadeLevel;
    f.dimGouraud = gDimGouraud;
    f.dimLevel = gDimLevel;
    f.shade[0] = vram ? &gShadeCaches[0] : nullptr;     // none without VRAM for the copies
    f.shade[1] = vram ? &gShadeCaches[1] : nullptr;
    f.pmod = PMOD_LUT16 | PMOD_GOURAUD | PMOD_HSS;
    f.lod = TEXTURE_LOD;
    for (int32 l = 0; l < 3; l++) {
        f.pools[l] = gDetailPoolVram[l];
    }
    f.frame = 1;
    f.roomTextures = roomTextures;
    f.clear = clearCommand();
    f.vertices = gVertices;
    render::frameInit(f);

    // the rooms' faces in what high work RAM is left, then where they are
    osPlaceRoomFaces();
    roomTables();
}

void renderLevelFree()
{
    gListPending = false;   // its textures are gone
    gRoomLevel = nullptr;
}

// --- the frame ----------------------------------------------------------------

// The inventory draws over the paused game: copyBackground returns this
// marker, and renderBackground redraws the scene behind it each frame. Its
// item textures replace the full-res room texture cache's first slots
// meanwhile.
static uint8 gSceneBackground;
static bool gInventoryFrame;    // renderBackground ran this frame
static bool gItemTextures;      // item textures are in VRAM

bool gListPending;
static bool gListResume;        // and the item textures leave the cache with it
static int32_t gBias;           // the frame plan's, from the frames before (frame.h frameRun)

// The list in gFrameCmds to VDP1 with the textures its faces queued, then
// its plot. VDP1 must have drawn the last list.
static void listUpload()
{
    uint32 start = timerTicks();
    traceBeginAt(TRACE_MASTER, start, TR_UPLOAD);
    detailItemUpload();
    render::frameUpload();
    if (gListResume)
    {
        detailResume();
        shadeTablesReset();     // the item textures may have covered them
        gListResume = false;
    }
    vdp1Plot();
    gListPending = false;
    uint32 end = timerTicks();
    gStats.dmaTicks = (end - start) & 0xFFFF;
    traceEndAt(TRACE_MASTER, end, TR_UPLOAD);
}

// Waits for VDP1 to draw the last list (adding to gStats.waitTicks), then
// uploads the pending one.
static void listUploadWaiting()
{
    uint32 waitStart = timerTicks();
    traceBeginAt(TRACE_MASTER, waitStart, TR_VDP1_WAIT);
    vdp1WaitDrawn();
    uint32 waitEnd = timerTicks();
    gStats.waitTicks += (waitEnd - waitStart) & 0xFFFF;
    traceEndAt(TRACE_MASTER, waitEnd, TR_VDP1_WAIT);
    listUpload();
}

void* renderScratch(uint32 size)
{
    ASSERT(size <= sizeof(render::gFrameCmds));
    gListPending = false;
    return render::gFrameCmds;
}

void renderUploadReady()
{
    if (vdp1Drawn()) {
        listUpload();
    }
}

// The frame's stages on the trace (renderer/frame.h FrameStage).
static const uint8 kStageIds[render::STAGE_COUNT] = {
    TR_DRAW_LIST, TR_WALK_WAIT, TR_PLAN, TR_POSE, TR_FEED, TR_POSE_WAIT, TR_CHUNKS, TR_DRAWS, TR_SLAVE_WAIT,
    TR_JOIN, TR_JOBS, TR_PLAN_WAIT, TR_SLAVE_POSE, TR_ROOMS, TR_DRAWS_WAIT, TR_SLAVE_JOIN
};

void stageBegin(uint32_t stage)
{
    traceBegin(stage < render::STAGE_JOBS ? TRACE_MASTER : TRACE_SLAVE, kStageIds[stage]);
}

void stageEnd(uint32_t stage)
{
    traceEnd(stage < render::STAGE_JOBS ? TRACE_MASTER : TRACE_SLAVE, kStageIds[stage]);
}

void renderPalettesSwapped()
{
    for (int32 i = 0; i < level.texturesCount; i++)
    {
        Texture &t = level.textures[i];
        X_SWAP(t.color, t.altColor);
    }
    // the room textures' materials made again, the pending list up first
    if (gListPending) {
        listUploadWaiting();
    }
    gFrameLevel.frame = render::gRoomTextures.frame;
    render::frameInit(gFrameLevel);
}

void renderBackground(const void* background)
{
    // the cache is about to make room for the item textures: the scene's
    // last list and its uploads go first
    if (gListPending) {
        listUploadWaiting();
    }
    gInventoryFrame = true;
    if (!gItemTextures)
    {
        detailSuspend();
        gItemTextures = true;
    }

    ItemObj* lara = (ItemObj*)(void*)players[0];
    if (background != &gSceneBackground || !lara)
        return;

    Camera &camera = lara->extraL->camera;
    matrixSetView(camera.view.pos, camera.angle.x, camera.angle.y);
    drawRooms(&camera);
}

void* copyBackground()
{
    return &gSceneBackground;
}

void flush_c()
{
    uint32 start = timerTicks();
    traceBeginAt(TRACE_MASTER, start, TR_FLUSH);
    gStats.drawTicks = (start - gDrawStart) & 0xFFFF;
    gStats.waitTicks = 0;

    // the last list leaves gFrameCmds before this one is built there
    if (gListPending) {
        listUploadWaiting();
    }

    // a frame without rooms (the title, the inventory's own pages)
    render::RoomsContext &walk = render::roomsContext();
    if (!gWalked)
    {
        walk.listed = 0;
        walk.jobCount = 0;
        walk.voids = 0;
    }
    int32_t fills;
    render::roomsFills(fills);

    // the scene's view (none walked: the view as it stands), where Lara
    // stands for the draw order hints
    if (!gWalked) {
        view(gSceneCamera, gSceneView);
    }
    render::drawsView(gSceneCamera, gSceneView);
    render::poseView(gSceneCamera, gSceneView);
    const ItemObj* lara = (const ItemObj*)(const void*)players[0];
    if (lara) {
        render::hintsLara(int32(lara->room - rooms), lara->pos.x >> 10, lara->pos.z >> 10);
    } else {
        render::hintsLara(-1, 0, 0);
    }
    uint32_t masks[2];
    render::hintsMasks(masks);
    render::drawsHints(masks);

    // the caches' frames on by one
    gShadeCaches[0].frame++;
    gShadeCaches[1].frame++;
    render::gRoomTextures.frame++;
    render::gFrameTextures.count = 0;

    render::FrameCalls calls;
    calls.calls = gCalls;
    calls.count = render::drawsFitting(gCalls, gCallCount, gItems);
    calls.items = gItems;
    calls.base = fills;
    calls.walked = true;
    calls.matrices = gGiven;
    calls.inventory = gInventoryFrame;
    render::FrameTimes s = render::frameRun(calls, gBias);
    gBias += s.late / 2;
    gStats.detailUploads = render::gFrameTextures.count;
#ifdef HW_LOG
    // the DSP's two runs on the trace, on the slave's clock (it started the
    // rooms'; the master the meshes', and took the ends)
    uint32_t ends[2], roomsAt, meshesAt;
    if (gTraceOn && dspEnds(ends, 2) == 2)
    {
        render::verticesDspStarts(roomsAt, meshesAt);
        traceBeginAt(TRACE_DSP, roomsAt, TR_DSP_RUN, 0);
        traceEndAt(TRACE_DSP, masterTickOnSlave(ends[0]), TR_DSP_RUN, 0);
        traceBeginAt(TRACE_DSP, masterTickOnSlave(meshesAt), TR_DSP_RUN, 1);
        traceEndAt(TRACE_DSP, masterTickOnSlave(ends[1]), TR_DSP_RUN, 1);
    }
#endif
    if (gWalked && !gSceneClear) {
        render::gFrameCmds[0].w[0] |= CMD_SKIP << 16;
    }

    gStats.faces = render::facesSlot(render::gRoomList.link) - 1 + render::gFrameEnd - render::gMeshFirst;
    gStats.rooms = render::gFrameRoomCount;
    gStats.meshes = render::drawsList().drawCount;
    gStats.roomVertices = 0;
    for (uint32 i = 0; i < render::gFrameRoomCount; i++) {
        gStats.roomVertices += render::gFrameRooms[i].verticesCount;
    }
    gStats.vertices = gStats.roomVertices + render::drawsContext().base - 2 * fills;
    gStats.slaveWorkTicks = s.slaveWork / 128;
    gStats.slaveIdleTicks = s.slaveWait / 128;
    gStats.slaveWaitTicks = s.masterWait / 128;

    gCallCount = 0;
    gItemCount = 0;
    gGivenCount = 0;
    gWalked = false;
    gSceneClear = false;
    gListResume = gItemTextures && !gInventoryFrame;
    if (gListResume) {
        gItemTextures = false;
    }

    // the inventory's frames and the one after go up at once, as the cache
    // changes hands
    gListPending = true;
    if (gInventoryFrame || gListResume || vdp1Drawn()) {
        listUploadWaiting();
    }
    gInventoryFrame = false;

    uint32 end = timerTicks();
    gStats.flushTicks = ((end - start) & 0xFFFF) - gStats.waitTicks;
    traceEndAt(TRACE_MASTER, end, TR_FLUSH);
}

// The frame's drawing begins. Timers are read only around whole stages: a
// read costs a Saturn about 110 cycles.
void clear()
{
    gDrawStart = timerTicks();
}

void renderInit()
{
    // The core's divide table holds 1 .. 1023 (its last entry is 0, past the
    // far plane there); the view reaches further here.
    for (int32 i = 1024; i < DIV_TABLE_SIZE; i++) {
        divTable[i] = (1 << 16) / i;
    }

    // shade g as a gouraud level, in even steps to black
    for (int32 g = 0; g < 32; g++)
    {
        uint32 c = 31 - g;
        gShadeLevel[g] = c;
        gShadeGouraud[g] = (c << 10) | (c << 5) | c;
        uint32 d = X_MAX(int32(c) - 8, 0);
        gDimLevel[g] = d;
        gDimGouraud[g] = (d << 10) | (d << 5) | d;
    }

    // the clear's texture
    REG32(VDP1_VRAM + VRAM_CLEAR_TEX) = 0;
    REG32(VDP1_VRAM + VRAM_CLEAR_TEX + 4) = 0;

    // slots 0-2: system clip, user clip, local coordinates; frame commands follow
    volatile vdp1_cmd_t* cmds = (volatile vdp1_cmd_t*)VDP1_VRAM;
    for (int32 i = 0; i < 3; i++)
    {
        cmds[i].link = 0;
        cmds[i].pmod = 0;
    }
    cmds[0].ctrl = CMD_SYSTEM_CLIP;
    cmds[0].xc = FRAME_WIDTH - 1;
    cmds[0].yc = FRAME_HEIGHT - 1;
    cmds[1].ctrl = CMD_USER_CLIP;
    cmds[1].xa = 0;
    cmds[1].ya = 0;
    cmds[1].xc = FRAME_WIDTH - 1;
    cmds[1].yc = FRAME_HEIGHT - 1;
    cmds[2].ctrl = CMD_LOCAL_COORD;
    cmds[2].xa = 0;
    cmds[2].ya = 0;
    ((volatile vdp1_cmd_t*)(VDP1_VRAM + VRAM_FRAME_CMDS))->ctrl = CMD_END;
}

void renderFree()
{
}

// flush() hands each frame to VDP1; frame buffers change in the vblank
// interrupts (system.cpp).
void renderSwap()
{
}
