// Polygon hunt (debug menu): the frame freezes, and the faces the depth sort
// painted over nearer ones light up, for whoever is playing to pick out the
// one that catches the eye and read off its name and where Lara stands.
//
//   d-pad  the next or previous face (red; the others green)
//   A      the frame as drawn, or lit up
//   C      the readout: Lara's room, position, block and facing, the face
//          painted over and the face it covers
//   B      back to the game
#ifndef HUNT_H
#define HUNT_H

#include "render.h"

// Starts a hunt: the next frame is recorded and frozen. False if the level
// leaves too little low work RAM.
bool huntArm();

// The frame being drawn is recorded: who draws each mesh's faces (render.cpp).
extern bool gHuntRecording;
void huntObject(int32 kind, int32 owner, int32 index);
void huntMesh(const Mesh* mesh, const Face* first, const Vertex* base);

// flush_c, before its commands: the faces in the order they are drawn, from
// the slave's and the master's ordering tables. After them: the commands.
void huntOrder(Face* const* roomOT, Face* const* meshOT, int32 buckets);
void huntFlushed(const uint32* cmds);

// A hunt holds the game: the main loop gives it the pad each tick instead.
bool huntFrozen();
void huntUpdate(uint16 pad);

#endif
