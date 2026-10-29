// Page 3, streaming: points read from high work RAM by the DSP's DMA,
// rotated (dspsched.py) and written back, 3 longwords in and out a point:
// fetched, worked and sent out in turn (gen_serial.py), or overlapped the
// way Sonic R does it (gen_stream.py); then what the master's reads cost
// beside each, which is what the DSP's transfers cost the CPUs.

#include "bench.h"
#include "dsp.h"
#include "serial_dsp.h"
#include "stream_dsp.h"

extern "C" void kRotate(const int32_t* points, const int32_t* rows, int32_t* out, uint32_t n);

#define COUNT       1024
#define BATCH       8
#define IN          ((int32_t*)0x06080000)
#define OUT         ((int32_t*)0x06084000)
#define SH2_OUT     ((int32_t*)0x06088000)
#define MASTER_BUF  ((uint8_t*)0x06090000)  // the master's reads, 64 KB
#define CHUNK       0x400

// a rotation in 16.16 and a translation as its products are scaled, each
// row m0 m1 m2 t
static const int32_t kRows[12] = {
    46341, 0, -46341, 1000 << 16,
    26755, 53510, 26755, -2500 << 16,
    37837, -37837, 37837, 30000 << 16
};

static int32_t reference(const int32_t* v, int32_t row)
{
    const int32_t* m = kRows + row * 4;
    int64_t s = int64_t(v[0]) * m[0] + int64_t(v[1]) * m[1] + int64_t(v[2]) * m[2] + m[3];
    return int32_t(s >> 16);
}

// The DSP's results (each batch x'0..x'7 y'.. z'..) against the reference.
static bool dspExact()
{
    cachePurge();   // the DSP wrote behind the cache
    for (int32_t p = 0; p < COUNT; p++)
    {
        for (int32_t r = 0; r < 3; r++)
        {
            int32_t at = (p / BATCH) * BATCH * 3 + r * BATCH + p % BATCH;
            if (OUT[at] != reference(IN + p * 3, r))
                return false;
        }
    }
    return true;
}

struct Run
{
    uint32_t cycles;        // the DSP's
    uint32_t masterTenths;  // the master's line reads meanwhile, tenths of a cycle each (0: none made)
};

// Starts entry on the points, the master reading cache lines meanwhile with
// load, else off the bus.
static Run dspRun(uint32_t entry, uint32_t repeats, bool load)
{
    const uint32_t md0[3] = { dspAddress(OUT), dspAddress(IN), repeats };
    dspWrite(0, 0, md0, 3);
    Run run = { 0, 0 };
    uint32_t lines = 0, readCycles = 0, offset = 0;
    uint32_t t0 = cycles();
    dspStart(entry);
    while (dspBusy())
    {
        cycles();
        if (!load)
        {
            kRegisters(64);
            continue;
        }
        uint32_t r0 = ticks();
        kReadLines(MASTER_BUF + offset, CHUNK / 16);
        readCycles += ((ticks() - r0) & 0xFFFF) * TICK_CYCLES;
        lines += CHUNK / 16;
        offset = (offset + CHUNK) & 0xFFFF;
    }
    run.cycles = cycles() - t0;
    run.masterTenths = lines ? readCycles * 10 / lines : 0;
    return run;
}

static void row(Page& page, const char* name, int32_t tenths, const char* note)
{
    char line[64];
    char* p = put(line, name);
    p = pad(line, p, 20);
    p = putTenths(p, tenths, 7);
    p = put(p, "  ");
    put(p, note);
    page.add(line);
}

void pageStream(Page& page)
{
    page.add("DSP BENCH  page 3: streaming");
    page.add("(SH-2 cycles; 1024 points of 3 longwords");
    page.add("read from high work RAM, rotated, written)");
    page.add("");

    uint32_t seed = 0x2468ACE1;
    for (int32_t i = 0; i < COUNT * 3; i++)
    {
        seed = seed * 1664525 + 1013904223;
        IN[i] = int32_t(seed >> 16) - 32768;
    }
    uint32_t rows[12];
    for (int32_t r = 0; r < 3; r++)
    {
        rows[r * 4] = kRows[r * 4 + 3];     // t first
        for (int32_t c = 0; c < 3; c++) {
            rows[r * 4 + 1 + c] = kRows[r * 4 + c];
        }
    }

    page.add("                     cycles a point exact");
    dspLoad(serial_dsp, SERIAL_DSP_COUNT);
    dspWrite(0, 4, rows, 12);
    Run serial = dspRun(SERIAL_DSP_SERIAL, COUNT / BATCH, false);
    row(page, "DSP fetch,work,send", serial.cycles * 10 / COUNT, dspExact() ? "yes" : "NO");
    Run serialLoaded = dspRun(SERIAL_DSP_SERIAL, COUNT / BATCH, true);

    dspLoad(stream_dsp, STREAM_DSP_COUNT);
    dspWrite(0, 4, rows, 12);
    Run stream = dspRun(STREAM_DSP_STREAM, COUNT / BATCH / 2, false);
    row(page, "DSP overlapped", stream.cycles * 10 / COUNT, dspExact() ? "yes" : "NO");
    Run streamLoaded = dspRun(STREAM_DSP_STREAM, COUNT / BATCH / 2, true);

    int32_t* sh2Out = SH2_OUT;
    cachePurge();
    uint32_t t0 = ticks();
    kRotate(IN, kRows, sh2Out, COUNT);
    uint32_t sh2 = ((ticks() - t0) & 0xFFFF) * TICK_CYCLES;
    row(page, "SH-2 mac.l", sh2 * 10 / COUNT, "from RAM");

    page.add("");
    page.add("Beside the master reading cache lines:");
    row(page, "DSP fetch,work,send", serialLoaded.cycles * 10 / COUNT, "a point");
    row(page, "DSP overlapped", streamLoaded.cycles * 10 / COUNT, "a point");
    page.add("the master's line read meanwhile:");
    uint32_t alone = 0;
    {
        uint32_t r0 = ticks();
        kReadLines(MASTER_BUF, 2048);
        alone = ((ticks() - r0) & 0xFFFF) * TICK_CYCLES * 10 / 2048;
    }
    row(page, "  alone", alone, "a line");
    row(page, "  beside fetch,work", serialLoaded.masterTenths, "a line");
    row(page, "  beside overlapped", streamLoaded.masterTenths, "a line");
}
