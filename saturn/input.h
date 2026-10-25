// Saturn control pad on port 1, read through SMPC INTBACK.
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

// Call during vblank, at least 300us after it starts: collects the result of
// the previous request and issues the next one. Returns the buttons held
// (PAD_* bits) as of the last completed read.
uint16_t padUpdate();

#endif
