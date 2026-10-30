// What the renderer needs of the Saturn, which each program it runs in (the
// lab, the game) provides: the CPUs' timers, their caches, the SCU's DMA to
// VDP1 VRAM, the DSP's end interrupt, the slave CPU.
#ifndef RENDER_PLATFORM_H
#define RENDER_PLATFORM_H

#include <stdint.h>

// This CPU's free-running timer: 16 bits, a tick every 32 cycles, so a span
// up to 2M cycles (73 ms) reads right.
#define TICK_CYCLES 32
uint32_t ticks();

inline uint32_t ticksCycles(uint32_t t0, uint32_t t1)
{
    return ((t1 - t0) & 0xFFFF) * TICK_CYCLES;
}

// The same memory through the cache-through mirror.
template <typename T>
inline T* uncached(T* p)
{
    return (T*)(uintptr_t(p) | 0x20000000);
}

// Drops this CPU's cached copies of memory: all of it, or of size bytes from p.
void cachePurge();

inline void cacheDrop(const void* p, uint32_t size)
{
    for (uintptr_t a = uintptr_t(p) & ~15u; a < uintptr_t(p) + size; a += 16) {
        *(volatile uint32_t*)(a | 0x40000000) = 0;  // the cache's associative purge
    }
}

// size bytes from src (high work RAM: the SCU's DMA can't read low) to VDP1
// VRAM at offset vram by the SCU's level 0 DMA, once the copy before has
// ended; dmaWait waits for the last.
void dmaToVdp1(const void* src, uint32_t vram, uint32_t size);
void dmaWait();

// The DSP's ends: a program ending with ENDI raises the SCU's DSP end
// interrupt, whose handler on the master notes the master's tick.
// dspEndsClear forgets those noted; dspEnds copies up to max of them, the
// first first: how many there were.
void dspEndsClear();
uint32_t dspEnds(uint32_t* ends, uint32_t max);

// A tick of the slave's timer as the master's reads at that moment (both
// count the same clock, started apart: the gap measured at start-up).
uint32_t slaveTickOnMaster(uint32_t tick);

// Starts task(arg) on the slave; the previous one must have returned.
void slaveRun(void (*task)(void*), void* arg);
void slaveWait();
bool slaveDone();

// Claims *p (uncached) for this CPU: true if neither had (TAS.B, atomic
// between the two on the Saturn: saturn/synctest.cpp).
inline bool tasClaim(volatile uint8_t* p)
{
    uint32_t t;
    asm volatile("tas.b @%1\n\tmovt %0" : "=r"(t) : "r"(p) : "t", "memory");
    return t;
}

#endif
