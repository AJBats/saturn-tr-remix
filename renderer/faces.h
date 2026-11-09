// Faces: straight to VDP1 commands, in one pass.
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
#ifndef RENDER_FACES_H
#define RENDER_FACES_H

#include <stdint.h>

#include "hints.h"
#include "types.h"

namespace render {

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

// A mesh's faces: its quads, then its triangles, as the game adds them. The
// last 'cards' of its quads are cards (tools/diet.py): a quad down the line on
// the screen from one end to the other, half wide either side of it at each
// end's depth, so that it always faces the camera. Its corners A B C D are
// its first end, its second, and copies of the second and the first (the
// mesh's vertices, transformed and lit with the rest), which facesMeshCards
// places across the line before the faces go; its texture runs along it
// from its first end.
//
// A segment joined to its parent (tools/diet.py join_rings) has corners on its
// parent's vertices, whose job lies back bytes before its own: its corners
// count from there, its own vertices from back on.
struct FaceSet
{
    uint16_t quads, triangles;
    uint16_t cards, back;
};

// In FaceSet.back: a see-through mesh whose near quads are left out (frame.cpp), which has nothing back.
#define FACE_NEAR_CUT   0x8000
#define FACE_NEAR_QUADS 96      // the most quads such a mesh has

// What a card takes above its half width, in FaceQuad.half: a facing card's
// length / 2, in model units, its first end placed that far from its second on
// the screen whichever way the line between them points at the camera.
#define CARD_HALF(half)     ((half) & 0xFF)
#define CARD_LENGTH(half)   ((half) >> 8)

struct FaceQuad
{
    uint16_t corners[4];    // byte offsets from the mesh's vertices (FaceSet.back)
    uint16_t material;
    uint16_t half;          // a card's half width, in model units (1 .. 255), and
                            // CARD_LENGTH; 0 else
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
    int32_t voidX0, voidY0, voidX1, voidY1; // +48 the box round this frame's fills so far, x0 > x1 none
    int32_t voidArea;       // +64 ... and their areas added up
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

#ifdef STEPWISE
// Who made which commands (tools/stepwise.py): a mark is the next command's
// place as a room or a draw begins, list 0 the rooms', 1 the draws'. A list's
// marks end at gStepUploaded when frameUpload sends it.
#define STEP_MARKS  512
#define STEP_ROOM   0x01000000      // | the room
#define STEP_DRAW   0x02000000      // | its type << 16 | its mesh
#define STEP_FILL   0x03000000

struct StepMark
{
    const uint32_t* cmd;
    uint32_t who;
};

extern StepMark gStepMarks[2][STEP_MARKS];
extern uint32_t gStepHead[2], gStepUploaded[2];

inline void stepMark(int32_t list, const CommandList &l, uint32_t who)
{
    StepMark &m = gStepMarks[list][gStepHead[list]++ % STEP_MARKS];
    m.cmd = l.cmd;
    m.who = who;
}
#endif

// A list taking the commands from slot first of cmds (and its gouraud
// tables) up to slot end, with heads of OT_SIZE + 1 buckets, emptied, and
// tails of OT_SIZE.
void facesListInit(CommandList &list, Command* cmds, uint32_t (*grd)[2], int32_t first, int32_t end,
                   uint16_t* heads, uint16_t* tails, const Material* materials, const uint16_t* shadeGouraud,
                   const uint8_t* shadeLevel, ShadeCache* shade, const Sprite* sprites);

// A list on to slots first .. end for a frame, its buckets as the last
// join left them: empty.
void facesListStart(CommandList &list, Command* cmds, uint32_t (*grd)[2], int32_t first, int32_t end);

// A mesh's faces, with its vertices; mode MESH_INSIDE: none of them is off
// the frame or past a plane, so no face is culled for that; MESH_WATER: under
// water, its shades fading to teal with distance.
#define MESH_INSIDE     1
#define MESH_WATER      2
extern "C" void facesMeshAsm(const FaceSet* set, const Vertex* vertices, uint32_t mode, CommandList* list);

// A mesh's cards' corners placed in its projected vertices: each corner its
// end moved across the line between the ends by the card's half width at
// that end's depth (half * divTable[z >> 4] / 4096 pixels), the end's depth
// and shade kept but for shade added to each (within 0 .. 31: the side of the
// segment the camera sees, verticesCardShade). Off the frame, the corners are
// flagged and clamped as the projection does a vertex (unless the mesh lies
// inside the frame: MESH_INSIDE, whose bound takes its cards in). A card with
// an end past a plane is left: its quad's corners carry the plane's flag.
// vertices: as the mesh's corners count from (FaceSet.back).
extern "C" void facesMeshCardsAsm(const FaceSet* set, Vertex* vertices, uint32_t mode, const uint16_t* divTable,
                                  int32_t shade);

// A face's command made but held out of its bucket (a draw order hint's):
// where it is, its first word (unlinked), its place, its bucket, the hint.
struct HeldCommand
{
    uint32_t* cmd;
    uint32_t w0, link;
    int32_t depth;
    const DepthHint* hint;
};

// A hinted mesh's faces (hints.h hintsOf), as facesMeshAsm makes them: each
// moved by the hints' buckets, or (the faces they name, in their order,
// while held has room) taken back out of its bucket and held, into held from
// count; count on past them. Then a held face into bucket depth.
void facesMeshHinted(const FaceSet* set, const Vertex* vertices, uint32_t mode, CommandList &list,
                     const HintFaces &hints, HeldCommand* held, int32_t &count, int32_t room);
void facesLinkHeld(CommandList &list, const HeldCommand &held, int32_t depth);

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
    uint32_t w1;                // +4  ... draw mode << 16 | colour table (a mip level's is + 4
                                // a level: the texture's tables lie side by side)
    int16_t key;                // +8  in the cache, -1 none
    uint16_t colour;            // +10 its mean
    uint8_t mips;               // +12 the mip levels it takes: 0 .. 2
    uint8_t skip;               // +13 see-through texels: left out without the cache; bit 1
                                // sparse (TEX_ATTR_SPARSE); bits 2 .. 7 the shade from which, at every
                                // corner, a face of it can't be seen, so black will do (63 none:
                                // render.cpp)
    uint16_t lines[2];          // +14 4 x its texels along a face's lines, as it is and turned
    uint16_t sizes[6];          // +18 CMDSIZE by variant: mip level * 2 + turned
    uint16_t quadrant;          // +30 a quadrant's characters / 8
    uint32_t frame;             // +32 the frame w2 is of
    uint32_t w2[6];             // +36 by variant, from the cache that frame: character << 16 |
                                // size; 0 not looked up yet
    uint32_t quads;             // +60 ... in quadrants
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

// What quads split near the camera project their new corners with; the lab's old
// path puts them in the vertices from top down to bottom.
struct RoomSplit
{
#ifdef RENDER_LAB
    Vertex* vertices;
    int32_t top, bottom;
#endif
    const uint16_t* divTable;
};

struct RoomRun;

// A solid box over a room's window (facesRoom, from a BoxSeen): its rectangle, and where a
// face's corners must lie to be behind it, in the coordinates of the room's points:
// lo < p < hi on each axis.
struct RoomBox
{
    int16_t x0, y0, x1, y1;
    int16_t lo[3], hi[3];
};

// A room's faces: its quads in their runs, then its triangles, as the game
// adds them (faces.s, rooms.cpp roomFaces), each straight to its command,
// through facesroom.s, the run made for the frame's rooms by
// facesRoomsBegin.
void facesRoomsBegin(RoomRun &run, RoomTextures &textures, RoomSplit &split, CommandList &list);

// The rooms' far plane, in world units (frame.h frameFar): a room face wholly
// past it is a fill behind everything, as one a plane cuts.
void facesFar(int32_t far);

void facesRoom(const FaceRoom &room, RoomRun &run);

// A floor on VDP2 and what lies under it. VDP1 paints in front of VDP2, so a face past the floor
// would paint over it where a polygon floor used to hide it. From above the floor, a room wholly
// under it shows only through the openings at the floor's height. facesFloorRoom notes such a room's
// commands as its faces are made; facesUnderFloor, the buckets joined, finds the openings on the
// frame, as the rows they cover in each of its columns, and keeps those commands to them: a face
// that misses them is left out, one partly in them is drawn clipped to those rows.
#define FACES_NO_FLOOR  0x7FFFFFFF

// Water's sparkle as a few flat shimmers. The level has no quads for it: a room's table (RoomGeometry.water)
// says where its water lies: int16 sheets, each int16 y, uint8 seen from above, uint8 runs, each a run of
// sectors in the room: uint8 z, x, count, shade. Each sector in view that's owed some by its size on the
// frame places them, stuck to the water, flat on the frame, at the sector's depth; the far ones a pixel or a
// line, the near a piece of art, in half tones as they come and go. A frame's count follows the water's
// area on the frame, whatever the sectors'.
#define SHIMMER_VRAM_BYTES  1024
// their art, the level file's (pack_level.py water_art), and colour tables go here in VDP1 VRAM; 0, lines alone
void facesShimmersInit(uint32_t vram, const uint8_t* art);
void facesShimmersDensity(int32_t oneIn);
struct ShimmerSide
{
    int32_t o, e, f;        // o + e * x + f * z: how far past this side of the clip a sector's middle is
    int32_t past;           // ... past by more than this, the sector is out
};
// What shimmers.s reads: the frame's, then a sheet of a room's water's.
struct ShimmerSheet
{
    CommandList* list;          // +0
    uint32_t* stop;             // +4   the list's commands stop here
    const uint16_t* divTable;   // +8
    const int32_t* depth;       // +12
    const uint8_t* bag;         // +16
    const void* pieces;         // +20
    const void* views;          // +24
    const uint8_t* seen;        // +28  by room: the frame's low byte if it's in this frame's walk
    uint32_t frame;             // +32
    int32_t spriteDd;           // +36
    int32_t limit;              // +40
    int32_t runs;               // +44  how many, of the generation being drawn
    void* lives;                // +48  the next shimmer born goes here
    void* livesEnd;             // +52
    uint32_t turn;              // +56
    const void* view;           // +60
    int32_t owed;               // +64
    int32_t oneZ;               // +68  past this a sector doesn't owe as many as this slot
    int32_t wx, wz;             // +72  its first sector's middle in the world, 8 to a unit
    int32_t x0, x1, y0, y1;     // +80  the room's clip, on the frame
    int32_t oz, ez, fz;         // +96  a sector's middle's z: o + e * x + f * z, 256 to a view unit
    int32_t nearZ;              // +108 its z below this, out
    int32_t reachZ;             // +112
    int32_t unused116, unused120;
    int32_t lineShift;          // +124 a line is half as long, this generation
    ShimmerSide right, left, bottom, top;   // +128
};
extern "C" int32_t facesShimmersBornAsm(ShimmerSheet* sheet, const uint8_t* sectors, int32_t count, int32_t first);
extern "C" int32_t facesShimmersDrawAsm(ShimmerSheet* sheet, const void* lives, const uint8_t* runs, const uint32_t* w1);
// Before either CPU comes to them; then, each CPU as its faces are done: this frame's new shimmers, and all that
// are alive into its list, a generation at a time, what the other hasn't taken. The master's draw after the rooms'
// faces in a bucket as the slave's do. Returns the commands this CPU made.
void facesShimmersReset();
int32_t facesShimmersFrame(const FaceRoom* rooms, uint32_t count, const RoomLevel* level, const RoomGeometry* geometry,
                           CommandList &list, uint32_t frame);
void facesFloorSet(int32_t y);          // the level's floor, or FACES_NO_FLOOR
void facesFloorKeep(bool on);           // the menu's
void facesFloorBegin();
void facesFloorRoom(const FaceRoom &room, const RoomLevel* level, int32_t first, int32_t end);
// A dip's commands; then, if there were any this frame, the masks': they're drawn as colour 0 inside the box
// round the dips' (facesUnderFloor, when the buckets are joined).
void facesFloorDip(const Command* cmds, int32_t first, int32_t end);
bool facesFloorDipDrawn();
void facesFloorMasks(int32_t first, int32_t end);
void facesUnderFloor(const FaceRoom* rooms, uint32_t count, const RoomLevel* level, const void* portals, CommandList &list,
                     Command* cmds);

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
    int32_t farBucket;          // +268 a corner at the far plane's (facesFar)
    void (*carefulTri)(RoomRun* run, const RoomQuad* triangle); // +272 a triangle the same, or
                                // none: filled where a plane cuts it, else on as it is
    RoomVariant quads;          // +276 the cache's quadrants, for faces split near the camera
    uint32_t pieceW0, pieceW1;  // +292 the face being split's command words
    const RoomMaterial* pieceMaterial;  // +300 ... its material
    const int8_t* wibble;       // +304 the room's ripple offsets (32), or none
    int32_t phase;              // +308 ... their phase
    int32_t window[4];          // +312 the room's portal window: x0 y0 x1 y1
    int32_t (*tiles)(RoomRun* run, uint32_t key, uint32_t size);    // +328 the texture's tiles'
                                // character / 8, 0 when the cache can't have them
    void (*near)();             // +332 facesRoomNear; the lab has none
    int32_t voidWay;            // +336 gFacesVoidWay, for the run
    int32_t boxNear;            // +340 a face with corner A nearer isn't behind any of the room's boxes
    int32_t boxCount;           // +344 those over the room's window
    const RoomBox* boxes;       // +348 ... roomBoxes
    const RoomVertex* points;   // +352 the room's
    uint32_t fullOut;           // +356 the frame the full-res pool last had no room
    int32_t margin;             // +360 DECIDE's margin on the turn (facesTurnMargin): 0 none
    int32_t turnRule;           // +364 DECIDE's turn (facesTurnRule): TurnRule
    const BoxSeen* seen;        // the frame's solid boxes (rooms.h roomsBoxes), for facesRoom
    int32_t seenCount;
    RoomBox roomBoxes[BOXES_SEEN];
};

// What room faces and the walk do where a plane cuts a face away, to keep old
// pixels from showing: each paints its own rectangle of colour 0 behind everything;
// or adds to the list's box, for one rectangle round them all, painted first; or, on a
// frame buffer already clear, nothing. Set between frames. Meshes' stay their own unless
// the buffer is clear: they're few.
enum FaceVoidWay { FACE_VOIDS_DRAWN, FACE_VOIDS_BOXED, FACE_VOIDS_NONE };
extern "C" volatile int32_t gFacesVoidWay;

// Whether a room face's turn, once decided, keeps till the other way does better by an eighth
// (facesroom.s DECIDE); set between frames.
void facesTurnMargin(bool on);

// How a room face's turn (its lines along its texture's rows or its columns) is decided: by the fewer
// lines, or by the most texels shown, Fast halving those along the lines; set between frames.
enum TurnRule { TURN_LINES, TURN_TEXELS };
void facesTurnRule(TurnRule rule);

extern "C" void facesRoomAsm(const FaceRoom* room, RoomRun* run);
extern "C" void facesRoomNear();    // facesroom.s's, as it calls it

// One face as the game made it whole (a sprite's, a rectangle's or line's,
// a fill's, a void's: render.cpp faceCommand), its corners in half, into
// bucket depth (facesother.s).
extern "C" void facesOtherAsm(const Face* face, const Vertex* half, int32_t depth, CommandList* list);

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

}

#endif
