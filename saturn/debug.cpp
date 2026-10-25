// The debug pad (port 2). Mednafen keys in brackets:
//   START [Tab]       opens and closes the menu
//   up/down [arrows]  pick an option, left/right change it
//   A [P], B [H], C [G]  step the palette, view and textures options
// Changes show for a moment at the bottom of the screen.

#include "debug.h"
#include "common.h"
#include "detail.h"
#include "input.h"
#include "palettes.h"
#include "render.h"
#include "system.h"

struct Option
{
    const char* name;
    const char* const* values;
    int32 count;
    int32 (*get)();
    void (*set)(int32 value);
};

static const char* const kTextures[] = { "Full res", "Saturn LOD" };
static const char* const kCurves[] = { "Saturn", "Soft", "Softer" };
static const char* const kDark[] = { "Add", "Half luminance" };
static const char* const kPalette[] = { "Saturn", "Remastered" };
static const char* const kViews[] = { "Normal", "Resolution", "Texel size" };
static const char* const kOnOff[] = { "Off", "On" };

static int32 gShadeCurve;
static bool gShowStats;

static const Option gOptions[] = {
    { "Textures", kTextures, 2,
        [] { return int32(gSaturnLod); },
        [](int32 v) { gSaturnLod = v; detailSetWhole(!v, level.textures, level.texturesCount); } },
    { "Shading", kCurves, SHADE_CURVE_COUNT,
        [] { return gShadeCurve; },
        [](int32 v) { gShadeCurve = v; renderSetShadeCurve(v); } },
    { "Dark faces", kDark, 2,
        [] { return int32(gShadeHalf); },
        [](int32 v) { gShadeHalf = v; } },
    { "Palette", kPalette, 2,
        [] { return int32(palettesRemastered()); },
        [](int32 v) { if (v != palettesRemastered()) palettesToggle(); } },
    { "View", kViews, TEXEL_VIEW_COUNT,
        [] { return gTexelView; },
        [](int32 v) { gTexelView = v; } },
    { "Stats", kOnOff, 2,
        [] { return int32(gShowStats); },
        [](int32 v) { gShowStats = v; } }
};

enum { OPTION_TEXTURES, OPTION_SHADING, OPTION_DARK, OPTION_PALETTE, OPTION_VIEW, OPTION_STATS, OPTION_COUNT };

static uint16 gPad;
static bool gMenuOpen;
static int32 gCursor;

static const char* gMessage;
static const char* gMessageName;
static uint32 gMessageEnd;

static void step(int32 index, int32 delta)
{
    const Option &o = gOptions[index];
    int32 v = (o.get() + delta + o.count) % o.count;
    o.set(v);

    gMessageName = o.name;
    gMessage = o.values[v];
    gMessageEnd = gVblankCount + 120;
}

void debugUpdate()
{
    uint16 pad = padDebug();
    uint16 pressed = pad & ~gPad;
    gPad = pad;

    if (pressed & PAD_START) {
        gMenuOpen = !gMenuOpen;
    }

    if (gMenuOpen)
    {
        if (pressed & PAD_UP) {
            gCursor = (gCursor + OPTION_COUNT - 1) % OPTION_COUNT;
        }
        if (pressed & PAD_DOWN) {
            gCursor = (gCursor + 1) % OPTION_COUNT;
        }
        if (pressed & PAD_LEFT) {
            step(gCursor, -1);
        }
        if (pressed & PAD_RIGHT) {
            step(gCursor, 1);
        }
    }

    if (pressed & PAD_A) {
        step(OPTION_PALETTE, 1);
    }
    if (pressed & PAD_B) {
        step(OPTION_VIEW, 1);
    }
    if (pressed & PAD_C) {
        step(OPTION_TEXTURES, 1);
    }
}

static char* append(char* p, const char* text)
{
    while (*text) {
        *p++ = *text++;
    }
    *p = 0;
    return p;
}

static char* appendStat(char* p, const char* name, uint32 value)
{
    p = append(p, name);
    int2str(int32(value), p);
    while (*p) {
        p++;
    }
    return append(p, "  ");
}

// Share of a 30 fps frame (FRT ticks at the master's clock / 128).
static uint32 framePercent(uint32 ticks)
{
    return ticks * 100 / 7457;
}

void osDrawDebug()
{
    char text[64];

    if (gMenuOpen)
    {
        for (int32 i = 0; i < OPTION_COUNT; i++)
        {
            const Option &o = gOptions[i];
            char* p = append(text, i == gCursor ? "> " : "  ");
            p = append(p, o.name);
            p = append(p, "  ");
            append(p, o.values[o.get()]);
            drawText(16, 40 + i * 18, text, TEXT_ALIGN_LEFT);
        }
    }

    int32 y = FRAME_HEIGHT - 20;

    if (int32(gMessageEnd - gVblankCount) > 0)
    {
        char* p = append(text, gMessageName);
        p = append(p, "  ");
        append(p, gMessage);
        drawText(0, y, text, TEXT_ALIGN_CENTER);
        y -= 18;
    }

    if (gShowStats || gTexelView)
    {
        // room faces drawn full res; left at quarter res for want of a cache
        // slot or upload; near ones without a full-res texture; near triangles
        char* p = appendStat(text, "full ", gStats.detailFaces);
        p = appendStat(p, "missed ", gStats.detailMisses);
        p = appendStat(p, "none ", gStats.nearNoDetail);
        appendStat(p, "tri ", gStats.nearTriangles);
        drawText(0, y, text, TEXT_ALIGN_CENTER);
        y -= 18;

        p = appendStat(text, "vdp1 ", framePercent(gStats.vdp1Ticks));
        p = appendStat(p, "cpu ", framePercent(gStats.updateTicks + gStats.renderTicks - gStats.waitTicks));
        appendStat(p, "uploads ", gStats.detailUploads);
        drawText(0, y, text, TEXT_ALIGN_CENTER);
    }
}
