// The debug pad (port 2). Mednafen keys in brackets:
//   START [Tab]       opens and closes the menu
//   up/down [arrows]  pick an option, left/right change it
//   A [P], B [H], C [G]  step the palette, view and dark faces options
// Changes show for a moment at the bottom of the screen.

#include "debug.h"
#include "common.h"
#include "crash.h"
#include "dsp.h"
#include "slave.h"
#include "xform_dsp.h"
#include "dmaprobe_dsp.h"
#include "input.h"
#include "overlay.h"
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
static const char* const kLod[] = { "Full res", "1 texel a pixel", "1.5 texels", "2 texels" };
static const char* const kShade[] = { "Gouraud", "Shaded tables", "All far faces" };
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
    { "Texture LOD", kLod, 4,
        [] { return gTextureLod / 2 - (gTextureLod > 0); },
        [](int32 v) { gTextureLod = v ? v * 2 + 2 : 0; } },
    { "Even shades", kShade, 3,
        [] { return gShadeTables; },
        [](int32 v) { gShadeTables = v; } },
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

enum { OPTION_SHADING, OPTION_DARK, OPTION_FAR, OPTION_SHRINK, OPTION_LINES, OPTION_LOD, OPTION_SHADE_TABLES,
       OPTION_PALETTE, OPTION_VIEW, OPTION_STATS, OPTION_COUNT };

static uint16 gPad;
static bool gMenuOpen;
static int32 gCursor;
static bool gBenchHidden;   // L or R: the bench's rows give way to the name of its way

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
    // the player's pad: Lara has no use for its shoulder buttons
    static uint32 shoulders;
    if (keys & (IK_L | IK_R) & ~shoulders) {
        gBenchHidden = !gBenchHidden;
    }
    shoulders = keys & (IK_L | IK_R);
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

// A number right-aligned in a column of 5.
static char* appendColumn(char* p, uint32 value)
{
    char digits[12];
    int2str(int32(value), digits);
    for (int32 n = strlen(digits); n < 5; n++) {
        *p++ = ' ';
    }
    return append(p, digits);
}

static const char* const kBenchNames[BENCH_COUNT] = {
    "Base", "Fixed lines", "No LOD", "Gouraud only", "No gouraud", "No texture", "No DSP", "Master meshes",
    "All meshes", "C meshes", "Full clear", "No clear", "Profile"
};

// each way's VDP1 and master CPU time, the master's drawing (rooms queued
// and meshes, before flush), the slave's on rooms (transform and faces) and
// the meshes it draws
static struct { uint32 vdp1, cpu, draw, slave, fps; } gBench[BENCH_COUNT];

// and the base way's CPU time by stage, as gStats keeps it
enum { SPLIT_UPDATE, SPLIT_DRAW, SPLIT_SLAVE_WAIT, SPLIT_FLUSH, SPLIT_XFORM, SPLIT_FACES, SPLIT_MESH, SPLIT_COUNT };
static const char* const kSplitNames[SPLIT_COUNT] = { "upd ", "draw ", "swait ", "flush ", "xform ", "faces ",
                                                      "mesh " };
static uint32 gDspShare;    // percent of the base way's room vertices the DSP projected
static uint32 gSplit[SPLIT_COUNT];

// A sampling profiler for the master CPU: during the Profile way, the free-
// running timer's compare A interrupts it every PROFILE_TICKS (128 cycles
// each) and up to 31 more at random, and the interrupted PC counts towards
// its 32-byte block of the program. The way draws as Base does; the other
// ways run without the interrupt.
#define FRT_TIER        (*(volatile uint8*)0xFFFFFE10)
#define FRT_FTCSR       (*(volatile uint8*)0xFFFFFE11)
#define FRT_OCRAH       (*(volatile uint8*)0xFFFFFE14)
#define FRT_OCRAL       (*(volatile uint8*)0xFFFFFE15)
#define FRT_TOCR        (*(volatile uint8*)0xFFFFFE17)
#define INTC_IPRB       (*(volatile uint16*)0xFFFFFE60)
#define INTC_VCRC       (*(volatile uint16*)0xFFFFFE66)
#define FRT_OCIAE       0x08    // TIER, FTCSR: compare A
#define FRT_OCRS        0x10    // TOCR: OCRB selected
#define BIOS_SET_UINT   (*(void (**)(uint32, void (*)()))0x06000310)

#define PROFILE_VECTOR  0x65    // as libyaul numbers it
#define PROFILE_LEVEL   1       // under every other interrupt
#define PROFILE_TICKS   40
#define PROFILE_START   0x06004000
#define PROFILE_SHIFT   5
#define PROFILE_BLOCKS  (160 * 1024 >> PROFILE_SHIFT)
#define PROFILE_TOP     18      // three to a row, under the table

static_assert(7 + BENCH_COUNT + PROFILE_TOP / 3 <= OVERLAY_ROWS, "the profile's rows fit the overlay");

extern "C" void profileEntry();     // irq.s
static uint16 gProfile[PROFILE_BLOCKS];   // held at 0xFFFF
static uint32 gProfileSamples, gProfileOther;
static uint32 gProfileRanked;           // gProfileSamples when ranked
static bool gProfiling;
static uint32 gProfileRandom = 2463534242u;
static struct { uint16 block, share; } gProfileTop[PROFILE_TOP];  // share in tenths of a percent

// From the count now: a sample held back past its compare (behind another
// interrupt) would otherwise wait for the counter to come round again.
static void profileArm()
{
    uint32 x = gProfileRandom;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    gProfileRandom = x;
    uint32 next = timerTicks() + PROFILE_TICKS + (x & 31);
    FRT_OCRAH = next >> 8;
    FRT_OCRAL = next;
}

// From the timer interrupt (irq.s), with the interrupted PC.
extern "C" void profileHandler(uint32 pc)
{
    FRT_FTCSR = FRT_FTCSR & ~FRT_OCIAE;
    profileArm();
    gProfileSamples++;
    uint32 offset = (pc & 0x07FFFFFF) - PROFILE_START;
    if (offset < (PROFILE_BLOCKS << PROFILE_SHIFT))
    {
        uint16 &n = gProfile[offset >> PROFILE_SHIFT];
        if (n != 0xFFFF) {
            n++;
        }
    }
    else
    {
        gProfileOther++;
    }
}

#define DSP_TEST_BATCHES    32      // of 16 vertices
#define DSP_TEST_VERTICES   (DSP_TEST_BATCHES * 16)

void debugDspTest()
{
    static const int16 rows[9] = { 14189, 0, -8192, 4096, 15851, 7094, 7094, -4096, 12288 };
    static const int32 t[3] = { -500000, 12345, 2000000 };

    // the vertices, the DSP's results and this CPU's in gVertices, unused
    // before a level
    uint32* in = (uint32*)gVertices;
    int32* out = (int32*)(in + DSP_TEST_VERTICES);
    int32* ref = out + DSP_TEST_VERTICES * 4;
    static_assert(DSP_TEST_VERTICES * 9 * 4 <= sizeof(gVertices), "the DSP test's buffers fit in gVertices");

    uint32 seed = 0x12345678;
    for (int32 i = 0; i < DSP_TEST_VERTICES; i++)
    {
        seed = seed * 1664525 + 1013904223;
        in[i] = seed;
    }

    // rows z, x, y, as xform.s takes them
    uint32_t md0[12];
    for (int32 r = 0; r < 3; r++)
    {
        int32 row = (r + 2) % 3;
        for (int32 c = 0; c < 3; c++) {
            md0[r * 4 + c] = rows[row * 3 + c] * 4;   // xform.s takes each byte times 4
        }
        md0[r * 4 + 3] = t[row];
    }
    const uint32_t md3[5] = { DSP_TEST_BATCHES, 1, dspAddress(in), dspAddress(out), 0xFF };

    dspLoad(xform_dsp, XFORM_DSP_COUNT);
    dspWrite(0, 0, md0, 12);
    dspWrite(3, 0, md3, 5);
    uint32 start = timerTicks();
    dspStart(0);
    while (dspBusy());
    uint32 dspTicks = (timerTicks() - start) & 0xFFFF;
    cachePurge();   // the DSP wrote out behind the cache

    // the same on this CPU, as xform.s's multiply-accumulates do it
    // records g z x y
    start = timerTicks();
    for (int32 i = 0; i < DSP_TEST_VERTICES; i++)
    {
        uint32 v = in[i];
        int32 x = (v >> 24) * 4, y = ((v >> 16) & 0xFF) * 4, z = ((v >> 8) & 0xFF) * 4;
        ref[i * 4] = v & 0xFF;
        for (int32 r = 0; r < 3; r++)
        {
            int32 row = (r + 2) % 3;
            ref[i * 4 + 1 + r] = t[row] + mul16(rows[row * 3], x) + mul16(rows[row * 3 + 1], y) +
                                 mul16(rows[row * 3 + 2], z);
        }
    }
    uint32 cpuTicks = (timerTicks() - start) & 0xFFFF;

    int32 errors = 0;
    for (int32 i = 0; i < DSP_TEST_VERTICES * 4; i += 4) {
        errors += ref[i] != out[i] || ref[i + 1] != out[i + 1] ||
                  ref[i + 2] != out[i + 2] || ref[i + 3] != out[i + 3];
    }
    // the same transfers alone (dmaprobe.dsp)
    dspLoad(dmaprobe_dsp, DMAPROBE_DSP_COUNT);
    dspWrite(3, 0, md3, 5);
    start = timerTicks();
    dspStart(0);
    while (dspBusy());
    uint32 dmaTicks = (timerTicks() - start) & 0xFFFF;
    cachePurge();

    crashLog("DSPXF", dspTicks);
    crashLog("DSPDMA", dmaTicks);
    crashLog("CPUXF", cpuTicks);
    crashLog("DSPERR", errors);

    // the whole projection (project.dsp) against xform.s's, 512 vertices
    roomsDspTest(dspTicks, cpuTicks, errors);
    crashLog("PJDSP", dspTicks);
    crashLog("PJCPU", cpuTicks);
    crashLog("PJERR", errors);
}

void debugProfileInit()
{
    BIOS_SET_UINT(PROFILE_VECTOR, profileEntry);
    INTC_VCRC = (INTC_VCRC & 0xFF00) | PROFILE_VECTOR;
    INTC_IPRB = (INTC_IPRB & ~0x0F00) | (PROFILE_LEVEL << 8);
    FRT_TOCR = FRT_TOCR & ~FRT_OCRS;
}

static void profileSampling(bool on)
{
    if (on == gProfiling)
        return;
    gProfiling = on;
    if (on)
    {
        profileArm();
        FRT_FTCSR = FRT_FTCSR & ~FRT_OCIAE;
        FRT_TIER = FRT_TIER | FRT_OCIAE;
    }
    else
    {
        FRT_TIER = FRT_TIER & ~FRT_OCIAE;
    }
}

// The blocks with the most samples, for the table.
static void profileRank()
{
    uint32 total = gProfileSamples;
    if (!total)
        return;
    gProfileRanked = total;
    memset(gProfileTop, 0, sizeof(gProfileTop));
    for (int32 b = 0; b < PROFILE_BLOCKS; b++)
    {
        uint32 share = gProfile[b] * 1000 / total;
        if (share <= gProfileTop[PROFILE_TOP - 1].share)
            continue;
        int32 i = PROFILE_TOP - 1;
        for (; i > 0 && gProfileTop[i - 1].share < share; i--) {
            gProfileTop[i] = gProfileTop[i - 1];
        }
        gProfileTop[i].block = b;
        gProfileTop[i].share = share;
    }
}

static char gBenchRows[OVERLAY_ROWS][OVERLAY_COLUMNS + 1];
static bool gBenchChanged = true;   // gBenchRows to compose again
static uint32 gReachLo, gReachHi;   // lines the vblank erase reached in the base way's frames

static void benchUpdate()
{
    static uint32 start;
    static uint32 frames, vdp1, cpu, draw, slave;
    static uint32 split[SPLIT_COUNT];
    static uint32 reachLo = ~0u, reachHi;
    static uint32 dspVertices, roomVertices;

    if (!start) {
        start = gVblankCount;
    }

    uint32 t = gVblankCount - start;
    profileSampling(gBenchMode == BENCH_PROFILE && t >= BENCH_SETTLE && t < BENCH_WINDOW);
    if (t >= BENCH_SETTLE)
    {
        frames++;
        vdp1 += gStats.vdp1Ticks;
        cpu += gStats.updateTicks + gStats.renderTicks - gStats.waitTicks;
        draw += gStats.drawTicks;
        slave += gStats.slaveXformTicks + gStats.slaveFaceTicks + gStats.slaveMeshTicks;
        split[SPLIT_UPDATE] += gStats.updateTicks;
        split[SPLIT_DRAW] += gStats.drawTicks;
        split[SPLIT_SLAVE_WAIT] += gStats.slaveWaitTicks;
        split[SPLIT_FLUSH] += gStats.flushTicks;
        split[SPLIT_XFORM] += gStats.slaveXformTicks;
        split[SPLIT_FACES] += gStats.slaveFaceTicks;
        split[SPLIT_MESH] += gStats.slaveMeshTicks;
        dspVertices += gStats.dspVertices;
        roomVertices += gStats.roomVertices;
        if (gBenchMode == BENCH_BASE)
        {
            reachLo = X_MIN(reachLo, gStats.eraseReach);
            reachHi = X_MAX(reachHi, gStats.eraseReach);
        }
    }

    if (t < BENCH_WINDOW)
        return;

    if (frames)
    {
        gBench[gBenchMode].vdp1 = framePercent(vdp1 / frames);
        gBench[gBenchMode].cpu = framePercent(cpu / frames);
        gBench[gBenchMode].draw = framePercent(draw / frames);
        gBench[gBenchMode].slave = framePercent(slave / frames);
        gBench[gBenchMode].fps = frames * 60 / (t - BENCH_SETTLE);
        if (gBenchMode == BENCH_BASE)
        {
            for (int32 i = 0; i < SPLIT_COUNT; i++) {
                gSplit[i] = framePercent(split[i] / frames);
            }
            gReachLo = reachLo;
            gReachHi = reachHi;
            reachLo = ~0u;
            reachHi = 0;
            gDspShare = roomVertices ? dspVertices * 100 / roomVertices : 0;
        }
    }
    if (gBenchMode == BENCH_PROFILE) {
        profileRank();
    }
    gBenchChanged = true;
    memset(split, 0, sizeof(split));
    dspVertices = roomVertices = 0;

    gBenchMode = (gBenchMode + 1) % BENCH_COUNT;
    start = gVblankCount;
    frames = vdp1 = cpu = draw = slave = 0;
}

// A number in tenths, as "12.3".
static char* appendTenths(char* p, uint32 tenths)
{
    char digits[12];
    int2str(int32(tenths / 10), digits);
    for (int32 n = strlen(digits); n < 3; n++) {
        *p++ = ' ';
    }
    p = append(p, digits);
    *p++ = '.';
    *p++ = '0' + tenths % 10;
    *p = 0;
    return p;
}

// Rows 1 on: the CPU stages, the table, then the profile's hottest blocks:
// each the low 5 hex digits of its address and its share of the samples.
// Composed only when they change, so the frames measured don't pay for it.
static void benchCompose()
{
    memset(gBenchRows, 0, sizeof(gBenchRows));
    char text[64];

    // the master's stages, then the slave's
    char* p = text;
    *p = 0;
    for (int32 i = 0; i < SPLIT_XFORM; i++) {
        p = appendStat(p, kSplitNames[i], gSplit[i]);
    }
    strncpy(gBenchRows[1], text, OVERLAY_COLUMNS);
    p = append(text, "slave ");
    for (int32 i = SPLIT_XFORM; i < SPLIT_COUNT; i++) {
        p = appendStat(p, kSplitNames[i], gSplit[i]);
    }
    appendStat(p, "dsp ", gDspShare);
    strncpy(gBenchRows[2], text, OVERLAY_COLUMNS);

    strncpy(gBenchRows[4], "   vdp1  cpu draw  slv  fps", OVERLAY_COLUMNS);
    for (int32 i = 0; i < BENCH_COUNT; i++)
    {
        p = append(text, i == gBenchMode ? "> " : "  ");
        if (gBench[i].fps)
        {
            p = appendColumn(p, gBench[i].vdp1);
            p = appendColumn(p, gBench[i].cpu);
            p = appendColumn(p, gBench[i].draw);
            p = appendColumn(p, gBench[i].slave);
            p = appendColumn(p, gBench[i].fps);
        }
        else
        {
            p = append(p, "                         ");
        }
        p = append(p, "  ");
        append(p, kBenchNames[i]);
        strncpy(gBenchRows[5 + i], text, OVERLAY_COLUMNS);
    }

    int32 row = 6 + BENCH_COUNT;
    p = append(text, "profile ");
    p = appendStat(p, "samples ", gProfileRanked);
    p = append(p, "erase ");
    int2str(int32(gReachLo), p);
    while (*p) {
        p++;
    }
    *p++ = '-';
    int2str(int32(gReachHi), p);
    strncpy(gBenchRows[row++], text, OVERLAY_COLUMNS);
    for (int32 i = 0; i < PROFILE_TOP && row < OVERLAY_ROWS; i += 3, row++)
    {
        p = text;
        for (int32 j = i; j < i + 3 && gProfileTop[j].share; j++)
        {
            static const char hex[] = "0123456789abcdef";
            uint32 addr = (PROFILE_START & 0xFFFFF) + (gProfileTop[j].block << PROFILE_SHIFT);
            for (int32 d = 16; d >= 0; d -= 4) {
                *p++ = hex[(addr >> d) & 15];
            }
            p = appendTenths(p, gProfileTop[j].share);
            p = append(p, "   ");
        }
        *p = 0;
        strncpy(gBenchRows[row], text, OVERLAY_COLUMNS);
    }
}

static void benchDraw()
{
    if (gBenchChanged)
    {
        benchCompose();
        gBenchChanged = false;
    }
    for (int32 i = 0; i < OVERLAY_ROWS; i++)
    {
        if (gBenchRows[i][0]) {
            overlayRow(i, gBenchRows[i]);
        }
    }
}
#endif

// Over the frame on VDP2 (overlay.h), with the fps counter: the frame's
// measurements don't count them.
void osDrawDebug()
{
    char text[64];

    if (gSettings.video_fps)
    {
        int2str(fps, text);
        overlayRow(0, text, OVERLAY_RIGHT);
    }

    if (gMenuOpen)
    {
        for (int32 i = 0; i < OPTION_COUNT; i++)
        {
            const Option &o = gOptions[i];
            char* p = append(text, i == gCursor ? "> " : "  ");
            p = append(p, o.name);
            p = append(p, "  ");
            append(p, o.values[o.get()]);
            overlayRow(2 + i, text);
        }
    }

    int32 row = OVERLAY_ROWS - 1;

    if (int32(gMessageEnd - gVblankCount) > 0)
    {
        char* p = append(text, gMessageName);
        p = append(p, "  ");
        append(p, gMessage);
        overlayRow(row--, text, OVERLAY_CENTRE);
    }

#ifdef HW_LOG
    if (gTexelView)     // the stats change every frame: not while measuring
#else
    if (gShowStats || gTexelView)
#endif
    {
        // room faces drawn with cached textures; in their mean colour for want
        // of a cache slot or upload; flat for distance
        char* p = appendStat(text, "cached ", gStats.detailFaces);
        p = appendStat(p, "missed ", gStats.detailMisses);
        appendStat(p, "flat ", gStats.flatFaces);
        overlayRow(row--, text, OVERLAY_CENTRE);

        p = appendStat(text, "vdp1 ", framePercent(gStats.vdp1Ticks));
        p = appendStat(p, "cpu ", framePercent(gStats.updateTicks + gStats.renderTicks - gStats.waitTicks));
        appendStat(p, "uploads ", gStats.detailUploads);
        overlayRow(row--, text, OVERLAY_CENTRE);
    }

#ifdef HW_LOG
    if (!gMenuOpen)
    {
        if (gBenchHidden) {
            overlayRow(1, kBenchNames[gBenchMode]);
        } else {
            benchDraw();
        }
    }
#endif

    overlayFrame();
}
