// The frame: the renderer's stages on both CPUs and the DSP at once, into
// the command list VDP1 draws.
//
// The slave walks the rooms and starts the DSP on them while the master
// makes the draw list and its meshes' jobs; the master plans the frame (the
// items the slave poses, the rooms' chunks of vertices the master projects)
// and claims its chunks; then the slave poses its items and takes the rooms
// in order, each projected as soon as the DSP has turned it and its faces
// added to the rooms' list, while the master poses its own items, feeds the
// meshes to the DSP and takes the draws in order, each mesh projected once
// the DSP has made it and its faces added to the draws' list. Both done,
// each joins half the lists' buckets, far to near. Room faces stay on the
// slave: they share the room texture cache and the materials' memo, and keep
// the game's order in the rooms' list.
#ifndef RENDER_FRAME_H
#define RENDER_FRAME_H

#include <stdint.h>

#include "draws.h"
#include "faces.h"
#include "hints.h"
#include "textures.h"
#include "types.h"

namespace render {

// The command list: the clear in slot 0, the rooms' commands from slot 1 to
// gMeshFirst, the draws' from there to the end's slot; their gouraud tables
// beside.
extern Command gFrameCmds[MAX_CMDS];
extern uint32_t gFrameGrd[MAX_CMDS][2];
extern int32_t gFrameEnd;
extern int32_t gMeshFirst;
extern CommandList gRoomList, gMeshList;

// The frame's rooms (the walk's jobs), the room texture cache as their faces
// take it, the room textures' materials, the uploads the faces queued, where
// quads split near the camera put their corners, the room faces' run.
#define MAX_FRAME_ROOMS     64      // with vertices, a frame
#define MAX_ROOM_TEXTURES   288     // a level's (GYM has 279)
extern FaceRoom* gFrameRooms;
extern uint32_t gFrameRoomCount;
extern RoomTextures gRoomTextures;
extern RoomMaterial gRoomMaterials[MAX_ROOM_TEXTURES];
extern TextureQueue gFrameTextures;
extern RoomSplit gSplit;
extern RoomRun gRoomRun;

// The level's for the faces: the meshes' faces (an offset table, then each
// mesh's FaceSet), the materials, the textures, palette and sprites, the
// shades' gouraud colours and levels (0 .. 31) and dimmed (the scene behind
// the inventory), the CPUs' caches of shaded colour tables (the master's,
// the slave's), the room texture cache's look and pools, the room textures'
// count, the list's first command, and the vertices the walk places the
// rooms' in (split corners from the top down).
struct FrameLevel
{
    const uint8_t* faceSets;
    const Material* materials;
    const Texture* textures;
    const uint16_t* palette;
    const Sprite* sprites;
    const uint16_t* shadeGouraud;
    const uint8_t* shadeLevel;
    const uint16_t* dimGouraud;
    const uint8_t* dimLevel;
    ShadeCache* shade[2];
    uint32_t pmod;              // textured faces' draw mode
    int32_t lod;                // RoomTextures.lod
    uint32_t pools[3];
    uint32_t frame;             // the cache's frame now
    uint32_t roomTextures;
    Command clear;
    Vertex* vertices;
};
void frameInit(const FrameLevel &level);

// The level's animated ranges of room textures shifted one along (ranges:
// their count, then each one's length - 1 and its textures, as the level
// holds them): their materials with them.
void frameTexturesShifted(const uint16_t* ranges);

// The lists emptied (their buckets too) on the slots the split last gave.
void frameLists();

// The slots split between the lists for the frame's rooms and draws (the
// draws make a command a face at most: their meshes' and shadows' faces,
// their copies, the fills; the rooms about one a face, some split): when
// both fit, the draws' list as long as their commands at most, the rooms'
// the rest; else each as long as its share. Then the lists on to their
// slots, their buckets empty since the join before, the split corners free.
void frameSplit(const DrawScene* draws, uint32_t count, int32_t fills);
void frameListsStart();

// The frame's stages for a timeline (platform.h stageBegin): the master's,
// then the slave's.
enum FrameStage
{
    STAGE_DRAW_LIST,    // the calls into the draw list
    STAGE_WALK_WAIT,    // waiting for the slave to lay out the rooms' jobs
    STAGE_PLAN,         // the lists split, the hints begun, the frame planned
    STAGE_POSE,         // the master's items lit and posed, the statics placed
    STAGE_FEED,         // the meshes fed to the DSP
    STAGE_POSE_WAIT,    // waiting for the items the slave poses
    STAGE_CHUNKS,       // the master's chunks of the rooms' vertices
    STAGE_DRAWS,        // the draws' vertices and faces
    STAGE_SLAVE_WAIT,   // waiting for the slave's rooms, then its half of the join
    STAGE_JOIN,         // the far half of the buckets joined
    STAGE_JOBS,         // the slave: the rooms' jobs laid out, the DSP started on them
    STAGE_PLAN_WAIT,    // waiting for the master's plan
    STAGE_SLAVE_POSE,   // its items lit and posed
    STAGE_ROOMS,        // its rooms' vertices and faces
    STAGE_DRAWS_WAIT,   // waiting for the master's draws
    STAGE_SLAVE_JOIN,   // the near half of the buckets joined
    STAGE_COUNT
};

// The frame's times, SH-2 cycles: start to end, each CPU's work and waits,
// the DSP's runs (start to end each: rooms, then meshes), and how late the
// master's draws ended after the slave's rooms (< 0: before), which the next
// frame's plan takes half of (bias).
struct FrameTimes
{
    uint32_t wall, masterWork, masterWait, slaveWork, slaveWait, dsp;
    int32_t late;
};

// A frame of the game's: its calls (their models' items), the draws' first
// vertex (base: the lab's checks place them after the walk's fills, as the
// game does), whether the rooms are walked already (roomsSeen: the game
// walks them on the master to know whose objects to draw), else the slave
// walks them; the matrices models are drawn under (CallRecord's), or none;
// whether the frame draws the inventory over the scene: then the scene is
// dimmed (the rooms, and the draws before the first model under a matrix
// given, through the dim shades), and the master's faces take no shaded
// copies of colour tables (the item textures' go up after the frame is
// built).
struct FrameCalls
{
    const CallRecord* calls;
    uint32_t count;
    ItemRecord* items;
    int32_t base;
    bool walked;
    const int32_t (*matrices)[12];
    bool inventory;
};
FrameTimes frameRun(const FrameCalls &calls, int32_t bias);

// A frame of draws given (the rooms set, the draws posed, the plan made: the
// lab's scenes): the copies' faces and their vertices, the draws whose
// faces hints move.
struct FrameDraws
{
    const DrawScene* draws;
    uint32_t count;
    const Face* copies;
    const Vertex* copyVertices;
    const HintedDraw* hinted;
    int32_t hintedCount;
};
FrameTimes frameDraws(const FrameDraws &draws, int32_t bias);

// The plan for a frame (vertices.h verticesPlan), with the items' pose
// shared (posed): the heaviest first, each that leaves the master's
// predicted work no less than the slave's.
void framePlan(int32_t bias, bool posed, bool started = false);

// A texture the room faces miss, into the cache, its upload queued.
int32_t frameTextureMiss(uint32_t key, uint32_t texSize, uint32_t variant);

// The draw order hints of a frame: the faces held out of their buckets
// (facesMeshHinted) until the rooms' are projected (wait: waited for, the
// cycles added to waited; else as they stand); the draws whose faces hints
// move, from; mesh draw i's faces, hinted or not.
#define MAX_HELD    32
void frameHintsBegin();
void frameHintsPlace(bool wait, uint32_t &waited);
void frameHinted(const HintedDraw* hinted, int32_t count);
void frameMesh(uint32_t i, const FaceSet* set, const Vertex* vertices, uint32_t mode);

// Mesh draw i's faces if hints move them (else false: the caller adds them).
bool frameMeshHinted(uint32_t i, const FaceSet* set, const Vertex* vertices, uint32_t mode);

// The list's ends: the clear in slot 0, the end after the draws' last.
void frameEnds();

// The list and the textures its faces queued into VDP1 VRAM where the game
// keeps its own, by the SCU's DMA: the textures, the clear and the rooms'
// commands, then the draws' and the end, and their gouraud tables. The
// list's bytes.
uint32_t frameUpload();

}

#endif
