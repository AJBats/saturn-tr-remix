// VDP1's frame buffer change timed on the Saturn: a program of its own (make
// vdp1bench), which runs its tests at boot and shows the results as pages of
// text to photograph, each for 8 seconds in turn.
//
// The game asks for the change at the vblank-in after VDP1 has drawn a list
// (saturn/system.cpp), so a list ending later in that vblank waits a whole field
// more, though the change itself comes at the display's start (issue 241).
// These say how late the Saturn still takes the request.
//
// Page 1, asked with VDP1 idle: a plot of nothing, then the request written at
// each line from the end of the display to the end of the vblank. "same" is a
// change in this vblank, "next" one a field later.
// Page 2, as a game would: lists of small polygons sized to end at a line of the
// field, the request written as soon as VDP1 ends, the next plot at the display's
// start right after the change, frame after frame. "hung" is a plot that never
// ended.
//
// A change shows twice over: EDSR's end flag moves on, and a word written to the
// draw buffer can no longer be read there.

#include "platform.h"
#include "hw.h"

#define EDSR_CEF        0x0002
#define FBCR_CHANGE     0x0003  // FCM | FCT: change frame buffers at the next field
#define DISPLAY_LINES   224
#define MARK            (*(volatile uint16_t*)(VDP1_FB + (255 * 512 + 511) * 2))    // off the picture

#define FIRST_LINE      216     // where the sweeps start
#define MAX_LINES       64
#define TRIALS          6
#define LIST_FRAMES     16
#define SETTLE_FRAMES   4       // a list's first frames, sizing it
#define POLY_MAX        8000
#define POLY_SIZE       16

static char gRows[2][TEXT_ROWS][TEXT_COLUMNS + 1];
static int32_t gCounts[2];

static void add(int32_t page, const char* text)
{
    if (gCounts[page] >= TEXT_ROWS)
        return;
    char* row = gRows[page][gCounts[page]++];
    int32_t i = 0;
    for (; i < TEXT_COLUMNS && text[i]; i++) {
        row[i] = text[i];
    }
    row[i] = 0;
}

static char* put(char* p, const char* s)
{
    while (*s) {
        *p++ = *s++;
    }
    *p = 0;
    return p;
}

static char* putInt(char* p, int32_t value, int32_t width)
{
    char digits[12];
    int32_t n = 0;
    uint32_t v = value < 0 ? -value : value;
    do
    {
        digits[n++] = '0' + v % 10;
        v /= 10;
    } while (v);
    if (value < 0) {
        digits[n++] = '-';
    }
    for (int32_t i = n; i < width; i++) {
        *p++ = ' ';
    }
    while (n) {
        *p++ = digits[--n];
    }
    *p = 0;
    return p;
}

// value / 100 as "12.34"
static char* putHundredths(char* p, uint32_t value)
{
    p = putInt(p, value / 100, 0);
    *p++ = '.';
    *p++ = '0' + value / 10 % 10;
    *p++ = '0' + value % 10;
    *p = 0;
    return p;
}

static void progress(const char* what, int32_t value)
{
    char text[TEXT_COLUMNS + 1];
    char* p = put(text, "VDP1 BENCH measuring ");
    p = put(p, what);
    putInt(p, value, 4);
    textRow(0, text);
}

static uint32_t line()
{
    (void)VDP2_EXTEN;   // latch the counter
    return VDP2_VCNT & 0x1FF;
}

// --- the field ----------------------------------------------------------------

static uint32_t gFieldCycles, gLineCycles;
static uint32_t gVblankOn, gVblankOff;
static uint16_t gSweep[MAX_LINES];  // the lines the counter shows from FIRST_LINE on, in order
static int32_t gSweepLines;
static int32_t gFieldLines;

static void waitWrap()
{
    uint32_t last = line();
    while (true)
    {
        uint32_t now = line();
        if (now < last)
            return;
        last = now;
    }
}

// A field from line 0 to line 0: the lines the counter goes through, and where
// the vblank flag comes and goes among them.
static void measureField()
{
    waitWrap();
    uint32_t start = cycles();
    uint32_t last = line();
    bool vblank = VDP2_TVSTAT & TVSTAT_VBLANK;
    gFieldLines = 1;
    while (true)
    {
        uint32_t now = line();
        bool flag = VDP2_TVSTAT & TVSTAT_VBLANK;
        if (flag != vblank)
        {
            vblank = flag;
            if (flag) {
                gVblankOn = now;
            } else {
                gVblankOff = now;
            }
        }
        if (now == last)
            continue;
        if (now < last)
            break;
        gFieldLines++;
        if (now >= FIRST_LINE && gSweepLines < MAX_LINES) {
            gSweep[gSweepLines++] = now;
        }
        last = now;
    }
    gFieldCycles = cycles() - start;
    gLineCycles = gFieldCycles / gFieldLines;
}

// Lines from the field's start to a line the counter shows: it jumps in the
// vblank, so a line's number doesn't say.
static int32_t linePosition(uint32_t l)
{
    for (int32_t i = 0; i < gSweepLines; i++)
    {
        if (gSweep[i] == l)
            return gFieldLines - gSweepLines + i;
    }
    return l;
}

// --- the list -----------------------------------------------------------------

static volatile vdp1_cmd_t* const gList = (volatile vdp1_cmd_t*)VDP1_VRAM;

static void listBuild()
{
    gList[0].ctrl = CMD_SYSTEM_CLIP;
    gList[0].xc = 351;
    gList[0].yc = 223;
    gList[1].ctrl = CMD_LOCAL_COORD;
    gList[1].xa = 0;
    gList[1].ya = 0;
    for (int32_t i = 2; i < POLY_MAX + 3; i++)
    {
        volatile vdp1_cmd_t& c = gList[i];
        c.ctrl = CMD_POLYGON;
        c.link = 0;
        c.pmod = PMOD_NO_ECD_SPD;
        c.colr = RGB(0, 0, 8);
        c.srca = 0;
        c.size = 0;
        c.xa = 0;
        c.ya = 0;
        c.xb = POLY_SIZE - 1;
        c.yb = 0;
        c.xc = POLY_SIZE - 1;
        c.yc = POLY_SIZE - 1;
        c.xd = 0;
        c.yd = POLY_SIZE - 1;
        c.grda = 0;
    }
}

static int32_t gListEnd = -1;

// Commands from the list's start: the setup, then polys polygons.
static void listLength(int32_t polys)
{
    if (gListEnd >= 0) {
        gList[gListEnd].ctrl = CMD_POLYGON;
    }
    gListEnd = polys + 2;
    gList[gListEnd].ctrl = CMD_END;
}

// --- a change -----------------------------------------------------------------

struct Change
{
    bool edsr, memory;              // seen each way
    uint32_t edsrLine, memoryLine;
    uint32_t cycles;                // from the request to the first of them
};

// The request just written: until the change shows both ways, or two fields.
static Change watchChange(uint16_t mark)
{
    Change c = {};
    uint32_t start = cycles();
    while (!(c.edsr && c.memory))
    {
        uint32_t elapsed = cycles() - start;
        uint32_t l = line();
        if (!c.edsr && !(VDP1_EDSR & EDSR_CEF))
        {
            c.edsr = true;
            c.edsrLine = l;
            c.cycles = elapsed;
        }
        if (!c.memory && MARK != mark)
        {
            c.memory = true;
            c.memoryLine = l;
            if (!c.edsr) {
                c.cycles = elapsed;
            }
        }
        if (elapsed > 2 * gFieldCycles + 16 * gLineCycles)
            break;
    }
    return c;
}

static bool waitDrawn()
{
    uint32_t start = cycles();
    while (!(VDP1_EDSR & EDSR_CEF))
    {
        if (cycles() - start > 3 * gFieldCycles)
        {
            VDP1_ENDR = 0;
            return false;
        }
    }
    return true;
}

static uint16_t gMark = 0x1111;

static uint16_t markBuffer()
{
    gMark++;
    MARK = gMark;
    return gMark;
}

static void waitDisplay(uint32_t from)
{
    while (true)
    {
        uint32_t l = line();
        if (l >= from && l < DISPLAY_LINES)
            return;
        cycles();
    }
}

// --- page 1: asked with VDP1 idle -----------------------------------------------

struct Tally
{
    uint8_t same, next, none;
};

static Tally gIdle[MAX_LINES];
static uint16_t gChangeLines[512];  // the lines the change was seen on
static uint32_t gDisagree;          // EDSR and the buffer showing it apart

static void note(const Change& c)
{
    if (c.edsr) {
        gChangeLines[c.edsrLine]++;
    }
    if (c.edsr != c.memory || (c.edsr && c.edsrLine != c.memoryLine)) {
        gDisagree++;
    }
}

// A request written at line l, and its change: in this field's vblank or later?
static void tally(Tally& t, const Change& c, uint32_t l)
{
    uint32_t toEnd = (gFieldLines - linePosition(l)) * gLineCycles;
    if (!c.edsr || !c.memory) {
        t.none++;
    } else if (c.cycles < toEnd + 16 * gLineCycles) {
        t.same++;
    } else {
        t.next++;
    }
}

static void testIdle()
{
    listLength(0);
    for (int32_t i = 0; i < gSweepLines; i++)
    {
        progress("A", gSweep[i]);
        for (int32_t t = 0; t < TRIALS; t++)
        {
            waitDisplay(8);
            uint16_t mark = markBuffer();
            VDP1_PTMR = 1;
            waitDrawn();
            while (line() != gSweep[i]) {
                cycles();
            }
            VDP1_FBCR = FBCR_CHANGE;
            Change c = watchChange(mark);
            note(c);
            tally(gIdle[i], c, gSweep[i]);
        }
    }
}

// --- page 2: as a game would ----------------------------------------------------

struct ListRow
{
    int32_t target, polys;
    uint32_t endFirst, endLast;     // lines from the field's start
    int32_t same, next, none, hung, inVblank;
};

#define LIST_ROWS   (TEXT_ROWS - 3)
static ListRow gLists[LIST_ROWS];
static int32_t gListRows;

// One list from the display's start, the request as soon as it ends: its time
// to the end in cycles (0: it never ended).
static uint32_t listFrame(Change& c, uint32_t& end)
{
    // the plot as soon as the last change allows, as the game's
    while (line() >= DISPLAY_LINES) {
        cycles();
    }
    uint16_t mark = markBuffer();
    uint32_t start = cycles();
    VDP1_PTMR = 1;
    bool drawn = waitDrawn();
    uint32_t elapsed = cycles() - start;
    end = line();
    VDP1_FBCR = FBCR_CHANGE;
    c = watchChange(mark);
    note(c);
    return drawn ? elapsed : 0;
}

static void testLists()
{
    int32_t polys = 256;
    for (int32_t r = 0; r < LIST_ROWS && 2 * r < gSweepLines; r++)
    {
        ListRow& row = gLists[gListRows];
        row.target = gFieldLines - gSweepLines + 2 * r;
        progress("B", gSweep[2 * r]);
        row.endFirst = 0xFFFF;
        for (int32_t f = -SETTLE_FRAMES; f < LIST_FRAMES; f++)
        {
            listLength(polys);
            Change c;
            uint32_t end;
            uint32_t elapsed = listFrame(c, end);
            if (f < 0)
            {
                // sized by its own time, on to the target
                if (elapsed)
                {
                    polys = int64_t(polys) * row.target * gLineCycles / elapsed;
                    polys = polys > POLY_MAX ? POLY_MAX : polys < 1 ? 1 : polys;
                }
                continue;
            }
            uint32_t position = linePosition(end);
            if (!elapsed) {
                row.hung++;
            }
            row.endFirst = position < row.endFirst ? position : row.endFirst;
            row.endLast = position > row.endLast ? position : row.endLast;
            if (position >= DISPLAY_LINES) {
                row.inVblank++;
            }
            Tally t = {};
            tally(t, c, end);
            row.same += t.same;
            row.next += t.next;
            row.none += t.none;
        }
        row.polys = polys;
        gListRows++;
    }
}

// --- the pages ------------------------------------------------------------------

static void pageIdle()
{
    char text[TEXT_COLUMNS + 1];
    add(0, "1 ASKED, VDP1 IDLE: line same/next/none");
    char* p = put(text, "vblank on ");
    p = putInt(p, gVblankOn, 0);
    p = put(p, " off ");
    p = putInt(p, gVblankOff, 0);
    p = put(p, " last ");
    p = putInt(p, gSweep[gSweepLines - 1], 0);
    p = put(p, " lines ");
    putInt(p, gFieldLines, 0);
    add(0, text);
    p = put(text, "field ");
    p = putHundredths(p, gFieldCycles / 286);
    p = put(p, " ms  line ");
    p = putHundredths(p, gLineCycles * 10000 / 2864);
    put(p, " us");
    add(0, text);

    // the lines the change came on, most often first
    p = put(text, "change at");
    for (int32_t i = 0; i < 4; i++)
    {
        int32_t best = -1;
        for (int32_t l = 0; l < 512; l++)
        {
            if (gChangeLines[l] && (best < 0 || gChangeLines[l] > gChangeLines[best])) {
                best = l;
            }
        }
        if (best < 0)
            break;
        *p++ = ' ';
        p = putInt(p, best, 0);
        *p++ = 'x';
        p = putInt(p, gChangeLines[best], 0);
        gChangeLines[best] = 0;
    }
    add(0, text);
    p = put(text, "edsr and buffer differ ");
    putInt(p, gDisagree, 0);
    add(0, text);

    const int32_t columns = 3;
    int32_t rows = (gSweepLines + columns - 1) / columns;
    for (int32_t r = 0; r < rows; r++)
    {
        p = text;
        for (int32_t c = 0; c < columns; c++)
        {
            int32_t i = r + c * rows;
            if (i >= gSweepLines)
                break;
            p = putInt(p, gSweep[i], 3);
            *p++ = ' ';
            p = putInt(p, gIdle[i].same, 0);
            *p++ = '/';
            p = putInt(p, gIdle[i].next, 0);
            *p++ = '/';
            p = putInt(p, gIdle[i].none, 0);
            p = put(p, "   ");
        }
        add(0, text);
    }
}

static void pageLists()
{
    char text[TEXT_COLUMNS + 1];
    add(1, "2 AS A GAME WOULD, 16 FRAMES A ROW");
    add(1, "lines from the field's start, 224 = vblank");
    add(1, "want pol. ends    inVB same next none hung");
    for (int32_t r = 0; r < gListRows; r++)
    {
        const ListRow& row = gLists[r];
        char* p = putInt(text, row.target, 4);
        p = putInt(p, row.polys, 5);
        *p++ = ' ';
        p = putInt(p, row.endFirst, 3);
        *p++ = '-';
        p = putInt(p, row.endLast, 3);
        p = putInt(p, row.inVblank, 5);
        p = putInt(p, row.same, 5);
        p = putInt(p, row.next, 5);
        p = putInt(p, row.none, 5);
        putInt(p, row.hung, 5);
        add(1, text);
    }
}

static void waitVblank()
{
    while (VDP2_TVSTAT & TVSTAT_VBLANK);
    while (!(VDP2_TVSTAT & TVSTAT_VBLANK));
}

int main()
{
    platformInit();
    textClear();
    textRow(0, "VDP1 BENCH measuring...");

    VDP1_TVMR = 0;          // 16bpp
    VDP1_PTMR = 0;          // plot only when asked
    waitVblank();
    VDP1_FBCR = FBCR_CHANGE;
    waitDisplay(0);

    measureField();
    listBuild();
    testIdle();
    testLists();
    pageIdle();
    pageLists();

    while (1)
    {
        for (int32_t page = 0; page < 2; page++)
        {
            textClear();
            for (int32_t r = 0; r < gCounts[page]; r++) {
                textRow(r, gRows[page][r]);
            }
            for (int32_t v = 0; v < 60 * 8; v++) {
                waitVblank();
            }
        }
    }
}
