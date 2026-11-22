// The CD drive measured (docs/ps1-fmv.md, "The other route"): a program of its own (make cdbench) that
// streams about 10 seconds of sectors five ways at boot, then shows the numbers: Mode 1 moved by cpu32
// and by scu, then Form 2 from the track's first sector (cpu32) and from its sector 100 (cpu32, scu).
// cpu16 and dmac were measured too (2026-11-22, docs/ps1-fmv.md) and dropped from the list.
//
// The disc holds STREAM.BIN on the data track (Mode 1: 2048 bytes a sector) and a last track of Mode 2
// Form 2 sectors (2324 bytes of data each, no error correction), both a pattern from
// cdbench/gen_data.py checked word by word. Each is read from start to end as fast as the drive gives
// it, looking at the CD block once a frame as a player would, and the sectors moved out by:
//   cpu16  the CPU, 16-bit reads of the data register (as saturn/cd.cpp does)
//   cpu32  the CPU, 32-bit reads of the 32-bit data port at 0x25818000
//   dmac   the SH-2's own DMA controller, 16-bit, burst (as libyaul's cd_block_transfer_data_dmac)
//   scu    SCU level 0 DMA from the 32-bit port (here waited for; a player could work meanwhile)
//
// For each way: sectors a second over the stream (after the first batch), KB/s of data, the time from
// the play command to the first sector, the master's cycles a sector spent moving data (the commands
// included), the most sectors seen waiting in the CD block at once, and whether every word was as written.
// For Form 2 also the first sector's first 24 bytes as handed over, and the FAD of the drive if it stalls.

#include "platform.h"
#include "hw.h"
#include "cd.h"

#define CLOCK_HZ        28636000
#define SECTORS         1500        // on the disc, each stream's
#define FORM2_READ      1400        // sectors read of the Form 2 track. On the Saturn (a Satiator), a read
                                    // from the track's first sector, straight after an audio track, gets
                                    // its sectors from 37 on; from sector 100 on, every one arrives.
#define BATCH           16          // sectors moved at most at once
#define POLL_CYCLES     477744      // a frame (1/59.94 s) between looks at the CD block
#define STALL_CYCLES    (CLOCK_HZ * 5)

// the SH-2's DMA controller, channel 0
#define DMAC_SAR0       (*(volatile uint32_t*)0xFFFFFF80)
#define DMAC_DAR0       (*(volatile uint32_t*)0xFFFFFF84)
#define DMAC_TCR0       (*(volatile uint32_t*)0xFFFFFF88)
#define DMAC_CHCR0      (*(volatile uint32_t*)0xFFFFFF8C)
#define DMAC_DRCR0      (*(volatile uint8_t*)0xFFFFFE71)
#define DMAC_DMAOR      (*(volatile uint32_t*)0xFFFFFFB0)
#define CHCR_DEST_INC   0x4000
#define CHCR_WORDS      0x0400
#define CHCR_AUTO       0x0200
#define CHCR_BURST      0x0010
#define CHCR_END        0x0002
#define CHCR_ENABLE     0x0001
#define DMAOR_ENABLE    0x0001

#define CD_DATA32       (*(volatile uint32_t*)0x25818000)

enum Way { CPU16, CPU32, DMAC, SCU };

static uint8_t gBatch[BATCH * CD_SECTOR_MODE2] __attribute__((aligned(16)));

static char gRows[TEXT_ROWS][TEXT_COLUMNS + 1];
static int32_t gRowCount;

// --- text --------------------------------------------------------------------

static char* put(char* p, const char* s)
{
    while (*s) {
        *p++ = *s++;
    }
    *p = 0;
    return p;
}

static char* putInt(char* p, uint32_t v, int32_t width)
{
    char digits[12];
    int32_t n = 0;
    do
    {
        digits[n++] = '0' + v % 10;
        v /= 10;
    } while (v);
    for (int32_t i = n; i < width; i++) {
        *p++ = ' ';
    }
    while (n) {
        *p++ = digits[--n];
    }
    *p = 0;
    return p;
}

static char* putTenths(char* p, uint32_t v, int32_t width)
{
    p = putInt(p, v / 10, width);
    *p++ = '.';
    *p++ = '0' + v % 10;
    *p = 0;
    return p;
}

static void add(const char* text)
{
    if (gRowCount >= TEXT_ROWS)
        return;
    char* row = gRows[gRowCount++];
    int32_t i = 0;
    for (; i < TEXT_COLUMNS && text[i]; i++) {
        row[i] = text[i];
    }
    row[i] = 0;
}

static void showRows()
{
    textClear();
    for (int32_t r = 0; r < gRowCount; r++) {
        textRow(r, gRows[r]);
    }
}

// --- moving sectors ------------------------------------------------------------

static void copyCpu(uint8_t* dst, uint32_t bytes)
{
    uint16_t* w = (uint16_t*)dst;
    for (uint32_t i = 0; i < bytes / 2; i++) {
        w[i] = CD_DATA;
    }
}

static void copyCpu32(uint8_t* dst, uint32_t bytes)
{
    uint32_t* w = (uint32_t*)dst;
    for (uint32_t i = 0; i < bytes / 4; i++) {
        w[i] = CD_DATA32;
    }
}

// SCU level 0 DMA, direct: the port's address fixed, the destination stepping 4
static void copyScu(uint8_t* dst, uint32_t bytes)
{
    while (SCU_DSTA & DSTA_D0_BUSY);
    SCU_D0R = uint32_t(&CD_DATA32);
    SCU_D0W = uint32_t(dst);
    SCU_D0C = bytes;
    SCU_D0AD = 0x002;       // read +0, write +4
    SCU_D0MD = 0x07;
    SCU_D0EN = 0x101;
    while (SCU_DSTA & DSTA_D0_BUSY);
}

static void copyDmac(uint8_t* dst, uint32_t bytes)
{
    (void)DMAC_CHCR0;
    DMAC_CHCR0 = 0;                     // off, its end flag cleared
    DMAC_SAR0 = uint32_t(&CD_DATA);
    DMAC_DAR0 = uint32_t(dst);
    DMAC_TCR0 = bytes / 2;
    DMAC_DRCR0 = 0;
    (void)DMAC_DMAOR;
    DMAC_DMAOR = DMAOR_ENABLE;          // its error flags cleared
    DMAC_CHCR0 = CHCR_DEST_INC | CHCR_WORDS | CHCR_AUTO | CHCR_BURST | CHCR_ENABLE;
    while (!(DMAC_CHCR0 & CHCR_END));
    DMAC_CHCR0 = 0;
}

// Sector k of a stream as cdbench/gen_data.py wrote it? (Sectors and their data start 4-byte aligned.)
static bool sectorOk(const uint8_t* s, uint32_t k, bool form2)
{
    uint32_t words = 512, salt = 0;
    if (form2)
    {
        if ((s[2] & 0x20) == 0)         // the subheader's submode: Form 2
            return false;
        s += 8;
        words = 581;
        salt = 0x5A5A5A5A;
    }
    const uint32_t* w = (const uint32_t*)s;
    for (uint32_t i = 0; i < words; i++)
    {
        if (w[i] != k * 0x9E3779B1u + i * 0x85EBCA77u + salt)
            return false;
    }
    return true;
}

struct Result
{
    int32_t err;
    uint32_t sectors, bad, firstBatch, maxReady;
    uint32_t startCyc, spanCyc, moveCyc;
    uint32_t stallFad, stallStatus;
    uint8_t head[24];       // the first sector's first bytes
};

// Sectors from fad on, the first being the stream's sector k0.
static Result stream(uint32_t fad, bool form2, Way way, uint32_t k0)
{
    Result r = {};
    const uint32_t count = form2 ? FORM2_READ : SECTORS;
    const uint32_t bytes = form2 ? CD_SECTOR_MODE2 : CD_SECTOR_SIZE;
    if ((r.err = cdSectorBytes(bytes)) != 0)
        return r;

    const uint32_t t0 = cycles();
    if ((r.err = cdStreamStartFad(fad, count)) != 0)
        return r;
    uint32_t first = 0, last = 0, seen = cycles();
    while (r.sectors < count)
    {
        uint32_t wait = cycles();
        while (cycles() - wait < POLL_CYCLES);

        uint32_t ready = cdSectorsReady();
        uint32_t now = cycles();
        if (!ready)
        {
            if (now - seen > STALL_CYCLES)
            {
                r.err = -10;            // nothing for 5 seconds
                r.stallStatus = cdStatus(&r.stallFad);
                break;
            }
            continue;
        }
        r.maxReady = ready > r.maxReady ? ready : r.maxReady;
        if (r.sectors == 0)
        {
            first = now;
            r.firstBatch = ready;
        }
        last = now;
        seen = now;

        while (ready)
        {
            uint32_t n = ready < BATCH ? ready : BATCH;
            uint32_t m0 = cycles();
            if ((r.err = cdTransferBegin(n)) != 0)
                break;
            switch (way)
            {
                case CPU16: copyCpu(gBatch, n * bytes); break;
                case CPU32: copyCpu32(gBatch, n * bytes); break;
                case DMAC: copyDmac(gBatch, n * bytes); break;
                case SCU: copyScu(gBatch, n * bytes); break;
            }
            cdTransferEnd();
            r.moveCyc += cycles() - m0;

            const uint8_t* got = uncached(gBatch);     // DMA's writes miss the cache
            if (r.sectors == 0) {
                for (uint32_t j = 0; j < sizeof(r.head); j++) {
                    r.head[j] = got[j];
                }
            }
            for (uint32_t j = 0; j < n; j++) {
                r.bad += !sectorOk(got + j * bytes, k0 + r.sectors + j, form2);
            }
            r.sectors += n;
            ready -= n;
        }
        if (r.err)
            break;
    }
    r.startCyc = first - t0;
    r.spanCyc = last - first;
    cdStreamStop();
    cdSectorBytes(CD_SECTOR_SIZE);
    return r;
}

static void report(const char* what, const Result& r, uint32_t dataBytes)
{
    char text[80];        // add() keeps TEXT_COLUMNS of it
    char* p;
    if (r.err)
    {
        p = put(text, what);
        p = put(p, " FAILED, error -");
        p = putInt(p, uint32_t(-r.err), 0);
        p = put(p, " after ");
        putInt(p, r.sectors, 0);
        add(text);
        p = put(text, "  drive at FAD ");
        p = putInt(p, r.stallFad, 0);
        p = put(p, ", status ");
        putInt(p, r.stallStatus, 0);
        add(text);
        if (r.sectors <= r.firstBatch)
            return;
    }
    uint32_t counted = r.sectors - r.firstBatch;
    uint32_t perSecX10 = r.spanCyc ? uint32_t(uint64_t(counted) * 10 * CLOCK_HZ / r.spanCyc) : 0;
    p = put(text, what);
    p = putTenths(p, perSecX10, 4);
    p = put(p, " sec/s ");
    p = putTenths(p, uint32_t(uint64_t(perSecX10) * dataBytes / 1024), 4);
    put(p, r.bad ? " KB/s BAD" : " KB/s ok");
    add(text);
    p = put(text, "  start ");
    p = putInt(p, uint32_t(uint64_t(r.startCyc) * 1000 / CLOCK_HZ), 0);
    p = put(p, " ms, move ");
    p = putInt(p, r.moveCyc / r.sectors, 0);
    p = put(p, " cyc/sct, wait ");
    putInt(p, r.maxReady, 0);
    add(text);
    if (r.bad)
    {
        p = put(text, "  bad sectors ");
        putInt(p, r.bad, 0);
        add(text);
    }
    if (dataBytes == 2324 && r.bad)
    {
        static const char hex[] = "0123456789ABCDEF";
        for (int32_t row = 0; row < 1; row++)
        {
            p = put(text, "  1st ");
            for (int32_t j = 8; j < 20; j++)
            {
                *p++ = hex[r.head[j] >> 4];
                *p++ = hex[r.head[j] & 15];
                if ((j & 3) == 3) {
                    *p++ = ' ';
                }
            }
            *p = 0;
            add(text);
        }
    }
}

int main()
{
    platformInit();
    textClear();
    textRow(0, "CD BENCH measuring, about 70 seconds...");

    add("CD DRIVE (docs/ps1-fmv.md)");
    int ret = cdInit();
    if (ret)
    {
        char text[80];        // add() keeps TEXT_COLUMNS of it
        putInt(put(text, "cdInit failed -"), uint32_t(-ret), 0);
        add(text);
    }

    static uint32_t toc[102];
    uint32_t form2Fad = 0;
    if (!ret && cdToc(toc) == 0)
    {
        // the last data track
        uint32_t tracks = 0, last = 0;
        for (uint32_t t = 0; t < 99; t++)
        {
            if (toc[t] == 0xFFFFFFFF)
                continue;
            tracks++;
            last = t;
        }
        if (tracks > 1 && (toc[last] >> 24) & 0x40) {
            form2Fad = toc[last] & 0xFFFFFF;
        }
        char text[80];        // add() keeps TEXT_COLUMNS of it
        char* p = put(text, "TOC ");
        p = putInt(p, tracks, 0);
        p = put(p, " tracks; last at FAD ");
        p = putInt(p, toc[last] & 0xFFFFFF, 0);
        p = put(p, " ctrl ");
        putInt(p, toc[last] >> 28, 0);
        add(text);
    }

    uint32_t lba, size;
    if (!ret && cdFind("STREAM.BIN", &lba, &size) == 0)
    {
        showRows();
        report("Mode 1 cpu32", stream(lba + 150, false, CPU32, 0), 2048);
        showRows();
        report("Mode 1 scu  ", stream(lba + 150, false, SCU, 0), 2048);
        showRows();
    }
    else {
        add("STREAM.BIN not found");
    }
    if (form2Fad)
    {
        report("F2 @0 cpu32 ", stream(form2Fad, true, CPU32, 0), 2324);
        showRows();
        report("F2 @100 cpu32", stream(form2Fad + 100, true, CPU32, 100), 2324);
        showRows();
        report("F2 @100 scu ", stream(form2Fad + 100, true, SCU, 100), 2324);
    }
    else {
        add("no Form 2 track in the TOC");
    }
    add("done");
    showRows();
    while (1);
}
