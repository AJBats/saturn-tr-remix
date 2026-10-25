// Saturn system setup shared by the game and test programs.
#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>

// Counts vblank-ins once systemInit has installed the interrupt.
extern volatile uint32_t gVblankCount;

// Per-frame performance counters, readable from the debugger (gStats).
struct FrameStats
{
    uint32_t fps;           // frames rendered in the last second
    uint32_t updateTicks;   // gameUpdate, FRT ticks (x128 = CPU cycles)
    uint32_t renderTicks;   // gameRender including flush
    uint32_t flushTicks;    // flush alone (VDP1 command build + DMA)
    uint32_t faces;         // polygons sent to VDP1
    uint32_t vdp1Late;      // vblanks where VDP1 had not finished drawing
    uint32_t frames;        // frames rendered since boot
};

extern FrameStats gStats;

void systemInit();
void waitVblank();

// Free-running timer at Pphi/128, 16 bits; subtract readings modulo 0x10000.
uint32_t timerTicks();

#endif
