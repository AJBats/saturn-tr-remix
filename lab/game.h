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

// The faces each list holds after gameFaces*.
int32_t gameMasterFaces();
int32_t gameSlaveFaces();

// The scene's: the master's cache (0) and the slave's (1), and shade 0 .. 31
// to its gouraud colour.
ShadeCache* gameShadeCache(int32_t cpu);
const uint16_t* gameShadeGouraud();

// The vertices and the ordering tables as flush was given them: both CPUs'
// faces, the master's alone, or the slave's (the rooms').
enum FlushFaces { FLUSH_ALL, FLUSH_MESHES, FLUSH_ROOMS };
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

// A command and its gouraud table as VDP1 reads them against the game's in
// that slot; and the commands from 1 up to count: those that differ.
bool gameCommandMatches(const Command &cmd, const uint32_t* grd, int32_t slot);
int32_t gameCommandsCheck(const Command* cmds, const uint32_t (*grd)[2], int32_t count);

#endif
