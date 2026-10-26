// The debug pad (port 2). Mednafen keys in brackets:
//   START [Tab]       opens and closes the menu
//   up/down [arrows]  pick an option, left/right change it
//   A [P], B [H], C [G]  step the palette, view and dark faces options
// Changes show for a moment at the bottom of the screen.

#include "debug.h"
#include "common.h"
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

static const char* const kCurves[] = { "Saturn", "Soft", "Softer" };
static const char* const kDark[] = { "Add", "Half luminance", "Contrast steps" };
static const char* const kPalette[] = { "Saturn", "Remastered" };
static const char* const kViews[] = { "Normal", "Texture cache", "Texel size" };
static const char* const kOnOff[] = { "Off", "On" };
static const char* const kShrink[] = { "Exact", "Fast" };
static const char* const kLines[] = { "Fixed", "Turned" };
static const char* const kFar[] = { "Textured", "Flat past 16", "Flat past 12", "Flat past 8" };
static const int32 kFarDist[] = { 0, 16 * 1024, 12 * 1024, 8 * 1024 };

static int32 gShadeCurve;
static int32 gFarFaces;
static bool gShowStats;
#ifdef HW_LOG
static void benchUpdate();
#endif

static const Option gOptions[] = {
    { "Shading", kCurves, SHADE_CURVE_COUNT,
        [] { return gShadeCurve; },
        [](int32 v) { gShadeCurve = v; renderSetShadeCurve(v); } },
    { "Dark faces", kDark, DARK_COUNT,
        [] { return gDarkFaces; },
        [](int32 v) { gDarkFaces = v; } },
    { "Far faces", kFar, 4,
        [] { return gFarFaces; },
        [](int32 v) { gFarFaces = v; gFlatDist = kFarDist[v]; } },
    { "Shrink", kShrink, 2,
        [] { return int32(renderFastShrink()); },
        [](int32 v) { renderSetFastShrink(v); } },
    { "Lines", kLines, 2,
        [] { return gTurnLines; },
        [](int32 v) { gTurnLines = v; } },
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

enum { OPTION_SHADING, OPTION_DARK, OPTION_FAR, OPTION_SHRINK, OPTION_LINES, OPTION_PALETTE, OPTION_VIEW, OPTION_STATS,
       OPTION_COUNT };

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
        step(OPTION_DARK, 1);
    }

#ifdef HW_LOG
    benchUpdate();
#endif
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

#ifdef HW_LOG
// Hardware measurements without a second pad: every 4 seconds of play the
// frames are built another way (gBenchMode), and a table keeps each way's
// averages over its last 3 seconds: one photo after a full round.
#define BENCH_WINDOW    240     // vblanks each way lasts
#define BENCH_SETTLE    60      // the first of them aren't averaged

static const char* const kBenchNames[BENCH_COUNT] = {
    "Base", "Fixed lines", "LOD x1", "No gouraud", "No texture"
};

static struct { uint32 vdp1, cpu, fps; } gBench[BENCH_COUNT];

// and the base way's CPU time by stage, as gStats keeps it
enum { SPLIT_UPDATE, SPLIT_MESH, SPLIT_SLAVE_WAIT, SPLIT_FLUSH, SPLIT_XFORM, SPLIT_FACES, SPLIT_COUNT };
static const char* const kSplitNames[SPLIT_COUNT] = { "upd ", "mesh ", "swait ", "flush ", "xform ", "faces " };
static uint32 gSplit[SPLIT_COUNT];

static void benchUpdate()
{
    static uint32 start;
    static uint32 frames, vdp1, cpu;
    static uint32 split[SPLIT_COUNT];

    if (!start) {
        start = gVblankCount;
    }

    uint32 t = gVblankCount - start;
    if (t >= BENCH_SETTLE)
    {
        frames++;
        vdp1 += gStats.vdp1Ticks;
        cpu += gStats.updateTicks + gStats.renderTicks - gStats.waitTicks;
        split[SPLIT_UPDATE] += gStats.updateTicks;
        split[SPLIT_MESH] += gStats.meshTicks;
        split[SPLIT_SLAVE_WAIT] += gStats.slaveWaitTicks;
        split[SPLIT_FLUSH] += gStats.flushTicks;
        split[SPLIT_XFORM] += gStats.slaveXformTicks;
        split[SPLIT_FACES] += gStats.slaveFaceTicks;
    }

    if (t < BENCH_WINDOW)
        return;

    if (frames)
    {
        gBench[gBenchMode].vdp1 = framePercent(vdp1 / frames);
        gBench[gBenchMode].cpu = framePercent(cpu / frames);
        gBench[gBenchMode].fps = frames * 60 / (t - BENCH_SETTLE);
        if (gBenchMode == BENCH_BASE)
        {
            for (int32 i = 0; i < SPLIT_COUNT; i++) {
                gSplit[i] = framePercent(split[i] / frames);
            }
        }
    }
    memset(split, 0, sizeof(split));

    gBenchMode = (gBenchMode + 1) % BENCH_COUNT;
    start = gVblankCount;
    frames = vdp1 = cpu = 0;
}

// Glyphs take the master's vertices, which a heavy view nearly uses up: the
// CPU stages first, then the table as terse as it reads.
static void benchDraw()
{
    char text[64];

    // the master's stages, then the slave's
    char* p = text;
    *p = 0;
    for (int32 i = 0; i < SPLIT_XFORM; i++) {
        p = appendStat(p, kSplitNames[i], gSplit[i]);
    }
    drawText(16, 30, text, TEXT_ALIGN_LEFT);
    p = append(text, "slave  ");
    for (int32 i = SPLIT_XFORM; i < SPLIT_COUNT; i++) {
        p = appendStat(p, kSplitNames[i], gSplit[i]);
    }
    drawText(16, 48, text, TEXT_ALIGN_LEFT);

    drawText(16, 72, "vdp1 cpu fps", TEXT_ALIGN_LEFT);
    for (int32 i = 0; i < BENCH_COUNT; i++)
    {
        p = append(text, i == gBenchMode ? "> " : "");
        if (gBench[i].fps)
        {
            p = appendStat(p, "", gBench[i].vdp1);
            p = appendStat(p, "", gBench[i].cpu);
            p = appendStat(p, "", gBench[i].fps);
        }
        append(p, kBenchNames[i]);
        drawText(16, 90 + i * 18, text, TEXT_ALIGN_LEFT);
    }
}
#endif

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

#ifdef HW_LOG
    if (gTexelView)     // the measurement table stands in for the stats
#else
    if (gShowStats || gTexelView)
#endif
    {
        // room faces drawn with cached textures; in their mean colour for want
        // of a cache slot or upload; flat for distance
        char* p = appendStat(text, "cached ", gStats.detailFaces);
        p = appendStat(p, "missed ", gStats.detailMisses);
        appendStat(p, "flat ", gStats.flatFaces);
        drawText(0, y, text, TEXT_ALIGN_CENTER);
        y -= 18;

        p = appendStat(text, "vdp1 ", framePercent(gStats.vdp1Ticks));
        p = appendStat(p, "cpu ", framePercent(gStats.updateTicks + gStats.renderTicks - gStats.waitTicks));
        appendStat(p, "uploads ", gStats.detailUploads);
        drawText(0, y, text, TEXT_ALIGN_CENTER);
    }

#ifdef HW_LOG
    if (!gMenuOpen) {
        benchDraw();
    }
#endif
}
