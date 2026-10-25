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

// Starts task on the slave; the previous task must have returned.
void slaveRun(void (*task)());

// Waits until the slave's task returns.
void slaveWait();

// The task the slave is running, or nullptr.
void (*slaveTask())();

// Drops this CPU's cached copies of memory.
void cachePurge();

#endif
