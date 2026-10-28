// The frame timeline (see trace.h).

#include "trace.h"
#include "crash.h"
#include "slave.h"

#include <string.h>

#ifdef HW_LOG

#define TRACE_HEADER    0x002FFF00  // the top of low work RAM, where tools/trace.py looks
#define TRACE_MAGIC     0x54524331  // "TRC1"
#define TRACE_DEPTH     8           // spans open at once on a lane
#define FRAME_TICKS     7457        // FRT ticks in a 30 fps frame

// Events each lane's ring holds at most, powers of two: some 120 frames with
// the detail spans. Halved together until they fit between the level and the
// header (LEVEL1 takes them whole).
static const uint32_t kRingSize[TRACE_LANES] = { 16384, 2048, 16384, 1024, 1024 };
#define TRACE_RING_MIN  256

struct TraceHeader
{
    uint32_t magic;         // TRACE_MAGIC while the lanes record
    uint32_t slaveOffset;   // the slave's FRT count less the master's
    // next: events written; vblank: gVblankCount at the last, which places
    // a lane that has stopped (its 16-bit times alone can't)
    struct { uint32_t ring, size, next, vblank; } lanes[TRACE_LANES];
    uint32_t profile;       // TraceArea's profile blocks (tools/profile_map.py)
};

// The lanes the slave writes, which the master reads past its cache.
static bool slaveLane(int32_t lane)
{
    return lane == TRACE_SLAVE || lane == TRACE_DSP;
}

struct Open
{
    uint32_t id, start, inner;  // inner: the time of the spans inside it
};

struct TraceTally
{
    uint32_t read[TRACE_LANES];     // events added up
    Open open[TRACE_LANES][TRACE_DEPTH];
    uint32_t depth[TRACE_LANES];
    uint32_t self[TR_FIRST_MOMENT]; // ticks, this window
    uint32_t busy[TRACE_LANES];     // ticks in spans, this window
    uint32_t events, frames;
    uint32_t start;                 // gVblankCount at the window's start
};

// Above the level, the rings after it.
struct TraceArea
{
    TraceTally tally;
    TraceStages stages;
    uint16_t profile[TRACE_PROFILE_BLOCKS];
    char text[TRACE_TEXT_BYTES];
};

static TraceHeader* const gHeader = (TraceHeader*)TRACE_HEADER;
static TraceArea* gArea;        // NULL without room

// The writers' copies, in high work RAM: each lane is written by one CPU only
struct Lane
{
    uint32_t* ring;
    uint32_t mask, next;
};

static Lane gLanes[TRACE_LANES];
static uint32_t gSlaveOffset;
static int32_t gMode = TRACE_STAGES;
bool gTraceOn;
bool gTraceDetail;

void traceSetMode(int32_t mode)
{
    gMode = mode;
    gTraceOn = gArea && mode != TRACE_OFF;
    gTraceDetail = gTraceOn && mode == TRACE_DETAIL;
}

int32_t traceMode()
{
    return gMode;
}

void traceAt(int32_t lane, uint32_t ticks, uint32_t code)
{
    if (!gTraceOn)
        return;

    // an interrupt may interrupt another, both writing their lane
    uint32_t sr;
    asm volatile("stc sr, %0" : "=r"(sr));
    asm volatile("ldc %0, sr" : : "r"(sr | 0xF0));
    Lane &l = gLanes[lane];
    uint32_t n = l.next;
    l.ring[n & l.mask] = (ticks << 16) | code;
    l.next = n + 1;
    gHeader->lanes[lane].next = n + 1;
    gHeader->lanes[lane].vblank = *uncached(&gVblankCount);
    asm volatile("ldc %0, sr" : : "r"(sr));
}

static volatile uint32_t gSyncGo, gSyncSlave;

static void syncTask()
{
    while (!*uncached(&gSyncGo));
    *uncached(&gSyncSlave) = timerTicks();
}

// The slave reads its count as soon as it sees that the master has read its
// own: the least difference of a few tries is the offset, to a tick.
void traceInit()
{
    uint32_t first = 0;
    int32_t least = 0x7FFF;
    for (int32_t i = 0; i < 8; i++)
    {
        *uncached(&gSyncGo) = 0;
        slaveRun(syncTask);
        for (volatile int32_t d = 0; d < 1000; d++);   // until it polls
        uint32_t master = timerTicks();
        *uncached(&gSyncGo) = 1;
        slaveWait();
        uint32_t offset = (*uncached(&gSyncSlave) - master) & 0xFFFF;
        if (i == 0) {
            first = offset;
        }
        int32_t lag = int16_t(offset - first);
        if (lag < least) {
            least = lag;
        }
    }
    gSlaveOffset = (first + least) & 0xFFFF;
    crashLog("TRSYNC", gSlaveOffset);
}

void traceLevel(const void* end)
{
    traceStop();
    uint32_t base = (uintptr_t(end) + 15) & ~15;
    int32_t room = int32_t(TRACE_HEADER - base - sizeof(TraceArea)) / 4;   // ring events
    int32_t shift = 0;
    uint32_t total = 0;
    for (int32_t i = 0; i < TRACE_LANES; i++) {
        total += kRingSize[i];
    }
    while (room >= 0 && total >> shift > uint32_t(room)) {
        shift++;
    }
    if (room < 0 || (kRingSize[TRACE_DSP] >> shift) < TRACE_RING_MIN)
    {
        crashLog("TRACE", 0);
        return;
    }

    TraceArea* area = (TraceArea*)base;
    memset(area, 0, sizeof(TraceArea));
    memset(gHeader, 0, sizeof(TraceHeader));
    gHeader->slaveOffset = gSlaveOffset;
    gHeader->profile = uint32_t(area->profile);
    uint32_t* ring = (uint32_t*)(area + 1);
    for (int32_t i = 0; i < TRACE_LANES; i++)
    {
        uint32_t size = kRingSize[i] >> shift;
        gLanes[i].ring = ring;
        gLanes[i].mask = size - 1;
        gLanes[i].next = 0;
        gHeader->lanes[i].ring = uint32_t(ring);
        gHeader->lanes[i].size = size;
        ring += size;
    }
    area->tally.start = gVblankCount;
    gHeader->magic = TRACE_MAGIC;
    crashLog("TRACE", kRingSize[0] >> shift);
    gArea = area;
    traceSetMode(gMode);
}

void traceStop()
{
    gArea = nullptr;
    traceSetMode(gMode);
}

uint16_t* traceProfileBlocks()
{
    return gArea ? gArea->profile : nullptr;
}

char* traceText()
{
    return gArea ? gArea->text : nullptr;
}

// Ends the innermost span with this id and any left open inside it; an end
// without its begin (from before the rings were on) is dropped.
static void tallyEnd(TraceTally &t, int32_t lane, uint32_t id, uint32_t ticks)
{
    uint32_t &depth = t.depth[lane];
    Open* open = t.open[lane];
    int32_t found = int32_t(depth) - 1;
    while (found >= 0 && open[found].id != id) {
        found--;
    }
    if (found < 0)
        return;

    while (int32_t(depth) > found)
    {
        Open &o = open[--depth];
        uint32_t span = (ticks - o.start) & 0xFFFF;
        t.self[o.id] += span > o.inner ? span - o.inner : 0;
        if (depth > 0) {
            open[depth - 1].inner += span;
        } else {
            t.busy[lane] += span;
        }
    }
}

static void tallyEvent(TraceTally &t, int32_t lane, uint32_t e)
{
    uint32_t ticks = e >> 16;
    uint32_t code = (e >> 8) & 0xFF;
    uint32_t id = code & ~TRACE_END;
    t.events++;
    if (id >= TR_FIRST_MOMENT)
        return;

    if (code & TRACE_END)
    {
        tallyEnd(t, lane, id, ticks);
        if (id == TR_TICK_WAIT) {
            t.frames++;
        }
    }
    else if (t.depth[lane] < TRACE_DEPTH)
    {
        t.open[lane][t.depth[lane]++] = { id, ticks, 0 };
    }
}

void traceTally(bool sum)
{
    if (!gTraceOn)
        return;
    TraceTally &t = gArea->tally;
    if (!sum)
    {
        // a fresh window when the page comes back
        for (int32_t lane = 0; lane < TRACE_LANES; lane++)
        {
            t.read[lane] = slaveLane(lane) ? uncached(gLanes + lane)->next : gLanes[lane].next;
            t.depth[lane] = 0;
        }
        memset(t.self, 0, sizeof(t.self));
        memset(t.busy, 0, sizeof(t.busy));
        t.events = t.frames = 0;
        t.start = gVblankCount;
        gArea->stages.valid = false;
        return;
    }

    for (int32_t lane = 0; lane < TRACE_LANES; lane++)
    {
        // the slave's lanes from memory, past this CPU's cache
        bool slave = slaveLane(lane);
        uint32_t written = slave ? uncached(gLanes + lane)->next : gLanes[lane].next;
        const uint32_t* ring = slave ? uncached(gLanes[lane].ring) : gLanes[lane].ring;
        uint32_t mask = gLanes[lane].mask;
        uint32_t n = t.read[lane];
        if (written - n > mask)
        {
            n = written - mask;     // overrun: what is left
            t.depth[lane] = 0;
        }
        for (; n != written; n++) {
            tallyEvent(t, lane, ring[n & mask]);
        }
        t.read[lane] = written;
    }

    uint32_t vblanks = gVblankCount - t.start;
    if (vblanks < TRACE_WINDOW)
        return;

    // percent of the window: ticks * 2 / vblanks of FRAME_TICKS
    TraceStages &s = gArea->stages;
    uint32_t window = vblanks * FRAME_TICKS;
    for (int32_t i = 0; i < TR_FIRST_MOMENT; i++) {
        s.self[i] = t.self[i] * 200 / window;
    }
    for (int32_t i = 0; i < TRACE_LANES; i++) {
        s.busy[i] = t.busy[i] * 200 / window;
    }
    s.frames = t.frames;
    s.vblanks = vblanks;
    s.events = t.frames ? t.events / t.frames : 0;
    s.window++;
    s.valid = true;

    memset(t.self, 0, sizeof(t.self));
    memset(t.busy, 0, sizeof(t.busy));
    t.events = t.frames = 0;
    t.start = gVblankCount;
}

const TraceStages* traceStages()
{
    return gTraceOn ? &gArea->stages : nullptr;
}

#endif
