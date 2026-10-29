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

// The vertices and the ordering tables as flush was given them.
void gameFlushPrepare();

// flush: their commands into gCmds from gCmds[1], with the game's clear first
// and the end after, the buckets shared out as the game shares them: the far
// ones run on the slave (split; slaveCycles then its time), or on the master
// before the near ones. Returns the end's slot.
int32_t gameFlush(bool split, uint32_t* slaveCycles = nullptr);

// A command and its gouraud table as VDP1 reads them against the game's in
// that slot; and the commands from 1 up to count: those that differ.
bool gameCommandMatches(const Command &cmd, const uint32_t* grd, int32_t slot);
int32_t gameCommandsCheck(const Command* cmds, const uint32_t (*grd)[2], int32_t count);

#endif
