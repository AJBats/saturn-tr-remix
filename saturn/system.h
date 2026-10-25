// Saturn system setup shared by the game and test programs.
#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>

// Display mode set by systemInit: 352x240 non-interlaced, like the original.
#define SCREEN_WIDTH    352
#define SCREEN_HEIGHT   240

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
    uint32_t roomTicks;     // renderRoom: room vertex transform + face bucketing
    uint32_t meshTicks;     // renderMesh: mesh vertex transform + face bucketing
    uint32_t vdp1EndLine;   // scanline where VDP1 last finished drawing
    uint32_t vertices;      // vertices transformed in the last frame
    uint32_t vdp1Ticks;     // VDP1 drawing time of the last frame
    uint32_t waitTicks;     // CPU time spent waiting for VDP1 to become idle
};

extern FrameStats gStats;

void systemInit();
void waitVblank();

// Current scanline: 0 at the top of the display, counting on through vblank
// (vertical sync lines read as higher values still).
uint32_t vdp2Line();

// VDP1 draws one command list per displayed frame: wait until it is idle
// (previous frame drawn and on screen), upload the next list, then plot.
void vdp1WaitIdle();
void vdp1Plot();

// Free-running timer at Pphi/128, 16 bits; subtract readings modulo 0x10000.
uint32_t timerTicks();

#endif
