// Debug overlay (see overlay.h).

#include "overlay.h"
#include "font.h"
#include "hw.h"
#include "screen.h"
#include "system.h"

#include <string.h>

#define PAGE_WIDTH  64      // pattern names a line of the page
#define COLUMNS     (SCREEN_WIDTH / 8)
#define LINES       (SCREEN_HEIGHT / 8)
#define CLEAR_NAME  SCREEN_OVERLAY_NAME0    // cell 0: clear

enum { CLEAR, WHITE, BLACK };   // pixel codes

static_assert(OVERLAY_ROWS + 2 <= LINES && OVERLAY_COLUMNS + 2 <= COLUMNS, "a cell's border all round");

struct Row
{
    char text[OVERLAY_COLUMNS + 1];
    uint8_t align;
};

static Row gShown[OVERLAY_ROWS];    // what the page shows
static Row gNext[OVERLAY_ROWS];     // what this frame gives
static bool gReady;                 // the cells are in VRAM, the page clear

static volatile uint16_t* names(int32_t line)
{
    return (volatile uint16_t*)(VDP2_VRAM + SCREEN_OVERLAY_NAMES) + line * PAGE_WIDTH;
}

// Cell 0 clear, then the font's glyphs white on black.
static void setup()
{
    volatile uint32_t* cells = (volatile uint32_t*)(VDP2_VRAM + SCREEN_OVERLAY_CELLS);
    for (int32_t i = 0; i < 8; i++) {
        *cells++ = 0;
    }
    for (int32_t c = 0; c < FONT8_COUNT; c++)
    {
        for (int32_t y = 0; y < 8; y++)
        {
            uint32_t bits = gFont8[c][y];
            uint32_t line = 0;
            for (int32_t x = 0; x < 8; x++) {
                line = (line << 4) | (((bits >> x) & 1) ? WHITE : BLACK);
            }
            *cells++ = line;
        }
    }

    for (int32_t line = 0; line < LINES; line++)
    {
        volatile uint16_t* n = names(line);
        for (int32_t i = 0; i < PAGE_WIDTH; i++) {
            n[i] = CLEAR_NAME;
        }
    }
    gReady = true;
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

// Rows and columns start a cell in from the screen's edge, inside the TV's
// overscan.
static void drawRow(int32_t row, const Row &r)
{
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

    volatile uint16_t* n = names(row + 1);
    for (int32_t i = 0; i < COLUMNS; i++)
    {
        int32_t k = i - start;
        uint32_t name = CLEAR_NAME;
        if (k >= 0 && k < len)
        {
            int32_t c = uint8_t(r.text[k]) - FONT8_FIRST;
            if (c < 0 || c >= FONT8_COUNT) {
                c = '?' - FONT8_FIRST;
            }
            name = CLEAR_NAME + 1 + c;
        }
        n[i] = name;
    }
}

void overlayFrame()
{
    if (!gReady) {
        setup();
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
