// A scene: one frame of the game, captured (tools/capture.py) and packed for
// the lab (tools/labscene.py) as SCENE.SCN on its disc, with what the game
// drew it with and what it made of it. The file is chunks, each found by its id, big-endian as the
// Saturn reads them; each lies where the game kept it (high work RAM, but
// for the palette and sprites in low) and VRAM in VDP1 VRAM.
//
// INFO  SceneInfo
// VRAM  VDP1 VRAM with the frame on screen: textures, colour tables, the
//       room texture cache's slots, the shaded tables
// VERT  the vertices as drawn (the game's gVertices, MAX_VERTICES)
// MPOL  the master's faces as it added them (Face), SPOL the slave's: a
//       face's next is the next face's index in its pool plus one, 0 none
// ORDR  the ordering tables flush was given: uint16 heads, as next, of the
//       slave's OT_SIZE buckets then the master's; then each's faces by
//       groups of buckets (OT_GROUPS uint16 each)
// CMDS  the game's command list: the clear, a command a face, the end (low work RAM)
// GRDS  its gouraud tables, one a command
// TEXS  the level's textures (Texture), PALT its palette (256 colours),
//       SPRS its sprites (Sprite)
// SHAD  shade to gouraud level (32 bytes), half levels (96), shade to
//       gouraud colour (32 uint16): the game's gShadeLevel, gHalfLevel,
//       gShadeGouraud
// SHCA  the CPUs' caches of shaded colour tables (ShadeCache, 2)
// DKEY  the room texture cache (gDetailKeys, 7 x 1088 bytes), DSLT its slots
//       (DetailSlot, 256)
// DIVT  the projection's divide table (1281 uint16)
// ROOM  the rooms, in the order the slave added their faces (RoomScene);
//       RGEO their quads, triangles and vertices as the game keeps them;
//       CAUS each one's caustics (32 int16: a vertex's shade offset under
//       water, by its index)
// DRAW  the master's draws in order (DrawScene), XFRM each one's transform
//       (DrawXform); MESH their meshes' offsets in MGEO, the meshes as the
//       game keeps them (Mesh and its arrays)
// MOVE  the master's faces the draw order hints moved (MoveScene)
// FLOR  the floor's faces, a command each in flush's order (FloorFace)
#ifndef SCENE_H
#define SCENE_H

#include <stdint.h>

#define SCENE_MAGIC     0x53434E31  // "SCN1"

struct SceneInfo
{
    uint32_t commands;      // the game's list: the clear, then a command a face
    uint32_t masterFaces;   // MPOL's
    uint32_t slaveFaces;    // SPOL's
    uint32_t rooms;         // ROOM's
    uint32_t draws;         // DRAW's
    uint32_t meshes;        // MESH's
    uint32_t moved;         // MOVE's
    uint32_t floorFaces;    // FLOR's
    uint32_t floorExact;    // ... marked FLOOR_EXACT
    uint32_t poolVram[3];   // the room texture cache's pools (gDetailPoolVram)
    uint32_t shadeFrame;    // the frame's, for the shaded colour tables
    uint32_t detailFrame;   // ... for the room texture cache
    uint32_t detailVram;    // gDetailVramStart
    uint32_t texturePmod;   // gTexturePmod
    int32_t lod;            // RoomFaces.lod: gTextureLod, 0 without mip levels
    int32_t bottom;         // the slave's vertices below this are the rooms' (gBottom)
    int32_t farShare;       // the slave's share of the commands flush gave it, percent
    uint32_t textures;      // TEXS's
    uint32_t sprites;       // SPRS's
    char name[16];          // the capture's
};

// A room as the slave was given it (rooms.cpp RoomJob, RoomWork, RoomInfo).
struct RoomScene
{
    int16_t m[9];           // RoomMatrix: the rotation
    uint16_t room;          // its index in the level
    int32_t tx, ty, tz;     // ... the translation
    int32_t clip[4];        // its portal window: x0 y0 x1 y1
    int32_t camera[3];      // the camera from the room's origin
    int32_t causticsFrame;
    uint8_t wet, ripple, dsp, pad2;
    int32_t base;           // its first vertex in the vertices
    uint16_t quadsCount, trianglesCount, verticesCount, pad3;
    uint16_t facing[6];     // RoomInfo.facing
    uint32_t quads, triangles, vertices;    // in RGEO
};

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

// POSE  the pose stage's inputs (tools/posepack.py): each item the frame drew
//       with its model's nodes, in the order drawn (PoseItem); PNOD their
//       models' nodes (ModelNode: int16 x y z, uint16 flags; a mesh's after
//       the first each); PFRM the animation frames they take (AnimFrame:
//       int16 box[6], pos[3], a spare, then a packed angle a node); SINC the
//       game's sines and cosines (gSinCosTable, 4096 longwords); CAMR the
//       camera (PoseCamera); PLIT what lights each item (PoseLight, POSE's
//       order), LGHT its room's lights (Light: int16 x y z from the room,
//       radius, intensity); STAT the static meshes drawn (PoseStatic), SHDW
//       the items' shadows (PoseShadow).
// RLVL  every room of the level as the portal walk reads it (RoomLevel), RPRT
//       their portals (room.h Portal: uint16 room, int16 normal, 4 corners);
//       RVIS the view the walk starts from and, for the check, the rooms the
//       frame queued (RoomView) (tools/roompack.py); RLGE each room's
//       geometry in RGEO (RoomGeometry: the rooms this frame drew, the others
//       none); CAUW the caustics' wave (RoomCaustics).
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

struct RoomView
{
    uint16_t camera, pad;   // the camera's room
    int32_t viewport[4];
    uint8_t water, pad2[3]; // the camera under water
    uint32_t count;         // the rooms the frame queued, in order
    RoomSeen queued[1];
};

struct PoseCamera
{
    int32_t pos[3];         // gCameraViewPos
    int16_t rotation[9];    // gViewRotation
    int16_t pad;
};

// The draw list's inputs (tools/drawpack.py):
// CALL  the frame's calls into the renderer after the walk, in order
//       (CallRecord); OITM the items their models are drawn for (ItemRecord);
//       ANIM each item's animation (GameAnim, its own copy) and AFRM their
//       frames: an animation's frame offset counts from AFRM_BASE uint16s
//       before AFRM, so its frame (and the next, where getFrames reads it)
//       lies there; MODL the level's models (GameModel), MIDX each level
//       mesh's index in MESH (0xFFFF: not in the scene), MNOD each model's
//       nodes in PNOD, SMSH the level's static meshes (GameStaticMesh), RLIT
//       each room's ambient and lights (RoomLights).
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

// A command as the floor builds it: three words as the game's, the corners
// whose positions the game's corner words hold (byte offsets in the
// vertices). FLOOR_EXACT: the floor gives the game's command.
struct FloorFace
{
    uint32_t w[3];
    uint16_t corners[4];
    uint32_t flags;
};

#define FLOOR_EXACT     1

struct MoveScene
{
    uint16_t face;          // in MPOL
    uint16_t bucket;        // where its corners put it
};

// Loads the scene file into low work RAM, each chunk where it belongs: NULL,
// or what went wrong.
const char* sceneLoad(const char* path);

// A chunk's data and bytes, or NULL.
const void* sceneChunk(const char* id, uint32_t* bytes = nullptr);

// Room in high work RAM after the scene's chunks, for what the lab makes of
// them (16-byte aligned), or NULL.
void* sceneAlloc(uint32_t bytes);

// Room in low work RAM past the scene file, for what isn't timed (16-byte
// aligned), or NULL.
void* sceneAllocLow(uint32_t bytes);

// The bytes of the high room the scene's chunks and sceneAlloc have taken,
// and its size; of low work RAM, the low chunks and sceneAllocLow.
uint32_t sceneUsed(uint32_t* size = nullptr);
uint32_t sceneUsedLow(uint32_t* size = nullptr);

template <typename T>
inline T* sceneArray(const char* id, uint32_t* count = nullptr)
{
    uint32_t bytes;
    T* p = (T*)sceneChunk(id, &bytes);
    if (count) {
        *count = p ? bytes / sizeof(T) : 0;
    }
    return p;
}

inline const SceneInfo& sceneInfo()
{
    return *sceneArray<const SceneInfo>("INFO");
}

#endif
