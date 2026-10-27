// The debug pad (port 2). Mednafen keys in brackets:
//   START [Tab]       opens and closes the menu
//   up/down [arrows]  pick an option, left/right change it
//   A [P], B [H], C [G]  step the palette, view and dark faces options
// Changes show for a moment at the bottom of the screen. In HW_LOG builds the
// player's pad pages the overlay with R and L, and on the menu's page its
// d-pad and A, B, C work the menu as the debug pad's do (Lara stands still).

#include "debug.h"
#include "common.h"
#include "crash.h"
#include "dsp.h"
#include "hw.h"
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
static const char* const kDark[] = { "Add", "Contrast steps", "Scaled" };
static const char* const kPalette[] = { "Saturn", "Remastered" };
static const char* const kViews[] = { "Normal", "Texture cache", "Texel size", "Wireframe", "Contrast steps" };
static const char* const kOnOff[] = { "Off", "On" };
static const char* const kShrink[] = { "Exact", "Fast" };
static const char* const kLines[] = { "Fixed", "Turned" };
static const char* const kLod[] = { "Full res", "1 texel a pixel", "1.5 texels", "2 texels" };
static const char* const kShade[] = { "Gouraud", "Shaded tables", "All far faces" };
static const char* const kShadow[] = { "Half", "Black", "Mesh" };
static const char* const kSplit[] = { "Plane only", "Far off frame", "Steep too" };
static const char* const kSteps[] = { "1 (7/8)", "2 (6/8)", "3 (5/8)", "4 (4/8)" };
static const char* const kMultiply[] = { "0 (Saturn add)", "1/4", "2/4", "3/4", "4/4 (PS1)" };
static const char* const kGrey[] = { "0 (own mean)", "1/4", "2/4", "3/4", "4/4" };
static const char* const kFar[] = { "Textured", "Flat past 16", "Flat past 12", "Flat past 8" };
static const int32 kFarDist[] = { 0, 16 * 1024, 12 * 1024, 8 * 1024 };

static int32 gShadeCurve;
static int32 gFarFaces;
static bool gShowStats;
#ifdef HW_LOG
static void benchUpdate();
static void frameShown(uint32_t plot);
#define FRAME_ROW   (OVERLAY_ROWS - 2)  // and the row under it (frameShown)
static volatile bool gFrameRows;    // they are pinned
static void sceneUpdate();
#endif

static const Option gOptions[] = {
    { "Shading", kCurves, SHADE_CURVE_COUNT,
        [] { return gShadeCurve; },
        [](int32 v) { gShadeCurve = v; renderSetShadeCurve(v); } },
    { "Dark faces", kDark, DARK_COUNT,
        [] { return gDarkFaces; },
        [](int32 v) { renderSetDarkFaces(v); } },
    { "Contrast steps", kSteps, 4,
        [] { return gContrastSteps - 1; },
        [](int32 v) { renderSetContrastSteps(v + 1); } },
    { "Multiply", kMultiply, 5,
        [] { return gScaleMultiply; },
        [](int32 v) { renderSetScaleMultiply(v); } },
    { "Grey strength", kGrey, 5,
        [] { return gContrastGrey; },
        [](int32 v) { renderSetContrastGrey(v); } },
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
    { "Near split", kSplit, NEAR_SPLIT_COUNT,
        [] { return gNearSplit; },
        [](int32 v) { gNearSplit = v; } },
    { "Shadows", kShadow, SHADOW_COUNT,
        [] { return gShadowMode; },
        [](int32 v) { gShadowMode = v; } },
    { "View", kViews, TEXEL_VIEW_COUNT,
        [] { return gTexelView; },
        [](int32 v) { gTexelView = v; } },
    { "Stats", kOnOff, 2,
        [] { return int32(gShowStats); },
        [](int32 v) { gShowStats = v; } }
};

enum { OPTION_SHADING, OPTION_DARK, OPTION_STEPS, OPTION_MULTIPLY, OPTION_GREY, OPTION_FAR, OPTION_SHRINK, OPTION_LINES, OPTION_LOD, OPTION_SHADE_TABLES,
       OPTION_PALETTE, OPTION_SPLIT, OPTION_SHADOWS, OPTION_VIEW, OPTION_STATS, OPTION_COUNT };

static uint16 gPad;
static bool gMenuOpen;
static int32 gCursor;
#ifdef HW_LOG
// R and L page through the overlay: the bench's table, then (held on Base)
// the scene's rows with each frame's CPU, slave and VDP1 time, each frame's
// time by stage, the menu over each frame's times, and nothing at all.
enum OverlayPage { PAGE_BENCH, PAGE_FRAME, PAGE_STAGES, PAGE_MENU, PAGE_NONE, PAGE_COUNT };
static int32 gPage;
#endif

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

// The menu shows: opened from the debug pad, or the overlay's page.
static bool menuShown()
{
#ifdef HW_LOG
    if (gPage == PAGE_MENU)
        return true;
#endif
    return gMenuOpen;
}

void debugUpdate()
{
    uint16 pad = padDebug();

#ifdef HW_LOG
    // the player's pad: Lara has no use for its shoulder buttons
    static uint32 shoulders;
    uint32 pressed2 = keys & (IK_L | IK_R) & ~shoulders;
    if (pressed2 & IK_R) {
        gPage = (gPage + 1) % PAGE_COUNT;
    } else if (pressed2 & IK_L) {
        gPage = (gPage + PAGE_COUNT - 1) % PAGE_COUNT;
    }
    shoulders = keys & (IK_L | IK_R);
    if (gPage == PAGE_MENU)
    {
        pad |= padButtons() & ~(PAD_START | PAD_L | PAD_R);
        keys = 0;
    }
#endif

    uint16 pressed = pad & ~gPad;
    gPad = pad;

    if (pressed & PAD_START) {
        gMenuOpen = !gMenuOpen;
    }

    if (menuShown())
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
    vdp1OnShown(frameShown);
    if ((gPage == PAGE_BENCH || gPage == PAGE_NONE) && gFrameRows)
    {
        gFrameRows = false;
        overlayUnpin(FRAME_ROW);
        overlayUnpin(FRAME_ROW + 1);
    }
    benchUpdate();
    sceneUpdate();
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
    "All meshes", "C meshes", "Clear", "Void colour", "Split shade", "Profile"
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
// its 64-byte block of the program. The way draws as Base does; the other
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
#define PROFILE_SHIFT   6
#define PROFILE_BLOCKS  (152 * 1024 >> PROFILE_SHIFT)  // the code ends ~146 KB in
#define PROFILE_TOP     15      // three to a row, under the table

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

// With the bench's rows hidden: where Lara stands (room, x, y, z, heading, as
// LARA_START and START_YAW take them) and what the scene costs, averaged
// over half a second, so that two views can be compared and reproduced.
#define SCENE_ROWS      3
#define SCENE_WINDOW    30      // vblanks averaged
static char gSceneRows[SCENE_ROWS][OVERLAY_COLUMNS + 1];

extern Lara* players[MAX_PLAYERS];

static char* appendHex(char* p, uint32 value, int32 digits)
{
    static const char hex[] = "0123456789abcdef";
    for (int32 d = (digits - 1) * 4; d >= 0; d -= 4) {
        *p++ = hex[(value >> d) & 15];
    }
    *p = 0;
    return p;
}

static void sceneUpdate()
{
    static uint32 start, frames, vdp1, cpu, drawn, roomVertices, vertices, faces, meshes, slaveMeshes;

    frames++;
    vdp1 += gStats.vdp1Ticks;
    cpu += gStats.updateTicks + gStats.renderTicks - gStats.waitTicks;
    drawn += gStats.rooms;
    roomVertices += gStats.roomVertices;
    vertices += gStats.vertices;
    faces += gStats.faces;
    meshes += gStats.meshes;
    slaveMeshes += gStats.slaveMeshes;

    if (gVblankCount - start < SCENE_WINDOW)
        return;

    char* p = gSceneRows[0];
    *p = 0;
    const ItemObj* lara = (const ItemObj*)(const void*)players[0];
    if (lara)
    {
        p = appendStat(p, "room ", uint32(lara->room - rooms));
        p = appendStat(p, "", uint32(lara->pos.x));
        p = appendStat(p, "", uint32(lara->pos.y));
        p = appendStat(p, "", uint32(lara->pos.z));
        appendHex(p, uint16(lara->angle.y), 4);
    }

    p = appendStat(gSceneRows[1], "rooms ", drawn / frames);
    p = appendStat(p, "rv ", roomVertices / frames);
    p = appendStat(p, "mv ", (vertices - roomVertices) / frames);
    appendStat(p, "cmds ", faces / frames);

    p = appendStat(gSceneRows[2], "meshes ", meshes / frames);
    p = appendStat(p, "+", slaveMeshes / frames);
    p = appendStat(p, "vdp1 ", framePercent(vdp1 / frames));
    p = appendStat(p, "cpu ", framePercent(cpu / frames));
    appendStat(p, "fps ", frames * 60 / (gVblankCount - start));

    start = gVblankCount;
    frames = vdp1 = cpu = drawn = roomVertices = vertices = faces = meshes = slaveMeshes = 0;
}

// With the bench's rows hidden, the two bottom rows show the frame on screen
// now, set from the vblank-out interrupt as it comes on: its plot's number
// (vdp1Plots), the vblanks since the last frame came on (2 at 30 fps, more is
// a dropped frame), its CPU and VDP1 time, and where Lara stood for it. Each
// matches the picture under it, so a video or a replay stepped frame by
// frame shows what each frame cost.
#define FRAME_TAGS  8

struct FrameTag
{
    uint32 plot;
    uint32 cpu;
    uint32 update, draw, flush, wait;   // the master's stages
    uint32 xform, faces, mesh, idle;    // the slave's
    uint32 dsp;                         // room vertices the DSP projected, %
    int32 room, x, y, z;
    uint32 yaw;
};
static FrameTag gFrameTags[FRAME_TAGS];

void debugFrameDone()
{
    uint32 plot = vdp1Plots();
    FrameTag &t = gFrameTags[plot % FRAME_TAGS];
    t.cpu = gStats.updateTicks + gStats.renderTicks - gStats.waitTicks;
    t.update = gStats.updateTicks;
    t.draw = gStats.drawTicks;
    t.flush = gStats.flushTicks;
    t.wait = gStats.waitTicks + gStats.slaveWaitTicks;
    t.xform = gStats.slaveXformTicks;
    t.faces = gStats.slaveFaceTicks;
    t.mesh = gStats.slaveMeshTicks;
    t.idle = gStats.slaveIdleTicks;
    t.dsp = gStats.roomVertices ? gStats.dspVertices * 100 / gStats.roomVertices : 0;
    const ItemObj* lara = (const ItemObj*)(const void*)players[0];
    t.room = lara ? int32(lara->room - rooms) : -1;
    t.x = lara ? lara->pos.x : 0;
    t.y = lara ? lara->pos.y : 0;
    t.z = lara ? lara->pos.z : 0;
    t.yaw = lara ? uint16(lara->angle.y) : 0;
    t.plot = plot;
}

static void frameShown(uint32_t plot)
{
    static uint32 last;
    uint32 vblanks = gVblankCount - last;
    last = gVblankCount;
    if (gPage == PAGE_BENCH || gPage == PAGE_NONE)
        return;

    const FrameTag &t = gFrameTags[plot % FRAME_TAGS];
    bool tagged = t.plot == plot;
    char text[OVERLAY_COLUMNS + 1];
    char* p = appendStat(text, "#", plot);
    p = appendStat(p, "vb ", vblanks);
    if (gPage == PAGE_STAGES)
    {
        // the master: update, drawing (rooms queued, meshes), flush, waits
        if (tagged)
        {
            p = appendStat(p, "u ", framePercent(t.update));
            p = appendStat(p, "d ", framePercent(t.draw));
            p = appendStat(p, "f ", framePercent(t.flush));
            appendStat(p, "w ", framePercent(t.wait));
        }
        overlayPin(FRAME_ROW, text);

        // the slave: room transform, room faces, meshes, idle; DSP share
        p = text;
        *p = 0;
        if (tagged)
        {
            p = appendStat(p, "slv x ", framePercent(t.xform));
            p = appendStat(p, "f ", framePercent(t.faces));
            p = appendStat(p, "m ", framePercent(t.mesh));
            p = appendStat(p, "i ", framePercent(t.idle));
            appendStat(p, "dsp ", t.dsp);
        }
        overlayPin(FRAME_ROW + 1, text);
        gFrameRows = true;
        return;
    }

    if (tagged)
    {
        p = appendStat(p, "cpu ", framePercent(t.cpu));
        p = appendStat(p, "slv ", framePercent(t.xform + t.faces + t.mesh));
    }
    appendStat(p, "vdp1 ", framePercent(gStats.vdp1Ticks));
    overlayPin(FRAME_ROW, text);

    p = text;
    *p = 0;
    if (tagged && t.room >= 0)
    {
        p = appendStat(p, "room ", uint32(t.room));
        p = appendStat(p, "", uint32(t.x));
        p = appendStat(p, "", uint32(t.y));
        p = appendStat(p, "", uint32(t.z));
        appendHex(p, t.yaw, 4);
    }
    overlayPin(FRAME_ROW + 1, text);
    gFrameRows = true;
}

static void benchUpdate()
{
    static uint32 start;
    static uint32 frames, vdp1, cpu, draw, slave;
    static uint32 split[SPLIT_COUNT];
    static uint32 dspVertices, roomVertices;

    // held on Base while the scene's rows show; a fresh window after
    if (gPage != PAGE_BENCH)
    {
        if (gBenchMode != BENCH_BASE)
        {
            profileSampling(false);
            gBenchMode = BENCH_BASE;
            vdp2SetBackColor(RGB(0, 0, 0));
        }
        start = gVblankCount;
        frames = vdp1 = cpu = draw = slave = 0;
        memset(split, 0, sizeof(split));
        dspVertices = roomVertices = 0;
        return;
    }

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
    vdp2SetBackColor(gBenchMode == BENCH_VOID_COLOR ? RGB(31, 0, 31) : RGB(0, 0, 0));
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
    appendStat(p, "samples ", gProfileRanked);
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

#ifdef HW_LOG
    if (gPage == PAGE_NONE && !gMenuOpen)
    {
        overlayFrame();
        return;
    }
#endif

    if (gSettings.video_fps)
    {
        int2str(fps, text);
        overlayRow(0, text, OVERLAY_RIGHT);
    }

    if (menuShown())
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

#ifdef HW_LOG
    int32 row = OVERLAY_ROWS - 3;   // above the frame rows
#else
    int32 row = OVERLAY_ROWS - 1;
#endif

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
    if (!menuShown() && gPage != PAGE_NONE)
    {
        if (gPage != PAGE_BENCH)
        {
            overlayRow(1, kBenchNames[gBenchMode]);
            for (int32 i = 0; i < SCENE_ROWS; i++) {
                overlayRow(2 + i, gSceneRows[i]);
            }
        }
        else
        {
            benchDraw();
        }
    }
#endif

    overlayFrame();
}
