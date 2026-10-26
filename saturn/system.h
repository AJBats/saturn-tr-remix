// Saturn system setup shared by the game and test programs.
#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>

// Display mode set by systemInit: 352x240 non-interlaced, like the original.
#define SCREEN_WIDTH    352
#define SCREEN_HEIGHT   224

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
    uint32_t drawTicks;     // drawing up to flush: rooms queued for the slave, meshes
    uint32_t vdp1EndLine;   // scanline where VDP1 last finished drawing
    uint32_t vertices;      // vertices transformed in the last frame
    uint32_t vdp1Ticks;     // VDP1 drawing time of the last frame
    uint32_t waitTicks;     // CPU time spent waiting for VDP1 to become idle
    uint32_t slaveWaitTicks;// flush waiting for the slave to finish the rooms
    uint32_t detailFaces;   // room faces drawn with cached textures (RoomStats)
    uint32_t detailMisses;  // room faces drawn in their mean colour for want of a slot or upload
    uint32_t detailUploads; // room textures copied to the cache last frame
    uint32_t flatFaces;     // faces drawn flat for distance (rooms and meshes)
    uint32_t roomVertices;  // room vertices transformed
    uint32_t slaveXformTicks; // slave: room vertex transform
    uint32_t slaveFaceTicks;  // slave: room face setup
    uint32_t slaveMeshTicks;  // slave: meshes
    uint32_t slaveIdleTicks;  // slave: waiting for rooms to be queued
    uint32_t dspVertices;     // room vertices the DSP projected
    uint32_t dspMeshVertices; // the master's mesh vertices the DSP projected
    uint32_t dmaTicks;      // flush: texture uploads and command DMA
    uint32_t farWaitTicks;  // flush: waiting for the slave's commands after building ours
    uint32_t eraseReach;    // lines the last frame change's vblank erase reached
};

extern FrameStats gStats;

void systemInit();
void waitVblank();

// Issues an SMPC command with no parameters and waits for it to finish.
void smpcCommand(uint8_t command);

// Current scanline: 0 at the top of the display, counting on through vblank
// (vertical sync lines read as higher values still).
uint32_t vdp2Line();

// VDP1 draws one command list per displayed frame: wait until it has drawn
// the last list (vdp1WaitDrawn; or until that is on screen too, vdp1WaitIdle),
// upload the next, then plot, which starts once the last is on screen.
void vdp1WaitIdle();
void vdp1WaitDrawn();
void vdp1Plot();

// How many of the top lines of each frame VDP1 erases in the vblank of the
// frame change (TVMR VBE), before it draws there; 0 for none. The vblank is
// too short for a whole 352-wide frame, so the frame's commands clear the
// rest. Takes effect from the frame after next.
void vdp1SetVblankErase(uint32_t lines);

// Calls fn from the vblank-out interrupt once the next frame VDP1 draws is on
// screen (nullptr cancels). Call with VDP1 idle, so that frame is a new one.
void vdp1OnNextFrame(void (*fn)());

// The frame sequencing's state, for the crash screen (crash.cpp).
uint32_t vdp1DebugState();

// Free-running timer at Pphi/128, 16 bits; subtract readings modulo 0x10000.
uint32_t timerTicks();

// SCU level 0 DMA from high work RAM to VDP1/VDP2 memory, started once the
// previous transfer has finished.
void dmaCopy(const void* src, uint32_t dst, uint32_t size);
void dmaWait();

#endif
