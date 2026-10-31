// The slave SH-2. It runs one task at a time for the master. The CPUs share
// memory but not caches: each purges its cache before reading what the other
// wrote, and flags they poll are read and written uncached. The renderer's
// side of it (uncached, cachePurge, cacheDrop, slaveRun with an argument,
// slaveWait, slaveDone, tasClaim) is renderer/platform.h's.
#ifndef SLAVE_H
#define SLAVE_H

#include <stdint.h>

#include "platform.h"

// Starts the slave CPU waiting for tasks, its timer read against the
// master's (slaveTickOnMaster).
void slaveInit();

// A tick of the master's timer as the slave's reads at that moment
// (slaveTickOnMaster's inverse).
uint32_t masterTickOnSlave(uint32_t tick);

// Starts this CPU's CpuState (common.h), which GBR points at: the master's
// in main, the slave's in slaveMain.
void cpuInit(bool slave);

// Starts task on the slave; the previous task must have returned.
void slaveRun(void (*task)());

// The task the slave is running, or nullptr.
void (*slaveTask())(void*);

// Between polls of a flag: the CPUs share one bus, and a CPU spinning on an
// uncached read takes it from the other.
inline void busPause()
{
    for (int i = 0; i < 32; i++) {
        asm volatile("nop");
    }
}

#endif
