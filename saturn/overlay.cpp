// Debug overlay (see overlay.h).

#include "overlay.h"
#include "font.h"
#include "hw.h"
#include "screen.h"
#include "system.h"

#include <string.h>

#define PITCH       256     // bytes a line of the bitmap
#define LINE_BYTES  (SCREEN_WIDTH / 2)
#define TOP         8       // the first row's top, inside the TV's overscan
#define MARGIN      8       // left and right text's distance from the side
#define ROW_HEIGHT  12      // a row's black band: the text and a line above and below
#define ADVANCE     8       // a character: 6 pixels across, then a gap of 2

enum { CLEAR, WHITE, BLACK };   // pixel codes

struct Row
{
    char text[OVERLAY_COLUMNS + 1];
    uint8_t align;
};

static Row gShown[OVERLAY_ROWS];    // what the bitmap holds
static Row gNext[OVERLAY_ROWS];     // what this frame gives
static uint32_t gPictures = ~0u;    // screenPictures() when the bitmap was last cleared

static volatile uint32_t* bitmapLine(int32_t y)
{
    return (volatile uint32_t*)(VDP2_VRAM + SCREEN_OVERLAY_VRAM + y * PITCH);
}

void overlayRow(int32_t row, const char* text, OverlayAlign align)
{
    if (row < 0 || row >= OVERLAY_ROWS)
        return;
    Row &r = gNext[row];
    strncpy(r.text, text, OVERLAY_COLUMNS);
    r.text[OVERLAY_COLUMNS] = 0;
    r.align = align;
}

// Builds the row's band in work RAM, then copies it over the bitmap's. A
// glyph pixel is a byte: two pixels across, on two lines.
static void drawRow(int32_t row, const Row &r)
{
    static uint32_t band[ROW_HEIGHT][LINE_BYTES / 4];
    memset(band, 0, sizeof(band));
    uint8_t* bytes = (uint8_t*)band;

    int32_t len = strlen(r.text);
    while (len && r.text[len - 1] == ' ') {
        len--;
    }
    if (len)
    {
        int32_t width = len * ADVANCE - 2;
        int32_t x0 = MARGIN;
        if (r.align == OVERLAY_CENTRE) {
            x0 = ((SCREEN_WIDTH - width) / 2) & ~1;
        } else if (r.align == OVERLAY_RIGHT) {
            x0 = SCREEN_WIDTH - MARGIN - width;
        }

        for (int32_t y = 0; y < ROW_HEIGHT; y++) {
            memset(bytes + y * LINE_BYTES + (x0 - 2) / 2, (BLACK << 4) | BLACK, (width + 4) / 2);
        }

        for (int32_t i = 0; i < len; i++)
        {
            const char* g = fontGlyph(r.text[i]);
            if (!g)
                continue;
            uint8_t* p = bytes + LINE_BYTES + (x0 + i * ADVANCE) / 2;
            for (int32_t gy = 0; gy < 5; gy++, p += 2 * LINE_BYTES)
            {
                for (int32_t gx = 0; gx < 3; gx++)
                {
                    if (g[gy * 3 + gx] == '1') {
                        p[gx] = p[gx + LINE_BYTES] = (WHITE << 4) | WHITE;
                    }
                }
            }
        }
    }

    int32_t y0 = TOP + row * ROW_HEIGHT;
    for (int32_t y = 0; y < ROW_HEIGHT; y++)
    {
        volatile uint32_t* dst = bitmapLine(y0 + y);
        for (int32_t i = 0; i < LINE_BYTES / 4; i++) {
            dst[i] = band[y][i];
        }
    }
}

void overlayFrame()
{
    // a picture loaded over the bitmap (and at first, whatever VRAM held)
    if (screenPictures() != gPictures)
    {
        gPictures = screenPictures();
        for (int32_t y = 0; y < SCREEN_HEIGHT; y++)
        {
            volatile uint32_t* dst = bitmapLine(y);
            for (int32_t i = 0; i < LINE_BYTES / 4; i++) {
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
        if (next.align != shown.align || strcmp(next.text, shown.text))
        {
            drawRow(i, next);
            shown = next;
        }
        any |= next.text[0] != 0;
        next.text[0] = 0;
        next.align = OVERLAY_LEFT;
    }
    screenOverlay(any);
}
