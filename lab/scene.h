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
// CMDS  the game's command list: the clear, a command a face, the end
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
    int16_t pad;
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
//       camera (PoseCamera).
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

struct PoseCamera
{
    int32_t pos[3];         // gCameraViewPos
    int16_t rotation[9];    // gViewRotation
    int16_t pad;
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
// and its size.
uint32_t sceneUsed(uint32_t* size = nullptr);

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
