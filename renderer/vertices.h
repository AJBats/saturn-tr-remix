// Vertices: the DSP rotates every vertex of the frame (and lights the
// meshes'), from the level's data; the CPUs project what it hands them, the
// divide, the clamps and the clip flags, into the records the faces read
// (Vertex).
//
// Rooms: roomrot.dsp (gen_roomrot.py) from the rooms' data as stored;
// meshes: meshrot.dsp (gen_meshrot.py) from their int16 vertices and byte
// normals, packed as the level would keep them.
#ifndef RENDER_VERTICES_H
#define RENDER_VERTICES_H

#include <stdint.h>

#include "types.h"

namespace render {

// A room vertex as the DSP hands it over: each of the room's rows times the
// vertex (its bytes times 4), plus the translation.
struct RoomRotated
{
    int32_t x, y, z;
};

// What projecting a room's vertices takes (roomproject.s).
struct RoomProject
{
    const RoomVertex* points;   // +0  the room's, for their shades
    int32_t x0, y0, x1, y1;     // +4  its portal window
    const int16_t* divScaled;   // +20 divTable << 4 where that fits, from z 1024 (VERTICES_DIV_SCALED_FROM)
    int32_t zMin, zMax;         // +24 near and far planes << 8
    const int8_t* wibble;       // +32 ripple offsets (32, twice over), or none
    int32_t phase;              // +36 the ripple's
    const int16_t* caustics;    // +40 shade offsets (32) by vertex index under water, or none
    int32_t count;              // +44 the room's vertices
    int32_t farScaled;          // +48 verticesFarScaled's
    int32_t zExact;             // +52 nearer than this << 8, divided: not the table
    const uint16_t* nearTable;  // +56 0x100000 / z by z, up to zExact >> 8
    int32_t safe;               // +60 RoomSafe: what every vertex of the room is sure of
    int32_t window[4];          // +64 roomproject.s's own
    const uint16_t* divide;     // +80 divTable
    const int8_t* rippleBase;   // +84 roomproject.s's own
    const int16_t* farTable;    // +88 past the far plane 0x1000000 / z by z >> 8, up to VERTICES_FAR_LAST; or none: exact
};

// z >> 8 from the nearest far plane there is (12 sectors) to where the table stops.
#define VERTICES_FAR_FIRST  48
#define VERTICES_FAR_LAST   335

// What a room's vertices are sure of this frame, by its box: every view x and y within what >> 8
// holds in 16 bits; every z between VIEW_EXACT and the far plane as well.
enum RoomSafe { ROOM_SAFE_XY = 1, ROOM_SAFE_Z = 2 };

// What a room's rotations are sure of, by its box: reach the largest x y z byte among its vertices, zMax the
// far plane << 8.
struct FaceRoom;
extern "C" uint32_t roomSafeAsm(const FaceRoom* room, const uint8_t* reach, int32_t zMax);

extern "C" void roomProjectAsm(const RoomRotated* src, int32_t count, Vertex* dst, RoomProject* rp);

// What's made of a room's window for roomProjectAsm.
inline void roomProjectReady(RoomProject &rp)
{
    rp.window[0] = rp.x0 << 16;
    rp.window[1] = rp.y0 << 16;
    rp.window[2] = (rp.x1 + 1) << 16;
    rp.window[3] = (rp.y1 + 1) << 16;
    rp.rippleBase = rp.wibble ? rp.wibble + (rp.phase & 31) : nullptr;
}

// A mesh vertex as the DSP leaves it: x >> 14 << 16 | y >> 14 & 0xFFFF,
// z >> 14 << 16 | shade << 8, the sums of each row times the vertex plus
// the translation >> FIXED_SHIFT (meshxform.s's), and for a mesh lit by its
// normals or by one shade for all, its shade (else 0).

// How a mesh is shaded: by the DSP (by its normals or one shade for all),
// by each vertex's own shade (meshShadesAsm), or by its normals here, when
// the light could take a shade out of 0 .. 31 on the DSP's arithmetic
// (never in TR1's levels so far).
enum MeshShading { MESH_SHADED, MESH_SHADES, MESH_NORMALS };

// What projecting a mesh's vertices takes (meshproject.s): the DSP's output
// in place.
struct MeshProject
{
    const uint16_t* divTable;   // +0
    int32_t inside;             // +4 every vertex lands inside the frame and between the planes
    void (*exact)(MeshProject* mp, int32_t index, Vertex* v);  // +8 a vertex z >> 14 can't place
                                // against the near plane, worked out from the mesh
    const struct MeshJob* job;  // +12 for exact
    int32_t farScaled;          // +16 verticesFarScaled's
    const int16_t* divScaled;   // +20 divTable << 4 from z 1024
};

// A vertex past the far plane takes its depth there (and the plane's clip
// flag), its x and y scaled by the plane's depth over its own so that it
// projects where it lies: what fills a face the plane cuts covers it there.
// Off, it projects at the plane, as the original game's did (the lab's
// checks against its captures).
void verticesFarScaled(bool on);

// A level room's vertices, once it has them (again, flipped): how far its box reaches.
void verticesRoomReach(uint32_t room, const RoomVertex* points, int32_t count);

// The rooms' far plane, in world units (frame.h frameFar); the meshes keep
// the game's.
void verticesFar(int32_t far);

extern "C" void meshProjectAsm(Vertex* v, int32_t count, MeshProject* mp);
extern "C" void meshShadesAsm(Vertex* v, int32_t count, const uint8_t* shades, int32_t ambient);

// The level's meshes (its mesh store): their parts, and slots for a frame's
// shadows, in memory alloc gives (16-byte aligned, or none). NULL, or what
// went wrong.
struct VerticesLevel
{
    const MeshStore* store;
    void* (*alloc)(uint32_t bytes);
};
const char* verticesInit(const VerticesLevel &level);

// A frame's rooms (the walk's jobs): where the DSP's rotations of each go,
// their chunks (the order the DSP turns them in stays the plan's).
void verticesRoomsSet(const FaceRoom* rooms, uint32_t count);

// A frame's draws (a draw list's) and their transforms: each mesh's and
// shadow's job; after feeding, whether a draw's mesh lies wholly inside the
// frame (meshxform.s's test, MESH_INSIDE).
void verticesDrawsSet(const DrawScene* draws, uint32_t count, const DrawXform* xforms);
int32_t verticesInsideOf(uint32_t draw);

// What a draw's cards' shades take for the side of their segments the camera
// sees: a box's visible side's normals lean to the camera by 0.36 of a whole
// normal (fitted, tools/model_view.py CARD_FACING), which the light turns into
// range * 0.36 * (to the camera . the light), the camera being along -z.
int32_t verticesCardShade(uint32_t draw);

// The faces of the draws' meshes and shadows: their commands at most; the
// rooms' faces.
int32_t verticesFaces();
int32_t verticesRoomFaces();

// The rooms' DSP run started in their order, before the plan (which then
// leaves the order be: verticesPlan's started).
void verticesRoomsStart();

// The ticks the rooms' DSP run and the meshes' last started at, each on the
// timer of the CPU that started it.
void verticesDspStarts(uint32_t &rooms, uint32_t &meshes);

// Stage 2 in pieces, for a frame's stages run together. The rooms are
// projected in chunks of vertices, each claimed by the CPU that takes it
// (tasClaim); the plan gives the master chunks from the last room back while
// that evens the work the counts predict for the two CPUs (the slave adds
// every room's faces; the master feeds, projects and adds the draws'), bias
// cycles added to the master's (what the frames before found it short of
// what it takes).
// - the plan made (a frame; ahead: the master's predicted work before its
//   feed, which the DSP's room order takes into account);
// - the rooms' DSP run started (the rooms' jobs built, roomrot.dsp loaded),
//   no chunk claimed yet; then the master's chunks claimed, before the
//   slave starts;
// - the master's chunks projected, each once the DSP has turned its room
//   (the cycles waiting added to wait), the meshes' DSP run started as soon
//   as the rooms' has ended (meshes: whether it has been);
// - room i's chunks for the slave, past the first from: those it claims
//   projected, the master's waited for (their lines dropped from its cache);
// - room i's chunks for the slave up to one the master has, which the master's
//   claims can make of a room begun: those projected, from how many that was,
//   whether it came to the room's end without one;
// - the meshes fed and their jobs marked (from to to: a feed can come in
//   parts); the meshes' DSP run started if the rooms' has ended (whether it
//   did), or once it has;
// - draw i's mesh job (a mesh or a shadow) waited for and projected: its
//   vertices;
// - the DSP's run waited for, unless nextRooms (read as it waits) says the
//   run is the next frame's rooms' (frame.h frameNextWork).
void verticesPlan(int32_t bias, int32_t ahead = 0, bool started = false);
void verticesRoomsBegin();
void verticesClaimMasters();
void verticesMasterRooms(uint32_t &wait, bool &meshes);
void verticesRoom(uint32_t i, uint32_t &wait, int32_t from = 0);
bool verticesRoomMine(uint32_t i, uint32_t &wait, int32_t &from);

// Room i has chunks claimed before the slave came to it: the master's.
bool verticesRoomMasters(uint32_t i);

// Room i's vertices all projected, by either CPU: this CPU's cached lines of
// them dropped (the cycles waiting added to wait).
void verticesRoomWait(uint32_t i, uint32_t &wait);
// As many of the rooms' chunks as get done before *stop, first to last, on the CPU that laid them out: the rooms
// finished, the chunks done of the next.
void verticesAhead(const volatile uint32_t* stop, uint32_t &rooms, int32_t &from);
void verticesMeshesFeed(int32_t from = 0, int32_t to = -1);
bool verticesMeshesTryStart();
void verticesMeshesStart(uint32_t &wait);
const Vertex* verticesDraw(uint32_t draw, uint32_t &wait);
void verticesEnd(const volatile uint32_t* nextRooms = nullptr);

// The work the plan's counts predict, in cycles: the master's (the draws)
// and the slave's (the rooms), before any chunk moves; a draw's mesh job, or
// -1; the jobs.
void verticesPredicted(int32_t &master, int32_t &slave);

// The cycles the slave predictably waits at a frame's start, for the DSP's
// first room.
int32_t verticesSlaveIdle();
int32_t verticesJobOfDraw(uint32_t draw);
int32_t verticesJobs();

}

#endif
