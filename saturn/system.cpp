// Saturn system setup shared by the game and test programs.

#include "system.h"
#include "crash.h"
#include "hw.h"

#define BACK_COLOR_ADDR 0x7FFFE  // last word of VDP2 VRAM

// BIOS services (function pointers in the BIOS table)
#define BIOS_SET_SINT   (*(void (**)(uint32_t, void (*)()))0x06000300)  // raw interrupt vector
#define BIOS_CHG_SCUIM  (*(void (**)(uint32_t, uint32_t))0x06000344)    // SCU mask = (mask & and) | or
#define BIOS_CHG_SYSCK  (*(void (**)(uint32_t))0x06000320)  // 0: 320-dot clock, 1: 352-dot
#define BIOS_SYSCK      (*(volatile uint32_t*)0x06000324)   // current clock setting

#define VECTOR_VBLANK_IN    0x40
#define VECTOR_VBLANK_OUT   0x41
#define VECTOR_SPRITE_END   0x4D
#define SCU_IMS_VBLANK_IN   0x0001
#define SCU_IMS_VBLANK_OUT  0x0002
#define SCU_IMS_SPRITE_END  0x2000

#define FRT_FTCSR   (*(volatile uint8_t*)0xFFFFFE11)
#define FRT_FRCH    (*(volatile uint8_t*)0xFFFFFE12)
#define FRT_FRCL    (*(volatile uint8_t*)0xFFFFFE13)
#define FRT_TCR     (*(volatile uint8_t*)0xFFFFFE16)

#define EDSR_CEF    0x0002  // VDP1 finished drawing the current frame
#define FBCR_FCM    0x0002  // manual frame buffer change
#define FBCR_FCT    0x0001  // change frame buffers at the next field

// VDP1 frame sequencing (after libyaul's variable-interval mode): a plot is
// started once the command list is uploaded; at the vblank-in after VDP1
// finishes, a frame buffer change is requested with vblank erase on; at
// vblank-out the change has happened and VDP1 can take the next list.
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
static uint32_t gPlotStart;
static void (*volatile gOnNextFrame)();

volatile uint32_t gVblankCount;
FrameStats gStats;

// irq.s entries: save registers, call the handlers below
extern "C" void vblankInEntry();
extern "C" void vblankOutEntry();
extern "C" void spriteEndEntry();

extern "C" void vblankInHandler(const uint32_t* irqStack)
{
    gVblankCount++;
    crashWatchdog(irqStack);

    if (gVdp1State == VDP1_PLOTTING)
    {
        if (VDP1_EDSR & EDSR_CEF)
        {
            VDP1_TVMR = 0;          // no vblank erase: each frame starts with a clear
            VDP1_FBCR = FBCR_FCM | FBCR_FCT;
            gVdp1State = VDP1_CHANGING;
        }
        else
        {
            gStats.vdp1Late++;
        }
    }
}

static void plotStart()
{
    gPlotStart = timerTicks();
    gVdp1Drawn = false;
    gVdp1State = VDP1_PLOTTING;
    VDP1_PTMR = 1;
}

extern "C" void vblankOutHandler()
{
    if (gVdp1State == VDP1_CHANGING)
    {
        VDP1_TVMR = 0;
        // a plot started before the change completes would lock VDP1 up
        while (VDP2_TVSTAT & TVSTAT_VBLANK);
        gVdp1State = VDP1_IDLE;

        if (gOnNextFrame)
        {
            gOnNextFrame();
            gOnNextFrame = nullptr;
        }
    }

    // also a plot queued just after the change
    if (gVdp1State == VDP1_IDLE && gPlotQueued)
    {
        gPlotQueued = false;
        plotStart();
    }
}

extern "C" void spriteEndHandler()
{
    gStats.vdp1EndLine = vdp2Line();
    gStats.vdp1Ticks = (timerTicks() - gPlotStart) & 0xFFFF;
    gVdp1Drawn = true;
}

void vdp1WaitIdle()
{
    while (gVdp1State != VDP1_IDLE || gPlotQueued);
}

void vdp1WaitDrawn()
{
    while (gPlotQueued || !gVdp1Drawn);
}

void vdp1Plot()
{
    if (gVdp1State == VDP1_IDLE) {
        plotStart();
    } else {
        gPlotQueued = true;
    }
}

void vdp1OnNextFrame(void (*fn)())
{
    gOnNextFrame = fn;
}

uint32_t vdp1DebugState()
{
    return (uint32_t(gVdp1State) << 24) | (uint32_t(gPlotQueued) << 16) | (uint32_t(gVdp1Drawn) << 8);
}

static void vdp2Init()
{
    VDP2_TVMD = 0;
    VDP2_BGON = 0;
    VDP2_CCCTL = 0;
    VDP2_CLOFEN = 0;
    VDP2_SPCTL = SPCTL_SPCLMD;
    VDP2_PRISA = VDP2_PRISB = VDP2_PRISC = VDP2_PRISD = 0x0606;

    REG16(VDP2_VRAM + BACK_COLOR_ADDR) = RGB(0, 0, 0);
    VDP2_BKTAU = (BACK_COLOR_ADDR >> 17) & 7;
    VDP2_BKTAL = (BACK_COLOR_ADDR >> 1) & 0xFFFF;

    VDP2_TVMD = TVMD_DISP | TVMD_VRES_240 | TVMD_HRES_352; // non-interlaced
}

static void vdp1Init()
{
    VDP1_TVMR = 0;          // 16bpp
    VDP1_EWDR = 0;          // erase to transparent
    VDP1_EWLR = 0;
    VDP1_EWRR = ((SCREEN_WIDTH / 8) << 9) | (SCREEN_HEIGHT - 1);
    VDP1_PTMR = 0;          // plot only when asked

    waitVblank();
    VDP1_FBCR = FBCR_FCM | FBCR_FCT;
    while (VDP2_TVSTAT & TVSTAT_VBLANK);
    gVdp1State = VDP1_IDLE;
}

void smpcCommand(uint8_t command)
{
    while (SMPC_SF & 1);
    SMPC_SF = 1;
    SMPC_COMREG = command;
    while (SMPC_SF & 1);
}

// The SMPC's status (INTBACK without peripheral data): the area code << 8 |
// system status 1, whose bit 6 (DOTSEL) is set on the 352-dot clock.
static uint32_t smpcStatus()
{
    waitVblank();
    while (SMPC_SF & 1);
    SMPC_SF = 1;
    SMPC_IREG(0) = 0x01;
    SMPC_IREG(1) = 0x00;
    SMPC_IREG(2) = 0xF0;
    SMPC_COMREG = 0x10;     // INTBACK
    while (SMPC_SF & 1);
    return (SMPC_OREG(9) << 8) | SMPC_OREG(10);
}

void systemInit()
{
    // 352x240; the 352-dot clock also runs the CPUs 6.6% faster. The BIOS
    // handles the SMPC's NMI and resets the VDPs. Its record of the clock
    // (BIOS_SYSCK) can be stale: booted from an optical drive emulator's menu
    // it read 1 on the 320-dot clock, and the TV couldn't hold the picture.
    crashLog("SYSCK", BIOS_SYSCK);
    BIOS_CHG_SYSCK(1);

    vdp2Init();
    vdp1Init();
    crashLog("SMPC", smpcStatus());

    FRT_TCR = 2;            // Pphi / 128

    BIOS_SET_SINT(VECTOR_VBLANK_IN, vblankInEntry);
    BIOS_SET_SINT(VECTOR_VBLANK_OUT, vblankOutEntry);
    BIOS_SET_SINT(VECTOR_SPRITE_END, spriteEndEntry);
    BIOS_CHG_SCUIM(~(SCU_IMS_VBLANK_IN | SCU_IMS_VBLANK_OUT | SCU_IMS_SPRITE_END), 0);
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

uint32_t timerTicks()
{
    uint32_t hi = FRT_FRCH;    // reading FRCH latches FRCL
    uint32_t lo = FRT_FRCL;
    return (hi << 8) | lo;
}

void dmaCopy(const void* src, uint32_t dst, uint32_t size)
{
    dmaWait();
    SCU_D0R = uint32_t(src);
    SCU_D0W = dst;
    SCU_D0C = size;
    SCU_D0AD = 0x101;   // read +4, write +4
    SCU_D0MD = 0x07;    // direct mode, start on CPU request
    SCU_D0EN = 0x101;   // enable + start
}

void dmaWait()
{
    while (SCU_DSTA & DSTA_D0_BUSY);
}
