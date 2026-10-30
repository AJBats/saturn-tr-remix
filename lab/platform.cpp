// The lab's Saturn start-up (see platform.h).

#include "platform.h"
#include "font.h"
#include "hw.h"

#define BIOS_CHG_SYSCK      (*(void (**)(uint32_t))0x06000320)  // 0: 320-dot clock, 1: 352-dot
#define BIOS_SET_UINT       (*(void (**)(uint32_t, void (*)()))0x06000310)  // CPU interrupt vector
#define BIOS_SET_SINT       (*(void (**)(uint32_t, void (*)()))0x06000300)  // SCU interrupt, dispatched
#define BIOS_CHG_SCUIM      (*(void (**)(uint32_t, uint32_t))0x06000344)    // SCU mask = (mask & and) | or
#define BIOS_SLAVE_ENTRY    (*(volatile uint32_t*)0x06000250)   // where the slave starts after SSHON
#define BIOS_SLAVE_STACK    (*(volatile uint32_t*)0x060002AC)   // its stack, if not 0

#define VECTOR_SLAVE_HBLANK_IN  (0x100 + 0x41)
#define VECTOR_SLAVE_VBLANK_IN  (0x100 + 0x43)
#define VECTOR_DSP_END          0x45
#define SCU_IMS_ALL             0xBFFF  // every SCU interrupt masked
#define SCU_IMS_DSP_END         0x0020
#define SR_LEVEL_9              0x90    // SR's interrupt mask: level 10 (the DSP's end) and up taken

#define SMPC_SSHON      0x02
#define SMPC_SSHOFF     0x03
#define SMPC_INTBACK    0x10
#define SMPC_SR_NPE     0x20    // an INTBACK holds more peripheral data
#define SMPC_BREAK      0x40    // IREG0: end that INTBACK
#define SMPC_DOTSEL     0x40    // system status 1: the 352-dot clock
#define IREG1_PEN       0x08    // peripheral data, 15-byte mode for both ports
#define PORT_DIRECT     0xF1    // one peripheral, no multitap
#define ID_DIGITAL      0x02    // a standard pad

#define FRT_TCR         (*(volatile uint8_t*)0xFFFFFE16)
#define FRT_FRCH        (*(volatile uint8_t*)0xFFFFFE12)
#define FRT_FRCL        (*(volatile uint8_t*)0xFFFFFE13)
#define FRT_DIV32       1
#define CCR             (*(volatile uint8_t*)0xFFFFFE92)
#define CCR_CE          0x01
#define CCR_CP          0x10

#define EDSR_CEF        0x0002  // VDP1 has drawn the current list
#define FBCR_FCM        0x0002  // manual frame buffer change
#define FBCR_FCT        0x0001  // change at the next field

#define SCREEN_WIDTH    352
#define SCREEN_HEIGHT   224
#define PITCH           512     // pixels a line of the bitmap
#define BACK_COLOR_ADDR 0x7FFFE // the last word of VDP2 VRAM
#define SPRITE_PRIORITY 0x0606  // the frame, as the game shows it
#define TEXT_PRIORITY   7       // the text, over it
#define WHITE           RGB(31, 31, 31)
#define BLACK           RGB(0, 0, 0)    // 0 is clear

extern "C" void slaveEntry();       // crt0.s
extern "C" void slaveIgnoreIrq();
extern "C" void dspEndEntry();

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
    SMPC_COMREG = SMPC_INTBACK;
    while (SMPC_SF & 1);
    return SMPC_OREG(10);
}

// --- VDP2: the frame and the text over it ---------------------------------

static uint32_t vdp2Line()
{
    (void)VDP2_EXTEN;   // latch the counter
    return VDP2_VCNT & 0x1FF;
}

static void waitVblankIn()
{
    while (VDP2_TVSTAT & TVSTAT_VBLANK);
    while (!(VDP2_TVSTAT & TVSTAT_VBLANK));
}

// The line counter counts on through the vertical blank: below the display's
// height, the display has started.
static void waitDisplayStart()
{
    while (vdp2Line() >= SCREEN_HEIGHT);
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

// NBG0: a 512x256 15-bit bitmap in VRAM bank A over VDP1's frame, as the
// game's debug overlay; 0 is clear.
static void vdp2Init()
{
    VDP2_EXTEN = 0;
    VDP2_BGON = 0;
    VDP2_CCCTL = 0;
    VDP2_CLOFEN = 0;
    VDP2_SPCTL = SPCTL_SPCLMD;
    VDP2_PRISA = VDP2_PRISB = VDP2_PRISC = VDP2_PRISD = SPRITE_PRIORITY;
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
    VDP2_PRINA = TEXT_PRIORITY;

    textPage();
    VDP2_BGON = BGON_N0ON;
    displayOn();
}

static bool gPage;      // the text layer covers the frame

static void textFill(uint16_t colour)
{
    uint32_t pair = (uint32_t(colour) << 16) | colour;
    volatile uint32_t* p = (volatile uint32_t*)VDP2_VRAM;
    for (uint32_t i = 0; i < PITCH * 256 / 2; i++) {
        p[i] = pair;
    }
}

void textPage()
{
    gPage = true;
    textFill(BLACK);
}

void textFrame()
{
    gPage = false;
    textFill(0);
}

// Rows and columns start a character in from the screen's edge.
void textRow(int32_t row, const char* text)
{
    if (row < 0 || row >= TEXT_ROWS)
        return;
    bool band = gPage || *text;
    for (int32_t y = 0; y < 8; y++)
    {
        volatile uint16_t* line = (volatile uint16_t*)VDP2_VRAM + ((row + 1) * 8 + y) * PITCH;
        const char* p = text;
        for (int32_t column = 0; column < TEXT_COLUMNS + 2; column++)
        {
            char c = column > 0 && *p ? *p++ : ' ';
            uint32_t bits = (column > 0 && c >= FONT8_FIRST && c < FONT8_FIRST + FONT8_COUNT) ?
                            gFont8[c - FONT8_FIRST][y] : 0;
            for (int32_t x = 0; x < 8; x++) {
                line[column * 8 + x] = (bits >> x) & 1 ? WHITE : band ? BLACK : 0;
            }
        }
    }
}

// --- VDP1 -------------------------------------------------------------------

static void vdp1Init()
{
    VDP1_TVMR = 0;          // 16bpp
    VDP1_PTMR = 0;          // plot only when asked
    waitVblankIn();
    VDP1_FBCR = FBCR_FCM | FBCR_FCT;
    waitDisplayStart();
}

// A plot started in the last lines of the vertical blank is aborted on a
// Saturn and VDP1 locks up; changing frame buffers under a plot does too.
uint32_t vdp1Plot()
{
    waitDisplayStart();
    uint32_t t0 = ticks();
    VDP1_PTMR = 1;
    // EDSR can still hold the last list's end for a moment after a plot starts
    for (int32_t i = 0; i < 1000 && (VDP1_EDSR & EDSR_CEF); i++);
    return t0;
}

bool vdp1ShowDrawn(uint32_t plotted, uint32_t &drawn, uint32_t &wait, uint32_t &vblank)
{
    uint32_t t0 = ticks(), last = t0, elapsed = 0;
    while (!(VDP1_EDSR & EDSR_CEF))
    {
        uint32_t now = ticks();
        elapsed += (now - last) & 0xFFFF;
        last = now;
        if (elapsed * TICK_CYCLES > 28600000 / 4)
            return false;   // a quarter of a second: never ending
    }
    uint32_t t1 = ticks();
    drawn = ticksCycles(plotted, t1);
    wait += (elapsed + ((t1 - last) & 0xFFFF)) * TICK_CYCLES;

    // the frame buffers change at the next field, asked for at a vblank-in
    waitVblankIn();
    VDP1_FBCR = FBCR_FCM | FBCR_FCT;
    waitDisplayStart();
    vblank += ticksCycles(t1, ticks());
    return true;
}

uint32_t vdp1Show()
{
    uint32_t drawn, wait = 0, vblank = 0;
    return vdp1ShowDrawn(vdp1Plot(), drawn, wait, vblank) ? drawn : 0;
}

// --- the SCU's DMA ------------------------------------------------------------

void dmaToVdp1(const void* src, uint32_t vram, uint32_t size)
{
    dmaWait();
    SCU_D0R = uint32_t(src);
    SCU_D0W = VDP1_VRAM + vram;
    SCU_D0C = size;
    SCU_D0AD = 0x101;   // read +4, write +4
    SCU_D0MD = 0x07;    // direct, started by the CPU
    SCU_D0EN = 0x101;   // enabled, started
}

void dmaWait()
{
    while (SCU_DSTA & DSTA_D0_BUSY);
}

// --- the pad ----------------------------------------------------------------

// INTBACK goes out inside the vertical blank, at least 300 us (about 10
// lines) in; the SMPC has the data by the next (saturn/input.cpp).
uint32_t padVblank()
{
    static bool pending;
    static uint32_t buttons;
    waitVblankIn();
    while (vdp2Line() < SCREEN_HEIGHT + 12);
    if (pending && !(SMPC_SF & 1))
    {
        if (SMPC_OREG(0) == PORT_DIRECT && SMPC_OREG(1) == ID_DIGITAL) {
            buttons = ~((SMPC_OREG(2) << 8) | SMPC_OREG(3)) & 0xFFF8;
        } else {
            buttons = 0;
        }
        if (SMPC_SR & SMPC_SR_NPE) {
            SMPC_IREG(0) = SMPC_BREAK;
        }
        pending = false;
    }
    if (!pending && !(SMPC_SF & 1))
    {
        SMPC_SF = 1;
        SMPC_IREG(0) = 0x00;
        SMPC_IREG(1) = IREG1_PEN;
        SMPC_IREG(2) = 0xF0;
        SMPC_COMREG = SMPC_INTBACK;
        pending = true;
    }
    return buttons;
}

// --- the slave --------------------------------------------------------------

static void (*volatile gTask)(void*);
static void* volatile gTaskArg;
static volatile bool gStarted;

extern "C" uint8_t gSlaveStack[4096] __attribute__((aligned(16)));
uint8_t gSlaveStack[4096];

extern "C" void slaveMain()
{
    cachePurge();
    FRT_TCR = FRT_DIV32;
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

bool slaveDone()
{
    return !*uncached(&gTask);
}

// The slave's timer against the master's: the master pings, the slave
// answers with its tick; the answer of the shortest round trip read as
// taken halfway through it.
#define CLOCK_PINGS 64
static volatile uint32_t gPing, gPong, gPongTick;
static uint32_t gSlaveOffset;   // the slave's tick less the master's, 16 bits

static void clockPong(void*)
{
    for (uint32_t k = 1; k <= CLOCK_PINGS; k++)
    {
        while (*uncached(&gPing) != k);
        *uncached(&gPongTick) = ticks();
        *uncached(&gPong) = k;
    }
}

static void clockSync()
{
    slaveRun(clockPong, nullptr);
    uint32_t best = 0x10000;
    for (uint32_t k = 1; k <= CLOCK_PINGS; k++)
    {
        uint32_t t0 = ticks();
        *uncached(&gPing) = k;
        while (*uncached(&gPong) != k);
        uint32_t trip = (ticks() - t0) & 0xFFFF;
        if (trip < best)
        {
            best = trip;
            gSlaveOffset = (*uncached(&gPongTick) - t0 - trip / 2) & 0xFFFF;
        }
    }
    slaveWait();
}

uint32_t slaveTickOnMaster(uint32_t tick)
{
    return (tick - gSlaveOffset) & 0xFFFF;
}

// --- the DSP's ends -----------------------------------------------------------

#define MAX_DSP_ENDS    4
static volatile uint32_t gDspEnds[MAX_DSP_ENDS];
static volatile uint32_t gDspEndCount;

extern "C" void dspEndHandler()
{
    uint32_t n = gDspEndCount;
    if (n < MAX_DSP_ENDS) {
        gDspEnds[n] = ticks();
    }
    gDspEndCount = n + 1;
}

void dspEndsClear()
{
    gDspEndCount = 0;
}

uint32_t dspEnds(uint32_t* ends, uint32_t max)
{
    uint32_t n = gDspEndCount;
    for (uint32_t i = 0; i < n && i < max && i < MAX_DSP_ENDS; i++) {
        ends[i] = gDspEnds[i];
    }
    return n;
}

static void dspEndInit()
{
    BIOS_SET_SINT(VECTOR_DSP_END, dspEndEntry);
    BIOS_CHG_SCUIM(0, SCU_IMS_ALL & ~SCU_IMS_DSP_END);
    uint32_t sr;
    asm volatile("stc sr, %0" : "=r"(sr));
    asm volatile("ldc %0, sr" : : "r"((sr & ~0xF0) | SR_LEVEL_9));
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
    vdp1Init();
    FRT_TCR = FRT_DIV32;
    slaveStart();
    clockSync();
    dspEndInit();
}
