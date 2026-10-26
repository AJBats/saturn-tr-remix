// NBG0 is a 512x256 15-bit RGB bitmap in VRAM bank A; pictures load into
// bank B first and are copied into place line by line. NBG3, the debug
// overlay, is set up as libyaul's debug console is (proven on hardware): 8 x 8
// cells of 16 colours, its pattern names read in slot T0 and its cells in
// T4 of bank B1.

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

static_assert(STAGING - VDP2_VRAM + STAGING_SIZE <= SCREEN_OVERLAY_NAMES, "pictures load over the overlay");

#define SPRITE_PRIORITY 0x0606  // as systemInit sets it
#define NBG0_PRIORITY   1       // under the sprites
#define NBG3_PRIORITY   7       // over them
#define OVERLAY_COLORS  768     // the overlay's colour 0 in colour RAM (CRAOFA)
#define OVERLAY_PAGE    0x2000  // a page of 64 x 64 one-word pattern names

static volatile bool gPicture;  // NBG0 shows
static volatile bool gOverlay;  // NBG3 shows

static void showLayers()
{
    VDP2_BGON = (gPicture ? BGON_N0ON : 0) | (gOverlay ? BGON_N3ON : 0);
}

static void showFrames()
{
    VDP2_PRISA = VDP2_PRISB = VDP2_PRISC = VDP2_PRISD = SPRITE_PRIORITY;
}

static void showFramesOnly()
{
    gPicture = false;
    showLayers();
    showFrames();
}

static void hideFrames()
{
    VDP2_PRISA = VDP2_PRISB = VDP2_PRISC = VDP2_PRISD = 0;
}

void screenInit()
{
    hideFrames();       // until the first frame is drawn
    gPicture = gOverlay = false;
    showLayers();

    // A 15-bit bitmap needs four reads per bank it spans; the CPU has the
    // rest, but for the overlay's two in bank B1.
    uint16_t bitmap = (CYCP_N0CG << 12) | (CYCP_N0CG << 8) | (CYCP_N0CG << 4) | CYCP_N0CG;
    uint16_t cpu = (CYCP_CPU << 12) | (CYCP_CPU << 8) | (CYCP_CPU << 4) | CYCP_CPU;
    VDP2_RAMCTL = RAMCTL_VRAMD | RAMCTL_VRBMD;
    VDP2_CYCA0L = VDP2_CYCA1L = bitmap;
    VDP2_CYCA0U = VDP2_CYCA1U = cpu;
    VDP2_CYCB0L = VDP2_CYCB0U = cpu;
    VDP2_CYCB1L = (CYCP_N3PN << 12) | (cpu & 0x0FFF);
    VDP2_CYCB1U = (CYCP_N3CG << 12) | (cpu & 0x0FFF);

    VDP2_CHCTLA = CHCTLA_N0_32K | CHCTLA_N0BMEN;
    VDP2_CHCTLB = 0;    // NBG3: 16 colours, 1 x 1 cells
    VDP2_BMPNA = 0;
    VDP2_MPOFN = 0;     // NBG0's bitmap at VRAM 0
    VDP2_PLSZ = 0;      // planes of one page
    VDP2_PNCN3 = PNCN_1WORD | ((SCREEN_OVERLAY_CELLS / 32) >> 10);  // the cells' number's top bits
    uint16_t page = SCREEN_OVERLAY_NAMES / OVERLAY_PAGE;
    VDP2_MPABN3 = VDP2_MPCDN3 = (page << 8) | page;
    VDP2_CRAOFA = (OVERLAY_COLORS / 256) << 12;
    VDP2_MZCTL = 0;
    VDP2_SCRCTL = 0;
    VDP2_ZMCTL = 0;
    VDP2_WCTLA = 0;
    VDP2_SCXIN0 = VDP2_SCXDN0 = VDP2_SCYIN0 = VDP2_SCYDN0 = 0;
    VDP2_ZMXIN0 = VDP2_ZMYIN0 = 1;
    VDP2_ZMXDN0 = VDP2_ZMYDN0 = 0;
    VDP2_SCXN3 = VDP2_SCYN3 = 0;
    VDP2_PRINA = NBG0_PRIORITY;
    VDP2_PRINB = NBG3_PRIORITY << 8;

    // the overlay's colours: 1 white, 2 black (in colour RAM mode 0, its two
    // halves alike)
    volatile uint16_t* cram = (volatile uint16_t*)VDP2_CRAM + OVERLAY_COLORS;
    for (int32_t half = 0; half < 2; half++)
    {
        cram[half * 1024 + 1] = RGB(31, 31, 31) & 0x7FFF;
        cram[half * 1024 + 2] = RGB(0, 0, 0) & 0x7FFF;
    }
}

void screenOverlay(bool show)
{
    if (gOverlay != show)
    {
        gOverlay = show;
        showLayers();
    }
}


void screenShow(const char* name, int32_t width)
{
    vdp1OnNextFrame(nullptr);
    hideFrames();
    gPicture = gOverlay = false;
    showLayers();

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

    gPicture = true;
    showLayers();
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

    gPicture = true;
    gOverlay = false;
    showLayers();
    return (volatile uint16_t*)VDP2_VRAM;
}

void screenRelease(bool keepPicture)
{
    vdp1WaitIdle();
    vdp1OnNextFrame(keepPicture ? showFrames : showFramesOnly);
}
