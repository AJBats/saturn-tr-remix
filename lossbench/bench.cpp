// The FMVs' sound on the sound CPU (docs/ps1-fmv.md, "The other route"): a program of its own (make
// lossbench) that runs saturn/fmvsound.68k, the 68000's decoder for the retail Saturn's PCM packed
// losslessly by tools/lossless.py. The clip is the 8 seconds of CANYON's sound that pack worst.
//
// Speed: the clip's first SPEED_CHUNKS chunks decoded by the 68000 over and over for two seconds, with no
// slot reading sound RAM, then with the two the sound plays from: samples a second (both channels) and the
// 68000's cycles a sample (11.2896 MHz). "ok" = the rings hold what tools/lossless.py decodes.
//
// Playing: the whole clip streamed into the packed ring from work RAM as a player would, a little each
// vertical blank, decoded by the 68000 and played by two slots at 22265 Hz. "low" = the fewest samples
// found decoded ahead of the slots while packed chunks waited (below 0: they played what wasn't decoded
// yet); "ok" = the rings at the end hold the clip's last samples as tools/lossless.py has them (the
// predictors carry from chunk to chunk, so a slip anywhere shows there). The clip then plays again and
// again, the numbers on screen.

#include "platform.h"
#include "hw.h"
#include "fmvsound_m68k.h"
#include "clip.h"

static_assert(FMVSOUND_SIZE <= 0xF000 - 256, "fmvsound.68k runs into its stack");

#define SOUND_RAM       0x25A00000
#define SCSP_REG(x)     (*(volatile uint16_t*)(0x25B00000 + (x)))
#define SCSP_SLOT(s, r) SCSP_REG((s) * 0x20 + (r) * 2)
#define SCSP_MIXER      SCSP_REG(0x400)
#define KYONEX          0x1000
#define KYONB           0x0800
#define LPCTL_LOOP      0x0020

// fmvsound.68k's
#define SHARED          0xF000
#define S_CMD           0x00
#define S_CHUNKS        0x02
#define S_PASSES        0x04
#define S_STATE         0x06
#define S_COUNT         0x08
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
#define CLIP            0x40000
#define AHEAD           16384

#define SLOT            24          // the FMV player's (saturn/fmv.cpp)
#define PITCH           ((0xF << 11) | 10)      // octave -1 + 10/1024: 22265 Hz
#define SAMPLES_PER_100_VBL 37146   // at 59.94 vertical blanks a second
#define SOUND_CPU_HZ    11289600

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

static char* putInt(char* p, int32_t v, int32_t width)
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
    for (int32_t i = n; i < width; i++) {
        *p++ = ' ';
    }
    while (n) {
        *p++ = digits[--n];
    }
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

// --- the sound CPU's memory -----------------------------------------------------------

static void waitVblank()
{
    while (VDP2_TVSTAT & TVSTAT_VBLANK);
    while (!(VDP2_TVSTAT & TVSTAT_VBLANK));
}

static volatile uint16_t* word(uint32_t at)
{
    return (volatile uint16_t*)(SOUND_RAM + at);
}

// A long the 68000 writes a word at a time: read until two reads agree.
static uint32_t readLong(uint32_t at)
{
    volatile uint32_t* p = (volatile uint32_t*)(SOUND_RAM + at);
    uint32_t a = *p, b;
    while ((b = *p) != a) {
        a = b;
    }
    return a;
}

static void writeLong(uint32_t at, uint32_t v)
{
    *(volatile uint32_t*)(SOUND_RAM + at) = v;
}

static void copyIn(uint32_t at, const uint8_t* src, uint32_t bytes)
{
    volatile uint16_t* dst = word(at);
    const uint16_t* s = (const uint16_t*)src;
    for (uint32_t i = 0; i < (bytes + 1) / 2; i++) {
        dst[i] = s[i];
    }
}

static uint32_t chunkBytes(uint32_t offset)
{
    return (kClip[offset] << 8) | kClip[offset + 1];
}

static void waitState(uint16_t state)
{
    for (int32_t i = 0; i < 600 && *word(SHARED + S_STATE) != state; i++) {
        waitVblank();
    }
}

// --- the slots -----------------------------------------------------------------

// The two slots over the rings at 22265 Hz: heard (left, right) or only reading (silent).
static void slotsOn(bool heard)
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
        SCSP_SLOT(s, 6) = heard ? 0 : 255;
        SCSP_SLOT(s, 7) = 0;
        SCSP_SLOT(s, 8) = PITCH;
        SCSP_SLOT(s, 9) = 0;
        SCSP_SLOT(s, 10) = 0;
        SCSP_SLOT(s, 11) = heard ? uint16_t((7 << 13) | pan[c]) : 0;
    }
    SCSP_SLOT(SLOT, 0) = SCSP_SLOT(SLOT, 0) | KYONB;
    SCSP_SLOT(SLOT + 1, 0) = SCSP_SLOT(SLOT + 1, 0) | KYONB | KYONEX;
}

static void slotsOff()
{
    SCSP_SLOT(SLOT, 0) = SCSP_SLOT(SLOT, 0) & ~(KYONB | KYONEX);
    SCSP_SLOT(SLOT + 1, 0) = (SCSP_SLOT(SLOT + 1, 0) & ~KYONB) | KYONEX;
}

// --- speed ---------------------------------------------------------------------

#define SPEED_VBLANKS   120

static uint32_t ringSum(uint32_t ring)
{
    volatile uint16_t* r = word(ring);
    uint32_t sum = 0;
    for (uint32_t i = 0; i < RING_SAMPLES; i++) {
        sum += uint32_t(r[i]) * (i + 1);
    }
    return sum;
}

static void speed(const char* what, bool slots)
{
    if (slots) {
        slotsOn(false);
    }
    *word(SHARED + S_CMD) = 1;
    waitState(1);
    for (int32_t v = 0; v < 4; v++) {
        waitVblank();
    }
    uint32_t c0 = readLong(SHARED + S_COUNT);
    for (int32_t v = 0; v < SPEED_VBLANKS; v++) {
        waitVblank();
    }
    uint32_t c1 = readLong(SHARED + S_COUNT);
    *word(SHARED + S_CMD) = 0;
    waitState(0);
    if (slots) {
        slotsOff();
    }
    uint32_t perSecond = uint32_t(uint64_t(c1 - c0) * 5994 / (SPEED_VBLANKS * 100));
    bool ok = ringSum(RING_L) == SPEED_SUM_LEFT && ringSum(RING_R) == SPEED_SUM_RIGHT;

    char text[80];
    char* p = put(text, what);
    p = putInt(p, int32_t(perSecond), 6);
    p = put(p, " smp/s ");
    p = putInt(p, int32_t(perSecond ? SOUND_CPU_HZ / perSecond : 0), 0);
    put(p, ok ? " cyc ok" : " cyc BAD");
    add(text);
}

// --- playing -------------------------------------------------------------------

struct Play
{
    int32_t low;
    uint32_t fullest;
    uint32_t vblanks;
    bool ok;            // the rings at the end hold what tools/lossless.py says
};

// The clip played once: fed into the packed ring as there is room, a vertical blank at a time.
static Play play()
{
    Play r = {};
    writeLong(SHARED + S_WR, 0);
    writeLong(SHARED + S_PLAY, 0);
    *word(SHARED + S_CMD) = 2;
    waitState(2);

    uint32_t chunk = 0, offset = 0, wr = 0;
    auto feed = [&]() {
        uint32_t rd = readLong(SHARED + S_RD);
        while (chunk < CLIP_CHUNKS)
        {
            uint32_t bytes = chunkBytes(offset);
            uint32_t pos = wr & (CRING_SIZE - 1);
            uint32_t need = bytes + (pos + bytes > CRING_SIZE ? CRING_SIZE - pos : 0);
            if (wr + need - rd > CRING_SIZE)
                break;
            if (pos + bytes > CRING_SIZE)
            {
                *word(CRING + pos) = 0;         // go on from the ring's start
                wr += CRING_SIZE - pos;
                pos = 0;
            }
            copyIn(CRING + pos, kClip + offset, bytes);
            wr += bytes;
            offset += bytes;
            chunk++;
        }
        writeLong(SHARED + S_WR, wr);
        uint32_t full = wr - rd;
        r.fullest = full > r.fullest ? full : r.fullest;
    };

    // decoded AHEAD before the slots start
    for (int32_t v = 0; v < 600; v++)
    {
        feed();
        if (readLong(SHARED + S_DECODED) >= AHEAD)
            break;
        waitVblank();
    }
    slotsOn(true);
    const uint32_t samples = CLIP_CHUNKS * 1024;
    uint32_t n = 0, played = 0;
    while (played < samples)
    {
        waitVblank();
        n++;
        played = n * SAMPLES_PER_100_VBL / 100;
        writeLong(SHARED + S_PLAY, played);
        feed();
    }
    slotsOff();
    r.low = int32_t(readLong(SHARED + S_LOW));
    r.vblanks = n;
    *word(SHARED + S_CMD) = 0;
    waitState(0);
    r.ok = ringSum(RING_L) == PLAY_SUM_LEFT && ringSum(RING_R) == PLAY_SUM_RIGHT;
    return r;
}

int main()
{
    platformInit();
    textClear();
    textRow(0, "FMV SOUND BENCH, about 20 seconds...");

    soundCpu(false);
    SCSP_MIXER = (1 << 9) | 0xF;        // 4 Mbit sound RAM, full volume
    for (int32_t s = 0; s < 32; s++) {
        SCSP_SLOT(s, 0) = 0;
    }
    SCSP_SLOT(0, 0) = KYONEX;
    copyIn(0, fmvsound, FMVSOUND_SIZE);
    for (uint32_t i = 0; i < 0x100; i += 2) {
        *word(SHARED + i) = 0;
    }
    uint32_t speedBytes = 0;
    for (int32_t c = 0; c < SPEED_CHUNKS; c++) {
        speedBytes += chunkBytes(speedBytes);
    }
    copyIn(CLIP, kClip, speedBytes);
    *word(SHARED + S_CHUNKS) = SPEED_CHUNKS;
    soundCpu(true);

    add("FMV SOUND ON THE 68000 (docs/ps1-fmv.md)");
    char text[80];
    char* p = put(text, "clip ");
    p = putInt(p, CLIP_BYTES * 15 / 1024 * 22254 / (CLIP_CHUNKS * 1024) / 15, 0);
    p = put(p, " KB/s packed; needs ");
    p = putInt(p, 22265 * 2, 0);
    put(p, " smp/s");
    add(text);
    speed("speed, idle  ", false);
    showRows();
    speed("speed, 2 slots", true);
    showRows();

    Play first = play();
    p = put(text, "played: low ");
    p = putInt(p, first.low, 0);
    p = put(p, " ahead, ring ");
    p = putInt(p, int32_t(first.fullest / 1024), 0);
    put(p, first.ok ? " KB ok" : " KB BAD");
    add(text);
    add("playing the clip again and again");
    showRows();
    for (int32_t round = 1;; round++)
    {
        Play again = play();
        char row[80];
        char* q = put(row, "round ");
        q = putInt(q, round, 0);
        q = put(q, ": low ");
        q = putInt(q, again.low, 0);
        q = put(q, ", vblanks ");
        q = putInt(q, int32_t(again.vblanks), 0);
        put(q, again.ok ? " ok" : " BAD");
        textRow(gRowCount, row);
    }
}
