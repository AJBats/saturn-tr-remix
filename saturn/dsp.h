// The SCU DSP: a fixed-point processor beside the SH-2s, at 14.3 MHz, with
// 256 instructions of program RAM, four banks of 64 longwords of data RAM and
// a DMA of its own to and from high work RAM. Its programs are saturn/*.dsp,
// assembled by tools/dspasm.py into NAME_dsp.h (the Makefile).
//
// The data RAM is the CPUs' only while the DSP is stopped.
#ifndef DSP_H
#define DSP_H

#include <stdint.h>

// Stops the DSP and loads a program at address 0.
void dspLoad(const uint32_t* program, int32_t count);

// Data RAM bank (0..3) from index on.
void dspWrite(uint32_t bank, uint32_t index, const uint32_t* data, int32_t count);
void dspRead(uint32_t bank, uint32_t index, uint32_t* data, int32_t count);

// Runs the program from pc until its END.
void dspStart(uint32_t pc);
bool dspBusy();

// A CPU address for the DSP's DMA (RA0, WA0): physical, in longwords.
inline uint32_t dspAddress(const void* p)
{
    return (uint32_t(p) & 0x07FFFFFF) >> 2;
}

#endif
