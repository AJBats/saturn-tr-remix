// The debug pad (port 2). Mednafen keys in brackets:
//   START [Tab]       opens and closes the menu
//   up/down [arrows]  pick an option, left/right change it
//   A [P], B [H], C [G]  step the palette, view and dark faces options
// Changes show for a moment at the bottom of the screen.

#include "debug.h"
#include "common.h"
#include "input.h"
#include "overlay.h"
#include "palettes.h"
#include "render.h"
#include "slave.h"
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
    "Base", "Fixed lines", "LOD x1", "No gouraud", "No texture", "Quiet poll", "Slave late"
};

// each way's VDP1 and master CPU time, the master's on meshes, the slave's
// on rooms (transform and faces)
static struct { uint32 vdp1, cpu, mesh, slave, fps; } gBench[BENCH_COUNT];

// and the base way's CPU time by stage, as gStats keeps it
enum { SPLIT_UPDATE, SPLIT_MESH, SPLIT_SLAVE_WAIT, SPLIT_FLUSH, SPLIT_XFORM, SPLIT_FACES,
       SPLIT_MESH_XFORM, SPLIT_MESH_FACES, SPLIT_COUNT };
static const char* const kSplitNames[SPLIT_COUNT] = { "upd ", "mesh ", "swait ", "flush ", "xform ", "faces ",
                                                      "xform ", "faces " };
static uint32 gSplit[SPLIT_COUNT];

static void benchUpdate()
{
    static uint32 start;
    static uint32 frames, vdp1, cpu, mesh, slave;
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
        mesh += gStats.meshTicks;
        slave += gStats.slaveXformTicks + gStats.slaveFaceTicks;
        split[SPLIT_UPDATE] += gStats.updateTicks;
        split[SPLIT_MESH] += gStats.meshTicks;
        split[SPLIT_SLAVE_WAIT] += gStats.slaveWaitTicks;
        split[SPLIT_FLUSH] += gStats.flushTicks;
        split[SPLIT_XFORM] += gStats.slaveXformTicks;
        split[SPLIT_FACES] += gStats.slaveFaceTicks;
        split[SPLIT_MESH_XFORM] += gStats.meshXformTicks;
        split[SPLIT_MESH_FACES] += gStats.meshFaceTicks;
    }

    if (t < BENCH_WINDOW)
        return;

    if (frames)
    {
        gBench[gBenchMode].vdp1 = framePercent(vdp1 / frames);
        gBench[gBenchMode].cpu = framePercent(cpu / frames);
        gBench[gBenchMode].mesh = framePercent(mesh / frames);
        gBench[gBenchMode].slave = framePercent(slave / frames);
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
    frames = vdp1 = cpu = mesh = slave = 0;
}

// Memory access times, cycles per 16 bytes: cached reads (a cache line
// filled), uncached reads, writes (the cache writes through); low work RAM,
// then high.
enum { PROBE_CACHED, PROBE_UNCACHED, PROBE_WRITE, PROBE_KINDS };
static uint32 gProbe[2][PROBE_KINDS];
static volatile uint32 gProbeSink;

#define PROBE_BYTES (64 * 1024)
#define LOW_RAM     0x00200000

static uint32 probeRead(const volatile uint32* p)
{
    uint32 t0 = timerTicks();
    uint32 sum = 0;
    for (uint32 i = 0; i < PROBE_BYTES / 4; i += 4) {
        sum += p[i] + p[i + 1] + p[i + 2] + p[i + 3];
    }
    uint32 t = (timerTicks() - t0) & 0xFFFF;
    gProbeSink = sum;
    return t;
}

static uint32 probeWrite(volatile uint32* p)
{
    uint32 t0 = timerTicks();
    for (uint32 i = 0; i < PROBE_BYTES / 4; i += 4)
    {
        p[i] = i;
        p[i + 1] = i;
        p[i + 2] = i;
        p[i + 3] = i;
    }
    return (timerTicks() - t0) & 0xFFFF;
}

// Instruction times, tenths of a cycle each, eight to a pass (probe.s): nops
// (the pass's own cost), mac.w from the cache, mac.w with one operand
// streaming from low work RAM, muls.w and dmuls.l each with sts macl, stores
// each followed by a load.
// Then: two dot products a pass as the mesh transform makes them (lds macl,
// three mac.w, sts macl), mul.l with sts macl, byte stores 8 apart (as the
// shade pass's), and reads of a cache line at random in 128 KB of high work
// RAM or 512 KB of low (tenths of a cycle a read, with the xorshift picking it).
enum { OP_NOP, OP_MAC, OP_MAC_LOW, OP_MULS, OP_DMULS, OP_STORE_LOAD,
       OP_DOT, OP_MULL, OP_BYTE_WRITE, OP_RANDOM_HIGH, OP_RANDOM_LOW, OP_KINDS };
static uint32 gOps[OP_KINDS];

extern "C" void probeNop(int32 count);
extern "C" void probeMac(int32 count, const int16* buf);
extern "C" void probeMacStream(int32 count, const int16* stream, const int16* buf);
extern "C" void probeMuls(int32 count);
extern "C" void probeDmuls(int32 count);
extern "C" void probeStoreLoad(int32 count, uint32* buf);
extern "C" void probeDot(int32 count, const int16* buf);
extern "C" void probeMull(int32 count);
extern "C" void probeByteWrite(int32 count, uint8* buf);

#define OP_PASSES   (PROBE_BYTES / 16)  // macStream reads 16 bytes a pass

static uint32 opTime(uint32 t0, uint32 perPass = 8)
{
    return ((timerTicks() - t0) & 0xFFFF) * 128 * 10 / (OP_PASSES * perPass);
}

static uint32 probeRandom(const volatile uint32* base, uint32 lines)
{
    cachePurge();
    uint32 x = 2463534242u;
    uint32 sum = 0;
    uint32 t0 = timerTicks();
    for (uint32 i = 0; i < OP_PASSES; i++)
    {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        sum += base[(x & (lines - 1)) * 4];
    }
    gProbeSink = sum;
    return opTime(t0, 1);
}

void debugMemoryProbe()
{
    uint32 sr;
    asm volatile("stc sr, %0" : "=r"(sr));
    asm volatile("ldc %0, sr" : : "r"(sr | 0xF0));

    // low work RAM is free until the first level loads; the vertex buffer
    // until the first frame
    const uint32 lines = PROBE_BYTES / 16;
    volatile uint32* mem[2] = { (volatile uint32*)LOW_RAM, (volatile uint32*)gVertices };
    for (int32 m = 0; m < 2; m++)
    {
        cachePurge();
        gProbe[m][PROBE_CACHED] = probeRead(mem[m]) * 128 / lines;
        gProbe[m][PROBE_UNCACHED] = probeRead(uncached(mem[m])) * 128 / lines;
        gProbe[m][PROBE_WRITE] = probeWrite(mem[m]) * 128 / lines;
    }

    static int16 buf[16] __attribute__((aligned(16)));
    uint32 t = timerTicks();
    probeNop(OP_PASSES);
    gOps[OP_NOP] = opTime(t);
    t = timerTicks();
    probeMac(OP_PASSES, buf);
    gOps[OP_MAC] = opTime(t);
    cachePurge();
    t = timerTicks();
    probeMacStream(OP_PASSES, (const int16*)LOW_RAM, buf);
    gOps[OP_MAC_LOW] = opTime(t);
    t = timerTicks();
    probeMuls(OP_PASSES);
    gOps[OP_MULS] = opTime(t);
    t = timerTicks();
    probeDmuls(OP_PASSES);
    gOps[OP_DMULS] = opTime(t);
    t = timerTicks();
    probeStoreLoad(OP_PASSES, (uint32*)buf);
    gOps[OP_STORE_LOAD] = opTime(t);
    t = timerTicks();
    probeDot(OP_PASSES, buf);
    gOps[OP_DOT] = opTime(t, 16);
    t = timerTicks();
    probeMull(OP_PASSES);
    gOps[OP_MULL] = opTime(t);
    t = timerTicks();
    probeByteWrite(OP_PASSES, (uint8*)gVertices);
    gOps[OP_BYTE_WRITE] = opTime(t);
    gOps[OP_RANDOM_HIGH] = probeRandom((const volatile uint32*)gVertices, 128 * 1024 / 16);
    gOps[OP_RANDOM_LOW] = probeRandom((const volatile uint32*)LOW_RAM, 512 * 1024 / 16);

    asm volatile("ldc %0, sr" : : "r"(sr));
}

// Rows 1 on: the CPU stages, then the table, then memory access times.
static void benchDraw()
{
    char text[64];

    // the master's stages, then the slave's
    char* p = text;
    *p = 0;
    for (int32 i = 0; i < SPLIT_XFORM; i++) {
        p = appendStat(p, kSplitNames[i], gSplit[i]);
    }
    overlayRow(1, text);
    p = append(text, "slave  ");
    for (int32 i = SPLIT_XFORM; i < SPLIT_MESH_XFORM; i++) {
        p = appendStat(p, kSplitNames[i], gSplit[i]);
    }
    overlayRow(2, text);
    // the master's mesh stage in parts
    p = append(text, "mesh   ");
    for (int32 i = SPLIT_MESH_XFORM; i < SPLIT_COUNT; i++) {
        p = appendStat(p, kSplitNames[i], gSplit[i]);
    }
    appendStat(p, "setup ", gSplit[SPLIT_MESH] - gSplit[SPLIT_MESH_XFORM] - gSplit[SPLIT_MESH_FACES]);
    overlayRow(3, text);

    overlayRow(4, "   vdp1  cpu mesh  slv  fps");
    for (int32 i = 0; i < BENCH_COUNT; i++)
    {
        p = append(text, i == gBenchMode ? "> " : "  ");
        if (gBench[i].fps)
        {
            p = appendColumn(p, gBench[i].vdp1);
            p = appendColumn(p, gBench[i].cpu);
            p = appendColumn(p, gBench[i].mesh);
            p = appendColumn(p, gBench[i].slave);
            p = appendColumn(p, gBench[i].fps);
        }
        else
        {
            p = append(p, "                         ");
        }
        p = append(p, "  ");
        append(p, kBenchNames[i]);
        overlayRow(5 + i, text);
    }

    overlayRow(6 + BENCH_COUNT, "cycles/16B  read  uncached  write");
    static const char* const kMemory[2] = { "low RAM   ", "high RAM  " };
    for (int32 m = 0; m < 2; m++)
    {
        p = append(text, kMemory[m]);
        p = appendColumn(p, gProbe[m][PROBE_CACHED]);
        p = append(p, "     ");
        p = appendColumn(p, gProbe[m][PROBE_UNCACHED]);
        p = append(p, "  ");
        appendColumn(p, gProbe[m][PROBE_WRITE]);
        overlayRow(7 + BENCH_COUNT + m, text);
    }

    overlayRow(10 + BENCH_COUNT, "op x10   nop  mac macLo muls dmuls stld");
    p = append(text, "cycles ");
    for (int32 i = 0; i < OP_DOT; i++) {
        p = appendColumn(p, gOps[i]);
    }
    overlayRow(11 + BENCH_COUNT, text);
    overlayRow(12 + BENCH_COUNT, "op x10   dot mul.l byteW rndHi rndLo");
    p = append(text, "cycles ");
    for (int32 i = OP_DOT; i < OP_KINDS; i++) {
        p = appendColumn(p, gOps[i]);
    }
    overlayRow(13 + BENCH_COUNT, text);
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
    if (!gMenuOpen) {
        benchDraw();
    }
#endif

    overlayFrame();
}
