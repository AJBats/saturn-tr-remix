// Stage 2 the new way: vertices. The DSP rotates every vertex of the frame
// (and lights the meshes'), from the level's data as stored; the CPUs
// project what it hands them, the divide, the clamps and the clip flags,
// into the records the face stage reads (game.h Vertex).
//
// Until the DSP's programs are written, vertexModel stands in for them: the
// same numbers, made in C.
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

// A mesh vertex as the DSP leaves it, in its place in gVertices: x >> 14 <<
// 16 | y >> 14 & 0xFFFF, z >> 14 << 16 | shade << 8, the sums of each row
// times the vertex plus the translation >> FIXED_SHIFT (meshxform.s's), and
// for a mesh lit by its normals, its shade (else 0).

// How a mesh is shaded: by its normals on the DSP, a shade for all its
// vertices (from the DSP too), by each vertex's own shade (meshShadesAsm),
// or by its normals here, when the light could take a shade out of 0 .. 31
// on the DSP's arithmetic (never in TR1's levels so far).
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

// The scene's rooms and draws, for the new way.
void verticesInit();

// The DSP's work for the frame, made in C: the rooms' rotations and the
// meshes' rotated and lit vertices, in place in gVertices; with each mesh's
// job.
void vertexModel();

// The rooms' vertices projected from their rotations, and the meshes' in
// place, into gVertices where the game put them.
void verticesRooms();
void verticesMeshes();

// What the scene seldom takes: the meshes' vertices projected as though none
// lay inside the frame, then as though each lay at the near plane (its z >>
// 14 64, so meshproject.s has it worked out from its mesh), against the
// game's (scene): the vertices that differ. vertexModel's output is used up.
int32_t verticesFallbacksCheck(const Vertex* scene);

#endif
