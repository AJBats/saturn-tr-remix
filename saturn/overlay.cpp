// Debug overlay (see overlay.h).

#include "overlay.h"
#include "font.h"
#include "hw.h"
#include "screen.h"
#include "system.h"

#include <string.h>

#define PITCH       512     // pixels a line of the bitmap
#define COLUMNS     (SCREEN_WIDTH / 8)
#define LINES       (SCREEN_HEIGHT / 8)
#define WHITE       RGB(31, 31, 31)
#define BLACK       RGB(0, 0, 0)    // 0 is clear

static_assert(OVERLAY_ROWS + 2 <= LINES && OVERLAY_COLUMNS + 2 <= COLUMNS, "a character's border all round");

struct Row
{
    char text[OVERLAY_COLUMNS + 1];
    uint8_t align;
};

static Row gShown[OVERLAY_ROWS];    // what the bitmap shows
static Row gNext[OVERLAY_ROWS];     // what this frame gives
static uint32_t gPictures = ~0u;    // screenPictures() when the bitmap was last cleared

static volatile uint32_t* bitmapLine(int32_t y)
{
    return (volatile uint32_t*)(VDP2_VRAM + y * PITCH * 2);
}

void overlayRow(int32_t row, const char* text, OverlayAlign align)
{
    if (row < 0 || row >= OVERLAY_ROWS)
        return;
    Row &r = gNext[row];
    int32_t i = 0;
    for (; i < OVERLAY_COLUMNS && text[i]; i++) {
        r.text[i] = text[i];
    }
    r.text[i] = 0;
    r.align = align;
}

// Builds the row's 8 lines in work RAM, then copies them over the bitmap's.
// Rows and columns start a character in from the screen's edge, inside the
// TV's overscan.
static void drawRow(int32_t row, const Row &r)
{
    static uint16_t band[8][SCREEN_WIDTH] __attribute__((aligned(4)));
    memset(band, 0, sizeof(band));

    int32_t len = strlen(r.text);
    while (len && r.text[len - 1] == ' ') {
        len--;
    }

    int32_t start = 1;
    if (r.align == OVERLAY_CENTRE) {
        start = (COLUMNS - len) / 2;
    } else if (r.align == OVERLAY_RIGHT) {
        start = COLUMNS - 1 - len;
    }

    for (int32_t k = 0; k < len; k++)
    {
        int32_t c = uint8_t(r.text[k]) - FONT8_FIRST;
        if (c < 0 || c >= FONT8_COUNT) {
            c = '?' - FONT8_FIRST;
        }
        int32_t x0 = (start + k) * 8;
        for (int32_t y = 0; y < 8; y++)
        {
            uint32_t bits = gFont8[c][y];
            for (int32_t x = 0; x < 8; x++) {
                band[y][x0 + x] = ((bits >> x) & 1) ? WHITE : BLACK;
            }
        }
    }

    for (int32_t y = 0; y < 8; y++)
    {
        volatile uint32_t* dst = bitmapLine((row + 1) * 8 + y);
        const uint32_t* src = (const uint32_t*)band[y];
        for (int32_t i = 0; i < SCREEN_WIDTH / 2; i++) {
            dst[i] = src[i];
        }
    }
}

void overlayFrame()
{
    bool free = screenOverlayFree();

    // a picture was in the bitmap: clear it before it shows over the frame
    if (free && screenPictures() != gPictures)
    {
        gPictures = screenPictures();
        for (int32_t y = 0; y < SCREEN_HEIGHT; y++)
        {
            volatile uint32_t* dst = bitmapLine(y);
            for (int32_t i = 0; i < SCREEN_WIDTH / 2; i++) {
                dst[i] = 0;
            }
        }
        memset(gShown, 0, sizeof(gShown));
    }

    bool any = false;
    for (int32_t i = 0; i < OVERLAY_ROWS; i++)
    {
        Row &next = gNext[i];
        Row &shown = gShown[i];
        if (free && (next.align != shown.align || strcmp(next.text, shown.text)))
        {
            drawRow(i, next);
            shown = next;
        }
        any |= next.text[0] != 0;
        next.text[0] = 0;
        next.align = OVERLAY_LEFT;
    }
    screenOverlay(free && any);
}
