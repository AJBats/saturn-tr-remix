// The debug pad (port 2). Mednafen keys in brackets:
//   START [Tab]       opens and closes the menu (not in HW_LOG builds)
//   up/down [arrows]  pick an option, left/right change it
//   A [P]             steps the palette
// Changes show for a moment at the bottom of the screen. In HW_LOG builds the
// player's pad pages the overlay with R and L, and the menu is one of its
// pages, where its d-pad and A, B, C work the menu too (Lara stands still).

#include "debug.h"
#include "common.h"
#include "crash.h"
#include "dsp.h"
#include "hw.h"
#include "slave.h"
#include "input.h"
#include "overlay.h"
#include "palettes.h"
#include "screen.h"
#include "system.h"
#include "trace.h"

struct Option
{
    const char* name;
    const char* const* values;
    int32 count;
    int32 (*get)();
    void (*set)(int32 value);
};

static const char* const kPalette[] = { "Saturn", "Remastered" };
static const char* const kOnOff[] = { "Off", "On" };
static const char* const kTrace[] = { "Off", "Stages", "Detail" };

static bool gShowStats;
#ifdef HW_LOG
static void benchUpdate();
static void frameShown(uint32_t plot);
#define FRAME_ROW   (OVERLAY_ROWS - 2)  // and the row under it (frameShown)
static volatile bool gFrameRows;    // they are pinned
static void sceneUpdate();
#endif

static const Option gOptions[] = {
    { "Palette", kPalette, 2,
        [] { return int32(palettesRemastered()); },
        [](int32 v) { if (v != palettesRemastered()) palettesToggle(); } },
    { "Stats", kOnOff, 2,
        [] { return int32(gShowStats); },
        [](int32 v) { gShowStats = v; } },
#ifdef HW_LOG
    { "Trace", kTrace, TRACE_MODES,
        [] { return int32(traceMode()); },
        [](int32 v) { traceSetMode(v); } },
#endif
};

enum { OPTION_PALETTE, OPTION_STATS,
#ifdef HW_LOG
       OPTION_TRACE,
#endif
       OPTION_COUNT };
static_assert(OPTION_COUNT == sizeof(gOptions) / sizeof(gOptions[0]), "an OPTION_ for each option");

static uint16 gPad;
static int32 gCursor;
#ifndef HW_LOG
static bool gMenuOpen;
#endif
#ifdef HW_LOG
// R and L page through the overlay: the bench's table, then (held on the
// menu's Way, Base unless changed) the scene's rows with each frame's CPU,
// slave and VDP1 time, each frame's time by stage, the menu over each frame's
// times, nothing at all, and the trace's playback (trace.h), which holds the
// game.
enum OverlayPage { PAGE_BENCH, PAGE_FRAME, PAGE_STAGES, PAGE_MENU, PAGE_NONE, PAGE_PLAYBACK, PAGE_COUNT };
static int32 gPage;
#endif

#ifdef HW_LOG
// The playback page holds the game and shows the trace's frames (trace.h)
// alone: on this colour round the overlay's text, for tools/trace.py to find.
#define PLAYBACK_BACK   RGB(0, 16, 31)

static void playbackStart()
{
    crashHold(true);    // the game loop stands still
    screenFramesHidden(true);
    vdp2SetBackColor(PLAYBACK_BACK);
    tracePlayStart(0);
}

static void playbackEnd()
{
    tracePlayStop();
    vdp2SetBackColor(RGB(0, 0, 0));
    screenFramesHidden(false);
    crashHold(false);
}

bool debugHeld()
{
    return gPage == PAGE_PLAYBACK;
}

void debugPlayFrame()
{
    tracePlayFrame();
}
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

static bool menuShown()
{
#ifdef HW_LOG
    return gPage == PAGE_MENU;
#else
    return gMenuOpen;
#endif
}

void debugUpdate()
{
    traceBegin(TRACE_MASTER, TR_DEBUG);
    uint16 pad = padDebug();

#ifdef HW_LOG
    // the player's pad: Lara has no use for its shoulder buttons
    static uint32 shoulders;
    uint32 pressed2 = keys & (IK_L | IK_R) & ~shoulders;
    int32 was = gPage;
    if (pressed2 & IK_R) {
        gPage = (gPage + 1) % PAGE_COUNT;
    } else if (pressed2 & IK_L) {
        gPage = (gPage + PAGE_COUNT - 1) % PAGE_COUNT;
    }
    shoulders = keys & (IK_L | IK_R);
    if (gPage != was && was == PAGE_PLAYBACK) {
        playbackEnd();
    }
    if (gPage != was && gPage == PAGE_PLAYBACK) {
        playbackStart();
    }
    if (gPage == PAGE_MENU)
    {
        pad |= padButtons() & ~(PAD_START | PAD_L | PAD_R);
        keys = 0;
    }
#endif

    uint16 pressed = pad & ~gPad;
    gPad = pad;

#ifndef HW_LOG
    if (pressed & PAD_START) {
        gMenuOpen = !gMenuOpen;
    }
#endif

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

#ifdef HW_LOG
    vdp1OnShown(frameShown);
    if ((gPage == PAGE_BENCH || gPage == PAGE_NONE || gPage == PAGE_PLAYBACK) && gFrameRows)
    {
        gFrameRows = false;
        overlayUnpin(FRAME_ROW);
        overlayUnpin(FRAME_ROW + 1);
    }
    benchUpdate();
    sceneUpdate();
#endif
    traceEnd(TRACE_MASTER, TR_DEBUG);
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
// Hardware measurements without a second pad: while the bench page shows,
// every 4 seconds of play the frame's averages over the last 3 seconds, and
// where the master's time went then.
#define BENCH_WINDOW    240     // vblanks a window lasts
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

// the frame's VDP1 and master CPU time, the master's drawing (its calls,
// before flush), the slave's work in the frame
static struct { uint32 vdp1, cpu, draw, slave, fps; } gBench;

// and the CPUs' time by stage, as gStats keeps it: the master's update, calls,
// flush (the frame's stages, the list's upload) and waits in the frame; the
// slave's work and waits
enum { SPLIT_UPDATE, SPLIT_DRAW, SPLIT_FLUSH, SPLIT_MASTER_WAIT, SPLIT_SLAVE, SPLIT_SLAVE_WAIT, SPLIT_COUNT };
static const char* const kSplitNames[SPLIT_COUNT] = { "upd ", "draw ", "flush ", "wait ", "work ", "wait " };
static uint32 gSplit[SPLIT_COUNT];

// A sampling profiler for the master CPU, while the bench page measures: the
// free-running timer's compare A interrupts it every PROFILE_TICKS (128
// cycles each) and up to 31 more at random, and the interrupted PC counts
// towards its 64-byte block of the program.
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
#define PROFILE_TOP     15      // three to a row, under the table

static_assert(8 + PROFILE_TOP / 3 <= OVERLAY_ROWS, "the profile's rows fit the overlay");

extern "C" void profileEntry();     // irq.s
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
    uint16* profile = traceProfileBlocks();   // held at 0xFFFF
    uint32 offset = (pc & 0x07FFFFFF) - TRACE_PROFILE_START;
    if (profile && offset < uint32(gTextEnd) - TRACE_PROFILE_START)
    {
        uint16 &n = profile[offset >> TRACE_PROFILE_SHIFT];
        if (n != 0xFFFF) {
            n++;
        }
    }
    else
    {
        gProfileOther++;
    }
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
    const uint16* profile = traceProfileBlocks();
    if (!total || !profile)
        return;
    gProfileRanked = total;
    memset(gProfileTop, 0, sizeof(gProfileTop));
    for (int32 b = 0, blocks = traceProfileCount(); b < blocks; b++)
    {
        uint32 share = profile[b] * 1000 / total;
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

// The bench table's rows, in traceText.
typedef char OverlayText[OVERLAY_COLUMNS + 1];
#define BENCH_TEXT_AT   320
static_assert(BENCH_TEXT_AT + OVERLAY_ROWS * sizeof(OverlayText) <= TRACE_TEXT_BYTES, "the bench's rows fit in traceText");

static OverlayText* benchRows()
{
    char* text = traceText();
    return text ? (OverlayText*)(text + BENCH_TEXT_AT) : nullptr;
}

static bool gBenchChanged = true;   // the bench's rows to compose again

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
    if (gPage == PAGE_BENCH || gPage == PAGE_NONE || gPage == PAGE_PLAYBACK)
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
    static uint32 generation;

    // the trace's memory set up afresh: the profile counts again from empty
    // blocks (emptied again here of the samples since), and the rows kept
    // there are composed again
    if (generation != traceGeneration())
    {
        generation = traceGeneration();
        if (uint16* profile = traceProfileBlocks()) {
            memset(profile, 0, traceProfileCount() * sizeof(uint16));
        }
        gProfileSamples = gProfileOther = 0;
        gBenchChanged = true;
    }

    // a fresh window when the page shows again
    if (gPage != PAGE_BENCH)
    {
        profileSampling(false);
        start = gVblankCount;
        frames = vdp1 = cpu = draw = slave = 0;
        memset(split, 0, sizeof(split));
        return;
    }

    uint32 t = gVblankCount - start;
    profileSampling(t >= BENCH_SETTLE && t < BENCH_WINDOW);
    if (t >= BENCH_SETTLE)
    {
        frames++;
        vdp1 += gStats.vdp1Ticks;
        cpu += gStats.updateTicks + gStats.renderTicks - gStats.waitTicks;
        draw += gStats.drawTicks;
        slave += gStats.slaveXformTicks;
        split[SPLIT_UPDATE] += gStats.updateTicks;
        split[SPLIT_DRAW] += gStats.drawTicks;
        split[SPLIT_FLUSH] += gStats.flushTicks;
        split[SPLIT_MASTER_WAIT] += gStats.slaveWaitTicks;
        split[SPLIT_SLAVE] += gStats.slaveXformTicks;
        split[SPLIT_SLAVE_WAIT] += gStats.slaveIdleTicks;
    }

    if (t < BENCH_WINDOW)
        return;

    if (frames)
    {
        gBench.vdp1 = framePercent(vdp1 / frames);
        gBench.cpu = framePercent(cpu / frames);
        gBench.draw = framePercent(draw / frames);
        gBench.slave = framePercent(slave / frames);
        gBench.fps = frames * 60 / (t - BENCH_SETTLE);
        for (int32 i = 0; i < SPLIT_COUNT; i++) {
            gSplit[i] = framePercent(split[i] / frames);
        }
    }
    profileRank();
    gBenchChanged = true;
    memset(split, 0, sizeof(split));
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
static void benchCompose(OverlayText* rows)
{
    memset(rows, 0, OVERLAY_ROWS * sizeof(OverlayText));
    char text[64];

    // the master's stages, then the slave's
    char* p = text;
    *p = 0;
    for (int32 i = 0; i < SPLIT_SLAVE; i++) {
        p = appendStat(p, kSplitNames[i], gSplit[i]);
    }
    strncpy(rows[1], text, OVERLAY_COLUMNS);
    p = append(text, "slave ");
    for (int32 i = SPLIT_SLAVE; i < SPLIT_COUNT; i++) {
        p = appendStat(p, kSplitNames[i], gSplit[i]);
    }
    strncpy(rows[2], text, OVERLAY_COLUMNS);

    strncpy(rows[4], " vdp1  cpu draw  slv  fps", OVERLAY_COLUMNS);
    p = text;
    *p = 0;
    if (gBench.fps)
    {
        p = appendColumn(p, gBench.vdp1);
        p = appendColumn(p, gBench.cpu);
        p = appendColumn(p, gBench.draw);
        p = appendColumn(p, gBench.slave);
        appendColumn(p, gBench.fps);
    }
    strncpy(rows[5], text, OVERLAY_COLUMNS);

    int32 row = 7;
    p = append(text, "profile ");
    appendStat(p, "samples ", gProfileRanked);
    strncpy(rows[row++], text, OVERLAY_COLUMNS);
    for (int32 i = 0; i < PROFILE_TOP && row < OVERLAY_ROWS; i += 3, row++)
    {
        p = text;
        for (int32 j = i; j < i + 3 && gProfileTop[j].share; j++)
        {
            static const char hex[] = "0123456789abcdef";
            uint32 addr = (TRACE_PROFILE_START & 0xFFFFF) + (gProfileTop[j].block << TRACE_PROFILE_SHIFT);
            for (int32 d = 16; d >= 0; d -= 4) {
                *p++ = hex[(addr >> d) & 15];
            }
            p = appendTenths(p, gProfileTop[j].share);
            p = append(p, "   ");
        }
        *p = 0;
        strncpy(rows[row], text, OVERLAY_COLUMNS);
    }
}

// The display's health (system.h SyncStats), a second at a time: field
// length in FRT ticks (least-most), TVSTAT ORed/ANDed, TVMD and EXTEN read
// back, VDP1's EDSR at the last frame change.
static const char* syncRow()
{
    static char text[64];   // 42 columns with 4-digit fields; a stall can make them 5
    static uint32 start;
    if (gVblankCount - start < 60 && text[0])
        return text;
    start = gVblankCount;
    SyncStats s = syncStatsTake();
    char* p = appendStat(text, "fld ", s.least);
    p = appendStat(p - 2, "-", s.most);
    p = append(p - 1, "st ");
    p = appendHex(p, s.statOr, 3);
    p = append(p, "/");
    p = appendHex(p, s.statAnd, 3);
    p = append(p, " md ");
    p = appendHex(p, VDP2_TVMD, 4);
    p = append(p, " ex ");
    p = appendHex(p, VDP2_EXTEN, 1);
    p = append(p, " ed ");
    appendHex(p, s.edsr, 1);
    return text;
}

static void benchDraw()
{
    OverlayText* rows = benchRows();
    if (!rows)
        return;
    if (gBenchChanged)
    {
        benchCompose(rows);
        gBenchChanged = false;
    }
    overlayRow(3, syncRow());
    for (int32 i = 0; i < OVERLAY_ROWS; i++)
    {
        if (rows[i][0]) {
            overlayRow(i, rows[i]);
        }
    }
}
#endif

#if defined(HW_LOG) && defined(SYNC_TEST)
static char* appendShare(char* p, const char* name, uint32 value)
{
    p = append(p, name);
    int2str(int32(value), p);
    while (*p) {
        p++;
    }
    return append(p, " ");
}

typedef char TraceRow[OVERLAY_COLUMNS + 1];

// The boot's sync test (debug.h), under the stages page's rows.
#define SYNC_ROWS   3
#define SYNC_TEXT_AT 1472
static_assert(SYNC_TEXT_AT + SYNC_ROWS * sizeof(TraceRow) <= TRACE_TEXT_BYTES, "the sync rows fit in traceText");
static_assert(BENCH_TEXT_AT + OVERLAY_ROWS * sizeof(OverlayText) <= SYNC_TEXT_AT, "the bench's rows end before them");

static void syncRows(int32 row)
{
    char* text = traceText();
    if (!text)
        return;
    TraceRow* rows = (TraceRow*)(text + SYNC_TEXT_AT);
    if (!rows[0][0])    // once for each trace area: a fresh one is empty
    {
        const SyncResults &s = gSync;
        char line[64];
        char* p = appendShare(line, "sync lost raw ", s.raw);
        p = appendShare(p, "tas ", s.tas);
        p = appendShare(p, "pet ", s.peterson);
        appendShare(p, "claim ", s.claim);
        strncpy(rows[0], line, OVERLAY_COLUMNS);

        p = appendShare(line, "order ", s.order);
        p = appendShare(p, "took ", s.claimMaster);
        p = appendShare(p, "/", s.claimSlave);
        p = appendShare(p, "gbr ", s.gbr);
        appendShare(p, "kept ", debugSyncKept());
        strncpy(rows[1], line, OVERLAY_COLUMNS);

        p = appendShare(line, "cycles unc ", s.uncachedCycles);
        p = appendShare(p, "lock ", s.lockCycles);
        p = appendShare(p, "tas2 ", s.tasCycles);
        appendShare(p, "pet2 ", s.petersonCycles);
        strncpy(rows[2], line, OVERLAY_COLUMNS);
    }
    for (int32 i = 0; i < SYNC_ROWS; i++) {
        overlayRow(row + i, rows[i]);
    }
}
#endif

// Over the frame on VDP2 (overlay.h), with the fps counter: the frame's
// measurements don't count them.
static void drawDebug();

void osDrawDebug()
{
    traceBegin(TRACE_MASTER, TR_OVERLAY);
    drawDebug();
    traceEnd(TRACE_MASTER, TR_OVERLAY);
}

static void drawDebug()
{
    char text[64];

#ifdef HW_LOG
    if (gPage == PAGE_NONE)
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

    if (gShowStats)
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
            for (int32 i = 0; i < SCENE_ROWS; i++) {
                overlayRow(2 + i, gSceneRows[i]);
            }
#ifdef SYNC_TEST
            if (gPage == PAGE_STAGES) {
                syncRows(3 + SCENE_ROWS);
            }
#endif
        }
        else
        {
            benchDraw();
        }
    }
#endif

    overlayFrame();
}
