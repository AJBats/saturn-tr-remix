// The SCU DSP benchmark: a program of its own (make dspbench), which runs a
// battery of tests at boot and shows the results as pages of text to
// photograph, each for 8 seconds in turn. Everything is in SH-2 cycles at the
// game's 28.6 MHz clock.
//
// Page 1, the bus (page_bus.cpp): what the DSP's DMA costs, and what the CPUs
// and the DSP cost each other on the bus.
// Page 2, the arithmetic (page_arithmetic.cpp): what the DSP's work on a point
// costs, by the way it is written, against the SH-2's.
// Page 3, streaming (page_stream.cpp): points through the DSP from memory and
// back, its transfers waited for or overlapped, and what they cost the CPUs.

#include "bench.h"
#include "dsp.h"
#include "hw.h"

void Page::add(const char* text)
{
    if (count >= TEXT_ROWS)
        return;
    int32_t i = 0;
    for (; i < TEXT_COLUMNS && text[i]; i++) {
        rows[count][i] = text[i];
    }
    rows[count][i] = 0;
    count++;
}

char* put(char* p, const char* s)
{
    while (*s) {
        *p++ = *s++;
    }
    *p = 0;
    return p;
}

char* putTenths(char* p, int32_t value, int32_t width)
{
    char digits[12];
    int32_t n = 0;
    uint32_t tenths = value < 0 ? -value : value;
    digits[n++] = '0' + tenths % 10;
    digits[n++] = '.';
    uint32_t whole = tenths / 10;
    do
    {
        digits[n++] = '0' + whole % 10;
        whole /= 10;
    } while (whole);
    if (value < 0) {
        digits[n++] = '-';
    }
    for (int32_t i = n; i < width; i++) {
        *p++ = ' ';
    }
    while (n) {
        *p++ = digits[--n];
    }
    *p = 0;
    return p;
}

char* pad(char* line, char* p, int32_t column)
{
    while (p < line + column) {
        *p++ = ' ';
    }
    *p = 0;
    return p;
}

void dspWaitQuietly()
{
    while (dspBusy()) {
        kRegisters(64);
    }
}

static void waitVblank()
{
    while (VDP2_TVSTAT & TVSTAT_VBLANK);
    while (!(VDP2_TVSTAT & TVSTAT_VBLANK));
}

static Page gPages[3];

int main()
{
    platformInit();
    textClear();
    textRow(0, "DSP BENCH  measuring...");

    pageBus(gPages[0]);
    pageArithmetic(gPages[1]);
    pageStream(gPages[2]);

    while (1)
    {
        for (const Page& page : gPages)
        {
            textClear();
            for (int32_t r = 0; r < page.count; r++) {
                textRow(r, page.rows[r]);
            }
            for (int32_t v = 0; v < 60 * 8; v++) {
                waitVblank();
            }
        }
    }
}
