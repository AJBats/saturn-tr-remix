// Page 4, the DSP's banks: whether its program waits on a data RAM bank
// that its DMA is using (banks.dsp). Mednafen moves a transfer at once, so
// only the Saturn can say.

#include "bench.h"
#include "dsp.h"
#include "banks_dsp.h"

#define BUFFER  ((uint32_t*)0x06080000)
#define ROUNDS  256

static uint32_t roundCycles(uint32_t entry)
{
    const uint32_t md0[2] = { ROUNDS, dspAddress(BUFFER) };
    dspWrite(0, 0, md0, 2);
    uint32_t t0 = cycles();
    dspStart(entry);
    dspWaitQuietly();
    return (cycles() - t0) * 10 / ROUNDS;
}

static void row(Page& page, const char* name, int32_t tenths)
{
    char line[64];
    char* p = put(line, name);
    p = pad(line, p, 28);
    putTenths(p, tenths, 8);
    page.add(line);
}

void pageBanks(Page& page)
{
    page.add("DSP BENCH  page 4: the DSP's banks");
    page.add("(SH-2 cycles a round: a 64-longword DMA");
    page.add("started, then 160 instructions each");
    page.add("reading a longword of a bank, then a wait");
    page.add("for the DMA)");
    page.add("");
    dspLoad(banks_dsp, BANKS_DSP_COUNT);
    row(page, "no DMA", roundCycles(BANKS_DSP_NONE));
    row(page, "out of MD3, reading MD2", roundCycles(BANKS_DSP_OUTOTH));
    row(page, "out of MD3, reading MD3", roundCycles(BANKS_DSP_OUTSAM));
    row(page, "into MD3, reading MD2", roundCycles(BANKS_DSP_INOTH));
    row(page, "into MD3, reading MD3", roundCycles(BANKS_DSP_INSAM));
    page.add("");
    page.add("If reading MD3 costs more than MD2 by about");
    page.add("the DMA's time, the program waits on a bank");
    page.add("the DMA is using.");
}
