// Stage 2 the new way: vertices. The DSP rotates every vertex of the frame
// (and lights the meshes'), from the level's data; the CPUs project what it
// hands them, the divide, the clamps and the clip flags, into the records
// the face stage reads (game.h Vertex).
//
// Rooms: roomrot.dsp (lab/gen_roomrot.py) from the rooms' data as stored;
// meshes: meshrot.dsp (lab/gen_meshrot.py) from their int16 vertices and
// byte normals, packed as the level would keep them. Beside it for its time,
// meshlight3.dsp (dspbench/gen_meshlight3.py, the bench's v3) from the same
// laid out for it, a longword each.
#ifndef VERTICES_H
#define VERTICES_H

#include <stdint.h>

#include "game.h"

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
    const uint16_t* divTable;   // +20
    int32_t zMin, zMax;         // +24 near and far planes << 8
    const int8_t* wibble;       // +32 ripple offsets (32), or none
    int32_t phase;              // +36 the ripple's
    const int16_t* caustics;    // +40 shade offsets (32) by vertex index under water, or none
    int32_t count;              // +44 the room's vertices
};

extern "C" void roomProjectAsm(const RoomRotated* src, int32_t count, Vertex* dst, RoomProject* rp);

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
};

extern "C" void meshProjectAsm(Vertex* v, int32_t count, MeshProject* mp);
extern "C" void meshShadesAsm(Vertex* v, int32_t count, const uint8_t* shades, int32_t ambient);

// The scene's rooms and draws for the new way: the meshes laid out for the
// DSP, each draw's job and where its vertices go. NULL, or what went wrong.
const char* verticesInit();

// The rooms' rotations on the DSP, a job a room, the master waiting for its
// end: the SH-2 cycles it took. Then its rotations against xform.s's
// arithmetic: those that differ.
uint32_t verticesRoomsDsp();
int32_t verticesRoomsDspCheck();

// The rooms' vertices projected from the DSP's rotations, into gVertices
// where the game put them; against the game's: the vertices that differ.
void verticesRooms();
int32_t verticesRoomsCheck(int32_t &vertices);

// The rooms' projection where the scene seldom takes it, made-up rotations
// through every path of both roomproject.s loops against a C model of its
// arithmetic: the vertices that differ.
int32_t verticesRoomsSeldomCheck();

// The meshes fed to the DSP: each one's job (its coefficients, how it's
// lit, whether it lies inside the frame), a shadow's vertices packed.
// Then the DSP run over them, the master waiting: the SH-2 cycles it took;
// and its output against meshxform.s's arithmetic: the vertices that differ.
// meshlight3.dsp's run over the same, fed from the jobs, for its time.
void verticesFeed();
uint32_t verticesMeshesDsp();

// The feeding (meshfeed.s) against its C model: on the scene's transforms,
// then on made-up ones through every case; the jobs that differ.
int32_t verticesFeedCheck();
uint32_t verticesMeshesDsp3();
int32_t verticesMeshesDspCheck();

// The meshes' vertices projected in place and shaded; against the game's:
// the vertices that differ (vertices: the meshes').
void verticesMeshes();
int32_t verticesMeshesCheck(int32_t &vertices);

// Stage 2 the new way as a frame would run it: the DSP turns the rooms then
// the meshes; the slave projects each room as soon as the DSP has turned it;
// the master feeds the meshes meanwhile, then projects each as soon as the
// DSP has made it, from the first, the slave joining from the last once its
// rooms are done. The SH-2 cycles from the start to both CPUs done, and
// each CPU's at work and waiting (polling the DSP, or the master the
// slave); the DSP's rooms (to their end as the master saw it), the gap to
// its meshes' start, and its meshes (to their end as the master saw it,
// its own jobs done).
struct Together
{
    uint32_t wall;
    uint32_t masterWork, masterWait;
    uint32_t slaveWork, slaveWait;
    uint32_t dspRooms, dspGap, dspMeshes;
};
Together verticesTogether();

// What the stages write overwritten with what they never make (the rooms'
// rotations, the meshes' outputs, the rooms' vertices), so a check after a
// run sees every value made in it.
void verticesPoison();

// The draws' transforms the meshes' jobs read: the scene's (XFRM) to begin
// with, or others laid out as it, by draw (pose.h poseXforms).
struct DrawXform;
void verticesXforms(const DrawXform* xforms);

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
// - room i's chunks for the slave: those it claims projected, the master's
//   waited for (their lines dropped from its cache);
// - the meshes fed and their jobs marked (from to to: a feed can come in
//   parts); the meshes' DSP run started if the rooms' has ended (whether it
//   did), or once it has;
// - draw i's mesh job (a mesh or a shadow) waited for and projected: its
//   vertices;
// - the DSP's run waited for.
void verticesPlan(int32_t bias, int32_t ahead = 0);
void verticesRoomsBegin();
void verticesClaimMasters();
void verticesMasterRooms(uint32_t &wait, bool &meshes);
void verticesRoom(uint32_t i, uint32_t &wait);
void verticesMeshesFeed(int32_t from = 0, int32_t to = -1);
bool verticesMeshesTryStart();
void verticesMeshesStart(uint32_t &wait);
const Vertex* verticesDraw(uint32_t draw, uint32_t &wait);
void verticesEnd();

// The work the plan's counts predict, in cycles: the master's (the draws)
// and the slave's (the rooms), before any chunk moves; a draw's mesh job, or
// -1; the jobs.
void verticesPredicted(int32_t &master, int32_t &slave);

// The cycles the slave predictably waits at a frame's start, for the DSP's
// first room.
int32_t verticesSlaveIdle();
int32_t verticesJobOfDraw(uint32_t draw);
int32_t verticesJobs();

// What the scene seldom takes: the meshes' vertices projected as though none
// lay inside the frame, then as though each lay at the near plane (its z >>
// 14 64, so meshproject.s has it worked out from its mesh), each from the
// DSP's output again, against the game's: the vertices that differ.
int32_t verticesFallbacksCheck();

#endif
