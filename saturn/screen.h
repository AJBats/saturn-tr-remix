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

// The debug overlay (overlay.cpp) draws into NBG0's bitmap during play, over
// the frame (0 clear): free while no picture holds it. screenPictures counts
// the pictures shown so far, each of which leaves the bitmap to clear again.
bool screenOverlayFree();
void screenOverlay(bool show);
uint32_t screenPictures();

#endif
