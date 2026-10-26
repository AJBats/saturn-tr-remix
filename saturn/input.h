// Saturn control pads read through SMPC INTBACK: the player's on port 1, and
// a debug pad on port 2 for development toggles.
#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

#define PAD_L       0x0008
#define PAD_Z       0x0010
#define PAD_Y       0x0020
#define PAD_X       0x0040
#define PAD_R       0x0080
#define PAD_B       0x0100
#define PAD_C       0x0200
#define PAD_A       0x0400
#define PAD_START   0x0800
#define PAD_UP      0x1000
#define PAD_DOWN    0x2000
#define PAD_LEFT    0x4000
#define PAD_RIGHT   0x8000

// Starts reading the pads in every vblank, from an interrupt.
void padInit();

// The buttons held (PAD_* bits) as of the last completed read.
uint16_t padButtons();

// The debug pad's buttons as of the same read (0 when none is connected).
uint16_t padDebug();

#endif
