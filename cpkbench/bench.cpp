// Cinepak in 24-bit on the Saturn (docs/ps1-fmv.md, "The other route"): a program of its own (make
// cpkbench) that times saturn/cinepak.cpp at boot, then shows two pages in turn.
//
// The frames are runs of the PlayStation's videos re-encoded by tools/cinepak.py benchframes (ffmpeg's
// Cinepak encoder, each run a keyframe and the frames after it), from the heaviest settings that are worth
// having up to the worst a frame can be (random pixels: every block V4, both codebooks full, 32 strips).
// They sit in high work RAM, so this is the decoder alone, without the CD.
//
// Measured for each run: every frame decoded by the master alone, then by the two CPUs (the slave taking
// the strips after about half the frame's bytes), the slowest frame and the mean in ms; "ok" = the
// pictures' checksums are tools/cinepak.py's. Then the picture's trip to VRAM by SCU DMA, a row at a time.
//
// Page 1 plays each run over and over, decoded live by both CPUs at 15 frames a second through the 24-bit
// double buffer of fmvbench (two 320x120 pictures in NBG0's 512x256 bitmap, flipped by scroll). Page 2 is
// the numbers.

#include "platform.h"
#include "hw.h"
#include "cinepak.h"
#include "cpkframes.h"

#define VDP2_WPSX0      REG16(0x25F800C0)
#define VDP2_WPSY0      REG16(0x25F800C2)
#define VDP2_WPEX0      REG16(0x25F800C4)
#define VDP2_WPEY0      REG16(0x25F800C6)
#define CHCTLA_N0_16M   0x0040  // NBG0 in 24-bit RGB (32 bits a pixel)
#define WCTLA_N0W0E     0x0002  // W0 makes NBG0 transparent...
#define WCTLA_N0W0A     0x0001  // ...outside it

#define FRT_TCR         (*(volatile uint8_t*)0xFFFFFE16)
#define FRT_SLOW        2       // a tick every 128 cycles: wraps after 293 ms, for whole frames
#define FRT_FAST        0       // a tick every 8 (platform.cpp's)

#define CLOCK_KHZ       28636   // the 352-dot clock
#define PIC_W           320
#define PIC_H           120
#define PITCH           512     // 32-bit pixels a bitmap line
#define TOP             ((224 - PIC_H) / 2)
#define LEFT            ((352 - PIC_W) / 2)
#define OPAQUE          0x80000000
#define ALL_STRIPS      0x7FFF
#define VBLANKS_A_FRAME 4       // 15 frames a second
#define PLAY_FRAMES     60      // each run on page 1: 4 seconds

static volatile uint32_t* const gBitmap = (volatile uint32_t*)VDP2_VRAM;

static uint32_t gPic[PIC_H * PIC_W];
static CpkBook gBook[2];        // the master's, the slave's

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

static char* putMs(char* p, uint32_t cyc, int32_t width)
{
    return putHundredths(p, uint32_t(uint64_t(cyc) * 100 / CLOCK_KHZ), width);
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

// --- the screen ------------------------------------------------------------------

static void waitVblank()
{
    while (VDP2_TVSTAT & TVSTAT_VBLANK);
    while (!(VDP2_TVSTAT & TVSTAT_VBLANK));
}

static int32_t gShown;

// Picture half (0: lines 0-119, 1: lines 128-247) on screen from this vertical blank: the shown banks'
// cycle slots all to the bitmap, the hidden ones' to the CPUs.
static void show(int32_t half)
{
    gShown = half;
    const uint16_t bitmap = (CYCP_N0CG << 12) | (CYCP_N0CG << 8) | (CYCP_N0CG << 4) | CYCP_N0CG;
    const uint16_t cpu = (CYCP_CPU << 12) | (CYCP_CPU << 8) | (CYCP_CPU << 4) | CYCP_CPU;
    const uint16_t a = half ? cpu : bitmap;
    const uint16_t b = half ? bitmap : cpu;
    waitVblank();
    VDP2_CYCA0L = VDP2_CYCA0U = VDP2_CYCA1L = VDP2_CYCA1U = a;
    VDP2_CYCB0L = VDP2_CYCB0U = VDP2_CYCB1L = VDP2_CYCB1U = b;
    VDP2_SCYIN0 = ((half ? 128 : 0) - TOP + 256) & 255;
}

static void clearHalf(int32_t half)
{
    volatile uint32_t* d = gBitmap + (half ? 128 : 0) * PITCH;
    for (int32_t i = 0; i < 128 * PITCH; i++) {
        d[i] = OPAQUE;
    }
}

static void mode24()
{
    waitVblank();
    VDP2_BGON = 0;
    clearHalf(0);
    clearHalf(1);
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

// SCU level 0 DMA, direct, as saturn/system.cpp's dmaCopy
static void dma(const void* src, uint32_t dst, uint32_t size)
{
    while (SCU_DSTA & DSTA_D0_BUSY);
    SCU_D0R = uint32_t(src);
    SCU_D0W = dst;
    SCU_D0C = size;
    SCU_D0AD = 0x101;       // read +4, write +2 a B-bus unit
    SCU_D0MD = 0x07;
    SCU_D0EN = 0x101;
    while (SCU_DSTA & DSTA_D0_BUSY);
}

// The picture into a half of the bitmap, a row at a time
static void toVram(int32_t half)
{
    volatile uint32_t* dst = gBitmap + (half ? 128 : 0) * PITCH;
    for (int32_t y = 0; y < PIC_H; y++) {
        dma(gPic + y * PIC_W, uint32_t(dst + y * PITCH), PIC_W * 4);
    }
}

// --- decoding ----------------------------------------------------------------------

// cycles over 128-cycle ticks, for spans up to 293 ms
static uint32_t slowSince(uint32_t start)
{
    return ((ticks() - start) & 0xFFFF) * 128;
}

struct Job
{
    const uint8_t* frame;
    uint32_t size;
    int32_t first;
};

static void slaveDecode(void* arg)
{
    const Job* j = (const Job*)arg;     // the slave purges its cache before a task
    cpkDecode(j->frame, j->size, gPic, PIC_W, &gBook[1], j->first, ALL_STRIPS);
}

// The strip the slave starts at: the first to begin past half the frame's bytes.
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

static Job gJob;

// Frame k by one CPU or two: the cycles taken.
static uint32_t decode(int32_t k, int32_t cpus)
{
    const uint8_t* f = kCpkData + kCpkFrame[k][0];
    const uint32_t size = kCpkFrame[k][1];
    const uint32_t start = ticks();
    if (cpus == 1)
    {
        cpkDecode(f, size, gPic, PIC_W, &gBook[0], 0, ALL_STRIPS);
        return slowSince(start);
    }
    const int32_t split = splitAt(f, size);
    gJob.frame = f;
    gJob.size = size;
    gJob.first = split;
    slaveRun(slaveDecode, &gJob);
    cpkDecode(f, size, gPic, PIC_W, &gBook[0], 0, split);
    slaveWait();
    return slowSince(start);
}

static uint32_t pictureChecksum()
{
    cachePurge();       // the slave's pixels
    uint32_t sum = 0;
    for (uint32_t i = 0; i < PIC_H * PIC_W; i++) {
        sum += gPic[i] * (i + 1);
    }
    return sum;
}

// A run by one CPU or two: the slowest frame's cycles and the mean, whether the checksums match
static bool runTimed(const CpkSeq& s, int32_t cpus, uint32_t* worst, uint32_t* mean)
{
    uint32_t sum = 0, total = 0;
    *worst = 0;
    for (int32_t k = s.first; k < s.first + s.count; k++)
    {
        FRT_TCR = FRT_SLOW;
        uint32_t cyc = decode(k, cpus);
        FRT_TCR = FRT_FAST;
        *worst = cyc > *worst ? cyc : *worst;
        total += cyc;
        sum += pictureChecksum();
    }
    *mean = total / s.count;
    return sum == s.checksum;
}

static void bench()
{
    char text[TEXT_COLUMNS + 1];
    add("CINEPAK 24-BIT (docs/ps1-fmv.md)");
    add("ms a frame, slowest/mean; ok = checksums");
    bool allOk = true;
    for (int32_t i = 0; i < CPK_SEQS; i++)
    {
        const CpkSeq& s = kCpkSeq[i];
        uint32_t w1, m1, w2, m2;
        bool ok1 = runTimed(s, 1, &w1, &m1);
        bool ok2 = runTimed(s, 2, &w2, &m2);
        allOk = allOk && ok1 && ok2;

        uint32_t biggest = 0;
        for (int32_t k = s.first; k < s.first + s.count; k++) {
            biggest = kCpkFrame[k][1] > biggest ? kCpkFrame[k][1] : biggest;
        }
        char* p = put(text, s.name);
        p = put(p, " ");
        p = putInt(p, uint32_t(uint64_t(s.bytes) * 15 / s.count / 1024), 0);
        p = put(p, " KB/s, max ");
        p = putInt(p, biggest, 0);
        put(p, " B");
        add(text);
        p = put(text, " 1 CPU ");
        p = putMs(p, w1, 3);
        p = put(p, "/");
        p = putMs(p, m1, 0);
        p = put(p, "  2 CPU ");
        p = putMs(p, w2, 3);
        p = put(p, "/");
        p = putMs(p, m2, 0);
        put(p, ok1 && ok2 ? " ok" : ok1 ? " BAD2" : " BAD");
        add(text);
    }

    // the picture's trip to VRAM, a row at a time (the bitmap's hidden half)
    FRT_TCR = FRT_SLOW;
    uint32_t start = ticks();
    toVram(gShown ^ 1);
    uint32_t dmaCyc = slowSince(start);
    FRT_TCR = FRT_FAST;
    char* p = put(text, "DMA to VRAM, 120 rows: ");
    p = putMs(p, dmaCyc, 0);
    put(p, " ms");
    add(text);
    add(allOk ? "all pictures as the PC decodes them" : "PICTURES DIFFER FROM THE PC'S");
}

// Page 1: each run decoded live by both CPUs, 15 frames a second, through the hidden half. A frame is
// due VBLANKS_A_FRAME vertical blanks after the last; one not ready by then shows a blank later, and the
// count is kept.
#define VBL_TICKS       3732    // a vertical blank's period in 128-cycle ticks (59.94 a second)

static uint32_t gLate;

static void play()
{
    FRT_TCR = FRT_SLOW;
    uint32_t flip = ticks();
    for (int32_t i = 0; i < CPK_SEQS; i++)
    {
        const CpkSeq& s = kCpkSeq[i];
        for (int32_t n = 0; n < PLAY_FRAMES; n++)
        {
            decode(s.first + n % s.count, 2);
            int32_t half = gShown ^ 1;
            toVram(half);
            const uint32_t due = (VBLANKS_A_FRAME - 1) * VBL_TICKS + VBL_TICKS / 2;     // between the last two
            if (((ticks() - flip) & 0xFFFF) > due) {
                gLate++;
            }
            while (((ticks() - flip) & 0xFFFF) < due);
            show(half);         // at the next vertical blank
            flip = ticks();
        }
    }
    FRT_TCR = FRT_FAST;
}

int main()
{
    platformInit();
    cpkInit();
    textClear();
    textRow(0, "CINEPAK BENCH measuring...");
    bench();
    mode24();

    while (1)
    {
        play();
        modeText();
        for (int32_t r = 0; r < gRowCount; r++) {
            textRow(r, gRows[r]);
        }
        char text[TEXT_COLUMNS + 1];
        char* p = put(text, "late frames while playing: ");
        putInt(p, gLate, 0);
        textRow(gRowCount, text);
        for (int32_t v = 0; v < 60 * 8; v++) {
            waitVblank();
        }
        mode24();
    }
}
