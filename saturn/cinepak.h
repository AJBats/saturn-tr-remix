// Cinepak frames decoded to VDP2's 24-bit pixels, 0x80000000 | B << 16 | G << 8 | R (docs/ps1-fmv.md,
// "The other route"). The frames come from tools/cinepak.py, which checks the strip rule this relies on:
// a strip's vectors use only the codebook entries its own chunks sent in the same frame. So any CPU can
// decode any strip, with one codebook of its own, and needs nothing from the strips before it.
#ifndef CINEPAK_H
#define CINEPAK_H

#include <stdint.h>

struct CpkBook
{
    uint32_t v4[256][4];    // a V4 entry's 2x2 pixels: top left, top right, bottom left, bottom right
    uint32_t v1[256][4];    // a V1 entry's 4 colours, each filling a 2x2 quarter of the block
};

void cpkInit();

// The strips in a frame.
int32_t cpkStrips(const uint8_t* frame);

// Decodes strips first to last - 1 of a frame (size bytes) over the picture at pic, pitch pixels a line;
// blocks a frame keeps from the last are left as they are.
void cpkDecode(const uint8_t* frame, uint32_t size, uint32_t* pic, int32_t pitch, CpkBook* book,
               int32_t first, int32_t last);

#endif
