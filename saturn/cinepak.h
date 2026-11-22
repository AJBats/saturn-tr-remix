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

// 15-bit output, for a picture too tall to double-buffer in 24-bit (docs/ps1-fmv.md, decision 2): pixels
// 0x8000 | B << 10 | G << 5 | R, each channel's 8 bits taken down to 5 by dropping 3 (as saturn/fmv.cpp
// does), rounding, or a 2x2 ordered dither. The dither is in the codebooks: a V4 entry's 4 pixels and each
// V1 colour's 2x2 quarter fall on the pattern's 4 places, so playing costs nothing more.
enum { CPK15_DROP, CPK15_ROUND, CPK15_DITHER };

struct CpkBook15
{
    uint32_t v4[256][2];    // a V4 entry's top pixel pair, bottom pair
    uint32_t v1[256][8];    // a V1 entry's 4 quarters, each its top pair and bottom pair
};

void cpkInit15(int32_t convert);

// As cpkDecode, into 15-bit pixels; pitch counts pixel pairs (32-bit words) a line.
void cpkDecode15(const uint8_t* frame, uint32_t size, uint32_t* pic, int32_t pitch, CpkBook15* book,
                 int32_t first, int32_t last);

// The strips in a frame.
int32_t cpkStrips(const uint8_t* frame);

// Decodes strips first to last - 1 of a frame (size bytes) over the picture at pic, pitch pixels a line;
// blocks a frame keeps from the last are left as they are.
void cpkDecode(const uint8_t* frame, uint32_t size, uint32_t* pic, int32_t pitch, CpkBook* book,
               int32_t first, int32_t last);

#endif
