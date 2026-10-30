// The new renderer's data: what it reads of the level and the frame, in the
// game's own layouts where it reads the game's, and what it hands between its
// stages. The level's own layouts are kept: the packer and the game write
// them as they are.
#ifndef RENDER_TYPES_H
#define RENDER_TYPES_H

#include <stdint.h>

#define ALIGN16 __attribute__((aligned(16)))

// The game's divide table (common.cpp): the projection's and the pose's.
extern uint16_t divTable[];

namespace render {

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
#define MAX_DRAWS       256     // a frame's: the master's 2048 vertices hold about that many of the smallest meshes
#define MASTER_VERTICES 2048
#define MAX_CMDS        1792    // VRAM_MAX_CMDS

#define OT_SHIFT        4
#define OT_SIZE         1281

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

// Command words: ctrl|link, pmod|colr, srca|size, A, B, C, D, grda|0.
union Command
{
    uint32_t w[8];
};

#define VRAM_FRAME_CMDS     0x00060
#define VRAM_GOURAUD_BASE   (VRAM_FRAME_CMDS + MAX_CMDS * 32)

// A draw of the master's.
enum DrawType
{
    DRAW_MESH = 1,          // a mesh or static mesh: its faces by faces.s
    DRAW_SHADOW,            // a shadow's: its quads
    DRAW_COPY               // a face as the game made it (a sprite's, a
                            // portal's fill), into its bucket
};

struct DrawScene
{
    uint8_t type;           // DrawType
    uint8_t inside;         // MeshFaces.inside
    uint16_t mesh;          // MESH's
    uint32_t flags;         // MeshFaces.flags
    int32_t base;           // its first vertex
    int32_t first, count;   // its faces in MPOL
    int32_t bucket;         // DRAW_COPY's
};

// A draw's thing (render.cpp depthHintObject's: an item, or a room's static)
// and its mesh, from the thing's first drawn: what the draw order hints name
// (DTHG, the scene's draws'; a draw list's hinted calls' draws).
#define NO_THING    0xFF        // kind: a draw of no thing (a shadow's, a copy)

struct DrawThing
{
    uint8_t kind;           // 0 an item, 1 a static
    uint8_t owner;          // a static's room
    uint8_t node;
    uint8_t pad;
    uint16_t index;         // the item's in the level, or the static's in its room
    uint16_t pad2;
};

// A call whose thing may have a draw order hint (the bit of its index & 31
// in the frame's mask of its kind: hints.h hintsMasks), and its draws: a
// draw list's.
struct HintedCall
{
    uint8_t kind;           // DrawThing's
    uint8_t owner;
    uint16_t index;
    uint16_t first, count;  // its draws
};

// A draw's transform as the game set it up (the capture's record).
struct DrawXform
{
    int32_t m[12];          // the matrix's rows: m00 m01 m02 tx, m10 m11 m12 ty, m20 m21 m22 tz
    int32_t ambient, range; // the light: gLightAmbient, gLightRange
    int16_t light[3];       // ... gLightView
    int16_t shadow[4];      // a shadow's x, z, half sizes x, z (render.cpp shadowVertices)
    int16_t pad;
};

struct PoseArm
{
    uint32_t frame;         // in PFRM, or ~0: the arm moves with the body
    int32_t frameRate;
    int16_t angle[3];
    uint8_t useBasis, pad;
};

struct PoseItem
{
    uint8_t lara;           // Lara's nodes (draw.h drawLaraNodesLerp), else drawNodesLerp's
    uint8_t count;          // its model's nodes, a mesh each
    uint16_t firstDraw;     // its first mesh's draw (DRAW, XFRM); the rest follow
    uint32_t nodes;         // in PNOD
    uint32_t frameA, frameB;    // in PFRM
    int32_t frameDelta, frameRate;  // ItemObj::getFrames's
    uint32_t visibleMask;
    int32_t pos[3];
    int16_t angle[3];
    int16_t extraCount;     // an enemy's extra angles (head, neck), as many as its nodes' ROT flags take
    int16_t extra[4];
    int16_t torso[3], head[3];  // Lara's
    uint8_t weapon, pad[3];
    PoseArm arms[2];        // Lara's right and left
};

struct PoseLight
{
    int16_t roomX, roomZ;   // its room's place, in sectors
    uint8_t intensity;      // the item's own (0: lit by its room)
    uint8_t ambient;        // its room's
    uint8_t lights, pad;    // its room's lights, from first in LGHT
    uint32_t first;
};

struct PoseStatic
{
    uint16_t draw;          // DRAW's
    uint8_t quadrant, pad;  // its quarter turn (STATIC_MESH_QUADRANT)
    int32_t intensity;      // STATIC_MESH_INTENSITY
    int32_t x, y, z;        // its place in the world
};

struct PoseShadow
{
    uint16_t draw;          // DRAW's
    uint16_t item;          // POSE's
    int32_t floor;          // the floor's height under the item
};

struct RoomLevel
{
    int16_t x, z;           // its place, in sectors
    int16_t yTop;
    uint8_t flags;          // ROOM_FLAG_WATER
    uint8_t portals;
    uint16_t vertices, pad;
    uint32_t first;         // its first portal in RPRT
};

// A room queued: its window (x0 y0 x1 y1) and matrix (the view placed at the
// room, Matrix's 12 int32).
struct RoomSeen
{
    uint16_t room, pad;
    int32_t window[4];
    int32_t m[12];
};

struct RoomGeometry
{
    uint32_t quads, triangles, points;  // offsets in RGEO; NO_GEOMETRY: none in the scene
    uint16_t quadsCount, trianglesCount;
    uint16_t facing[6];     // RoomInfo.facing
    uint32_t pad;
};

#define NO_GEOMETRY     0xFFFFFFFF

// The caustics under water: a vertex's shade offset is the wave at its start
// (its index's) plus the frame's phase.
struct RoomCaustics
{
    int32_t phase;          // gCausticsFrame
    int16_t wave[32];       // gCaustics
    uint8_t starts[32];     // gRandTable, & 31
};

struct PoseCamera
{
    int32_t pos[3];         // gCameraViewPos
    int16_t rotation[9];    // gViewRotation
    int16_t pad;
};

enum CallKind { CALL_VIEWPORT = 1, CALL_MODEL, CALL_STATIC, CALL_SHADOW, CALL_SPRITE, CALL_RECT };

struct CallRecord
{
    uint8_t kind, pad;
    uint16_t index;         // a model's or shadow's ItemRecord, a static's room, a sprite's, a rectangle's type
    uint32_t flags;         // the faces' at the call (FACE_WATER)
    int32_t args[6];        // a viewport's x0 y0 x1 y1; a model's item's index in the level; a static's
                            // RoomMesh (xy, zf), place from the camera and index in its room; a
                            // shadow's floor; a sprite's x y z and shade; a rectangle's x y width
                            // height, colour and bucket
};

#define AFRM_BASE   0x10000

// The game's own layouts, as the renderer reads them.
struct GameItem             // ItemObj
{
    uint32_t vtable;
    uint32_t room;          // +4 (the lab's none: its room is its record's)
    int32_t pos[3];         // +8
    int16_t angle[3];       // +20
    uint16_t flags;         // +26
    uint8_t pad0[4];
    uint16_t animIndex;     // +32
    uint16_t frameIndex;    // +34
    uint8_t pad1[16];
    uint8_t type;           // +52
    uint8_t intensity;      // +53
    uint8_t pad2[6];
    uint32_t visibleMask;   // +60
    const uint8_t* extra;   // +64 (the lab's: its record's)
    uint32_t nextItem;      // +68
    uint8_t pad3[4];
};

struct GameArm              // ExtraInfoLara::Arm
{
    int16_t angle[3];
    uint8_t pad0[6];
    uint16_t animIndex;     // +12
    uint16_t frameIndex;    // +14
    int16_t flash;          // +16 (its timer)
    uint8_t pad1[11];
    uint8_t useBasis;       // +29
    uint8_t pad2[2];
};

struct GameLara            // ExtraInfoLara, its first bytes
{
    uint8_t pad0[10];
    uint8_t weapon;         // +10
    uint8_t pad1;
    int16_t head[3];        // +12
    int16_t torso[3];       // +18
    GameArm arms[2];        // +24: right, left
    uint8_t pad2[108];
    uint16_t meshes[15];    // +196: each joint's mesh (level.meshes), in the order drawn
    uint16_t pad3;
};

struct ItemRecord
{
    GameItem item;
    uint16_t room;          // its room's index
    uint16_t index;         // the item's in the level
    uint8_t extra[228];     // its extra's first bytes: GameLara, or an enemy's angles
};

struct GameAnim             // Anim
{
    uint32_t frameOffset;   // bytes
    uint8_t frameRate;      // +4
    uint8_t frameSize;
    uint8_t pad0[10];
    uint16_t frameBegin;    // +16
    uint16_t frameEnd;      // +18
    uint8_t pad1[12];
};

struct GameModel            // Model
{
    uint8_t type;
    int8_t count;           // its nodes, a mesh each
    uint16_t start;         // its first mesh in level.meshes
    uint16_t nodeIndex;
    uint16_t animIndex;
};

struct GameStaticMesh       // StaticMesh
{
    uint16_t id;
    uint16_t meshIndex;     // +2
    uint32_t flags;         // +4: STATIC_MESH_FLAG_VISIBLE
    int32_t vsXY, vsZR;     // +8: its visible sphere, x y, z r (int16 each)
    uint8_t boxes[24];
};

struct RoomLights
{
    uint8_t ambient, lights;
    uint16_t pad;
    uint32_t first;         // in LGHT
};

// A room as its faces take it: a job of the walk's, for the vertices and
// the faces.
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

// The frame's draw list: the draws in order (meshes', shadows', and sprites'
// and rectangles' faces, made, two vertices each), the items posed (frames,
// extras, light), each static's place, each shadow's floor, and the calls
// whose thing may have a draw order hint.
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

}

#endif
