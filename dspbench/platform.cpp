// The DSP benchmark's Saturn start-up (see platform.h). The clock change,
// the bitmap layer and the slave's start follow the game's (saturn/system.cpp,
// screen.cpp, slave.cpp), which the Saturn has proven.

#include "platform.h"
#include "font.h"
#include "hw.h"

#define BIOS_SET_UINT       (*(void (**)(uint32_t, void (*)()))0x06000310)  // CPU interrupt vector
#define BIOS_CHG_SYSCK      (*(void (**)(uint32_t))0x06000320)  // 0: 320-dot clock, 1: 352-dot
#define BIOS_SLAVE_ENTRY    (*(volatile uint32_t*)0x06000250)   // where the slave starts after SSHON
#define BIOS_SLAVE_STACK    (*(volatile uint32_t*)0x060002AC)   // its stack, if not 0

#define VECTOR_SLAVE_HBLANK_IN  (0x100 + 0x41)
#define VECTOR_SLAVE_VBLANK_IN  (0x100 + 0x43)

#define SMPC_SSHON      0x02
#define SMPC_SSHOFF     0x03
#define SMPC_SR_NPE     0x20    // an INTBACK holds more peripheral data
#define SMPC_BREAK      0x40    // IREG0: end that INTBACK
#define SMPC_DOTSEL     0x40    // system status 1: the 352-dot clock

#define FRT_TCR         (*(volatile uint8_t*)0xFFFFFE16)
#define FRT_FRCH        (*(volatile uint8_t*)0xFFFFFE12)
#define FRT_FRCL        (*(volatile uint8_t*)0xFFFFFE13)
#define CCR             (*(volatile uint8_t*)0xFFFFFE92)
#define CCR_CE          0x01
#define CCR_CP          0x10

#define SCREEN_WIDTH    352
#define SCREEN_HEIGHT   224
#define PITCH           512     // pixels a line of the bitmap
#define BACK_COLOR_ADDR 0x7FFFE // the last word of VDP2 VRAM
#define WHITE           RGB(31, 31, 31)
#define BLACK           RGB(0, 0, 0)

extern "C" void slaveEntry();       // crt0.s
extern "C" void slaveIgnoreIrq();

uint32_t ticks()
{
    uint32_t sr;
    asm volatile("stc sr, %0" : "=r"(sr));
    asm volatile("ldc %0, sr" : : "r"(sr | 0xF0));
    uint32_t hi = FRT_FRCH;     // latches FRCL
    uint32_t lo = FRT_FRCL;
    asm volatile("ldc %0, sr" : : "r"(sr));
    return (hi << 8) | lo;
}

uint32_t cycles()
{
    static uint32_t high, last;
    uint32_t now = ticks();
    if (now < last) {
        high += 0x10000;
    }
    last = now;
    return (high + now) * TICK_CYCLES;
}

// Must run from the cache-through mirror: the cache is off part of the time.
static void __attribute__((noinline)) cachePurgeBody()
{
    uint8_t ccr = CCR & ~CCR_CE;
    CCR = ccr;
    CCR = ccr | CCR_CP;
    CCR = ccr | CCR_CE;
}

void cachePurge()
{
    uncached(cachePurgeBody)();
}

// --- the SMPC and the clock ---------------------------------------------

static void smpcIdle()
{
    while (SMPC_SF & 1);
    if (SMPC_SR & SMPC_SR_NPE)
    {
        SMPC_IREG(0) = SMPC_BREAK;
        while (SMPC_SF & 1);
    }
}

static void smpcCommand(uint8_t command)
{
    smpcIdle();
    SMPC_SF = 1;
    SMPC_COMREG = command;
    while (SMPC_SF & 1);
}

// INTBACK without peripheral data: system status 1 (SMPC_DOTSEL).
static uint32_t smpcStatus()
{
    smpcIdle();
    SMPC_SF = 1;
    SMPC_IREG(0) = 0x01;
    SMPC_IREG(1) = 0x00;
    SMPC_IREG(2) = 0xF0;
    SMPC_COMREG = 0x10;
    while (SMPC_SF & 1);
    return SMPC_OREG(10);
}

// --- the text page ------------------------------------------------------

static uint32_t vdp2Line()
{
    (void)VDP2_EXTEN;   // latch the counter
    return VDP2_VCNT & 0x1FF;
}

// The display comes on inside a vertical blank (TVMD's DISP may go from 0 to
// 1 only then), which the line counter shows while the display is off.
static void displayOn()
{
    uint32_t polls = 0;
    while (vdp2Line() >= SCREEN_HEIGHT && ++polls < 200000);
    while (vdp2Line() < SCREEN_HEIGHT && ++polls < 200000);
    VDP2_TVMD = TVMD_DISP | TVMD_HRES_352;  // 224 lines, non-interlaced
}

// NBG0: a 512x256 15-bit bitmap in VRAM bank A, the game's layout.
static void vdp2Init()
{
    VDP2_EXTEN = 0;
    VDP2_BGON = 0;
    VDP2_CCCTL = 0;
    VDP2_CLOFEN = 0;
    VDP2_SPCTL = SPCTL_SPCLMD;
    VDP2_PRISA = VDP2_PRISB = VDP2_PRISC = VDP2_PRISD = 0;
    REG16(VDP2_VRAM + BACK_COLOR_ADDR) = BLACK;
    VDP2_BKTAU = (BACK_COLOR_ADDR >> 17) & 7;
    VDP2_BKTAL = (BACK_COLOR_ADDR >> 1) & 0xFFFF;

    uint16_t bitmap = (CYCP_N0CG << 12) | (CYCP_N0CG << 8) | (CYCP_N0CG << 4) | CYCP_N0CG;
    uint16_t cpu = (CYCP_CPU << 12) | (CYCP_CPU << 8) | (CYCP_CPU << 4) | CYCP_CPU;
    VDP2_RAMCTL = RAMCTL_VRAMD | RAMCTL_VRBMD;
    VDP2_CYCA0L = VDP2_CYCA1L = bitmap;
    VDP2_CYCA0U = VDP2_CYCA1U = cpu;
    VDP2_CYCB0L = VDP2_CYCB0U = VDP2_CYCB1L = VDP2_CYCB1U = cpu;
    VDP2_CHCTLA = CHCTLA_N0_32K | CHCTLA_N0BMEN;
    VDP2_BMPNA = 0;
    VDP2_MPOFN = 0;
    VDP2_MZCTL = 0;
    VDP2_SCRCTL = 0;
    VDP2_ZMCTL = 0;
    VDP2_WCTLA = 0;
    VDP2_SCXIN0 = VDP2_SCXDN0 = VDP2_SCYIN0 = VDP2_SCYDN0 = 0;
    VDP2_ZMXIN0 = VDP2_ZMYIN0 = 1;
    VDP2_ZMXDN0 = VDP2_ZMYDN0 = 0;
    VDP2_PRINA = 7;

    textClear();
    VDP2_BGON = BGON_N0ON;
    displayOn();
}

void textClear()
{
    volatile uint32_t* p = (volatile uint32_t*)VDP2_VRAM;
    for (uint32_t i = 0; i < PITCH * 256 / 2; i++) {
        p[i] = 0;
    }
}

// Rows and columns start a character in from the screen's edge.
void textRow(int32_t row, const char* text)
{
    if (row < 0 || row >= TEXT_ROWS)
        return;
    for (int32_t y = 0; y < 8; y++)
    {
        volatile uint16_t* line = (volatile uint16_t*)VDP2_VRAM + ((row + 1) * 8 + y) * PITCH + 8;
        const char* p = text;
        for (int32_t column = 0; column < TEXT_COLUMNS; column++)
        {
            char c = *p ? *p++ : ' ';
            uint32_t bits = (c >= FONT8_FIRST && c < FONT8_FIRST + FONT8_COUNT) ? gFont8[c - FONT8_FIRST][y] : 0;
            for (int32_t x = 0; x < 8; x++) {
                line[column * 8 + x] = (bits >> x) & 1 ? WHITE : BLACK;
            }
        }
    }
}

// --- the slave ------------------------------------------------------------

static void (*volatile gTask)(void*);
static void* volatile gTaskArg;
static volatile bool gStarted;

extern "C" uint8_t gSlaveStack[4096] __attribute__((aligned(16)));
uint8_t gSlaveStack[4096];

extern "C" void slaveMain()
{
    cachePurge();
    FRT_TCR = 0;        // its own timer, a tick every 8 cycles
    *uncached(&gStarted) = true;
    while (1)
    {
        void (*task)(void*) = *uncached(&gTask);
        if (!task)
        {
            // off the bus between polls: an idle slave mustn't disturb the
            // master's measurements
            for (volatile int32_t i = 0; i < 64; i++);
            continue;
        }
        cachePurge();
        task(*uncached(&gTaskArg));
        *uncached(&gTask) = nullptr;
    }
}

static void slaveStart()
{
    smpcCommand(SMPC_SSHOFF);
    BIOS_SET_UINT(VECTOR_SLAVE_HBLANK_IN, slaveIgnoreIrq);
    BIOS_SET_UINT(VECTOR_SLAVE_VBLANK_IN, slaveIgnoreIrq);
    BIOS_SLAVE_ENTRY = uint32_t(slaveEntry);
    BIOS_SLAVE_STACK = uint32_t(gSlaveStack + sizeof(gSlaveStack));
    smpcCommand(SMPC_SSHON);
    for (uint32_t wait = 0; !*uncached(&gStarted) && wait < 0x100000; wait++);
}

void slaveRun(void (*task)(void*), void* arg)
{
    *uncached(&gTaskArg) = arg;
    *uncached(&gTask) = task;
}

void slaveWait()
{
    while (*uncached(&gTask));
}

void platformInit()
{
    // the 352-dot clock, repeated until the SMPC reports it (saturn/system.cpp)
    for (int32_t tries = 0; tries < 4; tries++)
    {
        smpcIdle();
        BIOS_CHG_SYSCK(1);
        if (smpcStatus() & SMPC_DOTSEL)
            break;
    }
    vdp2Init();
    FRT_TCR = 0;        // a tick every 8 cycles
    slaveStart();
}
