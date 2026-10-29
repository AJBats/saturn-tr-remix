// Stage 3 the new way: faces straight to VDP1 commands, in one pass.
//
// Each face a CPU keeps (facing the camera, not wholly off the frame) has its
// command and gouraud table written at once into the CPU's CommandList, and is
// chained into its depth bucket: its command's link (VDP1's jump) is the
// command added to that bucket before it, the order the game's ordering table
// gives. No face record is kept, and nothing is read again. Once both CPUs are
// done, facesJoin links the buckets far to near (in each, the rooms' chain
// before the meshes'), and the list is ready for VDP1 as it stands.
//
// What a face's command takes from the level is made ready by the packer
// (tools/facepack.py): its corners as byte offsets into its mesh's vertices,
// and a material, the command's first three words.
#ifndef FACES_H
#define FACES_H

#include <stdint.h>

#include "game.h"

// A command's first three words as a face's material makes them, and what may
// be done to them.
struct Material
{
    uint32_t w0;            // the control word (the command, a texture's flip, a jump to
                            // CMDLINK) << 16
    uint32_t w1;            // draw mode << 16 | colour or colour table
    uint32_t w2;            // character << 16 | its size
    uint32_t flags;         // MAT_*
};

#define MAT_SHADEABLE   1   // a shaded copy of its colour table may stand in for gouraud
#define MAT_SHADOW      0   // the shadows' material

// A mesh's faces: its quads, then its triangles, as the game adds them.
struct FaceSet
{
    uint16_t quads, triangles;
};

struct FaceQuad
{
    uint16_t corners[4];    // byte offsets into the mesh's vertices
    uint16_t material;
    uint16_t spare;
};

// Drawn as the parallelogram v0, x, x + y - v0, y (x, y: v1, v2, or v2, v1
// with TRI_SWAP), v0 at corner A or (TRI_V0_AT_D) D; without
// TRI_PARALLELOGRAM, as a quad v0 v1 v2 v2.
struct FaceTriangle
{
    uint16_t corners[3];
    uint16_t flags;         // TRI_*
    uint16_t material;
    uint16_t spare;
};

#define TRI_PARALLELOGRAM   1
#define TRI_V0_AT_D         2
#define TRI_SWAP            4

inline const FaceQuad* faceQuads(const FaceSet* set)
{
    return (const FaceQuad*)(set + 1);
}

inline const FaceTriangle* faceTriangles(const FaceSet* set)
{
    return (const FaceTriangle*)(faceQuads(set) + set->quads);
}

// A CPU's commands as its faces add them.
struct CommandList
{
    uint32_t* cmd;          // +0  the next command's words
    uint32_t* end;          // +4  where its commands must stop
    uint32_t* grd;          // +8  the next command's gouraud table
    uint32_t link;          // +12 the next command as VDP1 links to it (VRAM address / 8)
    uint32_t grdLink;       // +16 the next command's last word: its gouraud table's VRAM address / 8 << 16
    uint16_t* heads;        // +20 each bucket's last command added (link), 0 for none
    uint16_t* tails;        // +24 ... and its first, whose link the join sets
    const Material* materials;      // +28
    const uint16_t* shadeGouraud;   // +32 shade 0 .. 31 to its gouraud colour
    ShadeCache* shade;      // +36 the CPU's shaded colour tables, or none
};

// A command's place, as VDP1 links to it.
inline uint32_t facesLink(int32_t slot)
{
    return (VRAM_FRAME_CMDS >> 3) + slot * (sizeof(Command) >> 3);
}

inline int32_t facesSlot(uint32_t link)
{
    return (link - (VRAM_FRAME_CMDS >> 3)) / (sizeof(Command) >> 3);
}

// A list taking the commands from slot first of cmds (and its gouraud
// tables) up to slot end, with heads of OT_SIZE + 1 buckets, emptied, and
// tails of OT_SIZE.
void facesListInit(CommandList &list, Command* cmds, uint32_t (*grd)[2], int32_t first, int32_t end,
                   uint16_t* heads, uint16_t* tails, const Material* materials, const uint16_t* shadeGouraud,
                   ShadeCache* shade);

// A mesh's faces, with its vertices; inside: none of them is off the frame or
// past a plane, so no face is culled for that.
extern "C" void facesMeshAsm(const FaceSet* set, const Vertex* vertices, int32_t inside, CommandList* list);

// The same in C: what facesMeshAsm does, to check it against.
void facesMeshC(const FaceSet* set, const Vertex* vertices, int32_t inside, CommandList &list);

// The shadows' faces: an octagon's three quads.
const FaceSet* facesShadow();

// One face as the game made it (a sprite's, a portal's fill: render.cpp
// faceCommand), into bucket depth.
void facesOther(const Face &face, const Vertex* half, const Sprite* sprites, int32_t depth, CommandList &list);

// A command made elsewhere (w: its words but the link and gouraud table's;
// grd: its gouraud table) into bucket depth.
void facesCopy(CommandList &list, const uint32_t* w, const uint32_t* grd, int32_t depth);

// The lists' buckets far to near, in each the lists' chains in turn, from the
// command in slot first of cmds (whose link it sets) to the one in slot end;
// the heads emptied for the next frame. Each list's heads hold OT_SIZE + 1
// buckets, the last always empty.
void facesJoin(CommandList* const* lists, int32_t count, Command* cmds, int32_t first, int32_t end);

// The same for the rooms' list and the meshes' (facesjoin.s): linkBase, where
// the link field of the command VDP1 would link to as 0 lies; links, the
// first command's link << 16 | the end's.
extern "C" void facesJoinAsm(CommandList* rooms, CommandList* meshes, uint8_t* linkBase, uint32_t links);

inline void facesJoinRoomsMeshes(CommandList &rooms, CommandList &meshes, Command* cmds, int32_t first, int32_t end)
{
    uint8_t* linkBase = (uint8_t*)(uintptr_t(cmds) - (VRAM_FRAME_CMDS >> 3) * 8 + 2);
    facesJoinAsm(&rooms, &meshes, linkBase, (facesLink(first) << 16) | facesLink(end));
}

#endif
