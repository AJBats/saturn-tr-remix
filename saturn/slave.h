// The slave SH-2. It runs one task at a time for the master. The CPUs share
// memory but not caches: each purges its cache before reading what the other
// wrote, and flags they poll are read and written uncached.
#ifndef SLAVE_H
#define SLAVE_H

#include <stdint.h>

// The same memory through the cache-through mirror.
template <typename T>
inline T* uncached(T* p)
{
    return (T*)(uintptr_t(p) | 0x20000000);
}

// Starts the slave CPU waiting for tasks.
void slaveInit();

// Starts this CPU's CpuState (common.h), which GBR points at: the master's
// in main, the slave's in slaveMain.
void cpuInit(bool slave);

// Starts task on the slave; the previous task must have returned.
void slaveRun(void (*task)());

// Waits until the slave's task returns.
void slaveWait();

// The task the slave is running, or nullptr.
void (*slaveTask())();

// Drops this CPU's cached copies of memory.
void cachePurge();

// ... of the lines holding size bytes from p, which another bus master wrote.
inline void cacheDrop(const void* p, uint32_t size)
{
    for (uintptr_t a = uintptr_t(p) & ~15u; a < uintptr_t(p) + size; a += 16) {
        *(volatile uint32_t*)(a | 0x40000000) = 0;  // the cache's associative purge
    }
}

// Between polls of a flag: the CPUs share one bus, and a CPU spinning on an
// uncached read takes it from the other.
inline void busPause()
{
    for (int i = 0; i < 32; i++) {
        asm volatile("nop");
    }
}

#endif
