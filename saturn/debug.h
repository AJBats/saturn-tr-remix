// The debug pad on port 2: a menu of rendering options to compare in game, and
// lines of frame statistics.
#ifndef DEBUG_H
#define DEBUG_H

// Once a frame, before the game update: reads the debug pad.
void debugUpdate();

#ifdef HW_LOG
// Times reads and writes of low and high work RAM, for the measurement
// table: at boot, the slave idle and before low work RAM holds a level.
void debugMemoryProbe();
#endif

#endif
