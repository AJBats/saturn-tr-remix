// The clean-room renderer's lab: what its parts share (see lab.cpp).
#ifndef LAB_H
#define LAB_H

#include <stdint.h>

#include "labplatform.h"
#include "scene.h"

// A page of results: rows of text.
struct Page
{
    char rows[TEXT_ROWS][TEXT_COLUMNS + 1];
    int32_t count;

    void add(const char* text);
};

// Text: each returns the end of what it wrote.
char* put(char* p, const char* s);
char* putNumber(char* p, int32_t value, int32_t width);
char* putFixed(char* p, int32_t value, int32_t decimals, int32_t width);  // value / 10^decimals, right-aligned
char* pad(char* line, char* p, int32_t column);                           // spaces up to column

// SH-2 cycles to hundredths of a millisecond, at 28.6 MHz.
inline int32_t cyclesToHundredths(uint32_t cycles)
{
    return int32_t((uint64_t(cycles) * 100 + 14318) / 28636);
}

// The floor (floor.s): commands built from FloorFaces with nothing decided.
struct FloorXform
{
    const void* vertices;
    uint32_t* cmd;
    uint32_t* grd;
    uint32_t grdLink;
    const uint16_t* shadeGouraud;
};

extern "C" void floorAsm(const FloorFace* faces, int32_t count, FloorXform* xf);

#endif
