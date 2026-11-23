// The PlayStation's videos played on the Saturn (fmvplay.h, docs/ps1-fmv.md "The Cinepak route").
//
// The stream (tools/fmvpack.py): a header, then frames and sound chunks in the order they are needed, each
// behind an 8-byte header, across the Form 2 sectors' 2324 bytes. The drive's sectors go whole (2336 bytes:
// subheader, data, EDC) into the ring by SCU DMA from the CD block's 32-bit port, a look each vertical blank.
// Items are read out of the ring as they arrive: a sound chunk is copied into fmvsound.68k's packed ring at
// once; a frame waits in the ring for its turn. The sound's play position, counted in vertical blanks, is
// the clock: frame k is due when the slots have played k * 22254 / 15 samples, the stream's sample rate.
// Each frame is decoded as soon as the one before is on screen, by both CPUs (a strip split, as cpkbench),
// into the picture, then copied by DMA into the hidden half of VDP2's 24-bit double buffer; the vertical
// blank interrupt flips to it in the blank it is due (or the next, counted late).

#include "fmvplay.h"
#include "cd.h"
#include "hw.h"
#include "fmvsound_m68k.h"

#include <string.h>

static_assert(FMVSOUND_M68K_SIZE <= 0xF000 - 256, "fmvsound.68k runs into its stack");

// --- VDP2's double buffer: 24-bit (as fmvbench and cpkbench), or 15-bit for a taller picture ------------
//
// 24-bit: NBG0 a 512x256 bitmap of 32-bit pixels (all of VRAM), the pictures at lines 0 (banks A0/A1) and
// 128 (B0/B1); the shown banks' 8 cycle slots all to the bitmap, the hidden ones' to the CPUs. 15-bit: a
// 512x512 bitmap of 16-bit pixels, the pictures at lines 0 (A0/A1) and 256 (B0/B1); the shown banks' slots
// half to the bitmap (15-bit takes 4), the hidden ones' all to the CPUs. A flip swaps them with the vertical
// scroll, in a vertical blank; window W0 leaves only the picture's lines on screen.
//
// A taller picture in 24-bit (FMV_TALL_24) has one buffer, as Sega's Cinepak sample 6 does: the picture sits
// with its top half in banks A0/A1 (bitmap lines up to 127) and its bottom half in B0/B1, all 8 slots of each
// bank to the bitmap. A frame goes in a half at a time while the beam is in the other: the top half once the
// beam has passed into the bottom (the host's line interrupt), with A's slots handed to the CPUs meanwhile;
// the bottom half from the next vertical blank, B's slots to the CPUs, done before the beam comes down to it.

#define VDP2_WPSX0      REG16(0x25F800C0)
#define VDP2_WPSY0      REG16(0x25F800C2)
#define VDP2_WPEX0      REG16(0x25F800C4)
#define VDP2_WPEY0      REG16(0x25F800C6)
#define CHCTLA_N0_16M   0x0040
#define WCTLA_N0W0E     0x0002
#define WCTLA_N0W0A     0x0001
#define PITCH           512         // pixels a bitmap line
#define WIDTH           320
#define HALF_LINES      128         // the second 24-bit picture's first line
#define HALF_LINES15    256         // the second 15-bit picture's first line
#define CHCTLA_N0_512   0x0004      // NBG0's bitmap 512x512

static volatile uint32_t* const gBitmap = (volatile uint32_t*)VDP2_VRAM;
static int32_t gLines, gTop;
#ifdef CPK_15BIT
static bool g15;                    // the picture in 15-bit (benches comparing the ways)
#else
static const bool g15 = false;      // the game: 24-bit always
#endif
static bool gRace;                  // the picture in 24-bit, one buffer
static int32_t gRaceFirst;          // its first bitmap line
static const uint16_t kSlotsBitmap = (CYCP_N0CG << 12) | (CYCP_N0CG << 8) | (CYCP_N0CG << 4) | CYCP_N0CG;
static const uint16_t kSlotsCpu = (CYCP_CPU << 12) | (CYCP_CPU << 8) | (CYCP_CPU << 4) | CYCP_CPU;

static void cycles(int32_t half)
{
    const uint16_t bitmap = kSlotsBitmap;
    const uint16_t cpu = kSlotsCpu;
    if (gRace)
    {
        VDP2_CYCA0L = VDP2_CYCA0U = VDP2_CYCA1L = VDP2_CYCA1U = bitmap;
        VDP2_CYCB0L = VDP2_CYCB0U = VDP2_CYCB1L = VDP2_CYCB1U = bitmap;
        VDP2_SCYIN0 = (gRaceFirst - gTop + 256) & 255;
        return;
    }
    const uint16_t a = half ? cpu : bitmap;
    const uint16_t b = half ? bitmap : cpu;
    if (g15)
    {
        VDP2_CYCA0L = VDP2_CYCA1L = a;
        VDP2_CYCB0L = VDP2_CYCB1L = b;
        VDP2_CYCA0U = VDP2_CYCA1U = VDP2_CYCB0U = VDP2_CYCB1U = cpu;
        VDP2_SCYIN0 = ((half ? HALF_LINES15 : 0) - gTop + 512) & 511;
        return;
    }
    VDP2_CYCA0L = VDP2_CYCA0U = VDP2_CYCA1L = VDP2_CYCA1U = a;
    VDP2_CYCB0L = VDP2_CYCB0U = VDP2_CYCB1L = VDP2_CYCB1U = b;
    VDP2_SCYIN0 = ((half ? HALF_LINES : 0) - gTop + 256) & 255;
}

static void waitVblank()
{
    while (VDP2_TVSTAT & TVSTAT_VBLANK);
    while (!(VDP2_TVSTAT & TVSTAT_VBLANK));
}

static void mode(int32_t lines, int32_t tall)
{
    gLines = lines;
    gTop = (224 - lines) / 2;
#ifdef CPK_15BIT
    g15 = lines > HALF_LINES && tall != FMV_TALL_24;
#endif
    gRace = lines > HALF_LINES && tall == FMV_TALL_24;
    gRaceFirst = HALF_LINES - lines / 2;
    const int32_t left = (352 - WIDTH) / 2;
    waitVblank();
    VDP2_BGON = 0;
    for (int32_t i = 0; i < 256 * PITCH; i++) {
        gBitmap[i] = g15 ? 0x80008000 : 0x80000000;
    }
    VDP2_CHCTLA = g15 ? CHCTLA_N0_32K | CHCTLA_N0_512 | CHCTLA_N0BMEN : CHCTLA_N0_16M | CHCTLA_N0BMEN;
    VDP2_SCXIN0 = (PITCH - left) & (PITCH - 1);
    VDP2_WPSX0 = left * 2;
    VDP2_WPEX0 = (left + WIDTH - 1) * 2;
    VDP2_WPSY0 = gTop;
    VDP2_WPEY0 = gTop + lines - 1;
    VDP2_WCTLA = WCTLA_N0W0E | WCTLA_N0W0A;
    waitVblank();
    cycles(0);
    VDP2_BGON = BGON_N0ON;
}

// SCU level 0 DMA, direct; the CD block's port is read at one address (read add 0), the rest step 4.
static void dma(uint32_t src, uint32_t dst, uint32_t bytes, bool fixedSource)
{
    while (SCU_DSTA & DSTA_D0_BUSY);
    SCU_D0R = src;
    SCU_D0W = dst;
    SCU_D0C = bytes;
    SCU_D0AD = fixedSource ? 0x002 : 0x101;
    SCU_D0MD = 0x07;
    SCU_D0EN = 0x101;
    while (SCU_DSTA & DSTA_D0_BUSY);
}

// --- fmvsound.68k ------------------------------------------------------------------------------------------

#define SOUND_RAM       0x25A00000
#define SCSP_REG(x)     (*(volatile uint16_t*)(0x25B00000 + (x)))
#define SCSP_SLOT(s, r) SCSP_REG((s) * 0x20 + (r) * 2)
#define SCSP_MIXER      SCSP_REG(0x400)
#define KYONEX          0x1000
#define KYONB           0x0800
#define LPCTL_LOOP      0x0020
#define SHARED          0xF000
#define S_CMD           0x00
#define S_STATE         0x06
#define S_RD            0x0C
#define S_WR            0x10
#define S_PLAY          0x14
#define S_DECODED       0x18
#define S_LOW           0x1C
#define RING_L          0x10000
#define RING_R          0x20000
#define RING_SAMPLES    32768
#define CRING           0x40000
#define CRING_SIZE      0x40000
#define SLOT            24          // as saturn/fmv.cpp
#define PITCH_22265     ((0xF << 11) | 10)
#define SAMPLES_PER_100_VBL 37146   // the slots' samples in 100 vertical blanks
#define STREAM_RATE     22254

static volatile uint16_t* soundWord(uint32_t at) { return (volatile uint16_t*)(SOUND_RAM + at); }

static uint32_t soundLong(uint32_t at)
{
    volatile uint32_t* p = (volatile uint32_t*)(SOUND_RAM + at);
    uint32_t a = *p, b;
    while ((b = *p) != a) {
        a = b;
    }
    return a;
}

static void soundStart()
{
    fmvHostSoundCpu(false);
    SCSP_MIXER = (1 << 9) | 0xF;
    for (int32_t s = 0; s < 32; s++) {
        SCSP_SLOT(s, 0) = 0;
    }
    SCSP_SLOT(0, 0) = KYONEX;
    volatile uint16_t* ram = soundWord(0);
    const uint16_t* image = (const uint16_t*)fmvsound_m68k;
    for (uint32_t i = 0; i < FMVSOUND_M68K_SIZE / 2; i++) {
        ram[i] = image[i];
    }
    for (uint32_t i = 0; i < 0x100; i += 2) {
        *soundWord(SHARED + i) = 0;
    }
    for (uint32_t i = 0; i < RING_SAMPLES; i++) {
        soundWord(RING_L)[i] = 0;
        soundWord(RING_R)[i] = 0;
    }
    *(volatile uint32_t*)(SOUND_RAM + SHARED + S_WR) = 0;
    *(volatile uint32_t*)(SOUND_RAM + SHARED + S_PLAY) = 0;
    *soundWord(SHARED + S_CMD) = 2;
    fmvHostSoundCpu(true);
}

static void slotsOn()
{
    static const uint32_t ring[2] = { RING_L, RING_R };
    static const uint16_t pan[2] = { 0x1F << 8, 0x0F << 8 };
    for (int32_t c = 0; c < 2; c++)
    {
        int32_t s = SLOT + c;
        SCSP_SLOT(s, 0) = LPCTL_LOOP | ((ring[c] >> 16) & 15);
        SCSP_SLOT(s, 1) = uint16_t(ring[c]);
        SCSP_SLOT(s, 2) = 0;
        SCSP_SLOT(s, 3) = RING_SAMPLES - 1;
        SCSP_SLOT(s, 4) = 0x001F;
        SCSP_SLOT(s, 5) = 0x001F;
        SCSP_SLOT(s, 6) = 0;
        SCSP_SLOT(s, 7) = 0;
        SCSP_SLOT(s, 8) = PITCH_22265;
        SCSP_SLOT(s, 9) = 0;
        SCSP_SLOT(s, 10) = 0;
        SCSP_SLOT(s, 11) = uint16_t((7 << 13) | pan[c]);
    }
    SCSP_SLOT(SLOT, 0) = SCSP_SLOT(SLOT, 0) | KYONB;
    SCSP_SLOT(SLOT + 1, 0) = SCSP_SLOT(SLOT + 1, 0) | KYONB | KYONEX;
}

static void slotsOff()
{
    SCSP_SLOT(SLOT, 0) = SCSP_SLOT(SLOT, 0) & ~(KYONB | KYONEX);
    SCSP_SLOT(SLOT + 1, 0) = (SCSP_SLOT(SLOT + 1, 0) & ~KYONB) | KYONEX;
}

// --- the ring of the drive's sectors -------------------------------------------------------------------

#define SECTOR_BYTES    2336        // Mode 2: subheader, 2324 of data, EDC
#define PAYLOAD         2324
#define PAYLOAD_AT      8
#define CD_PORT32       0x25818000
#define MOVE_MOST       32
#define TRACK_DATA      0x40        // the TOC's control bit for a data track

static volatile FmvLive gLive;
const volatile FmvLive* fmvLive() { return &gLive; }

static const FmvMemory* gMem;
static uint32_t gSlots;             // sectors the ring holds
static uint32_t gArrived;           // sectors moved in, from the stream's first
static uint32_t gSectors;           // the stream's
static uint32_t gDoneTo;            // stream bytes before this are used
static FmvStats* gStats;

// The ring through the cache-through mirror: DMA's writes never reach the cache.
static const uint8_t* slotData(uint32_t sector)
{
    return (const uint8_t*)((uintptr_t(gMem->ring) | 0x20000000) + (sector % gSlots) * SECTOR_BYTES + PAYLOAD_AT);
}

static uint32_t arrivedBytes() { return gArrived * PAYLOAD; }

// The CD block's sectors into the ring: by SCU DMA from its 32-bit port when the ring is in high work RAM, by
// the SH-2's DMA controller from its 16-bit port when it is in low work RAM, which SCU DMA can't reach. The
// controller steals a cycle at a time: in bursts (24,392 cycles a sector, cdbench) it held the bus for up to
// ~9 ms, and a vertical blank's interrupt waiting behind it flipped the picture under the beam.
#define DMAC_SAR0       (*(volatile uint32_t*)0xFFFFFF80)
#define DMAC_DAR0       (*(volatile uint32_t*)0xFFFFFF84)
#define DMAC_TCR0       (*(volatile uint32_t*)0xFFFFFF88)
#define DMAC_CHCR0      (*(volatile uint32_t*)0xFFFFFF8C)
#define DMAC_DRCR0      (*(volatile uint8_t*)0xFFFFFE71)
#define DMAC_DMAOR      (*(volatile uint32_t*)0xFFFFFFB0)
#define CHCR_DEST_INC   0x4000
#define CHCR_WORDS      0x0400
#define CHCR_AUTO       0x0200
#define CHCR_END        0x0002
#define CHCR_ENABLE     0x0001
#define DMAOR_ENABLE    0x0001
#define HIGH_WORK_RAM   0x06000000

static void sectorsIn(uintptr_t dst, uint32_t bytes)
{
    if (dst >= HIGH_WORK_RAM)
    {
        dma(CD_PORT32, dst, bytes, true);
        return;
    }
    (void)DMAC_CHCR0;
    DMAC_CHCR0 = 0;                     // off, its end flag cleared
    DMAC_SAR0 = uintptr_t(&CD_DATA);
    DMAC_DAR0 = dst;
    DMAC_TCR0 = bytes / 2;
    DMAC_DRCR0 = 0;
    (void)DMAC_DMAOR;
    DMAC_DMAOR = DMAOR_ENABLE;          // its error flags cleared
    DMAC_CHCR0 = CHCR_DEST_INC | CHCR_WORDS | CHCR_AUTO | CHCR_ENABLE;      // cycle steal
    while (!(DMAC_CHCR0 & CHCR_END));
    DMAC_CHCR0 = 0;
}

// Each sector's EDC (ECMA-130 14.3: a CRC over the subheader and data, stored little-endian after them) and
// subheader checked as it lands in the ring, to tell bad data off the disc from anything after.
static uint32_t gEdcTable[256] FMV_TABLE;

static void edcInit()
{
    for (uint32_t i = 0; i < 256; i++)
    {
        uint32_t e = i;
        for (int32_t k = 0; k < 8; k++) {
            e = (e >> 1) ^ ((e & 1) ? 0xD8018001 : 0);
        }
        gEdcTable[i] = e;
    }
}

static bool sectorGood(uint32_t sector)
{
    const uint32_t* p = (const uint32_t*)((uintptr_t(gMem->ring) | 0x20000000) + (sector % gSlots) * SECTOR_BYTES);
    if (p[0] != 0x01002800 || p[1] != 0x01002800)
        return false;
    uint32_t e = 0;
    for (int32_t i = 0; i < (PAYLOAD_AT + PAYLOAD) / 4; i++)
    {
        const uint32_t w = p[i];
        e = gEdcTable[(e ^ (w >> 24)) & 0xFF] ^ (e >> 8);
        e = gEdcTable[(e ^ (w >> 16)) & 0xFF] ^ (e >> 8);
        e = gEdcTable[(e ^ (w >> 8)) & 0xFF] ^ (e >> 8);
        e = gEdcTable[(e ^ w) & 0xFF] ^ (e >> 8);
    }
    const uint32_t stored = p[(PAYLOAD_AT + PAYLOAD) / 4];
    return e == ((stored >> 24) | ((stored >> 8) & 0xFF00) | ((stored << 8) & 0xFF0000) | (stored << 24));
}

static void moveSectors()
{
    gLive.where = FMV_AT_READY;
    uint32_t ready = cdSectorsReady();
    gLive.lastReady = ready;
    if (ready > gStats->blockMost) {
        gStats->blockMost = ready;
    }
    uint32_t waiting = gArrived - gDoneTo / PAYLOAD;
    if (waiting > gStats->ringMost) {
        gStats->ringMost = waiting;
    }
    uint32_t room = gSlots - waiting;
    uint32_t toEnd = gSlots - gArrived % gSlots;
    uint32_t n = ready;
    n = n < room ? n : room;
    n = n < toEnd ? n : toEnd;
    n = n < MOVE_MOST ? n : MOVE_MOST;
    if (!n)
        return;
    uint32_t t = fmvHostCycles();
    gLive.where = FMV_AT_BEGIN;
    int ret = cdTransferBegin(n);
    gLive.lastRet = ret;
    if (ret != 0)
    {
        if (!gStats->cdFailed++)
        {
            uint32_t fad;
            gStats->cdFirstError = ret;
            gStats->cdFirstStatus = cdStatus(&fad);
            gStats->cdFirstSector = gArrived;
        }
        cdTransferEnd();
        return;
    }
    gLive.where = FMV_AT_DMA;
    sectorsIn(uintptr_t(gMem->ring) + (gArrived % gSlots) * SECTOR_BYTES, n * SECTOR_BYTES);
    gLive.where = FMV_AT_END;
    cdTransferEnd();
    for (uint32_t k = 0; k < n; k++)
    {
        if (!sectorGood(gArrived + k))
        {
            if (!gLive.edcBad++) {
                gLive.edcFirst = gArrived + k;
            }
            gLive.edcLast = gArrived + k;
        }
    }
    gArrived += n;
    gStats->sectors += n;
    gStats->moveSum += fmvHostCycles() - t;
}

// bytes of the stream from offset to dst, across sectors; offset and bytes 4-byte aligned
static void streamCopy(void* dst, uint32_t offset, uint32_t bytes)
{
    uint32_t* d = (uint32_t*)dst;
    while (bytes)
    {
        uint32_t sector = offset / PAYLOAD;
        uint32_t within = offset - sector * PAYLOAD;
        uint32_t n = PAYLOAD - within;
        n = n < bytes ? n : bytes;
        const uint32_t* s = (const uint32_t*)(slotData(sector) + within);
        for (uint32_t i = 0; i < n / 4; i++) {
            d[i] = s[i];
        }
        d += n / 4;
        offset += n;
        bytes -= n;
    }
}

// --- the stream's items -----------------------------------------------------------------------------------

#define HEADER          64
#define ITEM            8
#define KIND_END        0
#define KIND_FRAME      1
#define KIND_SOUND      2
#define QUEUE           256

struct Pending
{
    uint32_t at;        // its bytes' offset in the stream
    uint32_t bytes;
    uint32_t number;
};

static Pending gQueue[QUEUE] FMV_TABLE;
static uint32_t gQueueHead, gQueueTail;
static uint32_t gParse;             // the next item's offset
static bool gEnded;
static uint32_t gSoundWr;           // CRING bytes written, unwrapped

static void doneTo()
{
    gDoneTo = gQueueHead != gQueueTail ? gQueue[gQueueHead % QUEUE].at - ITEM : gParse;
}

// A sound chunk into the packed ring, if there's room: whether it went.
static bool soundChunk(uint32_t offset, uint32_t bytes)
{
    uint32_t rd = soundLong(SHARED + S_RD);
    uint32_t pos = gSoundWr & (CRING_SIZE - 1);
    uint32_t need = bytes + (pos + bytes > CRING_SIZE ? CRING_SIZE - pos : 0);
    if (gSoundWr + need - rd > CRING_SIZE)
        return false;
    if (pos + bytes > CRING_SIZE)
    {
        *soundWord(CRING + pos) = 0;        // go on from the ring's start
        gSoundWr += CRING_SIZE - pos;
        pos = 0;
    }
    volatile uint16_t* dst = soundWord(CRING + pos);
    while (bytes)
    {
        uint32_t sector = offset / PAYLOAD;
        uint32_t within = offset - sector * PAYLOAD;
        uint32_t n = PAYLOAD - within;
        n = n < bytes ? n : bytes;
        const uint16_t* s = (const uint16_t*)(slotData(sector) + within);
        for (uint32_t i = 0; i < n / 2; i++) {
            *dst++ = s[i];
        }
        offset += n;
        bytes -= n;
        gSoundWr += n;
    }
    *(volatile uint32_t*)(SOUND_RAM + SHARED + S_WR) = gSoundWr;
    return true;
}

static void parse()
{
    while (!gEnded)
    {
        if (arrivedBytes() < gParse + ITEM)
            return;
        uint32_t head[2];
        streamCopy(head, gParse, ITEM);
        uint32_t kind = head[0] >> 24, bytes = head[0] & 0xFFFFFF;
        if (kind == KIND_END)
        {
            gEnded = true;
            return;
        }
        if (arrivedBytes() < gParse + ITEM + bytes)
            return;
        if (kind == KIND_SOUND)
        {
            if (!soundChunk(gParse + ITEM, bytes))
                return;
        }
        else if (kind == KIND_FRAME)
        {
            if (gQueueTail - gQueueHead == QUEUE)
                return;
            Pending& p = gQueue[gQueueTail++ % QUEUE];
            p.at = gParse + ITEM;
            p.bytes = bytes;
            p.number = head[1];
        }
        gParse += ITEM + ((bytes + 3) & ~3u);
        doneTo();
    }
}

// --- decoding ---------------------------------------------------------------------------------------------

struct Job
{
    const uint8_t* frame;
    uint32_t bytes;
    int32_t first;
};

static Job gJob;

static void* book(int32_t cpu)
{
    return (uint8_t*)gMem->books + cpu * FMV_BOOK_BYTES;
}

static void decode(const uint8_t* frame, uint32_t bytes, int32_t cpu, int32_t first, int32_t last)
{
#ifdef CPK_15BIT
    if (g15) {
        cpkDecode15(frame, bytes, gMem->picture, WIDTH / 2, (CpkBook15*)book(cpu), first, last);
        return;
    }
#endif
    cpkDecode(frame, bytes, gMem->picture, WIDTH, (CpkBook*)book(cpu), first, last);
}

static void slaveDecode(void* arg)
{
    const Job* j = (const Job*)arg;
    volatile FmvLive* live = (volatile FmvLive*)(uintptr_t(&gLive) | 0x20000000);
    live->slaveIn = live->slaveIn + 1;
    decode(j->frame, j->bytes, 1, j->first, 0x7FFF);
    live->slaveOut = live->slaveOut + 1;
}

// the strip the slave starts at: the first past half the frame's bytes
static int32_t splitAt(const uint8_t* f, uint32_t size)
{
    const int32_t strips = cpkStrips(f);
    const uint8_t* d = f + 10;
    for (int32_t s = 0; s < strips; s++)
    {
        if (uint32_t(d - f) * 2 >= size)
            return s;
        d += (d[1] << 16) | (d[2] << 8) | d[3];
    }
    return strips;
}

// The one-buffer picture's half (0 top, 1 bottom) from the picture to its banks, by SCU level 1 DMA a line at
// a time (level 1 moves at most 4 KB, and its registers aren't level 0's, which the main loop may be using
// for the drive); in an interrupt, done before it returns.
#define SCU_D1R         REG32(0x25FE0020)
#define SCU_D1W         REG32(0x25FE0024)
#define SCU_D1C         REG32(0x25FE0028)
#define SCU_D1AD        REG32(0x25FE002C)
#define SCU_D1EN        REG32(0x25FE0030)
#define SCU_D1MD        REG32(0x25FE0034)
#define DSTA_D1_BUSY    0x00020300  // level 1 waiting, moving, or put aside for level 0

// the screen line the one-buffer picture's bottom half starts on
static int32_t raceSplit()
{
    return gTop + gLines / 2;
}

// the line the beam is on: reading EXTEN (its EXLTEN clear) latches the counters, and VCNT then holds the
// line (not interlaced); 224 up is the blank
#define VDP2_EXTEN      REG16(0x25F80002)

static int32_t beamLine()
{
    (void)VDP2_EXTEN;
    return VDP2_VCNT & 0x1FF;
}

static void copyHalf(int32_t half)
{
    volatile uint16_t* l0 = half ? &VDP2_CYCB0L : &VDP2_CYCA0L;
    volatile uint16_t* l1 = half ? &VDP2_CYCB1L : &VDP2_CYCA1L;
    volatile uint16_t* u0 = half ? &VDP2_CYCB0U : &VDP2_CYCA0U;
    volatile uint16_t* u1 = half ? &VDP2_CYCB1U : &VDP2_CYCA1U;
    *l0 = *u0 = *l1 = *u1 = kSlotsCpu;
    const int32_t first = half ? gLines / 2 : 0;
    const int32_t last = half ? gLines : gLines / 2;
    for (int32_t y = first; y < last; y++)
    {
        while (SCU_DSTA & DSTA_D1_BUSY);
        SCU_D1R = uintptr_t(gMem->picture + y * WIDTH);
        SCU_D1W = uintptr_t(gBitmap + (gRaceFirst + y) * PITCH);
        SCU_D1C = WIDTH * 4;
        SCU_D1AD = 0x101;
        SCU_D1MD = 0x07;
        SCU_D1EN = 0x101;
    }
    while (SCU_DSTA & DSTA_D1_BUSY);
    *l0 = *u0 = *l1 = *u1 = kSlotsBitmap;
}

// The next frame out of the ring, decoded, and (but for the one-buffer picture) into the hidden half.
static void prepare(const Pending& p, int32_t hiddenHalf)
{
    uint32_t t0 = fmvHostCycles();
    gLive.where = FMV_AT_COPY;
    streamCopy(gMem->frame, p.at, (p.bytes + 3) & ~3u);
    uint32_t t1 = fmvHostCycles();
    const int32_t split = splitAt(gMem->frame, p.bytes);
    gJob.frame = gMem->frame;
    gJob.bytes = p.bytes;
    gJob.first = split;
    fmvHostSlaveRun(slaveDecode, &gJob);
    gLive.where = FMV_AT_DECODE;
    decode(gMem->frame, p.bytes, 0, 0, split);
    gLive.where = FMV_AT_SLAVE;
    fmvHostSlaveWait();
    gLive.where = FMV_AT_VRAM;
    uint32_t t2 = fmvHostCycles();
    // a line's pixels: 320 32-bit words in 24-bit (the bitmap's lines 2 KB apart), 160 in 15-bit (1 KB)
    const uint32_t words = g15 ? WIDTH / 2 : WIDTH;
    const uint32_t stride = g15 ? PITCH / 2 : PITCH;
    volatile uint32_t* dst = gBitmap + (hiddenHalf ? (g15 ? HALF_LINES15 : HALF_LINES) : 0) * stride;
    for (int32_t y = 0; y < gLines && !gRace; y++) {
        dma(uintptr_t(gMem->picture + y * words), uintptr_t(dst + y * stride), words * 4, false);
    }
    uint32_t t3 = fmvHostCycles();
    gStats->copyMost = t1 - t0 > gStats->copyMost ? t1 - t0 : gStats->copyMost;
    gStats->decodeMost = t2 - t1 > gStats->decodeMost ? t2 - t1 : gStats->decodeMost;
    gStats->decodeSum += t2 - t1;
    gStats->vramMost = t3 - t2 > gStats->vramMost ? t3 - t2 : gStats->vramMost;
}

// --- the vertical blank -----------------------------------------------------------------------------------

static volatile int32_t gFlipAt;    // the vertical blank to flip at, or -1
static volatile uint32_t gDue;      // the vertical blank that frame was due in
static volatile int32_t gShown;     // the frame on screen, -1 none
static volatile int32_t gHalf;      // the half on screen
static volatile bool gTopCopied;    // the one-buffer picture's next frame has its top half in
static volatile bool gKeying;       // the slots start at gKeyon
static volatile bool gSounding;
static volatile uint32_t gKeyon;    // the vertical blank the sound starts in: its play position 0

static void onVblank()
{
    uint32_t v = fmvHostVblanks();
    gLive.arrived = gArrived;
    gLive.parse = gParse;
    gLive.doneTo = gDoneTo;
    gLive.queueHead = gQueueHead;
    gLive.queueTail = gQueueTail;
    gLive.soundWr = gSoundWr;
    gLive.cdFailed = gStats->cdFailed;
    gLive.shown = gShown;
    gLive.sounding = gSounding;
    gLive.vblank = v;
    if (gKeying && int32_t(v - gKeyon) >= 0)
    {
        slotsOn();
        gKeying = false;
        gSounding = true;
    }
    if (gSounding) {
        *(volatile uint32_t*)(SOUND_RAM + SHARED + S_PLAY) = (v - gKeyon) * SAMPLES_PER_100_VBL / 100;
    }
    // the one-buffer picture shows a frame once its top half went in during the field before
    if (gFlipAt >= 0 && int32_t(v - uint32_t(gFlipAt)) >= 0 && (!gRace || gTopCopied))
    {
        int32_t late = int32_t(v - gDue);
        if (gRace)
        {
            copyHalf(1);
            gTopCopied = false;
            // torn if the beam reached the bottom half before it was all in
            const int32_t line = beamLine();
            if (!(VDP2_TVSTAT & TVSTAT_VBLANK))
            {
                gStats->bottomLineMost = uint32_t(line) > gStats->bottomLineMost ? uint32_t(line) : gStats->bottomLineMost;
                if (line >= raceSplit()) {
                    gStats->torn++;
                }
            }
        }
        else
        {
            // torn if this vertical blank's interrupt came after the blank: the beam is on the picture
            if (!(VDP2_TVSTAT & TVSTAT_VBLANK)) {
                gStats->torn++;
            }
            gHalf ^= 1;
            cycles(gHalf);
        }
        gShown = gShown + 1;
        gFlipAt = -1;
        if (late > 0)
        {
            gStats->late++;
            gStats->mostLate = uint32_t(late) > gStats->mostLate ? uint32_t(late) : gStats->mostLate;
        }
    }
}

// The beam just past the one-buffer picture's top half: a frame to show in the next vertical blank has its top
// half put in now.
static void onLine()
{
    // an interrupt held up past the blank finds the beam on the top half again: not this field
    if (VDP2_TVSTAT & TVSTAT_VBLANK || beamLine() < raceSplit())
        return;
    if (gFlipAt >= 0 && !gTopCopied && int32_t(fmvHostVblanks() + 1 - uint32_t(gFlipAt)) >= 0)
    {
        copyHalf(0);
        gTopCopied = true;
        // torn if the beam came round to the top half before it was all in
        const int32_t line = beamLine();
        gStats->topLineMost = uint32_t(line) > gStats->topLineMost ? uint32_t(line) : gStats->topLineMost;
        if (!(VDP2_TVSTAT & TVSTAT_VBLANK) && line < raceSplit()) {
            gStats->torn++;
        }
    }
}

// the vertical blank frame k is due in: the slots have played k * 22254 / 15 samples, at 371.46 a blank
static uint32_t due(uint32_t k)
{
    return gKeyon + (k * 222540 + 55718) / 55719;
}

// --- playing ----------------------------------------------------------------------------------------------

static bool find(const char* name, uint32_t* first, uint32_t* sectors, uint32_t* frames, uint32_t* lines)
{
    static uint8_t index[2048] FMV_TABLE;
    if (cdLoadFile("FMV/INDEX.BIN", index, sizeof(index)) < 8 || memcmp(index, "TRSI", 4) != 0)
        return false;
    const uint32_t count = (index[6] << 8) | index[7];
    for (uint32_t i = 0; i < count && 8 + 32 * (i + 1) <= sizeof(index); i++)
    {
        const uint8_t* e = index + 8 + 32 * i;
        if (strncmp((const char*)e, name, 8) != 0)
            continue;
        auto be32 = [](const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; };
        *first = be32(e + 8);
        *sectors = be32(e + 12);
        *frames = be32(e + 16);
        *lines = (e[22] << 8) | e[23];
        return true;
    }
    return false;
}

// the Form 2 track's first frame address: the TOC's last data track
static uint32_t trackFad()
{
    static uint32_t toc[102] FMV_TABLE;
    if (cdToc(toc) != 0)
        return 0;
    uint32_t fad = 0;
    for (int32_t t = 1; t < 99; t++)
    {
        if (toc[t] != 0xFFFFFFFF && ((toc[t] >> 24) & TRACK_DATA)) {
            fad = toc[t] & 0xFFFFFF;
        }
    }
    return fad;
}

bool fmvHas(const char* name)
{
    uint32_t first, sectors, frames, lines;
    return find(name, &first, &sectors, &frames, &lines);
}

bool fmvPlayStream(const char* name, const FmvMemory& mem, bool skippable, int32_t tall, FmvStats* stats)
{
    memset(stats, 0, sizeof(*stats));
    stats->soundLow = 0x7FFFFFFF;
    cpkInit();
#ifdef CPK_15BIT
    cpkInit15(tall == FMV_TALL_24 ? CPK15_DROP : tall);
#endif
    edcInit();
    gLive.edcBad = gLive.edcFirst = gLive.edcLast = 0;
    gStats = stats;
    gMem = &mem;

    uint32_t first, sectors, frames, lines;
    uint32_t fad = trackFad();
    if (!fad || !find(name, &first, &sectors, &frames, &lines) || lines > 224)
        return false;

    gSlots = mem.ringBytes / SECTOR_BYTES;
    gArrived = gDoneTo = 0;
    gSectors = sectors;
    gQueueHead = gQueueTail = 0;
    gParse = HEADER;
    gEnded = false;
    gSoundWr = 0;
    gFlipAt = -1;
    gShown = -1;
    gHalf = 0;
    gKeying = false;
    gSounding = false;
    gTopCopied = false;

    mode(int32_t(lines), tall);
    soundStart();
    if (cdSectorBytes(CD_SECTOR_MODE2) != 0 || cdStreamStartFad(fad + first, sectors) != 0)
    {
        cdSectorBytes(CD_SECTOR_SIZE);
        return false;
    }
    fmvHostOnVblank(onVblank);
    if (gRace) {
        fmvHostOnLine(onLine, gTop + int32_t(lines) / 2 + 2);
    }

    const uint32_t start = fmvHostVblanks();
    uint32_t looked = start - 1;
    uint32_t next = 0;              // the frame to prepare next
    bool prepared = false;
    const uint32_t samples = frames * STREAM_RATE / 15;
    bool skipped = false;
    while (!stats->error)
    {
        gLive.where = FMV_AT_LOOP;
        uint32_t v = fmvHostVblanks();
        if (v != looked)
        {
            looked = v;
            moveSectors();
            if (skippable && fmvHostSkip())
            {
                skipped = true;
                break;
            }
        }
        gLive.where = FMV_AT_PARSE;
        parse();
        gLive.where = FMV_AT_REST;

        // the next frame, once the one before is on screen
        if (!prepared && next < frames && gFlipAt < 0 && gShown == int32_t(next) - 1 && gQueueHead != gQueueTail)
        {
            const Pending& p = gQueue[gQueueHead % QUEUE];
            prepare(p, gHalf ^ 1);
            gQueueHead++;
            doneTo();
            prepared = true;
        }

        if (!gSounding && !gKeying)
        {
            // a second of the stream in, the first frame ready, sound decoded ahead: the sound and the
            // first frame start together, in the next vertical blank
            if (prepared && v - start >= 60 && soundLong(SHARED + S_DECODED) >= 8192)
            {
                gKeyon = v + (gRace ? 2 : 1);     // the one-buffer picture's top half goes in a field ahead
                gDue = gKeyon;
                gKeying = true;
                gFlipAt = int32_t(gKeyon);
            }
        }
        else if (gSounding && prepared && gFlipAt < 0)
        {
            if (gShown == int32_t(next))
            {
                // on screen: the next can be prepared
                prepared = false;
                next++;
            }
            else
            {
                uint32_t at = due(next);
                gDue = at;
                gFlipAt = int32_t(int32_t(at - v) > 0 ? at : v + 1);
            }
        }

        if (gSounding && gShown == int32_t(frames) - 1 && (v - gKeyon) * SAMPLES_PER_100_VBL / 100 >= samples)
            break;
    }

    fmvHostOnVblank(nullptr);
    fmvHostOnLine(nullptr, 0);
    slotsOff();
    stats->soundLow = int32_t(soundLong(SHARED + S_LOW));
    *soundWord(SHARED + S_CMD) = 0;
    cdStreamStop();
    cdSectorBytes(CD_SECTOR_SIZE);
    stats->frames = uint32_t(gShown + 1);
    stats->vblanks = fmvHostVblanks() - start;
    (void)skipped;
    return stats->error == 0;
}
