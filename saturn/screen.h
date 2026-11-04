// Full-screen pictures from the original disc (BINDATA/*.BIN: the title and
// the loading screens) on VDP2's NBG0, behind the VDP1 frame.
#ifndef SCREEN_H
#define SCREEN_H

#include <stdint.h>

void screenInit();

// This level has a plane (plane.h): it shows under the frames, and the picture when
// there's one, from the next frame; VDP2's offsets are its and the picture's, and
// the frames fade by what screenFramesFade gives render.cpp.
void screenPlaneLevel(bool on);
int32_t screenFramesFade();

// Shows BINDATA/<name>.BIN, a 15-bit RGB picture width x 224, centred, in
// place of the game's frame until screenRelease.
void screenShow(const char* name, int32_t width);

// A black picture to draw into, in place of the game's frame until
// screenRelease: the bitmap, 512 pixels a line, its top left at the screen's.
volatile uint16_t* screenBlank();

// The frame VDP1 has drawn under vdp1Hold, still in its draw buffer, as the
// picture: the paused scene behind the inventory, as the original's
// (0603a9dc). It goes through scratch, in high work RAM (the SCU's DMA
// can't move from one B-bus device to another). It dims over 15 vblanks;
// VDP1's next frame (the ring, over a transparent clear) comes on that dim
// and brightens over 5.
void screenCapture(void* scratch, uint32_t scratchSize);
bool screenCaptured();  // till the picture is released or replaced

// VDP1's frames dim and the picture brightens again over 30 vblanks, as the
// original's inventory closes (0603aaf0); nothing without a picture.
void screenClosing();

// The game's frames show again from the next one VDP1 draws: over the picture
// with keepPicture (VDP1 clears to transparent), or on black.
void screenRelease(bool keepPicture);

// The debug overlay (overlay.cpp) draws into NBG0's bitmap during play, over
// the frame (0 clear): free while no picture holds it. screenPictures counts
// the pictures shown so far, each of which leaves the bitmap to clear again.
bool screenOverlayFree();
void screenOverlay(bool show);
uint32_t screenPictures();

// VDP1's frames hidden during play, the back colour showing round the
// overlay's text (the trace's playback page), and shown again.
void screenFramesHidden(bool hidden);

#endif
