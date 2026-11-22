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

#ifndef VIDEO
#define VIDEO "CANYON"
#endif

// an SCU interrupt's handler, called by the BIOS's dispatcher (which also acknowledges the SCU) as a
// subroutine: irq.s's entry saves the registers
#define BIOS_SET_SINT   (*(void (**)(uint32_t, void (*)()))0x06000300)
#define BIOS_CHG_SCUIM  (*(void (**)(uint32_t, uint32_t))0x06000344)
#define VECTOR_VBLANK_IN 0x40
#define SCU_IMS_VBLANK_IN 0x0001
#define FRT_TCR         (*(volatile uint8_t*)0xFFFFFE16)
#define FRT_SLOW        2           // a tick every 128 cycles
#define CLOCK_HZ        28636000

#define RING_BYTES      (256 * 2336)       // about 600 KB: with the CD block's 200 sectors, the plan's 1 MB
#define FRAME_BUFFER    ((uint8_t*)0x00200000)
#define FRAME_BYTES     (128 * 1024)

static uint8_t gRing[RING_BYTES] __attribute__((aligned(16)));     // high work RAM: SCU DMA can't reach low
static uint32_t gPicture[320 * 128];
static CpkBook gBooks[2];

// --- the host ---------------------------------------------------------------------------------------------

static volatile uint32_t gVblanks;
static void (*volatile gHook)();
static volatile uint32_t gTickBase;     // 128-cycle ticks up to gTickLast
static volatile uint16_t gTickLast;

extern "C" void vblankEntry();      // irq.s

extern "C" void vblankIn()
{
    uint16_t now = uint16_t(ticks());
    gTickBase += uint16_t(now - gTickLast);
    gTickLast = now;
    gVblanks++;
    if (gHook) {
        gHook();
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
    BIOS_CHG_SCUIM(0, 0xFFFF & ~SCU_IMS_VBLANK_IN);     // the SCU's other interrupts masked
    uint32_t sr;
    asm volatile("stc sr, %0" : "=r"(sr));
    asm volatile("ldc %0, sr" : : "r"((sr & ~0xF0u) | (14 << 4)));  // the CPU takes level 15 alone: vblank-in

    textClear();
    textRow(0, "PLAY BENCH: " VIDEO " starting...");
    int ret = cdInit();
    if (ret)
    {
        textRow(1, "cdInit failed");
        while (1);
    }

    FmvMemory mem;
    mem.ring = gRing;
    mem.ringBytes = RING_BYTES;
    mem.frame = FRAME_BUFFER;
    mem.frameBytes = FRAME_BYTES;
    mem.picture = gPicture;
    mem.books = gBooks;

    for (int32_t round = 1;; round++)
    {
        FmvStats stats;
        bool ok = fmvPlayStream(VIDEO, mem, false, &stats);
        modeText();
        if (!ok && stats.frames == 0) {
            textRow(1, "not played: no index entry or no Form 2 track?");
        }
        report(round, ok, stats);
        for (uint32_t v = gVblanks; gVblanks - v < 60 * 20;);
    }
}
