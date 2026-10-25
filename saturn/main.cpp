// Saturn platform entry: hardware setup, then OpenLara's game loop.

#include "game.h"
#include "hw.h"

#define BACK_COLOR_ADDR 0x7FFFE  // last word of VDP2 VRAM

int32 fps;

int32 osGetSystemTimeMS()
{
    return 0; // TODO FRT based timer
}

bool osSaveSettings()
{
    return false;
}

bool osLoadSettings()
{
    return false;
}

bool osCheckSave()
{
    return false;
}

bool osSaveGame()
{
    return false;
}

bool osLoadGame()
{
    return false;
}

void osJoyVibrate(int32 index, int32 L, int32 R)
{
}

void osSetPalette(const uint16* palette)
{
}

const void* osLoadScreen(LevelID id)
{
    return NULL;
}

const void* osLoadLevel(LevelID id)
{
    return NULL; // TODO load from CD
}

void updateInput()
{
    keys = 0; // TODO SMPC pad
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

static void waitVblank()
{
    while (VDP2_TVSTAT & TVSTAT_VBLANK);
    while (!(VDP2_TVSTAT & TVSTAT_VBLANK));
}

int main()
{
    vdp2Init();
    vdp1Init();

    gameInit();

    while (1)
    {
        updateInput();

        gameUpdate(1);

        gameRender();

        // game logic runs at 30 Hz
        waitVblank();
        renderSwap();
        waitVblank();
    }

    return 0;
}
