// The frame timeline (see trace.h).

#include "trace.h"
#include "crash.h"
#include "overlay.h"
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

// The playback (tracePlay...): a lane's place in its ring, and that event's
// time on one timeline (master ticks, 0 at the master's last event).
struct Cursor
{
    uint32_t n;
    int32_t t;
};

// An event on the page: its begin and end in tenths of a millisecond from the
// frame's start, and its lane's place on the page (kGroup).
struct Shown
{
    uint16_t begin, end;
    uint8_t letter, group;
};

#define PLAY_EVENTS     256     // a frame's, before the page's room
#define PLAY_CELLS      5       // events a row
#define PLAY_ROWS       (OVERLAY_ROWS - 1)
#define PLAY_HOLD       3       // ticks a page stays up once whole
#define PLAY_PLOTS      8

struct TracePlay
{
    uint32_t floor[TRACE_LANES];    // events written when the rings last started
    uint32_t written[TRACE_LANES];  // and when they stopped
    Cursor first[TRACE_LANES];      // each lane's oldest event
    Cursor at[TRACE_LANES];         // its first in the frame shown
    Cursor frame;                   // which starts at the master's end of TR_TICK_WAIT
    int32_t start;                  // the first frame's start
    uint32_t index, frames;         // the frame shown, of how many
    uint32_t phase;                 // ticks it has been up
    uint32_t count, dropped;        // its events
    Shown shown[PLAY_EVENTS];
    char rows[OVERLAY_ROWS][OVERLAY_COLUMNS + 1];
};

// Above the level, the rings after it.
struct TraceArea
{
    TracePlay play;
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
    uint32_t last;      // the last event's time
};

static Lane gLanes[TRACE_LANES];
static uint32_t gSlaveOffset;
static int32_t gMode = TRACE_DETAIL;
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
    // an interrupt between the caller's reading of the time and here may
    // have written a later event: this one goes no earlier (by under a
    // millisecond; farther back is a gap of more than half the count's turn)
    int32_t back = int16_t(ticks - l.last);
    if (back < 0 && back > -256) {
        ticks = l.last;
    }
    l.last = ticks;
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

static const char kLetters[] = TRACE_LETTERS;
static_assert(sizeof(kLetters) == TR_ID_COUNT + 1, "a letter for each id");

// The page's lanes in order: the master, the slave, the DSP's runs for each,
// then the interrupts' (VDP1's and the vblanks).
static const uint8_t kGroup[TRACE_LANES] = { 0, 4, 1, 2, 3 };

#define PLAY_LOOKAHEAD  (FRAME_TICKS * 2)   // past the frame's end, for the ends of its spans

static bool detailId(uint32_t id)
{
    return id == TR_OBJECTS || id == TR_STATIC || id == TR_MESH || id == TR_ITEM;
}

static uint32_t laneEvent(int32_t lane, uint32_t n)
{
    const uint32_t* ring = gLanes[lane].ring;
    if (slaveLane(lane)) {
        ring = uncached(ring);
    }
    return ring[n & gLanes[lane].mask];
}

// An event's 16-bit time on the master's clock.
static uint32_t laneTicks(int32_t lane, uint32_t e)
{
    uint32_t t = e >> 16;
    return slaveLane(lane) ? (t - gSlaveOffset) & 0xFFFF : t;
}

static void advance(int32_t lane, Cursor &c)
{
    uint32_t was = laneTicks(lane, laneEvent(lane, c.n));
    c.n++;
    c.t += (laneTicks(lane, laneEvent(lane, c.n)) - was) & 0xFFFF;
}

// The lane's first event at or after t, from c (its end if none).
static Cursor seek(int32_t lane, Cursor c, int32_t t)
{
    uint32_t written = gArea->play.written[lane];
    while (c.n < written && c.t < t)
    {
        if (c.n + 1 == written)
        {
            c.n = written;
            break;
        }
        advance(lane, c);
    }
    return c;
}

static bool frameStart(uint32_t n)
{
    return ((laneEvent(TRACE_MASTER, n) >> 8) & 0xFF) == (TR_TICK_WAIT | TRACE_END);
}

// The master's next frame start (its end of TR_TICK_WAIT) after c, or false.
static bool nextFrame(Cursor &c)
{
    uint32_t written = gArea->play.written[TRACE_MASTER];
    while (c.n + 1 < written)
    {
        advance(TRACE_MASTER, c);
        if (frameStart(c.n))
            return true;
    }
    return false;
}

// Ticks of 128 cycles at 28.636 MHz in tenths of a millisecond, to 7 parts
// in a million: frames seconds apart line up to the tenth.
static uint32_t tenthsOf(uint32_t ticks)
{
    return uint32_t((uint64_t(ticks) * 46870 + (1 << 19)) >> 20);
}

// ... from a frame's start, at most 999.
static uint32_t tenths(int32_t ticks)
{
    if (ticks <= 0)
        return 0;
    uint32_t v = tenthsOf(ticks);
    return v < 999 ? v : 999;
}

static void show(TracePlay &p, int32_t start, uint32_t id, uint8_t group, int32_t begin, int32_t end, bool open)
{
    if (p.count == PLAY_EVENTS)
    {
        p.dropped++;
        return;
    }
    Shown &s = p.shown[p.count++];
    s.begin = tenths(begin - start);
    s.end = open ? 999 : tenths(end - start);
    s.letter = kLetters[id];
    s.group = group;
}

// The spans of a lane that begin in the frame from start to end, with their
// ends (later, perhaps), and its moments; VDP1's plots and ends gathered.
static void collect(TracePlay &p, int32_t lane, int32_t start, int32_t end, int32_t* plots, int32_t &plotCount,
                    int32_t* drawn, int32_t &drawnCount)
{
    struct { uint32_t id; int32_t t; } open[TRACE_DEPTH];
    int32_t depth = 0;
    uint32_t written = p.written[lane];
    Cursor c = p.at[lane];
    Cursor next = c;
    bool nextFound = false;
    while (c.n < written)
    {
        int32_t t = c.t;
        if (t >= end)
        {
            if (!nextFound)
            {
                next = c;
                nextFound = true;
            }
            // VDP1 may finish the frame's list well into the next
            if ((depth == 0 && lane != TRACE_IRQ) || t >= end + PLAY_LOOKAHEAD)
                break;
        }

        uint32_t code = (laneEvent(lane, c.n) >> 8) & 0xFF;
        uint32_t id = code & ~TRACE_END;
        if (id >= TR_FIRST_MOMENT)
        {
            if (id == TR_PLOT && t < end && plotCount < PLAY_PLOTS) {
                plots[plotCount++] = t;
            } else if (id == TR_DRAWN && drawnCount < PLAY_PLOTS) {
                drawn[drawnCount++] = t;
            } else if (id == TR_SHOWN && t < end) {
                show(p, start, id, kGroup[TRACE_IRQ], t, t, false);
            }
        }
        else if (code & TRACE_END)
        {
            int32_t found = depth - 1;
            while (found >= 0 && open[found].id != id) {
                found--;
            }
            while (found >= 0 && depth > found)
            {
                depth--;
                if (kLetters[open[depth].id] != '.') {
                    show(p, start, open[depth].id, kGroup[lane], open[depth].t, t, false);
                }
            }
        }
        else if (t < end && depth < TRACE_DEPTH)
        {
            open[depth].id = id;
            open[depth].t = t;
            depth++;
        }

        if (c.n + 1 == written)
        {
            c.n = written;
            break;
        }
        advance(lane, c);
    }
    while (depth > 0)
    {
        depth--;
        if (kLetters[open[depth].id] != '.') {
            show(p, start, open[depth].id, kGroup[lane], open[depth].t, 0, true);
        }
    }
    p.at[lane] = nextFound ? next : c;
}

// Fixed-width decimal digits.
static char* digits(char* p, uint32_t value, int32_t width)
{
    for (int32_t i = width - 1; i >= 0; i--)
    {
        p[i] = '0' + value % 10;
        value /= 10;
    }
    return p + width;
}

// The row padded with spaces, and its last character checks it: the sum of
// each character's code times its column (from 1), mod 36, as 0-9 A-Z.
static void rowCheck(char* row)
{
    uint32_t sum = 0;
    bool end = false;
    for (int32_t i = 0; i < OVERLAY_COLUMNS - 1; i++)
    {
        end = end || !row[i];
        if (end) {
            row[i] = ' ';
        }
        sum += uint8_t(row[i]) * uint32_t(i + 1);
    }
    row[OVERLAY_COLUMNS - 1] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[sum % 36];
    row[OVERLAY_COLUMNS] = 0;
}

// The shown frame's events, as many as the page holds (the master's detail
// spans are left out first, the last first), and its rows.
static void composeFrame(TracePlay &p, int32_t start, int32_t end)
{
    p.count = p.dropped = 0;
    int32_t plots[PLAY_PLOTS], drawn[PLAY_PLOTS];
    int32_t plotCount = 0, drawnCount = 0;
    for (int32_t lane = 0; lane < TRACE_LANES; lane++) {
        collect(p, lane, start, end, plots, plotCount, drawn, drawnCount);
    }
    for (int32_t i = 1; i < plotCount; i++)  // the master's and the interrupts' in time order
    {
        int32_t t = plots[i], k = i - 1;
        for (; k >= 0 && plots[k] > t; k--) {
            plots[k + 1] = plots[k];
        }
        plots[k + 1] = t;
    }
    for (int32_t i = 0, d = 0; i < plotCount; i++, d++)
    {
        while (d < drawnCount && drawn[d] <= plots[i]) {
            d++;
        }
        show(p, start, TR_PLOT, kGroup[TRACE_IRQ], plots[i], d < drawnCount ? drawn[d] : 0, d >= drawnCount);
    }

    int32_t room = PLAY_CELLS * PLAY_ROWS;
    for (int32_t pass = 0; pass < 2; pass++)
    {
        for (int32_t i = p.count - 1; i >= 0 && int32_t(p.count) > room; i--)
        {
            if (pass == 0 && !(p.shown[i].group == kGroup[TRACE_MASTER] &&
                               detailId(strchr(kLetters, p.shown[i].letter) - kLetters)))
                continue;
            memmove(p.shown + i, p.shown + i + 1, (p.count - i - 1) * sizeof(Shown));
            p.count--;
            p.dropped++;
        }
    }

    // by lane, then begin, a span before those inside it
    for (uint32_t i = 1; i < p.count; i++)
    {
        Shown s = p.shown[i];
        int32_t k = i - 1;
        while (k >= 0 && (p.shown[k].group > s.group ||
                          (p.shown[k].group == s.group && (p.shown[k].begin > s.begin ||
                                                           (p.shown[k].begin == s.begin && p.shown[k].end < s.end)))))
        {
            p.shown[k + 1] = p.shown[k];
            k--;
        }
        p.shown[k + 1] = s;
    }

    char* row = p.rows[0];
    memset(row, 0, sizeof(p.rows[0]));
    char* q = row;
    *q++ = 'F';
    q = digits(q, p.index, 3);
    *q++ = '/';
    q = digits(q, p.frames, 3);
    *q++ = ' ';
    *q++ = 'S';
    q = digits(q, tenthsOf(start - p.start), 7);
    *q++ = ' ';
    *q++ = 'L';
    q = digits(q, tenths(end - start), 3);
    *q++ = ' ';
    *q++ = 'E';
    q = digits(q, p.count, 3);
    *q++ = ' ';
    *q++ = 'D';
    digits(q, p.dropped, 3);
    rowCheck(row);

    for (int32_t r = 0; r < PLAY_ROWS; r++)
    {
        row = p.rows[r + 1];
        memset(row, 0, sizeof(p.rows[0]));
        q = row;
        for (int32_t c = 0; c < PLAY_CELLS; c++)
        {
            uint32_t i = r * PLAY_CELLS + c;
            if (i >= p.count)
                break;
            const Shown &s = p.shown[i];
            *q++ = s.letter;
            q = digits(q, s.begin, 3);
            q = digits(q, s.end, 3);
            *q++ = ' ';
        }
        rowCheck(row);
    }
}

// From the first frame: the master's first end of TR_TICK_WAIT once every
// lane has events.
static void playFirst(TracePlay &p)
{
    p.frames = p.index = p.phase = 0;
    int32_t from = INT32_MIN;
    for (int32_t lane = 0; lane < TRACE_LANES; lane++) {
        if (p.first[lane].n < p.written[lane] && p.first[lane].t > from) {
            from = p.first[lane].t;
        }
    }
    p.frame = p.first[TRACE_MASTER];
    if (p.frame.n >= p.written[TRACE_MASTER])
        return;
    while (!frameStart(p.frame.n) || p.frame.t < from)
    {
        if (!nextFrame(p.frame))
            return;
    }
    p.start = p.frame.t;
    for (int32_t lane = 0; lane < TRACE_LANES; lane++) {
        p.at[lane] = seek(lane, p.first[lane], p.start);
    }
    for (Cursor c = p.frame; nextFrame(c);) {
        p.frames++;
    }
}

void tracePlayStart()
{
    if (!gArea)
        return;
    gTraceOn = gTraceDetail = false;

    // each lane's events on the master's timeline: its last placed against
    // the master's last by the vblanks between them (a lane may have stopped
    // long before), then to the tick by its 16-bit time, then back by the
    // differences
    TracePlay &p = gArea->play;
    const TraceHeader* header = uncached(gHeader);
    uint32_t masterVblank = header->lanes[TRACE_MASTER].vblank;
    uint32_t ref = 0;
    for (int32_t lane = 0; lane < TRACE_LANES; lane++)
    {
        uint32_t written = slaveLane(lane) ? uncached(gLanes + lane)->next : gLanes[lane].next;
        uint32_t oldest = written - p.floor[lane] > gLanes[lane].mask ? written - gLanes[lane].mask : p.floor[lane];
        p.written[lane] = written;
        p.first[lane].n = oldest;
        p.first[lane].t = 0;
        if (oldest == written)
            continue;

        uint32_t last = laneTicks(lane, laneEvent(lane, written - 1));
        int32_t t = 0;
        if (lane == TRACE_MASTER) {
            ref = last;
        } else {
            int32_t guess = -int32_t((masterVblank - header->lanes[lane].vblank) * FRAME_TICKS / 2);
            t = guess + int16_t(last - ref - guess);
        }
        for (uint32_t n = written - 1; n > oldest; n--) {
            t -= (laneTicks(lane, laneEvent(lane, n)) - laneTicks(lane, laneEvent(lane, n - 1))) & 0xFFFF;
        }
        p.first[lane].t = t;
    }
    playFirst(p);
}

void tracePlayFrame()
{
    if (!gArea)
        return;
    TracePlay &p = gArea->play;
    if (!p.frames)
    {
        overlayRow(0, "trace: no frames (Trace off in the menu?)");
        overlayFrame();
        return;
    }

    if (p.phase == 0)
    {
        Cursor end = p.frame;
        nextFrame(end);
        composeFrame(p, p.frame.t, end.t);
        p.frame = end;
    }

    // the header last, once the rest is up: a picture with it holds the
    // whole page
    for (int32_t r = 0; r < OVERLAY_ROWS; r++) {
        overlayRow(r, r == 0 && p.phase == 0 ? "" : p.rows[r]);
    }
    overlayFrame();

    if (++p.phase > PLAY_HOLD)
    {
        p.phase = 0;
        if (++p.index == p.frames) {
            playFirst(p);
        }
    }
}

void tracePlayStop()
{
    if (!gArea)
        return;
    TracePlay &p = gArea->play;
    for (int32_t lane = 0; lane < TRACE_LANES; lane++) {
        p.floor[lane] = slaveLane(lane) ? uncached(gLanes + lane)->next : gLanes[lane].next;
    }
    traceSetMode(gMode);
}

#endif
