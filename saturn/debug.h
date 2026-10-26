// The debug pad on port 2: a menu of rendering options to compare in game, and
// lines of frame statistics.
#ifndef DEBUG_H
#define DEBUG_H

// Once a frame, before the game update: reads the debug pad.
void debugUpdate();

#ifdef HW_LOG
// Starts the measurement table's sampling profiler (debug.cpp).
void debugProfileInit();

// Times the SCU DSP transforming room vertices (xform.dsp) against the SH-2
// doing the same arithmetic, and checks the DSP's results: boot log lines
// DSPXF (DSP ticks), DSPDMA (its transfers alone, dmaprobe.dsp), CPUXF (SH-2
// ticks), DSPERR (vertices that differ).
void debugDspTest();
#endif

#endif
