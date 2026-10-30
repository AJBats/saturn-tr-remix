// The lab's Saturn start-up: the 352-dot clock, VDP1 drawing a list on
// demand, a text layer over it on VDP2, the pad, the timers and the slave
// CPU. The clock, the layers and the slave's start follow the DSP bench's
// and the game's (saturn/system.cpp, screen.cpp, overlay.cpp), which the
// Saturn has proven.
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

void platformInit();

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

// The text layer: TEXT_ROWS rows of TEXT_COLUMNS characters, inside the TV's
// overscan, white on black. As a page it covers the frame; textFrame clears
// it to show the frame VDP1 drew, a row's black band behind any text on it.
#define TEXT_COLUMNS    42
#define TEXT_ROWS       26
void textPage();
void textFrame();
void textRow(int32_t row, const char* text);

// The command list in VDP1 VRAM drawn into the frame buffer not shown, then
// shown from the next field: the SH-2 cycles VDP1 took, or 0 if it never
// finished.
uint32_t vdp1Show();

// The same in two halves, for VDP1 drawing a list while the CPUs go on: the
// list plotted, in the display's lines (VDP1 locks up on a plot started late
// in the vertical blank): the tick it was plotted at. Then, once VDP1 has
// drawn it, that frame shown from the next field: the SH-2 cycles from the
// plot to this CPU seeing it drawn into drawn, waiting for that added to
// wait, then for the vertical blank and the change added to vblank; false if
// VDP1 never finished (a quarter of a second).
uint32_t vdp1Plot();
bool vdp1ShowDrawn(uint32_t plotted, uint32_t &drawn, uint32_t &wait, uint32_t &vblank);

// size bytes from src (high work RAM: the SCU's DMA can't read low) to VDP1
// VRAM at offset vram by the SCU's level 0 DMA, once the copy before has
// ended; dmaWait waits for the last.
void dmaToVdp1(const void* src, uint32_t vram, uint32_t size);
void dmaWait();

// Waits for the next vertical blank, then reads the pad on port 1: the
// buttons held (PAD_*), as of the read before.
#define PAD_L       0x0008
#define PAD_R       0x0080
#define PAD_B       0x0100
#define PAD_C       0x0200
#define PAD_A       0x0400
#define PAD_START   0x0800
#define PAD_LEFT    0x4000
#define PAD_RIGHT   0x8000
uint32_t padVblank();

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
