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
// NBG3, 8 x 8 cells of 16 colours (0 clear, 1 white, 2 black) at
// SCREEN_OVERLAY_CELLS, named by a page of 64 x 64 one-word pattern names at
// SCREEN_OVERLAY_NAMES (VDP2 VRAM offsets past the pictures' staging area).
// Pattern name n draws cell n - SCREEN_OVERLAY_NAME0. Hidden by a picture or
// screenBlank until shown again.
#define SCREEN_OVERLAY_NAMES    0x78000
#define SCREEN_OVERLAY_CELLS    0x7A000
#define SCREEN_OVERLAY_NAME0    ((SCREEN_OVERLAY_CELLS / 32) & 0x3FF)
void screenOverlay(bool show);

#endif
