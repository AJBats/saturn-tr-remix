// The DSP benchmark's Saturn start-up: the 352-dot clock, a text page on
// VDP2, the timers and the slave CPU. Nothing of the game's.
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

// The 352-dot clock (the game's: 28.6 MHz), the text page shown, each CPU's
// free-running timer counting every 8 cycles, the slave waiting for tasks.
void platformInit();

// This CPU's free-running timer: 16 bits, a tick every 8 cycles.
#define TICK_CYCLES 8
uint32_t ticks();

// The same memory through the cache-through mirror.
template <typename T>
inline T* uncached(T* p)
{
    return (T*)(uintptr_t(p) | 0x20000000);
}

// Drops this CPU's cached copies of memory.
void cachePurge();

// The text page: TEXT_ROWS rows of TEXT_COLUMNS characters, inside the TV's
// overscan.
#define TEXT_COLUMNS    42
#define TEXT_ROWS       26
void textClear();
void textRow(int32_t row, const char* text);

// Starts task(arg) on the slave; the previous one must have returned.
void slaveRun(void (*task)(void*), void* arg);
void slaveWait();

#endif
