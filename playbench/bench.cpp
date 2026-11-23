// The PlayStation's videos played on the Saturn, on a bench disc (make playbench; docs/ps1-fmv.md, "The
// Cinepak route", step 2): saturn/fmvplay.cpp streams VIDEO from the disc's Form 2 track (tools/fmvpack.py's
// FMV.TRK, its index FMV/INDEX.BIN on the data track), decodes it on both CPUs in 24-bit and its sound on the
// 68000, then shows what it measured; then plays it again, over and over.
//
// The host side here: a vertical blank interrupt of its own (irq.s) (the count, the player's hook, and the clock's
// 128-cycle ticks carried past the timer's wrap), the slave as dspbench's platform runs it, low work RAM for
// the drive's ring and the frame buffer, high work RAM for the picture.

#include "platform.h"
#include "hw.h"
#include "cd.h"
#include "fmvplay.h"

#include <string.h>

#ifndef VIDEO
#define VIDEO "CANYON"
#endif

// an SCU interrupt's handler, called by the BIOS's dispatcher (which also acknowledges the SCU) as a
// subroutine: irq.s's entry saves the registers
#define BIOS_SET_SINT   (*(void (**)(uint32_t, void (*)()))0x06000300)
#define BIOS_CHG_SCUIM  (*(void (**)(uint32_t, uint32_t))0x06000344)
#define VECTOR_VBLANK_IN 0x40
#define VECTOR_TIMER0   0x43
#define SCU_IMS_VBLANK_IN 0x0001
#define SCU_IMS_TIMER0  0x0008
#define FRT_TCR         (*(volatile uint8_t*)0xFFFFFE16)
#define FRT_SLOW        2           // a tick every 128 cycles
#define CLOCK_HZ        28636000

// low work RAM, as the game has it between levels (saturn/fmv.cpp): the ring, filled by the SH-2's DMA
// controller, and the frame buffer
#define RING            ((uint8_t*)0x00200000)
#define RING_BYTES      (256 * 2336)       // about 600 KB: with the CD block's 200 sectors, the plan's 1 MB
#define FRAME_BUFFER    (RING + RING_BYTES)
#define FRAME_BYTES     (128 * 1024)

static uint32_t gPicture[320 * 208];     // CORELOGO's whole picture in 24-bit
static uint8_t gBooks[2 * FMV_BOOK_BYTES] __attribute__((aligned(16)));

// VIDEO "CORETEST": CORELOGO from the PS1 in 24-bit with one buffer, then taken to 15-bit each of the three
// ways in turn, over and over with a title before each: for comparing them on a television.
struct Play
{
    const char* name;
    int32_t tall;
    const char* title[4];
};
// VIDEO "GAMETEST": the game's ways, over and over: CORELOGO in 24-bit with one buffer, then two letterboxed
// videos double-buffered
#define GAME_PLAYS 3
static const Play kGameTest[GAME_PLAYS] = {
    { "CORELOGO", FMV_TALL_24, { "1 of 3", "CORELOGO, 24-bit, one buffer", "", "" } },
    { "PYRAMID", FMV_TALL_24, { "2 of 3", "PYRAMID, 24-bit, double buffer", "", "" } },
    { "MANSION", FMV_TALL_24, { "3 of 3", "MANSION, 24-bit, double buffer", "", "" } },
};
#define CORE_PLAYS 4
static const Play kCoreTest[CORE_PLAYS] = {
    { "CORELOGO", FMV_TALL_24, { "1 of 4", "OURS: PS1 picture, 24-BIT,", "ONE BUFFER, COPIED BEHIND THE BEAM", "" } },
    { "CORELOGO", CPK15_DROP, { "2 of 4", "OURS: PS1 picture, 15-bit,", "BITS DROPPED (as retail)", "" } },
    { "CORELOGO", CPK15_ROUND, { "3 of 4", "OURS: PS1 picture, 15-bit,", "ROUNDED", "" } },
    { "CORELOGO", CPK15_DITHER, { "4 of 4", "OURS: PS1 picture, 15-bit,", "DITHERED", "" } },
};

// --- the host ---------------------------------------------------------------------------------------------

static volatile uint32_t gVblanks;
static void (*volatile gHook)();
static volatile uint32_t gTickBase;     // 128-cycle ticks up to gTickLast
static volatile uint16_t gTickLast;

extern "C" void vblankEntry();      // irq.s
extern "C" void lineEntry();
static void watch(const uint32_t* stack);
static void (*volatile gLineHook)();

// SCU timer 0: counts the lines since the vertical blank ended, and interrupts at the one compared
extern "C" void lineIn()
{
    if (gLineHook) {
        gLineHook();
    }
}

void fmvHostOnLine(void (*fn)(), int32_t line)
{
    gLineHook = nullptr;
    if (fn) {
        SCU_T0C = uint32_t(line);
    }
    gLineHook = fn;
}

extern "C" void vblankIn(const uint32_t* stack)
{
    uint16_t now = uint16_t(ticks());
    gTickBase += uint16_t(now - gTickLast);
    gTickLast = now;
    gVblanks++;
    if (gHook)
    {
        gHook();
        watch(stack);
    }
}

uint32_t fmvHostVblanks() { return gVblanks; }
void fmvHostOnVblank(void (*fn)()) { gHook = fn; }
void fmvHostSlaveRun(void (*fn)(void*), void* arg) { slaveRun(fn, arg); }
void fmvHostSlaveWait() { slaveWait(); }
void fmvHostSoundCpu(bool run) { soundCpu(run); }
bool fmvHostSkip() { return false; }

uint32_t fmvHostCycles()
{
    uint32_t base, last;
    do
    {
        base = gTickBase;
        last = gTickLast;
    } while (base != gTickBase);
    return (base + uint16_t(uint16_t(ticks()) - last)) * 128;
}

// --- the numbers ----------------------------------------------------------------------------------------

static char* put(char* p, const char* s)
{
    while (*s) {
        *p++ = *s++;
    }
    *p = 0;
    return p;
}

static char* putInt(char* p, int32_t v)
{
    char digits[12];
    int32_t n = 0;
    bool neg = v < 0;
    uint32_t u = neg ? uint32_t(-v) : uint32_t(v);
    do
    {
        digits[n++] = '0' + u % 10;
        u /= 10;
    } while (u);
    if (neg) {
        digits[n++] = '-';
    }
    while (n) {
        *p++ = digits[--n];
    }
    *p = 0;
    return p;
}

static char* putMs(char* p, uint32_t cycles)
{
    uint32_t hundredths = uint32_t(uint64_t(cycles) * 100000 / CLOCK_HZ);
    p = putInt(p, int32_t(hundredths / 100));
    *p++ = '.';
    *p++ = '0' + hundredths / 10 % 10;
    *p++ = '0' + hundredths % 10;
    *p = 0;
    return p;
}

static char* putHex(char* p, uint32_t v, int32_t digits)
{
    for (int32_t i = digits - 1; i >= 0; i--) {
        *p++ = "0123456789ABCDEF"[(v >> (i * 4)) & 15];
    }
    *p = 0;
    return p;
}

static void textScreen()
{
    const uint16_t bitmap = (CYCP_N0CG << 12) | (CYCP_N0CG << 8) | (CYCP_N0CG << 4) | CYCP_N0CG;
    const uint16_t cpu = (CYCP_CPU << 12) | (CYCP_CPU << 8) | (CYCP_CPU << 4) | CYCP_CPU;
    VDP2_BGON = 0;
    VDP2_WCTLA = 0;
    VDP2_CHCTLA = CHCTLA_N0_32K | CHCTLA_N0BMEN;
    VDP2_CYCA0L = VDP2_CYCA1L = bitmap;
    VDP2_CYCA0U = VDP2_CYCA1U = cpu;
    VDP2_CYCB0L = VDP2_CYCB0U = VDP2_CYCB1L = VDP2_CYCB1U = cpu;
    VDP2_SCXIN0 = 0;
    VDP2_SCYIN0 = 0;
    textClear();
    REG16(VDP2_VRAM + 0x7FFFE) = 0;
    VDP2_BGON = BGON_N0ON;
}

// A video whose picture hasn't moved for 4 s (a frame lasts 4 blanks, the wait before the first a second):
// what the player, the CD block and the 68000 are doing, on screen, every half second from then on (in the
// vertical blank's interrupt, so it shows even with the main loop stuck).
#define WATCH_BLANKS    240
static int32_t gWatchShown = -2;
static uint32_t gWatchSince;

static void watch(const uint32_t* stack)
{
    const volatile FmvLive* l = (const volatile FmvLive*)(uintptr_t(fmvLive()) | 0x20000000);
    if (l->shown != gWatchShown)
    {
        gWatchShown = l->shown;
        gWatchSince = gVblanks;
        return;
    }
    uint32_t still = gVblanks - gWatchSince;
    if (still < WATCH_BLANKS || (still - WATCH_BLANKS) % 30)
        return;
    if (still == WATCH_BLANKS)
    {
        textScreen();
        REG16(0x25B00400) = 1 << 9;     // the SCSP's master volume to 0: the slots loop what's left
    }
    const volatile uint32_t* shared = (const volatile uint32_t*)0x25A0F000;
    char text[80];
    int32_t row = 0;
    char* p = put(text, "FROZEN: frame ");
    p = putInt(p, l->shown);
    p = put(p, " for ");
    p = putInt(p, int32_t(still));
    p = put(p, " blanks, at ");
    putInt(p, int32_t(l->where));
    textRow(row++, text);
    p = put(text, "ring: in ");
    p = putInt(p, int32_t(l->arrived));
    p = put(p, " parse ");
    p = putInt(p, int32_t(l->parse));
    p = put(p, " done ");
    putInt(p, int32_t(l->doneTo));
    textRow(row++, text);
    p = put(text, "queue ");
    p = putInt(p, int32_t(l->queueHead));
    p = put(p, "-");
    p = putInt(p, int32_t(l->queueTail));
    p = put(p, " sound wr ");
    putInt(p, int32_t(l->soundWr));
    textRow(row++, text);
    p = put(text, "cd: ready ");
    p = putInt(p, int32_t(l->lastReady));
    p = put(p, " ret ");
    p = putInt(p, l->lastRet);
    p = put(p, " refused ");
    putInt(p, int32_t(l->cdFailed));
    textRow(row++, text);
    p = put(text, "cd regs ");
    p = putHex(p, REG16(0x25890008), 4);
    p = put(p, " ");
    p = putHex(p, REG16(0x25890018), 4);
    p = put(p, " ");
    p = putHex(p, REG16(0x2589001C), 4);
    p = put(p, " ");
    p = putHex(p, REG16(0x25890020), 4);
    p = put(p, " ");
    putHex(p, REG16(0x25890024), 4);
    textRow(row++, text);
    p = put(text, "68k: state ");
    p = putInt(p, int32_t(REG16(0x25A0F006)));
    p = put(p, " rd ");
    p = putInt(p, int32_t(shared[0x0C / 4]));
    p = put(p, " wr ");
    putInt(p, int32_t(shared[0x10 / 4]));
    textRow(row++, text);
    p = put(text, "68k: play ");
    p = putInt(p, int32_t(shared[0x14 / 4]));
    p = put(p, " decoded ");
    p = putInt(p, int32_t(shared[0x18 / 4]));
    p = put(p, " low ");
    putInt(p, int32_t(shared[0x1C / 4]));
    textRow(row++, text);
    p = put(text, "slave in ");
    p = putInt(p, int32_t(l->slaveIn));
    p = put(p, " out ");
    p = putInt(p, int32_t(l->slaveOut));
    p = put(p, "  dma ");
    putHex(p, REG32(0x25FE007C), 8);
    textRow(row++, text);
    p = put(text, "bad sectors ");
    p = putInt(p, int32_t(l->edcBad));
    p = put(p, ": first ");
    p = putInt(p, int32_t(l->edcFirst));
    p = put(p, " last ");
    putInt(p, int32_t(l->edcLast));
    textRow(row++, text);
    // code addresses on the interrupt's stack: the interrupted PC among them
    p = put(text, "stack:");
    for (int32_t i = 0, n = 0; i < 40 && n < 3; i++)
    {
        uint32_t v = stack[i];
        if (v >= 0x06004000 && v < 0x06010000)
        {
            p = put(p, " ");
            p = putInt(p, i);
            p = put(p, "=");
            p = putHex(p, v, 8);
            n++;
        }
    }
    textRow(row++, text);
}

static void modeText()
{
    const uint16_t bitmap = (CYCP_N0CG << 12) | (CYCP_N0CG << 8) | (CYCP_N0CG << 4) | CYCP_N0CG;
    const uint16_t cpu = (CYCP_CPU << 12) | (CYCP_CPU << 8) | (CYCP_CPU << 4) | CYCP_CPU;
    while (VDP2_TVSTAT & TVSTAT_VBLANK);
    while (!(VDP2_TVSTAT & TVSTAT_VBLANK));
    VDP2_BGON = 0;
    VDP2_WCTLA = 0;
    VDP2_CHCTLA = CHCTLA_N0_32K | CHCTLA_N0BMEN;
    VDP2_CYCA0L = VDP2_CYCA1L = bitmap;
    VDP2_CYCA0U = VDP2_CYCA1U = cpu;
    VDP2_CYCB0L = VDP2_CYCB0U = VDP2_CYCB1L = VDP2_CYCB1U = cpu;
    VDP2_SCXIN0 = 0;
    VDP2_SCYIN0 = 0;
    textClear();
    REG16(VDP2_VRAM + 0x7FFFE) = 0;
    VDP2_BGON = BGON_N0ON;
}

static void report(int32_t round, bool ok, const FmvStats& s)
{
    char text[80];
    int32_t row = 0;
    char* p = put(text, VIDEO " PLAYED, ROUND ");
    putInt(p, round);
    textRow(row++, text);
    if (!ok)
    {
        p = put(text, "FAILED, error ");
        putInt(p, s.error);
        textRow(row++, text);
    }
    p = put(text, "frames ");
    p = putInt(p, int32_t(s.frames));
    p = put(p, ", late ");
    p = putInt(p, int32_t(s.late));
    p = put(p, " (at most ");
    p = putInt(p, int32_t(s.mostLate));
    put(p, " blanks)");
    textRow(row++, text);
    p = put(text, "decode ms: most ");
    p = putMs(p, s.decodeMost);
    p = put(p, " mean ");
    putMs(p, s.frames ? s.decodeSum / s.frames : 0);
    textRow(row++, text);
    p = put(text, "frame out of ring ms ");
    p = putMs(p, s.copyMost);
    p = put(p, ", to VRAM ");
    putMs(p, s.vramMost);
    textRow(row++, text);
    p = put(text, "sectors ");
    p = putInt(p, int32_t(s.sectors));
    p = put(p, ", moving them ");
    p = putMs(p, s.sectors ? s.moveSum / s.sectors : 0);
    put(p, " ms each");
    textRow(row++, text);
    uint32_t seconds100 = s.vblanks * 100 / 60;
    p = put(text, "drive ");
    p = putInt(p, int32_t(seconds100 ? uint64_t(s.sectors) * 2324 * 100 / seconds100 / 1024 : 0));
    p = put(p, " KB/s over ");
    p = putInt(p, int32_t(seconds100 / 100));
    put(p, " s");
    textRow(row++, text);
    p = put(text, "waiting at most: ring ");
    p = putInt(p, int32_t(s.ringMost));
    p = put(p, ", CD block ");
    putInt(p, int32_t(s.blockMost));
    textRow(row++, text);
    p = put(text, "sound: least decoded ahead ");
    putInt(p, s.soundLow);
    textRow(row++, text);
    p = put(text, "CD refused ");
    p = putInt(p, int32_t(s.cdFailed));
    if (s.cdFailed)
    {
        p = put(p, ": first ");
        p = putInt(p, s.cdFirstError);
        p = put(p, " status ");
        p = putInt(p, s.cdFirstStatus);
        p = put(p, " at ");
        putInt(p, int32_t(s.cdFirstSector));
    }
    textRow(row++, text);
}

int main()
{
    platformInit();
    FRT_TCR = FRT_SLOW;
    gTickLast = uint16_t(ticks());
    BIOS_SET_SINT(VECTOR_VBLANK_IN, vblankEntry);
    BIOS_SET_SINT(VECTOR_TIMER0, lineEntry);
    SCU_T1MD = 0x001;                                   // the timers counting
    // the SCU's other interrupts masked
    BIOS_CHG_SCUIM(0, 0xFFFF & ~(SCU_IMS_VBLANK_IN | SCU_IMS_TIMER0));
    uint32_t sr;
    asm volatile("stc sr, %0" : "=r"(sr));
    // the CPU takes levels 12 up: vblank-in (15) and timer 0 (12), the only ones unmasked
    asm volatile("ldc %0, sr" : : "r"((sr & ~0xF0u) | (11 << 4)));

    textClear();
    textRow(0, "PLAY BENCH: " VIDEO " starting...");
    int ret = cdInit();
    if (ret)
    {
        textRow(1, "cdInit failed");
        while (1);
    }

    FmvMemory mem;
    mem.ring = RING;
    mem.ringBytes = RING_BYTES;
    mem.frame = FRAME_BUFFER;
    mem.frameBytes = FRAME_BYTES;
    mem.picture = gPicture;
    mem.books = gBooks;

    // VIDEO "CORE24": the first of CORETEST's alone, over and over
    const bool core24 = strcmp(VIDEO, "CORE24") == 0;
    const bool gameTest = strcmp(VIDEO, "GAMETEST") == 0;
    if (core24 || gameTest || strcmp(VIDEO, "CORETEST") == 0)
    {
        const Play* list = gameTest ? kGameTest : kCoreTest;
        const int32_t count = core24 ? 1 : gameTest ? GAME_PLAYS : CORE_PLAYS;
        char last[4][80] = { "", "", "", "" };
        uint32_t plays = 0, badPlays = 0, badSectors = 0;
        for (int32_t k = 0;; k = (k + 1) % count)
        {
            modeText();
            for (int32_t row = 0; row < 4; row++) {
                textRow(4 + row, list[k].title[row]);
            }
            for (int32_t row = 0; row < 4; row++) {
                textRow(10 + row, last[row]);
            }
            for (uint32_t v = gVblanks; gVblanks - v < 180;);
            FmvStats stats;
            bool ok = fmvPlayStream(list[k].name, mem, false, list[k].tall, &stats);
            const volatile FmvLive* live = fmvLive();
            plays++;
            badPlays += live->edcBad != 0;
            badSectors += live->edcBad;
            char* p = put(last[0], ok ? "last: " : "FAILED: ");
            p = putInt(p, int32_t(stats.frames));
            p = put(p, " frames, ");
            p = putInt(p, int32_t(stats.late));
            p = put(p, " late, ");
            p = putInt(p, int32_t(stats.torn));
            put(p, " torn");
            p = put(last[1], "decode ms: most ");
            p = putMs(p, stats.decodeMost);
            p = put(p, " mean ");
            putMs(p, stats.frames ? stats.decodeSum / stats.frames : 0);
            p = put(last[3], "halves in by line: top ");
            p = putInt(p, int32_t(stats.topLineMost));
            p = put(p, ", bottom ");
            putInt(p, int32_t(stats.bottomLineMost));
            p = put(last[2], "bad sectors ");
            p = putInt(p, int32_t(badSectors));
            p = put(p, " in ");
            p = putInt(p, int32_t(badPlays));
            p = put(p, " of ");
            p = putInt(p, int32_t(plays));
            put(p, " plays");
        }
    }

    for (int32_t round = 1;; round++)
    {
        FmvStats stats;
        bool ok = fmvPlayStream(VIDEO, mem, false, CPK15_DITHER, &stats);
        modeText();
        if (!ok && stats.frames == 0) {
            textRow(1, "not played: no index entry or no Form 2 track?");
        }
        report(round, ok, stats);
        for (uint32_t v = gVblanks; gVblanks - v < 60 * 20;);
    }
}
