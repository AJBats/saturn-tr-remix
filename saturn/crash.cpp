// Crash screen (see crash.h). On the left, a column of 8-digit hex numbers;
// on the right, the log's last lines.
//
// red (master) or magenta (slave) exception:
//   0  vector: 4 illegal instruction, 6 illegal slot, 9 address error, 10 DMA address error
//   1  PC     2  SR     3  PR     4  SP
//
// blue (the game loop stood still):
//   0  the master's PC where it stood    1  its PR
//   2  frames drawn    3  vblanks
//   4  VDP1: state << 24 | plot queued << 16 | drawn << 8 (vdp1DebugState)
//   5  VDP1 EDSR << 16 | COPR (the command it is on, address / 8)
//   6  the slave's task (0: idle)

#include "crash.h"
#include "hw.h"
#include "screen.h"
#include "slave.h"
#include "system.h"

#define BIOS_SET_UINT   (*(void (**)(uint32_t, void (*)()))0x06000310)  // CPU interrupt vector
#define SLAVE_VECTOR    0x100   // BIOS_SET_UINT: the slave's table

#define STILL_FRAMES    (60 * 6)

// Where the vblank-in interrupt finds the interrupted context: words up its
// stack (irq.s and the BIOS dispatcher's saves; measured in Mednafen, which
// runs the real BIOS).
#define IRQ_STACK_PC    23
#define IRQ_STACK_PR    13

extern "C" void crashIllegalEntry();
extern "C" void crashSlotEntry();
extern "C" void crashAddressEntry();
extern "C" void crashDmaEntry();
extern "C" void crashSlaveIllegalEntry();
extern "C" void crashSlaveSlotEntry();
extern "C" void crashSlaveAddressEntry();
extern "C" void crashSlaveDmaEntry();

static volatile bool gCrashed;  // a screen is up: leave it
static bool gHold = true;       // until the first level has loaded

#define LOG_LINES   16

static struct { const char* label; uint32_t value; } gLog[LOG_LINES];
static int32_t gLogCount;

// 3 x 5 pixels, rows from the top
static const char* const kDigits[16] = {
    "111101101101111", "010110010010111", "111001111100111", "111001111001111",
    "101101111001001", "111100111001111", "111100111101111", "111001001001001",
    "111101111101111", "111101111001111", "010101111101101", "110101110101110",
    "011100100100011", "110101101101110", "111100110100111", "111100110100100"
};
static const char* const kLetters[26] = {
    "010101111101101", "110101110101110", "011100100100011", "110101101101110",
    "111100110100111", "111100110100100", "011100101101011", "101101111101101",
    "111010010010111", "001001001101010", "101101110101101", "100100100100111",
    "101111111101101", "110101101101101", "010101101101010", "110101110100100",
    "010101101110011", "110101110101101", "011100010001110", "111010010010010",
    "101101101101111", "101101101101010", "101101111111101", "101101010101101",
    "101101010010010", "111001010100111"
};

static const char* glyph(char c)
{
    if (c >= '0' && c <= '9')
        return kDigits[c - '0'];
    if (c >= 'A' && c <= 'Z')
        return kLetters[c - 'A'];
    return nullptr;
}

#define BITMAP_PITCH    512

static void character(volatile uint16_t* bitmap, int32_t x0, int32_t y0, int32_t scale, const char* g)
{
    for (int32_t y = 0; y < 5 * scale; y++)
    {
        for (int32_t x = 0; x < 3 * scale; x++)
        {
            if (g[(y / scale) * 3 + x / scale] == '1') {
                bitmap[(y0 + y) * BITMAP_PITCH + x0 + x] = RGB(31, 31, 31);
            }
        }
    }
}

// Text and an 8-digit hex number from x0; returns the width
static int32_t text(volatile uint16_t* bitmap, int32_t x0, int32_t y0, int32_t scale, const char* label, bool hex, uint32_t value)
{
    int32_t x = x0;
    for (const char* p = label; *p; p++, x += 4 * scale)
    {
        const char* g = glyph(*p);
        if (g) {
            character(bitmap, x, y0, scale, g);
        }
    }
    if (hex)
    {
        if (*label) {
            x += 4 * scale;
        }
        for (int32_t d = 0; d < 8; d++, x += 4 * scale) {
            character(bitmap, x, y0, scale, kDigits[(value >> (28 - d * 4)) & 15]);
        }
    }
    return x - x0;
}

static void logLines(volatile uint16_t* bitmap, int32_t x0, int32_t y0, bool box)
{
    for (int32_t i = 0; i < gLogCount; i++)
    {
        int32_t y = y0 + i * 12;
        if (box)
        {
            for (int32_t r = -2; r < 12; r++) {
                for (int32_t x = x0 - 2; x < x0 + 120; x++) {
                    bitmap[(y + r) * BITMAP_PITCH + x] = RGB(0, 0, 0);
                }
            }
        }
        text(bitmap, x0, y, 2, gLog[i].label, true, gLog[i].value);
    }
}

void crashLog(const char* label, uint32_t value)
{
    if (gLogCount == LOG_LINES)
    {
        for (int32_t i = 1; i < LOG_LINES; i++) {
            gLog[i - 1] = gLog[i];
        }
        gLogCount--;
    }
    gLog[gLogCount].label = label;
    gLog[gLogCount].value = value;
    gLogCount++;

#ifdef HW_LOG
    crashLogShow();
#endif
}

void crashLogShow()
{
#ifdef HW_LOG
    if (!gCrashed) {
        logLines((volatile uint16_t*)VDP2_VRAM, 24, 16, true);
    }
#endif
}

static volatile uint16_t* screen(uint16_t color)
{
    gCrashed = true;
    volatile uint16_t* bitmap = screenBlank();
    for (int32_t i = 0; i < BITMAP_PITCH * SCREEN_HEIGHT; i++) {
        bitmap[i] = color;
    }
    logLines(bitmap, 192, 32, false);
    return bitmap;
}

static void number(volatile uint16_t* bitmap, int32_t row, uint32_t value)
{
    text(bitmap, 48, 32 + row * 24, 3, "", true, value);
}

// irq.s: the CPU's exception frame (PC, SR), its PR, the vector and the CPU
extern "C" void crashException(const uint32_t* frame, uint32_t pr, int32_t vector, int32_t slave)
{
    asm volatile("ldc %0, sr" : : "r"(0xF0)); // no more interrupts

    volatile uint16_t* bitmap = screen(slave ? RGB(20, 0, 20) : RGB(20, 0, 0));
    number(bitmap, 0, vector);
    number(bitmap, 1, frame[0]);
    number(bitmap, 2, frame[1]);
    number(bitmap, 3, pr);
    number(bitmap, 4, uint32_t(frame));

    while (1);
}

void crashInit()
{
    BIOS_SET_UINT(4, crashIllegalEntry);
    BIOS_SET_UINT(6, crashSlotEntry);
    BIOS_SET_UINT(9, crashAddressEntry);
    BIOS_SET_UINT(10, crashDmaEntry);
    BIOS_SET_UINT(SLAVE_VECTOR + 4, crashSlaveIllegalEntry);
    BIOS_SET_UINT(SLAVE_VECTOR + 6, crashSlaveSlotEntry);
    BIOS_SET_UINT(SLAVE_VECTOR + 9, crashSlaveAddressEntry);
    BIOS_SET_UINT(SLAVE_VECTOR + 10, crashSlaveDmaEntry);

#ifdef HW_LOG
    screenBlank();
    crashLogShow();
#endif
}

void crashHold(bool hold)
{
    gHold = hold;
}

void crashWatchdog(const uint32_t* irqStack)
{
    static uint32_t frames;
    static uint32_t still;

    if (gCrashed || gHold || gStats.frames != frames)
    {
        frames = gStats.frames;
        still = 0;
        return;
    }

    if (++still < STILL_FRAMES)
        return;

    volatile uint16_t* bitmap = screen(RGB(0, 0, 20));
    number(bitmap, 0, irqStack[IRQ_STACK_PC]);
    number(bitmap, 1, irqStack[IRQ_STACK_PR]);
    number(bitmap, 2, gStats.frames);
    number(bitmap, 3, gVblankCount);
    number(bitmap, 4, vdp1DebugState());
    number(bitmap, 5, (uint32_t(VDP1_EDSR) << 16) | VDP1_COPR);
    number(bitmap, 6, uint32_t(slaveTask()));
}
