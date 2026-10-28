// The SCU DSP benchmark's pages and what they share (see bench.cpp).
#ifndef BENCH_H
#define BENCH_H

#include <stdint.h>

#include "platform.h"

// A page of results: rows of text, shown in turn with the others.
struct Page
{
    char rows[TEXT_ROWS][TEXT_COLUMNS + 1];
    int32_t count;

    void add(const char* text);
};

// Text: each returns the end of what it wrote.
char* put(char* p, const char* s);
char* putTenths(char* p, int32_t tenths, int32_t width);   // "12.3", right-aligned
char* pad(char* line, char* p, int32_t column);             // spaces up to column

// The pages' tests.
void pageBus(Page& page);
void pageArithmetic(Page& page);

// The CPUs' loads (kernels.s).
extern "C" void kReadLines(const void* p, uint32_t n);
extern "C" void kReadLongs(const void* p, uint32_t n);
extern "C" void kWriteLongs(void* p, uint32_t n);
extern "C" void kRegisters(uint32_t n);

// Off the bus until the DSP has stopped.
void dspWaitQuietly();

#endif
