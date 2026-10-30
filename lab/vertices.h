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
uint32_t verticesMeshesDsp3();
int32_t verticesMeshesDspCheck();

// The meshes' vertices projected in place and shaded; against the game's:
// the vertices that differ (vertices: the meshes').
void verticesMeshes();
int32_t verticesMeshesCheck(int32_t &vertices);

// What the scene seldom takes: the meshes' vertices projected as though none
// lay inside the frame, then as though each lay at the near plane (its z >>
// 14 64, so meshproject.s has it worked out from its mesh), each from the
// DSP's output again, against the game's: the vertices that differ.
int32_t verticesFallbacksCheck();

#endif
