// What the Saturn can do for the PlayStation's videos (docs/ps1-fmv.md, step 3): a program of its own
// (make fmvbench) that measures at boot, then shows two pages in turn, 8 seconds each.
//
// Page 1, the 24-bit layout. VDP2's NBG0 is a 512x256 bitmap of 32-bit pixels (all 512 KB of VRAM)
// holding two 320x120 pictures: lines 0-119 (banks A0/A1) and 128-247 (banks B0/B1). The shown
// picture's banks give all 8 cycle slots to the bitmap (24-bit needs 8: VDP2 manual table 3.3), the
// hidden one's to the CPUs; a flip swaps them with the vertical scroll, in a vertical blank. Window W0
// leaves only the picture's lines on screen. The pictures alternate every second. Each band's upper
// half is drawn with 8 bits a colour, its lower half cut to 5: banding in the lower halves only means
// the Saturn shows all 24 bits.
//
// Page 2, the numbers, measured with picture A shown: CPU writes into the hidden picture and into
// the shown one, SCU DMA into the hidden one (a row at a time, and whole), CPU writes into sound
// RAM and into work RAM; MB/s and cycles a word. "ok" = the hidden picture read back as written.

#include "platform.h"
#include "hw.h"
#include "font.h"

#define VDP2_WPSX0      REG16(0x25F800C0)
#define VDP2_WPSY0      REG16(0x25F800C2)
#define VDP2_WPEX0      REG16(0x25F800C4)
#define VDP2_WPEY0      REG16(0x25F800C6)
#define CHCTLA_N0_16M   0x0040  // NBG0 in 24-bit RGB (32 bits a pixel)
#define WCTLA_N0W0E     0x0002  // W0 makes NBG0 transparent...
#define WCTLA_N0W0A     0x0001  // ...outside it

#define SOUND_RAM       0x25A00000
#define LOW_RAM         0x00200000

#define CLOCK_KHZ       28636   // the 352-dot clock
#define PIC_W           320
#define PIC_H           120
#define PITCH           512     // 32-bit pixels a bitmap line
#define TOP             ((224 - PIC_H) / 2)
#define LEFT            ((352 - PIC_W) / 2)
#define OPAQUE          0x80000000

static volatile uint32_t* const gBitmap = (volatile uint32_t*)VDP2_VRAM;

static uint32_t gCardA[PIC_H * PIC_W];      // picture A, for rewriting the shown picture as it is
static uint32_t gCardB[PIC_H * PITCH];      // picture B, laid out as the bitmap's lines: DMA's source

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

static char* putHundredths(char* p, uint32_t v, int32_t width)
{
    p = putInt(p, v / 100, width);
    *p++ = '.';
    *p++ = '0' + v / 10 % 10;
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

// what: "name", bytes in cycles: "  12.34 MB/s  9.30 cyc/w" (words of wordBytes)
static void result(const char* what, uint32_t bytes, uint32_t cyc, uint32_t wordBytes, const char* note)
{
    char text[TEXT_COLUMNS + 1];
    char* p = put(text, what);
    uint32_t mbs = cyc ? uint32_t(uint64_t(bytes) * CLOCK_KHZ / 10 / cyc) : 0;    // MB/s x 100
    uint32_t cpw = uint32_t(uint64_t(cyc) * 100 * wordBytes / bytes);               // cycles a word x 100
    p = putHundredths(p, mbs, 3);
    p = put(p, " MB/s ");
    p = putHundredths(p, cpw, 3);
    p = put(p, note);
    add(text);
}

// --- the screen ------------------------------------------------------------------

static void waitVblank()
{
    while (VDP2_TVSTAT & TVSTAT_VBLANK);
    while (!(VDP2_TVSTAT & TVSTAT_VBLANK));
}

// Picture half (0: lines 0-119, 1: lines 128-247) on screen from this vertical blank.
static void show(int32_t half)
{
    const uint16_t bitmap = (CYCP_N0CG << 12) | (CYCP_N0CG << 8) | (CYCP_N0CG << 4) | CYCP_N0CG;
    const uint16_t cpu = (CYCP_CPU << 12) | (CYCP_CPU << 8) | (CYCP_CPU << 4) | CYCP_CPU;
    const uint16_t a = half ? cpu : bitmap;
    const uint16_t b = half ? bitmap : cpu;
    waitVblank();
    VDP2_CYCA0L = VDP2_CYCA0U = VDP2_CYCA1L = VDP2_CYCA1U = a;
    VDP2_CYCB0L = VDP2_CYCB0U = VDP2_CYCB1L = VDP2_CYCB1U = b;
    VDP2_SCYIN0 = ((half ? 128 : 0) - TOP + 256) & 255;
}

static void mode24()
{
    waitVblank();
    VDP2_BGON = 0;
    VDP2_CHCTLA = CHCTLA_N0_16M | CHCTLA_N0BMEN;
    VDP2_SCXIN0 = (PITCH - LEFT) & (PITCH - 1);
    VDP2_WPSX0 = LEFT * 2;                      // in half dots (the normal modes' bit 0 is ignored)
    VDP2_WPEX0 = (LEFT + PIC_W - 1) * 2;
    VDP2_WPSY0 = TOP;
    VDP2_WPEY0 = TOP + PIC_H - 1;
    VDP2_WCTLA = WCTLA_N0W0E | WCTLA_N0W0A;
    show(0);
    VDP2_BGON = BGON_N0ON;
}

// Back to platform.cpp's 15-bit text page.
static void modeText()
{
    const uint16_t bitmap = (CYCP_N0CG << 12) | (CYCP_N0CG << 8) | (CYCP_N0CG << 4) | CYCP_N0CG;
    const uint16_t cpu = (CYCP_CPU << 12) | (CYCP_CPU << 8) | (CYCP_CPU << 4) | CYCP_CPU;
    waitVblank();
    VDP2_BGON = 0;
    VDP2_WCTLA = 0;
    VDP2_CHCTLA = CHCTLA_N0_32K | CHCTLA_N0BMEN;
    VDP2_CYCA0L = VDP2_CYCA1L = bitmap;
    VDP2_CYCA0U = VDP2_CYCA1U = cpu;
    VDP2_CYCB0L = VDP2_CYCB0U = VDP2_CYCB1L = VDP2_CYCB1U = cpu;
    VDP2_SCXIN0 = 0;
    VDP2_SCYIN0 = 0;
    textClear();
    REG16(VDP2_VRAM + 0x7FFFE) = 0;             // the back screen's colour, black
    VDP2_BGON = BGON_N0ON;
}

// --- the test pictures -------------------------------------------------------------

static uint32_t rgb24(uint32_t r, uint32_t g, uint32_t b)
{
    return OPAQUE | (b << 16) | (g << 8) | r;
}

static const char* const gCardText[2][2] = {
    { "PICTURE A  24-BIT LAYOUT  R G B GREY DARK", "BAND TOPS 8 BITS A COLOUR, BOTTOMS 5" },
    { "PICTURE B  24-BIT LAYOUT  R G B GREY DARK", "BAND TOPS 8 BITS A COLOUR, BOTTOMS 5" },
};

// Picture half's pixel at x, y: two lines of text, then five bands (red, green, blue, grey, dark
// grey) of 20 lines, ramps left to right, 8 bits in the top 10 lines and 5 in the lower 10.
static uint32_t cardPixel(int32_t half, int32_t x, int32_t y)
{
    if (y < 16)
    {
        const char* text = gCardText[half][y >> 3];
        int32_t column = x >> 3;
        int32_t n = 0;
        while (text[n]) {
            n++;
        }
        char c = column < n ? text[column] : ' ';
        uint32_t bits = gFont8[c - FONT8_FIRST][y & 7];
        return (bits >> (x & 7)) & 1 ? rgb24(255, 255, 255) : OPAQUE;
    }
    int32_t band = (y - 16) / 20;
    if (band >= 5)
        return OPAQUE;
    uint32_t v = band == 4 ? x * 63 / (PIC_W - 1) : x * 255 / (PIC_W - 1);
    if ((y - 16) % 20 >= 10) {
        v &= 0xF8;
    }
    if (half) {
        v = (band == 4 ? 63 : 255) - v;     // B ramps the other way, so a flip shows
        if ((y - 16) % 20 >= 10) {
            v &= 0xF8;
        }
    }
    switch (band)
    {
        case 0: return rgb24(v, 0, 0);
        case 1: return rgb24(0, v, 0);
        case 2: return rgb24(0, 0, v);
        default: return rgb24(v, v, v);
    }
}

static void cardsBuild()
{
    for (int32_t y = 0; y < PIC_H; y++)
    {
        for (int32_t x = 0; x < PITCH; x++)
        {
            if (x < PIC_W) {
                gCardA[y * PIC_W + x] = cardPixel(0, x, y);
            }
            gCardB[y * PITCH + x] = x < PIC_W ? cardPixel(1, x, y) : OPAQUE;
        }
    }
}

// --- measuring ----------------------------------------------------------------------

// rows of words 32-bit words from src (srcPitch words a row) to dst (dstPitch): cycles taken
static uint32_t copyTimed(volatile uint32_t* dst, int32_t dstPitch, const uint32_t* src, int32_t srcPitch,
                          int32_t rows, int32_t words)
{
    uint32_t total = 0;
    for (int32_t r = 0; r < rows; r++)
    {
        volatile uint32_t* d = dst + r * dstPitch;
        const uint32_t* s = src + r * srcPitch;
        uint32_t start = cycles();
        for (int32_t i = 0; i < words; i += 8)
        {
            d[i] = s[i];
            d[i + 1] = s[i + 1];
            d[i + 2] = s[i + 2];
            d[i + 3] = s[i + 3];
            d[i + 4] = s[i + 4];
            d[i + 5] = s[i + 5];
            d[i + 6] = s[i + 6];
            d[i + 7] = s[i + 7];
        }
        total += cycles() - start;
    }
    return total;
}

// words 32-bit stores of a value to dst, in chunks of 2 KB: cycles taken
static uint32_t fillTimed(volatile uint32_t* dst, int32_t words)
{
    uint32_t total = 0;
    for (int32_t at = 0; at < words; at += 512)
    {
        volatile uint32_t* d = dst + at;
        uint32_t start = cycles();
        for (int32_t i = 0; i < 512; i += 8)
        {
            d[i] = i;
            d[i + 1] = i;
            d[i + 2] = i;
            d[i + 3] = i;
            d[i + 4] = i;
            d[i + 5] = i;
            d[i + 6] = i;
            d[i + 7] = i;
        }
        total += cycles() - start;
    }
    return total;
}

static uint32_t fill16Timed(volatile uint16_t* dst, int32_t halfwords)
{
    uint32_t total = 0;
    for (int32_t at = 0; at < halfwords; at += 1024)
    {
        volatile uint16_t* d = dst + at;
        uint32_t start = cycles();
        for (int32_t i = 0; i < 1024; i += 8)
        {
            d[i] = i;
            d[i + 1] = i;
            d[i + 2] = i;
            d[i + 3] = i;
            d[i + 4] = i;
            d[i + 5] = i;
            d[i + 6] = i;
            d[i + 7] = i;
        }
        total += cycles() - start;
    }
    return total;
}

// SCU level 0 DMA, direct, as saturn/system.cpp's dmaCopy: cycles from start to end
static uint32_t dmaTimed(const void* src, uint32_t dst, uint32_t size)
{
    while (SCU_DSTA & DSTA_D0_BUSY);
    uint32_t start = cycles();
    SCU_D0R = uint32_t(src);
    SCU_D0W = dst;
    SCU_D0C = size;
    SCU_D0AD = 0x101;       // read +4, write +2 a B-bus unit
    SCU_D0MD = 0x07;
    SCU_D0EN = 0x101;
    while (SCU_DSTA & DSTA_D0_BUSY) {
        cycles();           // keeps the timer's wraps counted
    }
    return cycles() - start;
}

static uint32_t mismatches(volatile uint32_t* dst, int32_t dstPitch, const uint32_t* src, int32_t srcPitch)
{
    uint32_t bad = 0;
    for (int32_t y = 0; y < PIC_H; y++)
    {
        for (int32_t x = 0; x < PIC_W; x++) {
            bad += dst[y * dstPitch + x] != src[y * srcPitch + x];
        }
    }
    return bad;
}

static void clearHalf(int32_t half)
{
    volatile uint32_t* d = gBitmap + (half ? 128 : 0) * PITCH;
    for (int32_t i = 0; i < 128 * PITCH; i++) {
        d[i] = OPAQUE;
    }
}

static void measure()
{
    const int32_t picBytes = PIC_H * PIC_W * 4;

    // work RAM and sound RAM first, with the text page up (no VDP2 traffic of ours)
    uint32_t low = fillTimed((volatile uint32_t*)LOW_RAM, 32768);
    uint32_t high = fillTimed((volatile uint32_t*)gCardB, 32768);
    uint32_t sound = fill16Timed((volatile uint16_t*)(SOUND_RAM + 0x10000), 32768);

    cardsBuild();
    clearHalf(0);
    clearHalf(1);
    copyTimed(gBitmap, PITCH, gCardA, PIC_W, PIC_H, PIC_W);
    mode24();
    for (int32_t i = 0; i < 10; i++) {
        waitVblank();
    }

    // picture A shown: CPU into the hidden half, read back
    volatile uint32_t* hidden = gBitmap + 128 * PITCH;
    uint32_t cpuHidden = 0;
    for (int32_t pass = 0; pass < 4; pass++) {
        cpuHidden += copyTimed(hidden, PITCH, gCardB, PITCH, PIC_H, PIC_W);
    }
    uint32_t badCpu = mismatches(hidden, PITCH, gCardB, PITCH);

    // CPU into the shown half, the same pixels again
    uint32_t cpuShown = copyTimed(gBitmap, PITCH, gCardA, PIC_W, PIC_H, PIC_W);

    // DMA into the hidden half: a row (1280 bytes) at a time, then whole lines in one go
    clearHalf(1);
    uint32_t dmaRows = 0;
    for (int32_t y = 0; y < PIC_H; y++) {
        dmaRows += dmaTimed(gCardB + y * PITCH, uint32_t(hidden + y * PITCH), PIC_W * 4);
    }
    uint32_t badRows = mismatches(hidden, PITCH, gCardB, PITCH);
    clearHalf(1);
    uint32_t dmaWhole = dmaTimed(gCardB, uint32_t(hidden), PIC_H * PITCH * 4);
    uint32_t badWhole = mismatches(hidden, PITCH, gCardB, PITCH);

    add("FMV BENCH (docs/ps1-fmv.md step 3)");
    add("24-bit bitmap, picture A shown:");
    result("CPU>hidden  ", picBytes * 4, cpuHidden, 4, badCpu ? " BAD" : " ok");
    result("CPU>shown   ", picBytes, cpuShown, 4, "");
    result("DMA>hid row ", picBytes, dmaRows, 4, badRows ? " BAD" : " ok");
    result("DMA>hid all ", PIC_H * PITCH * 4, dmaWhole, 4, badWhole ? " BAD" : " ok");
    add("text page (VDP2 idle for us):");
    result("CPU>sound16 ", 65536, sound, 2, "");
    result("CPU>lowRAM  ", 131072, low, 4, "");
    result("CPU>highRAM ", 131072, high, 4, "");
    add("MB/s, then cycles a word (16 or 32 bit)");
    if (badCpu || badRows || badWhole)
    {
        char text[TEXT_COLUMNS + 1];
        char* p = put(text, "mismatches: cpu ");
        p = putInt(p, badCpu, 0);
        p = put(p, " rows ");
        p = putInt(p, badRows, 0);
        p = put(p, " whole ");
        putInt(p, badWhole, 0);
        add(text);
    }
    // the hidden half holds picture B again for the flips
    dmaTimed(gCardB, uint32_t(hidden), PIC_H * PITCH * 4);
}

int main()
{
    platformInit();
    textClear();
    textRow(0, "FMV BENCH measuring...");
    measure();

    while (1)
    {
        // page 1: the pictures, flipped every second
        for (int32_t s = 0; s < 8; s++)
        {
            show(s & 1);
            for (int32_t v = 0; v < 59; v++) {
                waitVblank();
            }
        }
        // page 2: the numbers
        modeText();
        for (int32_t r = 0; r < gRowCount; r++) {
            textRow(r, gRows[r]);
        }
        for (int32_t v = 0; v < 60 * 8; v++) {
            waitVblank();
        }
        // the pictures back
        clearHalf(0);
        clearHalf(1);
        copyTimed(gBitmap, PITCH, gCardA, PIC_W, PIC_H, PIC_W);
        copyTimed(gBitmap + 128 * PITCH, PITCH, gCardB, PITCH, PIC_H, PIC_W);
        mode24();
    }
}
