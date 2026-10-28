// The SCU DSP benchmark: a program of its own (make dspbench), which runs a
// battery of tests at boot and shows the results as a page of text to
// photograph. Everything is in SH-2 cycles at the game's 28.6 MHz clock.
//
// Page 1, the bus. What a CPU's memory access costs, alone and while the
// other CPU and the DSP use the bus; what the DSP's DMA costs a longword in
// transfers of each size, alone and while the CPUs use the bus.

#include <stddef.h>
#include <stdint.h>

#include "platform.h"
#include "dsp.h"
#include "dma_dsp.h"

extern "C" void kReadLines(const void* p, uint32_t n);
extern "C" void kReadLongs(const void* p, uint32_t n);
extern "C" void kWriteLongs(void* p, uint32_t n);
extern "C" void kRegisters(uint32_t n);

// Each CPU's and the DSP's buffers in high work RAM, apart; the CPUs' 64 KB,
// sixteen times their caches
#define DSP_BUFFER      ((uint32_t*)0x06080000)
#define MASTER_BUFFER   ((uint32_t*)0x06090000)
#define SLAVE_BUFFER    ((uint32_t*)0x060A0000)
#define MASTER_LOW      ((uint32_t*)0x00200000)
#define SLAVE_LOW       ((uint32_t*)0x00210000)
#define BUFFER_BYTES    0x10000
#define PASS_BYTES      0x8000  // a timed pass: well inside the timer's 16 bits
#define CHUNK_BYTES     0x400   // a pass between polls of the DSP

// --- text -------------------------------------------------------------------

static char* put(char* p, const char* s)
{
    while (*s) {
        *p++ = *s++;
    }
    *p = 0;
    return p;
}

// value / 10 as "12.3", right-aligned in width
static char* putTenths(char* p, int32_t value, int32_t width)
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

static char* pad(char* line, char* p, int32_t column)
{
    while (p < line + column) {
        *p++ = ' ';
    }
    *p = 0;
    return p;
}

// --- the CPUs' loads ----------------------------------------------------------

enum Kernel { K_LINES, K_LONGS, K_WRITES, K_UNCACHED, K_LOW_LINES, K_COUNT };
static const char* const kKernelNames[K_COUNT] = {
    "read lines", "read longs", "write longs", "uncached rd", "lowram lines"
};

// Kernel over bytes of the buffer from offset (high, or low work RAM's for
// K_LOW_LINES): returns its accesses.
static uint32_t run(int32_t kernel, uint32_t* high, uint32_t* low, uint32_t offset, uint32_t bytes)
{
    uint8_t* h = (uint8_t*)high + offset;
    switch (kernel)
    {
    case K_LINES:
        kReadLines(h, bytes / 16);
        return bytes / 16;
    case K_LONGS:
        kReadLongs(h, bytes / 4);
        return bytes / 4;
    case K_WRITES:
        kWriteLongs(h, bytes / 4);
        return bytes / 4;
    case K_UNCACHED:
        kReadLongs(uncached(h), bytes / 4);
        return bytes / 4;
    default:
        kReadLines((uint8_t*)low + offset, bytes / 16);
        return bytes / 16;
    }
}

// The slave's load until stopped.
struct Load
{
    int32_t kernel;
    volatile uint32_t stop;
};

static void slaveLoad(void* arg)
{
    Load* load = (Load*)arg;
    while (!*uncached(&load->stop)) {
        run(load->kernel, SLAVE_BUFFER, SLAVE_LOW, 0, BUFFER_BYTES);
    }
}

static Load gLoad;

static void slaveLoadStart(int32_t kernel)
{
    gLoad.kernel = kernel;
    *uncached(&gLoad.stop) = 0;
    slaveRun(slaveLoad, &gLoad);
}

static void slaveLoadStop()
{
    *uncached(&gLoad.stop) = 1;
    slaveWait();
}

// --- the DSP ------------------------------------------------------------------

// The DSP's loop of transfers of longs longwords, from entry (dma.dsp).
static void dspStream(uint32_t entry, uint32_t transfers, uint32_t longs)
{
    const uint32_t md0[3] = { transfers, dspAddress(DSP_BUFFER), longs };
    dspWrite(0, 0, md0, 3);
    dspStart(entry);
}

// Off the bus between polls.
static void dspWaitQuietly()
{
    while (dspBusy()) {
        kRegisters(64);
    }
}

// Cycles the DSP takes for transfers of longs, the master waiting off the bus.
static uint32_t dspTime(uint32_t entry, uint32_t transfers, uint32_t longs)
{
    uint32_t t0 = ticks();
    dspStream(entry, transfers, longs);
    dspWaitQuietly();
    return ((ticks() - t0) & 0xFFFF) * TICK_CYCLES;
}

// --- the tests ----------------------------------------------------------------

// Tenths of a cycle an access of kernel on the master, over passes.
static uint32_t masterAccess(int32_t kernel)
{
    uint32_t cycles = 0, accesses = 0;
    for (int32_t i = 0; i < 4; i++)
    {
        uint32_t t0 = ticks();
        accesses += run(kernel, MASTER_BUFFER, MASTER_LOW, i & 1 ? PASS_BYTES : 0, PASS_BYTES);
        cycles += ((ticks() - t0) & 0xFFFF) * TICK_CYCLES;
    }
    return cycles * 10 / accesses;
}

// The DSP's transfers outlast the master's passes (bench at most ~2 M
// cycles): a stream of 64-longword transfers
#define LONG_STREAM     8192

// The master's kernel alone, beside the slave's, beside the DSP reading or
// writing, beside both.
static void cpuRows(int32_t& row)
{
    textRow(row++, "Master's cycles an access, beside");
    textRow(row++, "             alone slave dsprd dspwr   all");
    for (int32_t k = 0; k < K_COUNT; k++)
    {
        uint32_t v[5];
        v[0] = masterAccess(k);

        slaveLoadStart(k);
        v[1] = masterAccess(k);
        slaveLoadStop();

        dspStream(DMA_DSP_READ, LONG_STREAM, 64);
        v[2] = masterAccess(k);
        dspWaitQuietly();

        dspStream(DMA_DSP_WRITE, LONG_STREAM, 64);
        v[3] = masterAccess(k);
        dspWaitQuietly();

        slaveLoadStart(k);
        dspStream(DMA_DSP_READ, LONG_STREAM, 64);
        v[4] = masterAccess(k);
        dspWaitQuietly();
        slaveLoadStop();

        char line[64];
        char* p = put(line, kKernelNames[k]);
        p = pad(line, p, 12);
        for (int32_t i = 0; i < 5; i++) {
            p = putTenths(p, v[i], 6);
        }
        textRow(row++, line);
    }
}

// Tenths of a cycle a transfer of each size, and a longword, the loop's own
// instructions taken off (they run while a transfer's DMA does).
static void dspSizeRows(int32_t& row)
{
    static const int32_t sizes[] = { 1, 4, 16, 64 };
    textRow(row++, "DSP DMA cycles, transfers of");
    textRow(row++, "                  1      4     16     64");
    int32_t v[2][4];
    for (int32_t s = 0; s < 4; s++)
    {
        int32_t longs = sizes[s] == 1 ? 1024 : 4096;
        int32_t transfers = longs / sizes[s];
        int32_t none = dspTime(DMA_DSP_NONE, transfers, sizes[s]);
        v[0][s] = (int32_t(dspTime(DMA_DSP_READ, transfers, sizes[s])) - none) * 10 / transfers;
        v[1][s] = (int32_t(dspTime(DMA_DSP_WRITE, transfers, sizes[s])) - none) * 10 / transfers;
    }
    static const char* const names[4] = { "read /xfer", "read /long", "write /xfer", "write /long" };
    for (int32_t r = 0; r < 4; r++)
    {
        char line[64];
        char* p = put(line, names[r]);
        p = pad(line, p, 12);
        for (int32_t s = 0; s < 4; s++) {
            p = putTenths(p, r & 1 ? v[r >> 1][s] / sizes[s] : v[r >> 1][s], 7);
        }
        textRow(row++, line);
    }
}

// The DSP's 64-longword reads timed while the master runs kernel in chunks
// (polling between them), the slave beside it with slave >= 0.
static uint32_t dspUnderLoad(int32_t kernel, int32_t slave)
{
    const uint32_t transfers = 512;
    if (slave >= 0) {
        slaveLoadStart(slave);
    }
    uint32_t t0 = ticks();
    dspStream(DMA_DSP_READ, transfers, 64);
    uint32_t offset = 0;
    while (dspBusy())
    {
        if (kernel >= 0)
        {
            run(kernel, MASTER_BUFFER, MASTER_LOW, offset, CHUNK_BYTES);
            offset = (offset + CHUNK_BYTES) & (BUFFER_BYTES - 1);
        }
        else
        {
            kRegisters(64);
        }
    }
    uint32_t cycles = ((ticks() - t0) & 0xFFFF) * TICK_CYCLES;
    if (slave >= 0) {
        slaveLoadStop();
    }
    return cycles * 10 / (transfers * 64);
}

static void dspLoadRows(int32_t& row)
{
    textRow(row++, "DSP 64-longword reads, cycles a longword,");
    textRow(row++, "beside the master's and slave's reads (rd)");
    textRow(row++, "or writes (wr)");
    textRow(row++, "             alone   mrd   mwr  msrd  mswr");
    uint32_t v[5] = {
        dspUnderLoad(-1, -1), dspUnderLoad(K_LINES, -1), dspUnderLoad(K_WRITES, -1),
        dspUnderLoad(K_LINES, K_LINES), dspUnderLoad(K_WRITES, K_WRITES)
    };
    char line[64];
    char* p = pad(line, line, 12);
    for (int32_t i = 0; i < 5; i++) {
        p = putTenths(p, v[i], 6);
    }
    textRow(row++, line);
}

int main()
{
    platformInit();
    dspLoad(dma_dsp, DMA_DSP_COUNT);

    int32_t row = 0;
    textRow(row++, "DSP BENCH  page 1: the bus");
    textRow(row++, "(SH-2 cycles at 28.6 MHz, lower is better)");
    row++;
    dspSizeRows(row);
    row++;
    dspLoadRows(row);
    row++;
    cpuRows(row);
    row++;
    textRow(row++, "done");
    while (1);
}
