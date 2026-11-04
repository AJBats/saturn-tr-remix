// Saturn system setup shared by the game and test programs.

#include "system.h"
#include "platform.h"
#include "crash.h"
#include "hw.h"
#include "vram.h"
#include "trace.h"

#define BACK_COLOR_ADDR 0x7FFFE  // last word of VDP2 VRAM

// BIOS services (function pointers in the BIOS table)
#define BIOS_SET_SINT   (*(void (**)(uint32_t, void (*)()))0x06000300)  // raw interrupt vector
#define BIOS_CHG_SCUIM  (*(void (**)(uint32_t, uint32_t))0x06000344)    // SCU mask = (mask & and) | or
#define BIOS_CHG_SYSCK  (*(void (**)(uint32_t))0x06000320)  // 0: 320-dot clock, 1: 352-dot
#define BIOS_SYSCK      (*(volatile uint32_t*)0x06000324)   // current clock setting

#define VECTOR_VBLANK_IN    0x40
#define VECTOR_VBLANK_OUT   0x41
#define VECTOR_SPRITE_END   0x4D
#define VECTOR_TIMER0       0x43
#define VECTOR_DSP_END      0x45
#define SCU_IMS_VBLANK_IN   0x0001
#define SCU_IMS_VBLANK_OUT  0x0002
#define SCU_IMS_SPRITE_END  0x2000
#define SCU_IMS_TIMER0      0x0008
#define SCU_IMS_DSP_END     0x0020
#define T1MD_TENB           0x0001  // the timers run
#define LINE_LEVEL          0xC0    // timer 0's interrupt level, in SR's I bits

#define FRT_FTCSR   (*(volatile uint8_t*)0xFFFFFE11)
#define FRT_FRCH    (*(volatile uint8_t*)0xFFFFFE12)
#define FRT_FRCL    (*(volatile uint8_t*)0xFFFFFE13)
#define FRT_TCR     (*(volatile uint8_t*)0xFFFFFE16)

#define EDSR_CEF    0x0002  // VDP1 finished drawing the current frame
#define FBCR_FCM    0x0002  // manual frame buffer change
#define FBCR_FCT    0x0001  // change frame buffers at the next field
#define TVMR_VBE    0x0008  // the erase sweeps the buffer on screen in the vblank
#define EWRR_FRAME  ((SCREEN_WIDTH / 8) << 9 | (SCREEN_HEIGHT - 1))
#define SWEEP_LINES 185     // its reach at 352 x 224, the Saturn's: about 65,000 pixels a vblank

// VDP1 frame sequencing (after libyaul's variable-interval mode): a plot is
// started once the command list is uploaded; at the vblank-in after VDP1
// finishes, or as it finishes if that is inside a vertical blank, a frame
// buffer change is requested; at the first line of the display the change
// happens and VDP1 can take the next list. The Saturn takes the request up to
// the vertical blank's last line (make vdp1bench). A frame stays on screen for
// two fields at least, the game's 30 Hz tick: a list drawn early waits, or lists
// ending either side of a change's deadline would show for one field and three.
//
// On hardware the vblank-out interrupt and the vblank flag come a few lines
// before that line: a plot started then is aborted and VDP1 locks up, never
// ending the list (Mednafen holds the flag until line 0, so it can't show).
//
// The next list may be uploaded as soon as VDP1 has drawn the last one, before
// the change: its plot then waits for the vblank-out (gPlotQueued), and the
// CPUs go on with the frame after.
enum Vdp1State
{
    VDP1_IDLE,
    VDP1_PLOTTING,
    VDP1_CHANGING
};

static volatile int32_t gVdp1State;
static volatile bool gVdp1Drawn = true;    // the last plot has finished
static volatile bool gPlotQueued;          // a plot waits for the change
static volatile bool gHoldNext;            // the next vdp1Plot's frame is to stay in the draw buffer
static volatile bool gHoldQueued;          // and that plot is the one queued
static volatile bool gHolding;             // this plot's does, until vdp1Release
static uint32_t gPlotStart;
static void (*volatile gOnNextFrame)();
static void (*volatile gOnShown)(uint32_t plot);
static volatile uint32_t gFieldsShown = 2; // vblank-ins since the last change
static volatile uint32_t gPlotsAsked;      // vdp1Plot calls
static volatile uint32_t gPlotsStarted;    // plots started: the one drawing or last drawn
static void (*volatile gOnLine)();
static void (*volatile gOnVblank)();
static uint32_t gSmpcCommands;
static volatile bool gEraseFrames;  // vdp1EraseFrames
static volatile bool gSweepAsked;   // TVMR's VBE is on, for this change
static volatile bool gSwept;        // the buffer the next list draws on was swept
static volatile bool gOpeningNext;  // the next plot's list opens with the polygon
#ifdef HW_LOG
static volatile EraseCounts gEraseCounts;
#endif

volatile uint32_t gVblankCount;
FrameStats gStats;

#ifdef HW_LOG
static SyncStats gSync = { 0xFFFF, 0, 0, 0xFFFF, 0 };

SyncStats syncStatsTake()
{
    uint32_t sr;
    asm volatile("stc sr, %0" : "=r"(sr));
    asm volatile("ldc %0, sr" : : "r"(sr | 0xF0));
    SyncStats s = gSync;
    gSync.least = gSync.statAnd = 0xFFFF;
    gSync.most = gSync.statOr = 0;
    asm volatile("ldc %0, sr" : : "r"(sr));
    return s;
}
#endif

// irq.s entries: save registers, call the handlers below
extern "C" void vblankInEntry();
extern "C" void vblankOutEntry();
extern "C" void spriteEndEntry();
extern "C" void dspEndEntry();
extern "C" void lineEntry();

#define MIN_FIELDS_SHOWN    2

// The last list's frame buffers changed, the buffer on screen erased if sweep
// and the erase is on. The write to VDP1 waits for an SCU DMA on the bus (a list's
// upload), so the line is read after it: asked past vblank-in the sweep doesn't
// start, though the buffers still change.
static void changeAsk(bool sweep)
{
    gSwept = false;
    if (sweep && gEraseFrames)
    {
        VDP1_TVMR = TVMR_VBE;
        gSweepAsked = true;
        gSwept = vdp2Line() <= SCREEN_HEIGHT;
#ifdef HW_LOG
        if (!gSwept) {
            gEraseCounts.tooLate++;
        }
#endif
    }
    VDP1_FBCR = FBCR_FCM | FBCR_FCT;
    gVdp1State = VDP1_CHANGING;
    traceBegin(TRACE_IRQ, TR_CHANGE);
}

// ... if it has been on screen long enough.
static void changeRequest(bool sweep)
{
    if (gFieldsShown >= MIN_FIELDS_SHOWN) {
        changeAsk(sweep);
    }
}

extern "C" void vblankInHandler(const uint32_t* irqStack)
{
    gVblankCount++;
    gFieldsShown++;
    traceBegin(TRACE_IRQ, TR_IRQ_VBLANK_IN, gVblankCount);
    crashWatchdog(irqStack);
#ifdef HW_LOG
    static uint32_t last;
    uint32_t now = timerTicks();
    uint32_t field = (now - last) & 0xFFFF;
    last = now;
    gSync.least = field < gSync.least ? field : gSync.least;
    gSync.most = field > gSync.most ? field : gSync.most;
    uint32_t status = VDP2_TVSTAT;
    gSync.statOr |= status;
    gSync.statAnd &= status;
#endif

    if (gVdp1State == VDP1_PLOTTING && !gHolding)
    {
        // CEF can still hold the last list's end for a moment after a plot
        // starts, and changing frame buffers under a plot locks VDP1 up (on
        // hardware): this plot's sprite-end interrupt must have come too
        if (gVdp1Drawn && (VDP1_EDSR & EDSR_CEF))
        {
            changeRequest(true);
        }
        else
        {
            gStats.vdp1Late++;
        }
    }
    if (gOnVblank) {
        gOnVblank();
    }
    traceEnd(TRACE_IRQ, TR_IRQ_VBLANK_IN);
}

// The frame buffer change is complete by then.
static void waitDisplayStart()
{
    while (vdp2Line() >= SCREEN_HEIGHT);
}

// From the vblank-out interrupt or the master's flush: lane is the trace's.
static void plotStart(int32_t lane, bool hold)
{
    if (gOpeningNext)
    {
        gOpeningNext = false;
        const uint16_t top = gSwept ? SWEEP_LINES : 0;
        REG16(VDP1_VRAM + VRAM_FRAME_CMDS + 0x0E) = top;    // CMDYA
        REG16(VDP1_VRAM + VRAM_FRAME_CMDS + 0x12) = top;    // CMDYB
#ifdef HW_LOG
        if (gSwept) {
            gEraseCounts.swept++;
        } else {
            gEraseCounts.unswept++;
        }
#endif
    }
    gPlotsStarted++;
    gPlotStart = timerTicks();
    traceAt(lane, gPlotStart, (TR_PLOT << 8) | (gPlotsStarted & 0xFF));
    gVdp1Drawn = false;
    gVdp1State = VDP1_PLOTTING;
    gHolding = hold;
    VDP1_PTMR = 1;
}

extern "C" void vblankOutHandler()
{
    traceBegin(TRACE_IRQ, TR_IRQ_VBLANK_OUT);
    if (gSweepAsked)
    {
        gSweepAsked = false;
        VDP1_TVMR = 0;
    }
    bool changed = gVdp1State == VDP1_CHANGING;
    uint32_t shown = gPlotsStarted;
    if (changed)
    {
#ifdef HW_LOG
        gSync.edsr = VDP1_EDSR;     // the change done: the last list's end now BEF
#endif
        waitDisplayStart();
        gVdp1State = VDP1_IDLE;
        gFieldsShown = 0;
        traceBegin(TRACE_IRQ, TR_SHOWN, shown);
    }

    // a plot queued for the change (or just after it) starts before the
    // callbacks: a list that fills most of two fields has little time to
    // lose, and a late start misses the next change by a whole field
    if (gVdp1State == VDP1_IDLE && gPlotQueued)
    {
        waitDisplayStart();
        gPlotQueued = false;
        plotStart(TRACE_IRQ, gHoldQueued);
    }

    if (changed)
    {
        if (gOnShown) {
            gOnShown(shown);
        }

        if (gOnNextFrame)
        {
            gOnNextFrame();
            gOnNextFrame = nullptr;
        }
    }
    traceEnd(TRACE_IRQ, TR_IRQ_VBLANK_OUT);
}

extern "C" void lineHandler()
{
    traceBegin(TRACE_IRQ, TR_IRQ_LINE);
    gOnLine();
    traceEnd(TRACE_IRQ, TR_IRQ_LINE);
}

extern "C" void spriteEndHandler()
{
    gStats.vdp1EndLine = vdp2Line();
    uint32_t t = timerTicks();
    gStats.vdp1Ticks = (t - gPlotStart) & 0xFFFF;
    traceAt(TRACE_IRQ, t, TR_DRAWN << 8);
    gVdp1Drawn = true;

    // past this vertical blank's vblank-in, in time for its change: with the
    // interrupts off, so that the vblank-out handler sees the request or the
    // flag is already down. Before it, with the coming vertical blank the list's
    // change and the erase on: asked now, before the master, let go by this
    // interrupt, starts the next list's upload and holds up the request.
    uint32_t sr;
    asm volatile("stc sr, %0" : "=r"(sr));
    asm volatile("ldc %0, sr" : : "r"(sr | 0xF0));
    if (gVdp1State == VDP1_PLOTTING && !gHolding)
    {
        if (VDP2_TVSTAT & TVSTAT_VBLANK) {
            changeRequest(false);
        } else if (gEraseFrames && gFieldsShown + 1 >= MIN_FIELDS_SHOWN && vdp2Line() < SCREEN_HEIGHT) {
            changeAsk(true);
        }
    }
    asm volatile("ldc %0, sr" : : "r"(sr));
}

void vdp1EraseFrames(bool on)
{
    gEraseFrames = on;
}

void vdp1OpeningPolygonNext()
{
    gOpeningNext = true;
}

#ifdef HW_LOG
EraseCounts vdp1EraseTake()
{
    EraseCounts c;
    c.swept = gEraseCounts.swept;
    c.unswept = gEraseCounts.unswept;
    c.tooLate = gEraseCounts.tooLate;
    gEraseCounts.swept = gEraseCounts.unswept = gEraseCounts.tooLate = 0;
    return c;
}
#endif

void vdp1WaitIdle()
{
    while (gVdp1State != VDP1_IDLE || gPlotQueued);
}

void vdp1WaitDrawn()
{
    while (!vdp1Drawn());
}

bool vdp1Drawn()
{
    return !gPlotQueued && gVdp1Drawn;
}

uint32_t vdp1Plots()
{
    return gPlotsAsked;
}

void vdp1OnShown(void (*fn)(uint32_t plot))
{
    gOnShown = fn;
}

void vdp1Plot()
{
    gPlotsAsked++;
    bool hold = gHoldNext;
    gHoldNext = false;
    if (gVdp1State == VDP1_IDLE)
    {
        plotStart(TRACE_MASTER, hold);
    }
    else
    {
        gHoldQueued = hold;
        gPlotQueued = true;
    }
}

void vdp1OnNextFrame(void (*fn)())
{
    gOnNextFrame = fn;
}

void vdp1Hold()
{
    gHoldNext = true;
}

void vdp1Release()
{
    gHolding = false;
}

void onVblank(void (*fn)())
{
    gOnVblank = fn;
}

uint32_t vdp1DebugState()
{
    return (uint32_t(gVdp1State) << 24) | (uint32_t(gPlotQueued) << 16) | (uint32_t(gVdp1Drawn) << 8);
}

// The display comes on inside a vertical blank, as the VDP2 manual has it
// (TVMD's DISP may go from 0 to 1 only then). TVSTAT's VBLANK bit reads 1
// while the display is off, so the line counter says where the beam is. Log
// line TVMD: the line << 16 | dot it was written at (bit 31: the counter
// never got there, written anyway).
#define DISPLAY_POLLS   200000

static void displayOn()
{
    uint32_t polls = 0;
    while (vdp2Line() >= SCREEN_HEIGHT && ++polls < DISPLAY_POLLS);
    while (vdp2Line() < SCREEN_HEIGHT && ++polls < DISPLAY_POLLS);
    (void)VDP2_EXTEN;   // latch the counters
    VDP2_TVMD = TVMD_DISP | TVMD_HRES_352; // 224 lines, non-interlaced, as the original
    crashLog("TVMD", (polls >= DISPLAY_POLLS ? 0x80000000u : 0) | (uint32_t(VDP2_VCNT & 0x1FF) << 16) |
                     (VDP2_HCNT & 0x3FF));
}

// The BIOS's clock change left VDP2 as at power on, with the display off at
// 352 dots (TVMD 0x0001): the 320/352 switch is the BIOS's alone (in
// horizontal retrace), so TVMD is written only to turn the display on.
static void vdp2Init()
{
    VDP2_EXTEN = 0;
    VDP2_BGON = 0;
    VDP2_CCCTL = 0;
    VDP2_CLOFEN = 0;
    VDP2_SPCTL = SPCTL_SPCLMD;
    VDP2_PRISA = VDP2_PRISB = VDP2_PRISC = VDP2_PRISD = 0x0606;

    vdp2SetBackColor(RGB(0, 0, 0));
    VDP2_BKTAU = (BACK_COLOR_ADDR >> 17) & 7;
    VDP2_BKTAL = (BACK_COLOR_ADDR >> 1) & 0xFFFF;

    displayOn();
}

void vdp2SetBackColor(uint16_t color)
{
    REG16(VDP2_VRAM + BACK_COLOR_ADDR) = color;
}

static void vdp1Init()
{
    VDP1_TVMR = 0;          // 16bpp
    VDP1_PTMR = 0;          // plot only when asked
    VDP1_EWDR = 0;          // the erase: colour 0, the whole frame
    VDP1_EWLR = 0;
    VDP1_EWRR = EWRR_FRAME;

    waitVblank();
    VDP1_FBCR = FBCR_FCM | FBCR_FCT;
    waitDisplayStart();
    gVdp1State = VDP1_IDLE;
}

#define SMPC_SR_NPE     0x20    // an INTBACK holds more peripheral data
#define SMPC_BREAK      0x40    // IREG0: end that INTBACK
#define SMPC_DOTSEL     0x40    // system status 1: the 352-dot clock

// Lets a running command finish, and ends an INTBACK still holding peripheral
// data: the SMPC takes no other command meanwhile.
static void smpcIdle()
{
    while (SMPC_SF & 1);
    if (SMPC_SR & SMPC_SR_NPE)
    {
        SMPC_IREG(0) = SMPC_BREAK;
        while (SMPC_SF & 1);
    }
}

// The pads' reads issue INTBACK from the line interrupt (onLine): it is held
// off meanwhile.
void smpcCommand(uint8_t command)
{
    uint32_t sr;
    asm volatile("stc sr, %0" : "=r"(sr));
    if ((sr & 0xF0) < LINE_LEVEL) {
        asm volatile("ldc %0, sr" : : "r"((sr & ~0xF0) | LINE_LEVEL));
    }
    smpcIdle();
    gSmpcCommands++;
    SMPC_SF = 1;
    SMPC_COMREG = command;
    while (SMPC_SF & 1);
    asm volatile("ldc %0, sr" : : "r"(sr));
}

uint32_t smpcCommands()
{
    return gSmpcCommands;
}

void onLine(uint32_t line, void (*fn)())
{
    gOnLine = fn;
    SCU_T0C = line + 1;     // timer 0 counts the lines' hblanks from the display's first
    SCU_T1MD = T1MD_TENB;
    BIOS_SET_SINT(VECTOR_TIMER0, lineEntry);
    BIOS_CHG_SCUIM(~SCU_IMS_TIMER0, 0);
}

// The SMPC's status (INTBACK without peripheral data, which needs no vblank:
// just after a clock change VDP2 is reset and has none): the area code << 8 |
// system status 1 (SMPC_DOTSEL).
static uint32_t smpcStatus()
{
    smpcIdle();
    SMPC_SF = 1;
    SMPC_IREG(0) = 0x01;
    SMPC_IREG(1) = 0x00;
    SMPC_IREG(2) = 0xF0;
    SMPC_COMREG = 0x10;     // INTBACK
    while (SMPC_SF & 1);
    return (SMPC_OREG(9) << 8) | SMPC_OREG(10);
}

#ifdef HW_LOG
static uint32_t rtcSecond()
{
    smpcStatus();
    return SMPC_OREG(7);    // BCD
}

static uint32_t vblanksPerRtc()
{
    uint32_t start = gVblankCount;
    uint32_t s = rtcSecond();
    while (rtcSecond() == s) {
        if (gVblankCount - start > 180)
            return 0;
    }
    start = gVblankCount;
    for (int32_t i = 0; i < 2; i++)
    {
        s = rtcSecond();
        while (rtcSecond() == s) {
            if (gVblankCount - start > 300)
                return 0;
        }
    }
    return gVblankCount - start;
}
#endif

void systemInit()
{
    // 352x240; the 352-dot clock also runs the CPUs 6.6% faster. The BIOS
    // handles the SMPC's NMI and resets the VDPs. On a Saturn booted from an
    // optical drive emulator's menu the change didn't always take (VDP2 then
    // runs 352-dot timing on the 320-dot clock: 56 Hz, and the TV can't hold
    // the picture), so the SMPC is left idle first, and the change repeats
    // until the SMPC reports the 352-dot clock.
    crashLog("SYSCK", BIOS_SYSCK);
    uint32_t status = smpcStatus();
    crashLog("CLOCK0", status);
    uint32_t tries = 0;
    do
    {
        smpcIdle();
        BIOS_CHG_SYSCK(1);
        tries++;
        status = smpcStatus();
    } while (!(status & SMPC_DOTSEL) && tries < 4);
    crashLog("CLOCK", (tries << 16) | status);

    vdp2Init();
    vdp1Init();

    FRT_TCR = 2;            // Pphi / 128

    BIOS_SET_SINT(VECTOR_VBLANK_IN, vblankInEntry);
    BIOS_SET_SINT(VECTOR_VBLANK_OUT, vblankOutEntry);
    BIOS_SET_SINT(VECTOR_SPRITE_END, spriteEndEntry);
    BIOS_SET_SINT(VECTOR_DSP_END, dspEndEntry);
    BIOS_CHG_SCUIM(~(SCU_IMS_VBLANK_IN | SCU_IMS_VBLANK_OUT | SCU_IMS_SPRITE_END | SCU_IMS_DSP_END), 0);

#ifdef HW_LOG
    // vblanks over two seconds of the SMPC's clock, which runs off its own
    // crystal: 120 at the 352-dot clock's 59.8 Hz, 112 had the system clock
    // stayed at the 320-dot rate under 352-dot timing (0: the clock never
    // ticked)
    crashLog("VBRTC", vblanksPerRtc());
#endif
}

void waitVblank()
{
    while (VDP2_TVSTAT & TVSTAT_VBLANK);
    while (!(VDP2_TVSTAT & TVSTAT_VBLANK));
}

uint32_t vdp2Line()
{
    (void)VDP2_EXTEN;   // latch the counter
    return VDP2_VCNT & 0x1FF;
}

// Reading FRCH latches FRCL for the next read: an interrupt reading the
// counter in between would leave its own low byte there, so none may.
uint32_t timerTicks()
{
    uint32_t sr;
    asm volatile("stc sr, %0" : "=r"(sr));
    asm volatile("ldc %0, sr" : : "r"(sr | 0xF0));
    uint32_t hi = FRT_FRCH;
    uint32_t lo = FRT_FRCL;
    asm volatile("ldc %0, sr" : : "r"(sr));
    return (hi << 8) | lo;
}

void dmaCopy(const void* src, uint32_t dst, uint32_t size)
{
    dmaWait();
    SCU_D0R = uint32_t(src);
    SCU_D0W = dst;
    SCU_D0C = size;
    // read +4; write +4, the B-bus's in 16-bit units: +2 a unit there
    SCU_D0AD = (dst & 0x0F000000) == 0x06000000 ? 0x102 : 0x101;
    SCU_D0MD = 0x07;    // direct mode, start on CPU request
    SCU_D0EN = 0x101;   // enable + start
}

void dmaWait()
{
    while (SCU_DSTA & DSTA_D0_BUSY);
}

// --- the renderer's platform (renderer/platform.h) ---------------------------

uint32_t ticks()
{
    return timerTicks();
}

uint32_t ticksCycles(uint32_t t0, uint32_t t1)
{
    return ((t1 - t0) & 0xFFFF) * 128;
}

void dmaToVdp1(const void* src, uint32_t vram, uint32_t size)
{
    dmaCopy(src, VDP1_VRAM + vram, size);
}

// The DSP's ends: a renderer program ending with ENDI raises the SCU's DSP
// end interrupt, which notes the master's tick.
#define MAX_DSP_ENDS    4
static volatile uint32_t gDspEnds[MAX_DSP_ENDS];
static volatile uint32_t gDspEndCount;

extern "C" void dspEndHandler()
{
    uint32_t n = gDspEndCount;
    if (n < MAX_DSP_ENDS) {
        gDspEnds[n] = timerTicks();
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
