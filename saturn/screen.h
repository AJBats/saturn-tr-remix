// Full-screen pictures from the original disc (BINDATA/*.BIN: the title and
// the loading screens) on VDP2's NBG0, behind the VDP1 frame.
#ifndef SCREEN_H
#define SCREEN_H

#include <stdint.h>

void screenInit();

// Shows BINDATA/<name>.BIN, a 15-bit RGB picture width x 224, centred, in
// place of the game's frame until screenRelease.
void screenShow(const char* name, int32_t width);

// A black picture to draw into, in place of the game's frame until
// screenRelease: the bitmap, 512 pixels a line, its top left at the screen's.
volatile uint16_t* screenBlank();

// The game's frames show again from the next one VDP1 draws: over the picture
// with keepPicture (VDP1 clears to transparent), or on black.
void screenRelease(bool keepPicture);

// The debug overlay's layer (overlay.cpp), over the frame and any picture:
// NBG1, a 512x256 bitmap of 4-bit pixels (0 clear, 1 white, 2 black) at this
// VDP2 VRAM offset, 256 bytes a line. Hidden by a picture or screenBlank
// until shown again.
#define SCREEN_OVERLAY_VRAM 0x60000
void screenOverlay(bool show);

// Pictures shown so far: loading one overwrites the overlay's bitmap.
uint32_t screenPictures();

#endif
