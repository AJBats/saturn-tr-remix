// The debug pad on port 2: a menu of rendering options to compare in game, and
// lines of frame statistics.
#ifndef DEBUG_H
#define DEBUG_H

// Once a frame, before the game update: reads the debug pad.
void debugUpdate();

#ifdef HW_LOG
// Starts the measurement table's sampling profiler (debug.cpp).
void debugProfileInit();
#endif

#endif
