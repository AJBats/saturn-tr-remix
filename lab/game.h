// The game's way: stage 3, faces to VDP1 commands, as saturn/ does it today,
// for the lab's baseline. faces.s and flush.s are linked as they are; the C
// they call and the C around them (render.cpp, rooms.cpp) is copied into
// game_faces.cpp and game_flush.cpp, for the look the lab builds (the game's
// defaults: dark faces by gouraud alone, shaded tables, mip levels, turned
// lines, mesh shadows, near quads split only where a plane cuts them, far
// faces textured). The layouts here are the game's, which the assembly reads.
#ifndef GAME_H
#define GAME_H

#include <stdint.h>

#include "textures.h"

#define ALIGN16 __attribute__((aligned(16)))

struct Vertex
{
    union
    {
        struct
        {
            int16_t x;
            int16_t y;
        };
        uint32_t xy;
    };
    int16_t z;
    uint8_t g;
    uint8_t clip;
};

struct Face
{
    uint32_t flags;
    Face* next;
    int16_t indices[4];     // corners: byte offsets in their CPU's half of gVertices
};

#define MAX_VERTICES    (5 * 1024)
#define MASTER_VERTICES 2048
#define gSlaveVertices  (gVertices + MASTER_VERTICES)
#define MAX_FACES       1920
#define MAX_CMDS        1792    // VRAM_MAX_CMDS

#define OT_SHIFT        4
#define OT_SIZE         1281
#define OT_GROUP_SHIFT  5
#define OT_GROUPS       ((OT_SIZE >> OT_GROUP_SHIFT) + 1)

struct FaceList
{
    Face* next;
    Face* end;
    Face** ot;
    uint16_t groups[OT_GROUPS];
};

struct RoomQuad
{
    uint32_t flags;
    uint16_t indices[4];    // byte offsets into the room's vertices
};

struct RoomVertex
{
    uint8_t x, y, z, g;
};

struct MeshVertex
{
    int16_t x, y, z;
};

struct MeshQuad
{
    uint16_t flags;
    uint8_t indices[4];
};

struct Texture
{
    uint16_t srca, colr, size, attr, color, reserved, altColor, contrast;
};

struct Sprite
{
    uint16_t srca, colr, size, attr;
    int16_t l, t, r, b;
};

// The room texture cache (saturn/detail.h): slots in VDP1 VRAM holding the
// room textures in use, whole, in quadrants for a split quad, transposed, or
// at half or a quarter of their size; a texture's key is Texture.reserved - 1.
#define DETAIL_MAX_KEYS     1088
#define DETAIL_SLOT_SIZE    2048
#define DETAIL_FULL_SLOTS   128
#define DETAIL_MIP_SLOTS    64
enum DetailVariant { DETAIL_WHOLE, DETAIL_QUADS, DETAIL_TURNED, DETAIL_MIP1, DETAIL_MIP1_TURNED, DETAIL_MIP2,
                     DETAIL_MIP2_TURNED, DETAIL_VARIANTS };

struct DetailSlot
{
    uint16_t key;           // the texture's key, its variant << 12
    uint32_t frame;         // last drawn
};

// The CPUs' caches of shaded copies of colour tables (render.cpp): a copy of
// a face's table with a gouraud value added to its colours draws as gouraud
// would, all corners taking that value, without its cost. Two-way set
// associative by table and value, in VDP1 VRAM.
struct ShadeCache
{
    uint32_t frame;         // the frame being built
    uint32_t validFrom;     // copies drawn before this frame are stale
    uint32_t vram;          // the copies' VDP1 VRAM offset; 0: none
    uint32_t (*miss)(ShadeCache* cache, uint32_t key, uint32_t index);
    uint32_t keys[256];     // table address / 8 << 15 | gouraud value
    uint32_t frames[256];   // last drawn
};

#define SHADE_TABLE_SIZE 32

// A copy for the key, in the pair of slots holding index whose copy was drawn
// longest ago, unless both are drawn this frame or the last: its table
// address / 8, or 0.
uint32_t shadeTableMiss(ShadeCache* cache, uint32_t key, uint32_t index);

// Command words: ctrl|link, pmod|colr, srca|size, A, B, C, D, grda|0.
union Command
{
    uint32_t w[8];
};

#define VRAM_FRAME_CMDS     0x00060
#define VRAM_GOURAUD_BASE   (VRAM_FRAME_CMDS + MAX_CMDS * 32)

extern Vertex gVertices[MAX_VERTICES];
extern Command gCmds[MAX_CMDS];
extern uint32_t gGouraud[MAX_CMDS][2];

// From the scene: the tables and caches the game's stage reads.
void gameInit();

// The game's divide table (gamevertices.cpp), the projection's and the pose's.
extern uint16_t divTable[];

// Stage 1 the game's way (gamepose.cpp): the items' node matrices as
// openlara/draw.h drawModel, drawNodesLerp and drawLaraNodesLerp make them
// with common.cpp's matrix arithmetic, each drawn mesh's to its draw; Init
// returns the items, Check the matrices that differ from the capture's (XFRM)
// (matrices: those made).
int32_t gamePoseInit();
void gamePose();
int32_t gamePoseCheck(int32_t &matrices);

// Stage 1 the game's way, the light (gamelight.cpp): each item's as drawModel
// sets it (calcLightingDynamic at getRelative, or calcLightingStatic; the
// fog), against the capture's (XFRM: ambient, range, and the light's
// direction where the range is above 0): the draws that differ. Init
// returns the items.
struct GameLight
{
    int32_t ambient, range;
    int16_t view[3];
    int16_t pad;
};

int32_t gameLightInit();
void gameLight();
const GameLight* gameLights();
int32_t gameLightCheck();

// Stage 1 the game's way, the static meshes' and the shadows' transforms
// (gamelight.cpp: drawRoomStatic's, drawShadow's; their visibility tests and
// the floor's lookup aside), by draw; against the capture's (XFRM): those
// that differ (statics, shadows: how many), made: the new way's, or none for
// the game's way's. Run on others: the shadows' items' light by item.
struct DrawXform;
struct PoseItem;
struct PoseStatic;
struct PoseShadow;
void gameStaticsInit();
void gameStatics();
int32_t gameStaticsCheck(const DrawXform* made, int32_t &statics, int32_t &shadows);
int32_t xformOff(const DrawXform &a, const DrawXform &b, bool shadow);
void gameStaticsRun(const PoseStatic* statics, uint32_t statCount, const PoseShadow* shadows, uint32_t shadowCount,
                    const PoseItem* items, const GameLight* lights, const uint8_t* frames, DrawXform* out);

// Stage 1 the game's way, the rooms seen (gamerooms.cpp: room.h's portal
// walk, draw.h queueRoom's matrices); Init returns the rooms the frame
// queued; Check the rooms that differ from the capture's (list, windows,
// matrices; seen: another's, or none for the game's way's), voids: the fills
// past the draw distance the walk made.
struct RoomSeen;
int32_t gameRoomsInit();
void gameRooms();
int32_t gameRoomsCheck(const RoomSeen* seen, int32_t count, int32_t &voids);

// The game's walk from another camera, in another room, and view (12 int32,
// no translation), the rooms queued to out, their jobs to jobs and the
// fills' rectangles (the first 64) to fills; near: the portals cut by the
// near plane it kept.
struct FaceRoom;
void gameRoomsRun(uint32_t room, const int32_t* camera, const int32_t* view, RoomSeen* out, int32_t &count,
                  int32_t &voids, int32_t &near, FaceRoom* jobs, int32_t &jobCount, int32_t (*fills)[4]);

// Room jobs (FaceRoom: the rooms queued with vertices, in order, placed in
// gVertices, as stages 2 and 3 take them; jobs null: the game's own) against
// the game's (ROOM, by room): those that differ, or a list of another length.
int32_t gameRoomJobsCheck(const FaceRoom* jobs, int32_t count);

// Stage 1 the game's way, the draw list (gamedraws.cpp): the renderer's side
// of the frame's calls after the walk (CALL), as draw.h and render.cpp take
// them: the draws in order (DrawScene: the meshes' and shadows' with their
// mesh, faces' flags and first vertex; sprites' and rectangles' faces, made,
// in faces with two vertices each), each item posed with its frames (as
// pointers), extras and light (PoseItem, PoseLight), each static's place
// (PoseStatic), each shadow's floor (PoseShadow), the calls whose thing may
// have a draw order hint (HintedCall: the hints' masks, gameDrawsHints).
// Init: the calls; gameDraws after fills of the walk's (their vertices
// first); the check against the scene's (DRAW past its fills, POSE, PLIT,
// STAT, SHDW, the hinted calls' things against DTHG): those that differ.
struct DrawScene;
struct HintedCall;
struct PoseItem;
struct PoseLight;
struct PoseStatic;
struct PoseShadow;
struct DrawList
{
    DrawScene* draws;
    uint32_t drawCount;
    PoseItem* items;
    PoseLight* lights;
    uint32_t itemCount;
    PoseStatic* statics;
    uint32_t staticCount;
    PoseShadow* shadows;
    uint32_t shadowCount;
    Face* faces;
    Vertex* vertices;
    uint32_t faceCount;
    HintedCall* hinted;
    uint32_t hintedCount;
};
int32_t gameDrawsInit();
void gameDrawsHints(const uint32_t masks[2]);
void gameDraws(int32_t fills);
const DrawList& gameDrawList();
int32_t gameDrawsCheck(const DrawList &list, int32_t fills);

// The game's way on other calls, items and animations, into out's arrays
// (room enough for them).
struct CallRecord;
struct ItemRecord;
struct GameAnim;
void gameDrawsRun(const CallRecord* calls, uint32_t count, ItemRecord* items, const GameAnim* anims, int32_t fills,
                  DrawList &out);

// The game's jobs for a list of rooms, with another level, camera room and
// camera: the jobs made.
struct RoomLevel;
int32_t gameRoomJobsRun(const RoomLevel* level, const RoomSeen* seen, int32_t count, uint32_t cameraRoom,
                        const int32_t* camera, FaceRoom* jobs);

// The game's light on other items, rooms (PLIT's layout), lights (LGHT's)
// and frames, by item to out.
struct PoseItem;
struct PoseLight;
void gameLightRun(const PoseItem* items, const PoseLight* rooms, const void* lights, uint32_t count,
                  const uint8_t* frames, GameLight* out);

// The game's way on other items, nodes and frames (PNOD's and PFRM's
// layouts), the matrices (12 int32 each) to out by draw.
struct PoseItem;
void gamePoseRun(const PoseItem* items, uint32_t count, const uint8_t* nodes, const uint8_t* frames, void* out);

// Stage 2 the game's way (gamevertices.cpp): the rooms' vertices (xform.s)
// and the master's draws' (meshxform.s), each set up as the game sets it up,
// into gVertices where the game put them; Prepare empties gVertices, Check
// counts the vertices not the game's, and those of the rooms and the draws.
void gameVerticesInit();
void gameVerticesPrepare();
void gameVerticesRooms();
void gameVerticesMeshes();
int32_t gameVerticesCheck(int32_t &roomVertices, int32_t &meshVertices);

// The vertices as drawn and both lists empty, then the master's draws' faces
// into its list, as the game adds them, and the rooms' into the slave's; then
// the check of both against the game's: the faces that differ, 0 when every
// face is the game's, in the game's bucket.
void gameFacesPrepare();
void gameFacesMaster();
void gameFacesRooms();
int32_t gameFacesCheck();

// The rooms' faces as the game adds them, the rooms given in another order.
struct RoomScene;
void gameFacesRooms(const RoomScene* rooms, uint32_t count);

// The faces each list holds after gameFaces*.
int32_t gameMasterFaces();
int32_t gameSlaveFaces();

// The scene's: the master's cache (0) and the slave's (1), and shade 0 .. 31
// to its gouraud colour.
ShadeCache* gameShadeCache(int32_t cpu);
const uint16_t* gameShadeGouraud();

// The vertices and the ordering tables as flush was given them: both CPUs'
// faces, the master's alone, or the slave's (the rooms'); or (made) the
// faces gameFaces* made, in their own tables, at the vertices they made.
enum FlushFaces { FLUSH_ALL, FLUSH_MESHES, FLUSH_ROOMS, FLUSH_MADE };
void gameFlushPrepare(FlushFaces faces = FLUSH_ALL);

// flush: their commands into gCmds from gCmds[1], with the game's clear first
// and the end after, the buckets shared out as the game shares them: the far
// ones run on the slave (split; slaveCycles then its time), or on the master
// before the near ones. Returns the end's slot.
int32_t gameFlush(bool split, uint32_t* slaveCycles = nullptr);

// A list VDP1 follows from slot 0 against the game's as it draws it, command
// by command (their jumps and gouraud tables' places aside, a shaded copy of
// a colour table as good as another of the same): those that differ; in
// visited the commands it followed, in shadedOther those the same but for
// one drawn through a shaded copy of its colour table where the other has
// gouraud of that one value at every corner (each CPU's cache of copies had
// room for different ones).
int32_t gameListCheck(const Command* cmds, const uint32_t (*grd)[2], int32_t &visited, int32_t &shadedOther);

// A list of the game's made in the lab: its commands and gouraud tables (room
// for MAX_CMDS), its end's slot, and the caches of shaded copies its flush
// left.
struct GameList
{
    Command* cmds;
    uint32_t (*grd)[2];
    int32_t end;
    ShadeCache caches[2];
    TextureQueue textures;      // the uploads its faces queued
};

// The game's list for this frame with its rooms in another order: the faces
// as the game adds them, flush on them, into list.
void gameListMake(const RoomScene* rooms, uint32_t count, GameList &list);

// The uploads the game's way's room faces last queued (from gameFacesPrepare).
const TextureQueue& gameTextureQueue();

// The list gameListCheck checks against: list, or (nullptr) the game's own.
void gameListReference(const GameList* list);

// A command and its gouraud table as VDP1 reads them against the game's in
// that slot; and the commands from 1 up to count: those that differ.
bool gameCommandMatches(const Command &cmd, const uint32_t* grd, int32_t slot);
int32_t gameCommandsCheck(const Command* cmds, const uint32_t (*grd)[2], int32_t count);

#endif
