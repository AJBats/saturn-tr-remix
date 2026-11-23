// Full-screen pictures from the original disc (BINDATA/*.BIN: the title and
// the loading screens) on VDP2's NBG0, behind the VDP1 frame.
#ifndef SCREEN_H
#define SCREEN_H

#include <stdint.h>

void screenInit();

// VDP2's NBG0 set back to the 15-bit bitmap the pictures use (after a video's modes, for a crash screen).
void screenVdp2();

// After fmvplay.cpp's video, which sets VDP2 up its own way and has the vertical blank's hook: its last
// picture faded to black, VDP2 set back, a blank picture up.
void screenAfterVideo();

// This level has a plane (plane.h): it shows under the frames, and the picture when
// there's one, from the next frame; VDP2's offsets are its and the picture's, and
// the frames fade by what screenFramesFade gives render.cpp.
void screenPlaneLevel(bool on);
int32_t screenFramesFade();

// The next level's first frames fade up from black; the frame shown fades
// to black (the level's end), returning when it's black: retail's 30 vblanks.
void screenFadeIn();
void screenFadeOut();
// (A picture screenShow shows fades up from black the same way.)

// The options' Gamma (-127..127): added to every colour offset but black.
void screenGamma(int32_t gamma);
// The title's picture dimmed (the Controls page) or back.
void screenDimPicture(bool dim);

// Shows BINDATA/<name>.BIN, a 15-bit RGB picture width x 224, centred, in
// place of the game's frame until screenRelease. The picture already shown
// stays, brought back up to full; one held is copied from work RAM, not read
// from the CD (a read stops the CD's music).
void screenShow(const char* name, int32_t width);

// <name>'s picture at data (width x 224 15-bit pixels) for screenShow, till
// screenHeldForget: at most 2.
void screenHoldPicture(const char* name, const void* data);
void screenHeldForget();

// <name>'s picture, if it's the one shown, copied out of the bitmap into data
// (width x 224 pixels).
bool screenCopyShown(const char* name, int32_t width, void* data);

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
