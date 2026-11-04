// A level's flat floor and ceiling on VDP2, under VDP1's frame (docs/vdp2-production.md):
// RBG0 in VRAM bank B, set A of its rotation parameters the floor, set B the ceiling, a
// window choosing by line. tools/planepack.py took those faces out of the level, and wrote
// the tiles and colours this loads.
//
// A level has a plane if the disc has its .PLC.
#ifndef PLANE_H
#define PLANE_H

#include <stdint.h>

// In VRAM: where the level's .PLC and .PLN go, loaded before planeLoaded.
void* planeCellsAt();
void* planeNamesAt();

// What was loaded (0 bytes of cells: no plane). Bank B must be the CPU's (planeOff).
void planeLoaded(uint32_t cellBytes, uint32_t nameBytes);

// Whether this level has a plane.
bool planeLevel();

// Where the level's item textures go in bank B, read by the CPU alone: its top, past the
// names, or past the plane's tables in B0, whichever holds them. Null: neither does.
uint8_t* planeItemsAt(uint32_t bytes);

// Bank B RBG0's, from the coming vertical blank: for the level once its files are in. BGON
// stays the screen's (screen.cpp).
void planeReady();

// Bank B the CPU's again: for pictures, videos and the next level. Safe in a crash.
void planeOff();

// The view of the frame being made: the camera's place, the matrix's rows as
// render.cpp view has them. As it has them, the x and y rows narrowed by gFocalScale.
void planeView(const int32_t camera[3], const int32_t view[12]);

// The camera under water or not, for the views to come.
void planeWater(bool under);

// The list just made goes with the last view; and its plot is this one.
void planeListMade();
void planeListPlot(uint32_t plot);

// The plane's constant offset: what its line colours add back, in 8-bit colour.
#define PLANE_OFFSET    (-14 * 8)

#endif
