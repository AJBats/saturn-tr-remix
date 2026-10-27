// The debug pad on port 2: a menu of rendering options to compare in game, and
// lines of frame statistics.
#ifndef DEBUG_H
#define DEBUG_H

#include <stdint.h>

// Once a frame, before the game update: reads the debug pad.
void debugUpdate();

#ifdef HW_LOG
// Starts the measurement table's sampling profiler (debug.cpp).
void debugProfileInit();

// Once a frame, after it is drawn and its plot asked for: notes its CPU time
// and where Lara stands, for the rows that show each frame's own numbers
// while that frame is on screen.
void debugFrameDone();

// The boot's test of how the CPUs can share work (synctest.cpp), shown on
// the trace page: counter adds lost by both CPUs without a lock (raw: must
// not be 0, or the test sees no race), with TAS.B and with a Peterson lock on
// uncached flags (both must be 0); messages read out of order (cached, after
// a line purge); tasks claimed twice or never under TAS, and how
// many each CPU took; GBR changing under a CPU. And the cycles each costs: an
// uncached access, an add under TAS alone, then under TAS and Peterson with
// both CPUs at it.
struct SyncResults
{
    uint32_t raw, tas, peterson, claim, claimMaster, claimSlave, order, gbr;
    uint32_t uncachedCycles, lockCycles, tasCycles, petersonCycles;
};
extern SyncResults gSync;
void debugSyncTest();
bool debugSyncKept();   // GBR still holds what debugSyncTest left in it

// Times the SCU DSP's whole room projection (project.dsp) against the
// SH-2's and checks its results: boot log lines PJDSP and PJCPU (ticks),
// PJERR (vertices that differ).
void debugDspTest();
#endif

#endif
