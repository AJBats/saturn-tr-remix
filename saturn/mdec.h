// The PlayStation's video frames decoded on an SH-2 (docs/ps1-fmv.md), from records as tools/mdec.py's
// pack() writes them: the PS1's BS v2 codes copied bit for bit, macroblocks row by row, in two parts (one
// a CPU). The pixels are bit for bit those of tools/mdec.py's unpack() and pixels32().
//
// Written from psx-spx (Martin Korth, problemkaputt.de/psx-spx.htm): "CDROM File Video BS Compression"
// (DC values, AC codes) and "MDEC Decompression" (dequantising, the fast IDCT, YUV to RGB, the quant
// table, the zigzag).
#ifndef MDEC_H
#define MDEC_H

#include <stdint.h>

// A record's header (big-endian, as written): part 0's codes follow it.
struct MdecRecord
{
    uint16_t quant;     // the frame's quantiser scale
    uint16_t mbs;       // macroblocks, row by row, 20 a row
    uint16_t firstRow;  // the PS1 frame's macroblock row they start at
    uint16_t rows;
    uint16_t split;     // part 1's first macroblock
    uint16_t pad;
    uint32_t part1;     // part 1's byte offset from the record's start
};

// Builds the tables: once, before any decode.
void mdecInit();

// Decodes part (0 or 1) of a record into 24-bit pixels (VDP2's: 0x80000000 | B << 16 | G << 8 | R):
// picture is the record's first macroblock row's top left, pitch pixels a line.
void mdecDecode(const MdecRecord* record, int part, uint32_t* picture, int pitch);

// The stages mdecDecode runs, for timing them apart (fmvbench).
struct MdecBits
{
    uint32_t buf;           // the next bits, top first
    int32_t cnt;            // how many of them are loaded
    const uint16_t* p;      // the next 16
};
struct MdecCoef
{
    int32_t pre;            // the IDCT's prescale for the value's place (16 fraction bits)
    int16_t scale;          // quant table x quantiser scale, in zigzag order
    int16_t raster;         // the value's place in the block
};
void mdecPartStart(const MdecRecord* record, int part, MdecBits* bits, MdecCoef* coefs);
// Returns whether the block has any AC value (else its IDCT is its DC everywhere).
int mdecParseBlock(MdecBits* bits, const MdecCoef* coefs, int32_t* block);
// mdecparse.s: a macroblock's six blocks, as mdecParseBlock parses them; bit i for block i's AC values
extern "C" uint32_t mdecParseMbAsm(MdecBits* bits, const MdecCoef* coefs, int32_t blocks[6][64]);
extern "C" uint32_t gMdecCodes8[256];
extern "C" uint32_t gMdecCodes9[8];
extern "C" uint32_t gMdecPow2[32];
extern "C" uint16_t gMdecLong[6][16];
void mdecIdct(int32_t* block);                      // the C, the assembly's yardstick
extern "C" void mdecIdctAsm(int32_t* block);        // mdecidct.s: the same, bit for bit
void mdecColour(int32_t blocks[6][64], uint32_t* dst, int pitch);  // the C, the assembly's yardstick
extern "C" void mdecColourAsm(int32_t blocks[6][64], uint32_t* dst, int pitch);   // mdeccolour.s
extern "C" uint8_t gMdecClamp[1024];
extern "C" uint16_t gMdecClampB[1024];

#endif
