// NBG0 is a 512x256 15-bit RGB bitmap in VRAM bank A; pictures load into
// bank B first and are copied into place line by line. During play, between
// pictures, the debug overlay draws into the same bitmap, shown over the
// frame: on a Saturn, layers read from bank B showed only garbage.
//
// VDP2's colour offsets darken the layers: A the bitmap, B VDP1's frames,
// each fading at the vblanks (0 as drawn, -255 black). A level with a plane
// (plane.h) has three layers to two offsets, and the plane's fog holds one down
// by its constant: there A is the plane, B the bitmap, each on the bitmap's fade.

#include "screen.h"
#include "cd.h"
#include "crash.h"
#include "hw.h"
#include "plane.h"
#include "system.h"

#include <string.h>

#define BITMAP_WIDTH    512
#define BITMAP_HEIGHT   256
#define PICTURE_HEIGHT  224
#define STAGING         (VDP2_VRAM + 0x40000)
#define STAGING_SIZE    (BITMAP_WIDTH * PICTURE_HEIGHT * 2)

#define SPRITE_PRIORITY 0x0606  // as systemInit sets it
#define PICTURE_PRIORITY 2      // under the sprites, over the plane
#define OVERLAY_PRIORITY 7      // over them

#define PICTURE_DIM     -64     // the paused scene behind the inventory, as the original's

static volatile bool gPicture;  // NBG0 holds a picture, and shows
static volatile bool gOverlay;  // NBG0 is the overlay, and shows
static bool gCaptured;          // the picture is the frame screenCapture took, till released
static bool gPlaneLevel;        // the level has a plane
static volatile bool gPlaneShown;
static uint32_t gPictures;

// The colour offsets' fades, the bitmap's and the frames': each one's offset
// << 16, its step a vblank, its target and the vblanks left.
enum { FADE_PICTURE, FADE_FRAMES };
static volatile int32_t gFadeValue[2];
static volatile int32_t gFadeStep[2];
static volatile int32_t gFadeTarget[2];
static volatile uint32_t gFadeLeft[2];

static void fadeWrite()
{
    int32_t picture = gFadeValue[FADE_PICTURE] >> 16;
    uint16_t a = uint16_t(gPlaneLevel ? picture + PLANE_OFFSET : picture) & 0x1FF;
    uint16_t b = uint16_t(gPlaneLevel ? picture : gFadeValue[FADE_FRAMES] >> 16) & 0x1FF;
    VDP2_COAR = VDP2_COAG = VDP2_COAB = a;
    VDP2_COBR = VDP2_COBG = VDP2_COBB = b;
}

static void fadeVblank()
{
    bool changed = false;
    for (uint32_t i = 0; i < 2; i++)
    {
        uint32_t left = gFadeLeft[i];
        if (left)
        {
            gFadeValue[i] = left == 1 ? gFadeTarget[i] << 16 : gFadeValue[i] + gFadeStep[i];
            gFadeLeft[i] = left - 1;
            changed = true;
        }
    }
    if (changed) {
        fadeWrite();
    }
}

// The layer's offset from where it stands to target, over that many vblanks
// (0: at once). From the vblank-out interrupt too.
static void fade(uint32_t layer, int32_t target, uint32_t vblanks)
{
    gFadeLeft[layer] = 0;
    gFadeTarget[layer] = target;
    if (vblanks)
    {
        gFadeStep[layer] = ((target << 16) - gFadeValue[layer]) / int32_t(vblanks);
        gFadeLeft[layer] = vblanks;
    }
    else
    {
        gFadeValue[layer] = target << 16;
        fadeWrite();
    }
}

static void fadesClear()
{
    fade(FADE_PICTURE, 0, 0);
    fade(FADE_FRAMES, 0, 0);
}

static void showLayers()
{
    VDP2_PRINA = gPicture ? PICTURE_PRIORITY : OVERLAY_PRIORITY;
    VDP2_BGON = ((gPicture || gOverlay) ? BGON_N0ON : 0) | (gPlaneShown ? BGON_R0ON | BGON_R0TPON : 0);
}

static void showFrames()
{
    VDP2_PRISA = VDP2_PRISB = VDP2_PRISC = VDP2_PRISD = SPRITE_PRIORITY;
}

static void showFramesOnly()
{
    gPicture = false;
    gPlaneShown = gPlaneLevel;
    showLayers();
    showFrames();
    fadesClear();
}

static void hideFrames()
{
    VDP2_PRISA = VDP2_PRISB = VDP2_PRISC = VDP2_PRISD = 0;
}

void screenFramesHidden(bool hidden)
{
    if (hidden) {
        hideFrames();
    } else {
        showFrames();
    }
}

void screenPlaneLevel(bool on)
{
    gPlaneLevel = on;
    VDP2_CLOFSL = on ? CLOF_N0 : CLOF_SP;
    VDP2_CLOFEN = on ? CLOF_N0 | CLOF_R0 : CLOF_N0 | CLOF_SP;
    fadeWrite();
}

int32_t screenFramesFade()
{
    return gFadeValue[FADE_FRAMES] >> 16;
}

// The bitmap is about to take a picture.
static void takeBitmap()
{
    gPicture = gOverlay = gCaptured = false;
    gPictures++;
    showLayers();
    fadesClear();
}

// ... that fills the screen, and may stage in bank B: no plane.
static void takeScreen()
{
    gPlaneShown = false;
    showLayers();
    planeOff();
    screenPlaneLevel(false);
    takeBitmap();
}

void screenInit()
{
    hideFrames();       // until the first frame is drawn
    gPicture = gOverlay = false;
    showLayers();

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

    fadesClear();
    VDP2_CLOFSL = CLOF_SP;
    VDP2_CLOFEN = CLOF_N0 | CLOF_SP;
    onVblank(fadeVblank);
}

void screenShow(const char* name, int32_t width)
{
    vdp1OnNextFrame(nullptr);
    hideFrames();
    takeScreen();

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

// The frames back over the picture, fading in.
static void showCapturedFrames()
{
    showFrames();
    fade(FADE_FRAMES, 0, 5);
}

void screenCapture(void* scratch, uint32_t scratchSize)
{
    vdp1WaitDrawn();
    takeBitmap();

    // whole lines at a time (the frame buffer and the bitmap are both 512
    // pixels a line), through the scratch by the SCU's DMA
    uint32_t lineBytes = BITMAP_WIDTH * 2;
    uint32_t lines = scratchSize / lineBytes;
    for (uint32_t y = 0; y < SCREEN_HEIGHT; y += lines)
    {
        uint32_t size = (y + lines < SCREEN_HEIGHT ? lines : SCREEN_HEIGHT - y) * lineBytes;
        dmaCopy((const void*)(VDP1_FB + y * lineBytes), uint32_t(scratch), size);
        dmaCopy(scratch, VDP2_VRAM + y * lineBytes, size);
    }
    dmaWait();

    // the picture in place of the frame on the screen, which is the same,
    // till the frames come back over it
    gPicture = true;
    showLayers();
    hideFrames();
    fade(FADE_PICTURE, PICTURE_DIM, 15);
    fade(FADE_FRAMES, PICTURE_DIM, 0);
    vdp1Release();
    vdp1WaitIdle();
    vdp1OnNextFrame(showCapturedFrames);
    gCaptured = true;
}

bool screenCaptured()
{
    return gCaptured;
}

void screenClosing()
{
    if (!gPicture)
        return;
    fade(FADE_FRAMES, PICTURE_DIM, 30);
    fade(FADE_PICTURE, 0, 30);
}

volatile uint16_t* screenBlank()
{
    vdp1OnNextFrame(nullptr);
    hideFrames();
    takeScreen();

    volatile uint32_t* dst = (volatile uint32_t*)VDP2_VRAM;
    for (uint32_t i = 0; i < BITMAP_WIDTH * BITMAP_HEIGHT / 2; i++) {
        dst[i] = 0;
    }

    gPicture = true;
    showLayers();
    return (volatile uint16_t*)VDP2_VRAM;
}

void screenRelease(bool keepPicture)
{
    gCaptured = false;
    vdp1WaitIdle();
    vdp1OnNextFrame(keepPicture ? showFrames : showFramesOnly);
}

bool screenOverlayFree()
{
    return !gPicture;
}

void screenOverlay(bool show)
{
    if (gPicture || gOverlay == show)
        return;
    gOverlay = show;
    showLayers();
}

uint32_t screenPictures()
{
    return gPictures;
}
