// The lab's Saturn start-up: the 352-dot clock, VDP1 drawing a list on
// demand, a text layer over it on VDP2, the pad, the timers and the slave
// CPU. The clock, the layers and the slave's start follow the DSP bench's
// and the game's (saturn/system.cpp, screen.cpp, overlay.cpp), which the
// Saturn has proven.
#ifndef LAB_PLATFORM_H
#define LAB_PLATFORM_H

#include <stdint.h>

#include "platform.h"

void platformInit();

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

#endif
