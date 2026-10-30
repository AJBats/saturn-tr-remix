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
    const uint8_t* shadeLevel;      // +40 shade 0 .. 31 to its gouraud level, for water
    const Sprite* sprites;  // +44 the level's, for a sprite's face
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
                   const uint8_t* shadeLevel, ShadeCache* shade, const Sprite* sprites);

// A mesh's faces, with its vertices; mode MESH_INSIDE: none of them is off
// the frame or past a plane, so no face is culled for that; MESH_WATER: under
// water, its shades fading to teal with distance.
#define MESH_INSIDE     1
#define MESH_WATER      2
extern "C" void facesMeshAsm(const FaceSet* set, const Vertex* vertices, uint32_t mode, CommandList* list);

// The same in C: what facesMeshAsm does, to check it against.
void facesMeshC(const FaceSet* set, const Vertex* vertices, uint32_t mode, CommandList &list);

// The shadows' faces: an octagon's three quads.
const FaceSet* facesShadow();

// A room texture as facesroom.s draws it (facesRoomMaterials): from the
// cache, or without it. Room textures have no character but in the cache
// (pack_level.py), so without it a face is a polygon in the texture's mean
// colour, or left out for one with see-through texels.
struct RoomMaterial
{
    uint32_t w0;                // +0  from the cache: the control word (a distorted sprite, the
                                // texture's flip, a jump to CMDLINK) << 16
    uint32_t w1;                // +4  ... draw mode << 16 | colour table
    int16_t key;                // +8  in the cache, -1 none
    uint16_t colour;            // +10 its mean
    uint8_t mips;               // +12 the mip levels it takes: 0 .. 2
    uint8_t skip;               // +13 see-through texels: left out without the cache
    uint16_t lines[2];          // +14 4 x its texels along a face's lines, as it is and turned
    uint16_t sizes[6];          // +18 CMDSIZE by variant: mip level * 2 + turned
    uint16_t spare;
    uint32_t frame;             // +32 the frame w2 is of
    uint32_t w2[6];             // +36 by variant, from the cache that frame: character << 16 |
                                // size; 0 not looked up yet
    uint32_t spare2;
};

// The room texture cache as the frame finds it, and what room faces take
// from the level besides their own data.
struct RoomTextures
{
    const Texture* textures;
    const RoomMaterial* materials;  // by texture, for facesroom.s
    const uint16_t* palette;    // coloured faces' colours
    uint8_t (*keys)[DETAIL_MAX_KEYS];  // a texture's slot + 1 in its level's pool, by variant; 0 none
    DetailSlot* slots;          // each pool's in turn
    uint32_t frame;             // the frame's: a slot drawn is marked with it
    int32_t lod;                // 4 x the texels a pixel along a face's lines past which it
                                // takes the next mip level; 0, full res only
    uint32_t pmod;              // textured faces' draw mode
    uint32_t pools[3];          // each level's pool in VDP1 VRAM: full res, half, quarter
    int32_t (*miss)(uint32_t key, uint32_t size, uint32_t variant);  // a texture the cache
                                // hasn't, into it: its slot, or -1 (detail.cpp detailSlotMiss)
};

// The room materials of textures 0 .. count - 1.
void facesRoomMaterials(const RoomTextures &textures, RoomMaterial* materials, int32_t count);

// A room as its faces take it.
struct FaceRoom
{
    RoomQuad* quads;            // the runs square to an axis, then the rest; a quad keeps its
                                // turn and mip level in its flags' top byte
    const RoomQuad* triangles;
    const RoomVertex* points;   // its vertices in room space
    const Vertex* vertices;     // ... as drawn
    uint16_t quadsCount, trianglesCount;
    uint16_t facing[6];         // the runs square to an axis, facing +x, -x, +y, -y, +z, -z
    int32_t camera[3];          // the camera from the room's origin
    int16_t m[9];               // its matrix's rows (room vertex units times 4)
    int32_t t[3];               // ... and translation
    int32_t clip[4];            // its portal window: x0 y0 x1 y1
    uint32_t flags;             // FACE_WATER for a wet room
    int32_t ripple;             // across the water's surface from the camera: its corners wibble
    int32_t phase;              // ... by this phase
    uint16_t verticesCount;
    uint16_t room;              // its index in the level
    const int16_t* caustics;    // a wet room's: each vertex's shade offset, by its index (32); else none
};

// Where quads split near the camera put their new corners: the vertices
// from top down to bottom.
struct RoomSplit
{
    Vertex* vertices;
    int32_t top, bottom;
    const uint16_t* divTable;
};

struct RoomRun;

// A room's faces: its quads in their runs, then its triangles, as the game
// adds them (faces.s, rooms.cpp roomFaces), each straight to its command,
// through facesroom.s, the run made for the frame's rooms by
// facesRoomsBegin; facesRoomC does the same in C.
void facesRoomsBegin(RoomRun &run, RoomTextures &textures, RoomSplit &split, CommandList &list);
void facesRoom(const FaceRoom &room, RoomRun &run);
void facesRoomC(const FaceRoom &room, RoomTextures &textures, RoomSplit &split, CommandList &list);

// A face's shading in facesroom.s.
enum RoomShading { SHADING_WATER = -1, SHADING_NEAR, SHADING_FAR, SHADING_PLAIN };

// A variant of a room texture in the cache (a mip level, turned or not) as
// facesroom.s draws it.
struct RoomVariant
{
    const uint8_t* keys;        // +0  the cache's slot + 1 by key
    uint32_t* frames;           // +4  its pool's first slot's frame (DetailSlot.frame)
    uint16_t pool;              // +8  the pool's place in VDP1 VRAM / 8
    uint16_t step;              // +10 a slot's size / 8
    uint8_t size;               // +12 the variant's CMDSIZE's offset in a RoomMaterial
    uint8_t turned;             // +13 drawn A D C B, the texture transposed
    int8_t shading;             // +14 RoomShading
    uint8_t variant;            // +15 the cache's (DetailVariant), for its miss
};

// What facesroom.s takes for a room, from the cache and the list
// (facesRoomsBegin) and the room (facesRoom), at GBR as its faces go.
struct RoomRun
{
    const Vertex* vertices;     // +0  the room's
    const RoomMaterial* materials;  // +4
    uint32_t frame;             // +8  RoomTextures'
    int32_t lod;                // +12 ...
    int32_t parity;             // +16 the run's quads', as facesRoomC's
    uint32_t* end;              // +20 the list's
    uint16_t* heads;            // +24 ...
    uint16_t* tails;            // +28
    const uint16_t* shadeGouraud;   // +32
    ShadeCache* shade;          // +36
    const uint8_t* shadeLevel;  // +40
    CommandList* list;          // +44 its cursors, kept in registers but for calls
    int32_t (*miss)(uint32_t key, uint32_t size, uint32_t variant);  // +48 RoomTextures'
    void (*careful)(RoomRun* run, const RoomQuad* quad);    // +52 a quad a plane cuts, or near
                                // the camera: split, filled or whole, in C
    int32_t plain;              // +56 the shading of faces drawn without the cache
    const RoomVariant* variants;    // +60 by mip level * 2 + turned: dry's or wet's
    const FaceRoom* room;       // +64 for careful
    RoomTextures* textures;     // +68
    RoomSplit* split;           // +72
    RoomVariant dry[6], wet[6]; // the variants in a room out of water and in it
};

extern "C" void facesRoomAsm(const FaceRoom* room, RoomRun* run);

// One face as the game made it whole (a sprite's, a rectangle's or line's,
// a fill's, a void's: render.cpp faceCommand), its corners in half, into
// bucket depth (facesother.s).
extern "C" void facesOtherAsm(const Face* face, const Vertex* half, int32_t depth, CommandList* list);

// The same in C: what facesOtherAsm does, to check it against.
void facesOther(const Face &face, const Vertex* half, int32_t depth, CommandList &list);

// facesOtherAsm against facesOther on made-up faces of every kind (sprites
// flipped or not and shaded, rectangles and lines of any place and
// size, voids reaching past each edge or off the frame, buckets shared, a
// list filling up): the commands, gouraud tables, buckets and cursors that
// differ (and paths not taken).
int32_t facesOtherCheck(const uint16_t* shadeGouraud);

// The lists' buckets far to near, in each the lists' chains in turn, from the
// command in slot first of cmds (whose link it sets) to the one in slot end;
// the heads emptied for the next frame. Each list's heads hold OT_SIZE + 1
// buckets, the last always empty.
void facesJoin(CommandList* const* lists, int32_t count, Command* cmds, int32_t first, int32_t end);

// The same for the rooms' list and the meshes' (facesjoin.s), a range of
// their buckets' pairs (nearest << 16 | farthest) from the link field field
// on: the link field the next chain goes into. linkBase: where the link
// field of the command VDP1 would link to as 0 lies.
#define JOIN_PAIRS  ((OT_SIZE + 1) / 2)
extern "C" uint16_t* facesJoinPairs(CommandList* rooms, CommandList* meshes, uint8_t* linkBase, uint16_t* field,
                                    uint32_t pairs);

inline uint8_t* facesLinkBase(Command* cmds)
{
    return (uint8_t*)(uintptr_t(cmds) - (VRAM_FRAME_CMDS >> 3) * 8 + 2);
}

inline uint16_t* facesLinkField(Command* cmds, int32_t slot)
{
    return (uint16_t*)(facesLinkBase(cmds) + facesLink(slot) * 8);
}

// Every bucket, from the command in slot first to the one in slot end.
inline void facesJoinRoomsMeshes(CommandList &rooms, CommandList &meshes, Command* cmds, int32_t first, int32_t end)
{
    uint16_t* last = facesJoinPairs(&rooms, &meshes, facesLinkBase(cmds), facesLinkField(cmds, first), JOIN_PAIRS - 1);
    *last = facesLink(end);
}

#endif
