// A screen point (H, V) looks along H x + V y + F z in the world, x, y and z the view's
// axes, F the focal length. It meets a plane at the camera plus that ray times t, where t
// brings its y to the plane. Without roll t depends on the line alone, so VDP2's line
// scale carries it and its matrix the rest:
//
//   Xs = k (A (H - Px) + B (V - Py) + C (Zst - Pz)) + Cx + Mx
//
// with Zst - Pz = -256, so that C is the z row times F / 256, the matrix MATRIX_TIMES those
// rows, k = t / (16 MATRIX_TIMES) (16 world units a texel), and M the camera's place on the
// map. A ceiling meets the same rays above the horizon: only t differs.
//
// k comes from a table that never changes, Sonic Jam's way: 8,192 entries, entry i for a
// ray's down / height = 1.6 i (down in 2^14), so that on a line the entry is linear in the
// line, and each frame only where each set starts reading it and its step a line. An entry
// holds k = 2^24 / (down / height), which holds whatever the focal length; the far cut in its
// top bit; and in its next 7 bits the ray's depth in steps of 256, which picks the colour the
// line colour screen adds to the plane. A constant colour offset takes it off again, so the
// plane darkens by what's missing: the game's fog, and under water its fading red.

#include "plane.h"
#include "hw.h"
#include "system.h"

#include <stdint.h>

extern "C" int32_t gFocalScale;

#define CELLS_AT        0x40000     // bank B0: the cells, the table, the parameters at its top
#define PARAMS_AT       0x5FF00
#define LINE_BASE_AT    0x5FFF0     // the line colour screen's one word: where its colours are
#define SET_B_LONGS     (0x80 / 4)
#define NAMES_AT        0x60000     // bank B1: the names, 4 KB a page
#define NAMES_PAGE      0x1000
#define B0_END          PARAMS_AT
#define B1_END          0x7FF00

#define RAMCTL_TEXT     0x0300      // banks split, none for rotation (screen.cpp)
#define RAMCTL_RBG0     0x13B0      // ... 2048 colours, B0 RBG0's cells, B1 its names
#define NO_ACCESS       0xFFFF
#define CPU_ACCESS      0xEEEE
#define CHCTLB_R0       0x0100      // RBG0: 16 colours, 2 x 2 cells
#define PNCR_TWO_WORD   0x0000
#define KTCTL_FIXED     0x1111      // coefficients, both sets: a line's, two words, kx = ky, line colour index on top
#define LNCLEN_R0       0x0010      // RBG0's colour calculation takes the line colour screen
#define CCCTL_ADD_R0    0x0110      // ... added, RBG0 alone
#define RBG0_PRIORITY   1           // under the picture (2) and the frames (6)
#define RPMD_A          0
#define RPMD_B          1
#define RPMD_WINDOW     3
#define WCTLD_RPW1      0x0008      // inside window 1 is set B
#define LAST_DOT_X      702         // in half dots, at 352 wide

#define PLANE_NONE      0x7FFFFFFF  // a height the level hasn't: tools/planepack.py NO_PLANE
#define PALETTES        120         // tools/planepack.py PALETTES
#define LINE_COLOURS    0x0780      // in CRAM, past them

#define CENTER_X        176
#define CENTER_Y        112
#define FOCAL           256
#define LAST_LINE       (SCREEN_HEIGHT - 1)
#define MATRIX_TIMES    4           // the matrix's 10 fraction bits go furthest big
#define WORLD_TEXEL_SHIFT 4         // 16 world units a texel
#define ROW_TIMES       (MATRIX_TIMES * 4)  // rows 1 << 14, as VDP2 reads 16.16

#define DEPTH_ENTRIES   8192
#define DEPTH_BYTES     (DEPTH_ENTRIES * 4)
#define DEPTH_K         10485760    // 2^24 / 1.6: k at entry 1
#define DEPTH_Z         2147840     // focal length (209.75, retail's camera) x 2^14 / 1.6: a ray's depth at entry 1
#define DEPTH_SCALE     13424       // that focal length as gFocalScale has it
#define DEPTH_FAR       20480       // the renderer's far cut
#define DEPTH_LIMIT_SHIFT 16        // down / height no more than 8192 x 8 / 5: a height no less than down x 5 >> 16
#define TABLE_TIMES_5   5           // entries for down / height: 5 / 8
#define TABLE_OVER_8    8
#define KA_FRACTION     10          // bits of KAst and DKAst, the top 26 of their 16.16
#define DEPTH_STEP_SHIFT 8          // a line colour a 256 units of depth
#define LINE_ENTRIES    81          // ... to the far cut

#define FOG_MIN         12288       // the renderer's: past it a shade darker each 256 units
#define FOG_STEPS       14          // what the line colours give back, -PLANE_OFFSET a step of 8

#define DVSR            REG32(0xFFFFFF00)
#define DVDNTH          REG32(0xFFFFFF10)
#define DVDNTL          REG32(0xFFFFFF14)

// A frame's parameters: the rest of the table never changes.
struct Frame
{
    int32_t matrix[6];      // +1C A .. +30 F
    int32_t map[2];         // +44 Mx, +48 My
    uint32_t reads[2][2];   // each set's +54 KAst, +58 DKAst
    int16_t ceilingBelow;   // the lines above this are the ceiling's, the rest the floor's
    bool water;
};

// tools/planepack.py's, at the end of the .PLC.
struct Info
{
    int32_t floorY, ceilingY;
    uint8_t maps[2][16];    // each set's planes' pages
};

static bool gLevel;
static bool gBankOurs;
static uint32_t gTableAt;   // VRAM: past the cells
static uint32_t gNamesEnd;
static Info gInfo;
static bool gWater;
static Frame gMade;         // the last view's
static Frame gPending;      // the list made's, till its plot
static Frame gLists[4];     // the lists' by their plots
static bool gLineWater;     // the line colours in CRAM
static uint16_t gLineColours[2][LINE_ENTRIES];
static int32_t gLineScale;  // the camera's focal length they were made for
static volatile bool gLineStale;    // ... and CRAM's were not

static inline volatile uint32_t* vram32(uint32_t at)
{
    return (volatile uint32_t*)(VDP2_VRAM + at);
}

static void lineColoursWrite(bool water)
{
    volatile uint16_t* cram = (volatile uint16_t*)VDP2_CRAM + LINE_COLOURS;
    const uint16_t* colours = gLineColours[water];
    for (int32_t i = 0; i < LINE_ENTRIES; i++) {
        cram[i] = colours[i];
    }
    gLineWater = water;
}

#ifdef HW_LOG
#define WRITE_COST_EVERY    32      // lists
#define WRITE_COST_LONGS    512     // what a row of 32 sectors comes to
#define BLANK_NAME          0x00772000  // page 0's, tools/planepack.py's: rewriting it changes nothing

static PlaneWriteCost gWriteCost;

static void writeCostMeasure(uint32_t plot)
{
    if (plot % WRITE_COST_EVERY)
        return;
    const uint32_t t0 = timerTicks();
    volatile uint32_t* names = vram32(NAMES_AT);
    for (int32_t i = 0; i < WRITE_COST_LONGS; i++) {
        names[i] = BLANK_NAME;
    }
    gWriteCost.last = (timerTicks() - t0) & 0xFFFF;
    gWriteCost.line = vdp2Line();
    gWriteCost.most = gWriteCost.last > gWriteCost.most ? gWriteCost.last : gWriteCost.most;
}

PlaneWriteCost planeWriteCost()
{
    return gWriteCost;
}
#endif

// The buffers are changing to this list's frame, in this vertical blank.
static void listChanging(uint32_t plot)
{
    const Frame &f = gLists[plot & 3];
    volatile uint32_t* a = vram32(PARAMS_AT);
    volatile uint32_t* b = a + SET_B_LONGS;
    for (int32_t i = 0; i < 6; i++) {
        a[7 + i] = b[7 + i] = uint32_t(f.matrix[i]);
    }
    a[17] = b[17] = uint32_t(f.map[0]);
    a[18] = b[18] = uint32_t(f.map[1]);
    a[21] = f.reads[0][0];
    a[22] = f.reads[0][1];
    b[21] = f.reads[1][0];
    b[22] = f.reads[1][1];
    if (f.ceilingBelow <= 0)
    {
        VDP2_RPMD = RPMD_A;
    }
    else if (f.ceilingBelow >= SCREEN_HEIGHT)
    {
        VDP2_RPMD = RPMD_B;
    }
    else
    {
        VDP2_WPEY1 = uint16_t(f.ceilingBelow - 1);
        VDP2_RPMD = RPMD_WINDOW;
    }
    if (f.water != gLineWater || gLineStale)
    {
        gLineStale = false;
        lineColoursWrite(f.water);
    }
#ifdef HW_LOG
    writeCostMeasure(plot);
#endif
}

void* planeCellsAt()
{
    return (void*)(VDP2_VRAM + CELLS_AT);
}

void* planeNamesAt()
{
    return (void*)(VDP2_VRAM + NAMES_AT);
}

bool planeLevel()
{
    return gLevel;
}

uint8_t* planeItemsAt(uint32_t bytes)
{
    if (!gLevel) {
        return (uint8_t*)(VDP2_BANK_B_END - bytes);
    }
    if (gNamesEnd + bytes <= B1_END) {
        return (uint8_t*)(VDP2_VRAM + B1_END - bytes);
    }
    if (gTableAt + DEPTH_BYTES + bytes <= B0_END) {
        return (uint8_t*)(VDP2_VRAM + B0_END - bytes);
    }
    return nullptr;
}

// The table of k, far cut and depth.
static void depthTableWrite()
{
    volatile uint32_t* table = vram32(gTableAt);
    for (int32_t i = 0; i < DEPTH_ENTRIES; i++)
    {
        int32_t z = i ? DEPTH_Z / i : DEPTH_FAR;
        if (z >= DEPTH_FAR)
        {
            table[i] = 0x80000000;
            continue;
        }
        table[i] = (uint32_t(z >> DEPTH_STEP_SHIFT) << 24) | uint32_t(DEPTH_K / i);
    }
}

// What each depth's line colour gives back: in air the fog not yet taken off, as the
// renderer's shades go; under water its red less by the fade faces.cpp waterColour has.
// The table's depths are retail's camera's: a wider or narrower camera's are in proportion.
static void lineColoursMake(int32_t scale)
{
    gLineScale = scale;
    for (int32_t i = 0; i < LINE_ENTRIES; i++)
    {
        const int32_t z = (i << DEPTH_STEP_SHIFT) * scale / DEPTH_SCALE;
        int32_t fog = (z - FOG_MIN) >> DEPTH_STEP_SHIFT;
        fog = fog < 0 ? 0 : fog > FOG_STEPS ? FOG_STEPS : fog;
        const int32_t level = FOG_STEPS - fog;
        gLineColours[0][i] = uint16_t((level << 10) | (level << 5) | level);
        int32_t red = level;
        if (z > 1024)
        {
            red -= ((z - 1024) >> 10) << 1;
            red = red < 0 ? 0 : red;
        }
        gLineColours[1][i] = uint16_t((level << 10) | (level << 5) | red);
    }
}

// The parameters that never change: both sets' the same, but where they read.
static void paramsWrite()
{
    uint32_t p[24];
    for (int32_t i = 0; i < 24; i++) {
        p[i] = 0;
    }
    p[4] = 0x10000;                     // +10 DYst: a line on
    p[5] = 0x10000;                     // +14 DX: a pixel on
    p[13] = (CENTER_X << 16) | CENTER_Y;    // +34 Px Py
    p[14] = uint32_t(FOCAL) << 16;          // +38 Pz
    p[15] = (CENTER_X << 16) | CENTER_Y;    // +3C Cx Cy
    p[16] = uint32_t(FOCAL) << 16;          // +40 Cz
    p[19] = 0x10000;                    // +4C kx (the coefficients replace them)
    p[20] = 0x10000;                    // +50 ky
    volatile uint32_t* a = vram32(PARAMS_AT);
    for (int32_t i = 0; i < 24; i++) {
        a[i] = a[SET_B_LONGS + i] = p[i];
    }
    *(volatile uint16_t*)(VDP2_VRAM + LINE_BASE_AT) = LINE_COLOURS;
}

static inline uint32_t tableBase()
{
    return ((gTableAt >> 2) & 0xFFFF) << 16;
}

// No view yet: both sets read the far end of the table, transparent.
static void frameBlank(Frame &f)
{
    for (int32_t i = 0; i < 6; i++) {
        f.matrix[i] = 0;
    }
    f.map[0] = f.map[1] = 0;
    f.reads[0][0] = f.reads[1][0] = tableBase();
    f.reads[0][1] = f.reads[1][1] = 0;
    f.ceilingBelow = 0;
    f.water = false;
}

void planeLoaded(uint32_t cellBytes, uint32_t nameBytes)
{
    gLevel = cellBytes != 0;
    gWater = false;
    if (!gLevel)
        return;

    const uint32_t colourBytes = PALETTES * 16 * 2;
    const uint32_t cellsOnly = cellBytes - colourBytes - sizeof(Info);
    volatile uint32_t* from = vram32(CELLS_AT + cellsOnly + colourBytes);
    uint32_t* info = (uint32_t*)&gInfo;
    for (uint32_t i = 0; i < sizeof(Info) / 4; i++) {
        info[i] = from[i];
    }

    // CRAM's the 2048 colours' way before it's written
    VDP2_RAMCTL = RAMCTL_TEXT | 0x1000;
    volatile uint16_t* colours = (volatile uint16_t*)(VDP2_VRAM + CELLS_AT + cellsOnly);
    volatile uint16_t* cram = (volatile uint16_t*)VDP2_CRAM;
    for (uint32_t i = 0; i < colourBytes / 2; i++) {
        cram[i] = colours[i];
    }

    gTableAt = CELLS_AT + cellsOnly;
    gNamesEnd = NAMES_AT + nameBytes;
    depthTableWrite();
    lineColoursMake(DEPTH_SCALE);
    lineColoursWrite(false);
    paramsWrite();

    frameBlank(gMade);
    gPending = gMade;
    for (int32_t i = 0; i < 4; i++) {
        gLists[i] = gMade;
    }

    VDP2_CHCTLB = CHCTLB_R0;
    VDP2_PNCR = PNCR_TWO_WORD;
    VDP2_PLSZ = 0;                      // both sets: planes of a page, repeating past the map
    VDP2_MPOFR = (NAMES_AT / NAMES_PAGE >> 6) * 0x11;   // each set's map: the pages' top bits
    const uint32_t first = NAMES_AT / NAMES_PAGE & 0x3F;
    for (int32_t s = 0; s < 2; s++)
    {
        volatile uint16_t* map = s ? &VDP2_MPABRB : &VDP2_MPABRA;
        for (int32_t i = 0; i < 16; i += 2) {
            map[i / 2] = uint16_t(((first + gInfo.maps[s][i + 1]) << 8) | (first + gInfo.maps[s][i]));
        }
    }
    VDP2_RPRCTL = 0;
    VDP2_KTCTL = KTCTL_FIXED;
    VDP2_KTAOF = (CELLS_AT >> 18) * 0x0101;
    VDP2_LCTAU = LINE_BASE_AT >> 17;
    VDP2_LCTAL = (LINE_BASE_AT >> 1) & 0xFFFF;
    VDP2_WPSX1 = 0;                     // lines 0 to the horizon, whole, in listChanging
    VDP2_WPSY1 = 0;
    VDP2_WPEX1 = LAST_DOT_X;
    VDP2_WPEY1 = 0;
    VDP2_WCTLD = WCTLD_RPW1;
    VDP2_RPTAU = PARAMS_AT >> 17;
    VDP2_RPTAL = (PARAMS_AT >> 1) & 0xFFFF;
    VDP2_RPMD = RPMD_A;
    VDP2_CRAOFB = 0;
    VDP2_PRIR = RBG0_PRIORITY;
    VDP2_LNCLEN = LNCLEN_R0;
    VDP2_CCCTL = CCCTL_ADD_R0;
}

void planeReady()
{
    if (!gLevel)
        return;
    waitVblank();
    VDP2_RAMCTL = RAMCTL_RBG0;
    VDP2_CYCB0L = VDP2_CYCB0U = VDP2_CYCB1L = VDP2_CYCB1U = NO_ACCESS;
    gBankOurs = true;
    vdp1OnChange(listChanging);
}

void planeOff()
{
    gLevel = false;
    if (!gBankOurs)
        return;
    gBankOurs = false;
    vdp1OnChange(nullptr);
    VDP2_LNCLEN = 0;
    VDP2_CCCTL = 0;
    VDP2_WCTLD = 0;
    VDP2_RAMCTL = RAMCTL_TEXT;
    VDP2_CYCB0L = VDP2_CYCB0U = VDP2_CYCB1L = VDP2_CYCB1U = CPU_ACCESS;
}

void planeWater(bool under)
{
    gWater = under;
}

// value x 5 / (height x 8) in the coefficient address's fraction bits, as VDP2 reads it.
static inline uint32_t tableStep(int32_t value, int32_t height)
{
    DVSR = height * TABLE_OVER_8;
    const int64_t n = int64_t(value) * TABLE_TIMES_5 * (1 << KA_FRACTION);
    DVDNTH = int32_t(n >> 32);
    DVDNTL = uint32_t(n);
    return uint32_t(int32_t(DVDNTL)) << (16 - KA_FRACTION);
}

// How far a plane is for the table: the table ends at a depth of about 262, where a ray
// looks steeply and the camera is near, and there the near floor is slightly off.
static inline int32_t tableHeight(int32_t height, int32_t downMost)
{
    const int32_t least = (downMost * TABLE_TIMES_5 >> DEPTH_LIMIT_SHIFT) + 1;
    return height > least ? height : least;
}

void planeView(const int32_t camera[3], const int32_t view[12])
{
    if (!gLevel)
        return;

    // the rows as if unnarrowed, and the z row times F / 256
    const int32_t scale = gFocalScale;
    const int32_t wider = (1 << 28) / scale;
    if (scale != gLineScale)
    {
        lineColoursMake(scale);
        gLineStale = true;
    }
    int32_t r[9];
    for (int32_t row = 0; row < 3; row++)
    {
        for (int32_t c = 0; c < 3; c++)
        {
            const int32_t v = view[row * 4 + c];
            r[row * 3 + c] = row < 2 ? (v * wider) >> 14 : (v * scale) >> 14;
        }
    }

    Frame &f = gMade;
    f.water = gWater;
    f.matrix[0] = r[0] * ROW_TIMES;     // A: world x by the screen's x
    f.matrix[1] = r[3] * ROW_TIMES;     // B: ... by its y
    f.matrix[2] = -r[6] * ROW_TIMES;    // C: ... by its depth, Zst - Pz being -FOCAL
    f.matrix[3] = r[2] * ROW_TIMES;     // D: world z
    f.matrix[4] = r[5] * ROW_TIMES;     // E
    f.matrix[5] = -r[8] * ROW_TIMES;    // F
    f.map[0] = ((camera[0] & 0xFFFF) << (16 - WORLD_TEXEL_SHIFT)) - (CENTER_X << 16);
    f.map[1] = ((camera[2] & 0xFFFF) << (16 - WORLD_TEXEL_SHIFT)) - (CENTER_Y << 16);

    // a ray's world y on the first and last lines, 1 << 14
    const int32_t downTop = r[4] * -CENTER_Y + r[7] * FOCAL;
    const int32_t downLast = downTop + r[4] * LAST_LINE;
    const uint32_t base = tableBase();

    // a plane the camera isn't on the right side of reads the table's far end: transparent
    const int32_t toFloor = gInfo.floorY == PLANE_NONE ? 0 : gInfo.floorY - camera[1];
    if (toFloor > 0)
    {
        const int32_t height = tableHeight(toFloor, downLast);
        f.reads[0][0] = base + tableStep(downTop, height);
        f.reads[0][1] = tableStep(r[4], height);
    }
    else
    {
        f.reads[0][0] = base;
        f.reads[0][1] = 0;
    }
    const int32_t toCeiling = gInfo.ceilingY == PLANE_NONE ? 0 : camera[1] - gInfo.ceilingY;
    if (toCeiling > 0)
    {
        const int32_t height = tableHeight(toCeiling, -downTop);
        f.reads[1][0] = base + tableStep(-downTop, height);
        f.reads[1][1] = tableStep(-r[4], height);
    }
    else
    {
        f.reads[1][0] = base;
        f.reads[1][1] = 0;
    }

    // the rays turn from up to down at this line: the ceiling's above it, the floor's below
    int32_t below = SCREEN_HEIGHT;
    if (downTop > 0)
    {
        below = 0;
    }
    else if (r[4] > 0)
    {
        DVSR = r[4];
        DVDNTH = 0;
        DVDNTL = uint32_t(-downTop);
        const int32_t turn = int32_t(DVDNTL) + 1;
        below = turn < SCREEN_HEIGHT ? turn : SCREEN_HEIGHT;
    }
    f.ceilingBelow = int16_t(below);
}

void planeListMade()
{
    if (gLevel) {
        gPending = gMade;
    }
}

void planeListPlot(uint32_t plot)
{
    if (gLevel) {
        gLists[plot & 3] = gPending;
    }
}
