// NBG0 is a 512x256 15-bit RGB bitmap in VRAM bank A; pictures load into
// bank B first and are copied into place line by line.

#include "screen.h"
#include "cd.h"
#include "crash.h"
#include "hw.h"
#include "system.h"

#include <string.h>

#define BITMAP_WIDTH    512
#define BITMAP_HEIGHT   256
#define PICTURE_HEIGHT  224
#define STAGING         (VDP2_VRAM + 0x40000)
#define STAGING_SIZE    (BITMAP_WIDTH * PICTURE_HEIGHT * 2)

#define SPRITE_PRIORITY 0x0606  // as systemInit sets it
#define NBG0_PRIORITY   1       // under the sprites

static void showFrames()
{
    VDP2_PRISA = VDP2_PRISB = VDP2_PRISC = VDP2_PRISD = SPRITE_PRIORITY;
}

static void showFramesOnly()
{
    VDP2_BGON = 0;
    showFrames();
}

static void hideFrames()
{
    VDP2_PRISA = VDP2_PRISB = VDP2_PRISC = VDP2_PRISD = 0;
}

void screenInit()
{
    hideFrames();       // until the first frame is drawn
    VDP2_BGON = 0;

    // A 15-bit bitmap needs four reads per bank it spans; the CPU has the rest.
    uint16_t bitmap = (CYCP_N0CG << 12) | (CYCP_N0CG << 8) | (CYCP_N0CG << 4) | CYCP_N0CG;
    uint16_t cpu = (CYCP_CPU << 12) | (CYCP_CPU << 8) | (CYCP_CPU << 4) | CYCP_CPU;
    VDP2_RAMCTL = RAMCTL_VRAMD | RAMCTL_VRBMD;
    VDP2_CYCA0L = VDP2_CYCA1L = bitmap;
    VDP2_CYCA0U = VDP2_CYCA1U = cpu;
    VDP2_CYCB0L = VDP2_CYCB0U = VDP2_CYCB1L = VDP2_CYCB1U = cpu;

    VDP2_CHCTLA = CHCTLA_N0_32K | CHCTLA_N0BMEN;
    VDP2_BMPNA = 0;
    VDP2_MPOFN = 0;     // bitmap at VRAM 0
    VDP2_MZCTL = 0;
    VDP2_SCRCTL = 0;
    VDP2_ZMCTL = 0;
    VDP2_WCTLA = 0;
    VDP2_SCXIN0 = VDP2_SCXDN0 = VDP2_SCYIN0 = VDP2_SCYDN0 = 0;
    VDP2_ZMXIN0 = VDP2_ZMYIN0 = 1;
    VDP2_ZMXDN0 = VDP2_ZMYDN0 = 0;
    VDP2_PRINA = NBG0_PRIORITY;
}

void screenShow(const char* name, int32_t width)
{
    vdp1OnNextFrame(nullptr);
    hideFrames();
    VDP2_BGON = 0;

    char path[32] = "BINDATA/";
    strcat(path, name);
    strcat(path, ".BIN");
    if (cdLoadFile(path, (void*)STAGING, STAGING_SIZE) != width * PICTURE_HEIGHT * 2)
        return;

    // centred on the 352x240 screen; 0 is transparent, showing the back colour
    uint32_t left = (SCREEN_WIDTH - width) / 2;
    uint32_t top = (SCREEN_HEIGHT - PICTURE_HEIGHT) / 2;
    volatile uint32_t* dst = (volatile uint32_t*)VDP2_VRAM;
    const volatile uint32_t* src = (const volatile uint32_t*)STAGING;

    for (uint32_t y = 0; y < BITMAP_HEIGHT; y++)
    {
        uint32_t x = 0;
        if (y >= top && y < top + PICTURE_HEIGHT)
        {
            for (; x < left / 2; x++) {
                *dst++ = 0;
            }
            for (; x < (left + width) / 2; x++) {
                *dst++ = *src++;
            }
        }
        for (; x < BITMAP_WIDTH / 2; x++) {
            *dst++ = 0;
        }
    }

    VDP2_BGON = BGON_N0ON;
    crashLogShow();
}

volatile uint16_t* screenBlank()
{
    vdp1OnNextFrame(nullptr);
    hideFrames();

    volatile uint32_t* dst = (volatile uint32_t*)VDP2_VRAM;
    for (uint32_t i = 0; i < BITMAP_WIDTH * BITMAP_HEIGHT / 2; i++) {
        dst[i] = 0;
    }

    VDP2_BGON = BGON_N0ON;
    return (volatile uint16_t*)VDP2_VRAM;
}

void screenRelease(bool keepPicture)
{
    vdp1WaitIdle();
    vdp1OnNextFrame(keepPicture ? showFrames : showFramesOnly);
}
