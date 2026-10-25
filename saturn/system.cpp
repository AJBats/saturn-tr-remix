// Saturn system setup shared by the game and test programs.

#include "system.h"
#include "hw.h"

#define BACK_COLOR_ADDR 0x7FFFE  // last word of VDP2 VRAM

// BIOS services (function pointers in the BIOS table)
#define BIOS_SET_SINT   (*(void (**)(uint32_t, void (*)()))0x06000300)  // raw interrupt vector
#define BIOS_CHG_SCUIM  (*(void (**)(uint32_t, uint32_t))0x06000344)    // SCU mask = (mask & and) | or

#define VECTOR_VBLANK_IN 0x40
#define SCU_IMS_VBLANK_IN 0x0001

#define FRT_FTCSR   (*(volatile uint8_t*)0xFFFFFE11)
#define FRT_FRCH    (*(volatile uint8_t*)0xFFFFFE12)
#define FRT_FRCL    (*(volatile uint8_t*)0xFFFFFE13)
#define FRT_TCR     (*(volatile uint8_t*)0xFFFFFE16)

#define EDSR_CEF    0x0002  // VDP1 finished drawing the current frame

volatile uint32_t gVblankCount;
FrameStats gStats;

extern "C" void vblankInEntry();   // irq.s: saves registers, calls vblankInHandler

extern "C" void vblankInHandler()
{
    gVblankCount++;
    if (!(VDP1_EDSR & EDSR_CEF)) {
        gStats.vdp1Late++;
    }
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

    VDP2_TVMD = TVMD_DISP;  // 320x224, non-interlaced
}

static void vdp1Init()
{
    VDP1_TVMR = 0;          // 320x224, 16bpp
    VDP1_FBCR = 0;          // swap and erase every field
    VDP1_EWDR = 0;          // erase to transparent
    VDP1_EWLR = 0;
    VDP1_EWRR = (40 << 9) | 223;
    VDP1_PTMR = 2;          // draw at every frame change
}

void systemInit()
{
    vdp2Init();
    vdp1Init();

    FRT_TCR = 2;            // Pphi / 128

    BIOS_SET_SINT(VECTOR_VBLANK_IN, vblankInEntry);
    BIOS_CHG_SCUIM(~SCU_IMS_VBLANK_IN, 0);
}

void waitVblank()
{
    while (VDP2_TVSTAT & TVSTAT_VBLANK);
    while (!(VDP2_TVSTAT & TVSTAT_VBLANK));
}

uint32_t timerTicks()
{
    uint32_t hi = FRT_FRCH;    // reading FRCH latches FRCL
    uint32_t lo = FRT_FRCL;
    return (hi << 8) | lo;
}
