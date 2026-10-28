// Page 2, the arithmetic: what rotating a point by a 3x4 matrix costs on the
// DSP written two ways (gen_rotate.py), and on the SH-2. The DSP's runs read
// and write its data RAM only: no DMA. Mednafen times the DSP's arithmetic
// as the Saturn does.

#include "bench.h"
#include "dsp.h"
#include "rotate_dsp.h"

extern "C" void kRotate(const int32_t* points, const int32_t* rows, int32_t* out, uint32_t n);

#define POINTS  8
#define BATCHES 256

// Points within 16 bits; rows a rotation in 16.16 and a translation as their
// products are scaled
static const int32_t kPoints[POINTS][3] = {
    { 12000, -3000, 25000 }, { -20000, 15000, 4000 }, { 0, 0, 0 }, { 32767, -32768, 32767 },
    { -1, 1, -1 }, { 7000, 21000, -14000 }, { -30000, -30000, 30000 }, { 123, -4567, 8901 }
};
static const int32_t kRows[12] = {
    46341, 0, -46341, 1000 << 16,
    26755, 53510, 26755, -2500 << 16,
    37837, -37837, 37837, 30000 << 16
};

static int32_t reference(int32_t point, int32_t row)
{
    const int32_t* v = kPoints[point];
    const int32_t* m = kRows + row * 4;
    int64_t s = int64_t(v[0]) * m[0] + int64_t(v[1]) * m[1] + int64_t(v[2]) * m[2] + m[3];
    return int32_t(s >> 16);
}

// The DSP's cycles for batches from entry.
static uint32_t dspCycles(uint32_t entry, uint32_t batches)
{
    const uint32_t md3[2] = { 1, batches };
    dspWrite(3, 0, md3, 2);
    uint32_t t0 = ticks();
    dspStart(entry);
    dspWaitQuietly();
    return ((ticks() - t0) & 0xFFFF) * TICK_CYCLES;
}

// Its outputs (rowMajor: x'0..x'7 y'.. z'..; else x' y' z' a point) against
// the reference: true if all agree.
static bool dspExact(bool rowMajor)
{
    uint32_t out[POINTS * 3];
    dspRead(2, 0, out, POINTS * 3);
    for (int32_t p = 0; p < POINTS; p++)
    {
        for (int32_t r = 0; r < 3; r++)
        {
            if (int32_t(out[rowMajor ? r * POINTS + p : p * 3 + r]) != reference(p, r))
                return false;
        }
    }
    return true;
}

static void row(Page& page, const char* name, int32_t tenths, const char* note)
{
    char line[64];
    char* p = put(line, name);
    p = pad(line, p, 18);
    p = putTenths(p, tenths, 8);
    p = put(p, "  ");
    put(p, note);
    page.add(line);
}

static void rotateRows(Page& page)
{
    page.add("Rotate a point: a 3x4 matrix, 32 bits");
    page.add("                  cycles a point  exact");

    dspLoad(rotate_dsp, ROTATE_DSP_COUNT);
    dspWrite(0, 0, (const uint32_t*)kPoints, POINTS * 3);
    dspWrite(1, 0, (const uint32_t*)kRows, 12);

    uint32_t none = dspCycles(ROTATE_DSP_NONE, BATCHES);
    uint32_t plain = dspCycles(ROTATE_DSP_PLAIN, BATCHES) - none;
    bool plainExact = dspExact(false);
    uint32_t sched = dspCycles(ROTATE_DSP_SCHED, BATCHES) - none;
    bool schedExact = dspExact(true);
    row(page, "DSP plain", plain * 10 / (BATCHES * POINTS), plainExact ? "yes" : "NO");
    row(page, "DSP scheduled", sched * 10 / (BATCHES * POINTS), schedExact ? "yes" : "NO");
    row(page, "DSP batch loop", none * 10 / BATCHES, "a batch of 8");

    // the SH-2: the points again and again, 512 of them
    static int32_t points[512][3], out[512][3];
    for (int32_t i = 0; i < 512; i++)
    {
        for (int32_t c = 0; c < 3; c++) {
            points[i][c] = kPoints[i % POINTS][c];
        }
    }
    kRotate(&points[0][0], kRows, &out[0][0], 512);     // warm the cache
    uint32_t t0 = ticks();
    kRotate(&points[0][0], kRows, &out[0][0], 512);
    uint32_t cycles = ((ticks() - t0) & 0xFFFF) * TICK_CYCLES;
    bool exact = true;
    for (int32_t i = 0; i < 512; i++)
    {
        for (int32_t r = 0; r < 3; r++) {
            exact = exact && out[i][r] == reference(i % POINTS, r);
        }
    }
    row(page, "SH-2 mac.l", cycles * 10 / 512, exact ? "yes" : "NO");
}

void pageArithmetic(Page& page)
{
    page.add("DSP BENCH  page 2: the arithmetic");
    page.add("(SH-2 cycles at 28.6 MHz, lower is better;");
    page.add("a DSP instruction takes 2 of them)");
    page.add("");
    rotateRows(page);
}
