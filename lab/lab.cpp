// The clean-room renderer's lab: a program of its own (make lab) that loads
// one frame of the game, captured (scene.h), renders it a stage at a time,
// the game's way and the new, checks each stage against what the game made
// of the frame, draws it with VDP1 and shows the times. A time is the
// master's, in SH-2 cycles at 28.6 MHz: the mean of RUNS runs, the cache
// emptied before each (a frame's data never stays in a 4 KB cache).
//
// Stage 3, faces to VDP1 commands, the game's way (game.h): the master's
// draws' faces (faces.s), the rooms' (faces.s and the C it calls), then flush
// (flush.s and the C it calls), on the master alone and split with the slave
// as the game splits it. The floor (floor.s): the same commands from words
// made ready and their corners, nothing decided.
//
// Pages, L and R: the results, the frame from the game's own list, the frame
// from the lab's.

#include "lab.h"
#include "game.h"
#include "hw.h"

#include <string.h>

#define SCENE_FILE  "SCENE.SCN"
#define RUNS        4
#define LAB_MAGIC   0x4C414231  // "LAB1"

// What tools/lab.py reads back.
struct LabResults
{
    uint32_t magic;             // LAB_MAGIC once measured
    uint32_t facesMeshes;       // cycles: the master's draws' faces
    uint32_t facesRooms;        // ... the rooms'
    uint32_t flush;             // ... flush on the master alone
    uint32_t splitMaster;       // ... split: the master's share, and the wait for the slave
    uint32_t splitSlave;        // ... the slave's share
    uint32_t floor;
    int32_t facesBad;           // faces not the game's
    int32_t flushBad;           // commands not the game's
    int32_t splitBad;
    int32_t floorBad;           // commands marked exact the floor got wrong
    uint32_t vdp1Game;          // cycles VDP1 drew the game's list in
    uint32_t vdp1Lab;           // ... the lab's
    uint32_t meshFacesGiven;    // mesh quads and triangles the master's draws gave faces.s
    uint32_t roomFacesGiven;    // ... the rooms'
};

extern "C" LabResults gLab;
LabResults gLab;

static ALIGN16 Command gFloorCmds[MAX_CMDS];
static ALIGN16 uint32_t gFloorGrd[MAX_CMDS][2];

void Page::add(const char* text)
{
    if (count >= TEXT_ROWS)
        return;
    int32_t i = 0;
    for (; i < TEXT_COLUMNS && text[i]; i++) {
        rows[count][i] = text[i];
    }
    rows[count][i] = 0;
    count++;
}

char* put(char* p, const char* s)
{
    while (*s) {
        *p++ = *s++;
    }
    *p = 0;
    return p;
}

char* putFixed(char* p, int32_t value, int32_t decimals, int32_t width)
{
    char digits[16];
    int32_t n = 0;
    uint32_t v = value < 0 ? -value : value;
    for (int32_t i = 0; i < decimals; i++)
    {
        digits[n++] = '0' + v % 10;
        v /= 10;
    }
    if (decimals) {
        digits[n++] = '.';
    }
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

char* putNumber(char* p, int32_t value, int32_t width)
{
    return putFixed(p, value, 0, width);
}

char* pad(char* line, char* p, int32_t column)
{
    while (p < line + column) {
        *p++ = ' ';
    }
    *p = 0;
    return p;
}

// tools/lab.py --profile traces each measurement's first run, from a call of
// the first to a call of the second: it breaks at their third instruction,
// which a fetch running on from the code before them can't reach.
extern "C" void __attribute__((noinline)) labTraceBegin()
{
    asm volatile("nop\n\tnop\n\tnop");
}

extern "C" void __attribute__((noinline)) labTraceEnd()
{
    asm volatile("nop\n\tnop\n\tnop");
}

// The mean of RUNS runs of work, each after prepare and an empty cache.
template <typename Prepare, typename Work>
static uint32_t measure(Prepare prepare, Work work)
{
    uint32_t total = 0;
    for (int32_t r = 0; r < RUNS; r++)
    {
        prepare();
        cachePurge();
        if (r == 0) {
            labTraceBegin();
        }
        uint32_t t0 = ticks();
        work();
        total += ticksCycles(t0, ticks());
        if (r == 0) {
            labTraceEnd();
        }
    }
    return total / RUNS;
}

// A list into VDP1 VRAM where the game puts its own.
static void upload(const Command* cmds, const uint32_t (*grd)[2], int32_t count)
{
    volatile uint32_t* dst = (volatile uint32_t*)(VDP1_VRAM + VRAM_FRAME_CMDS);
    const uint32_t* src = cmds[0].w;
    for (int32_t i = 0; i < (count + 1) * 8; i++) {
        dst[i] = src[i];
    }
    dst = (volatile uint32_t*)(VDP1_VRAM + VRAM_GOURAUD_BASE);
    src = grd[0];
    for (int32_t i = 0; i < count * 2; i++) {
        dst[i] = src[i];
    }
}

static void stageThree()
{
    const SceneInfo &info = sceneInfo();

    gLab.facesMeshes = measure([] { gameFacesPrepare(); }, [] { gameFacesMaster(); });
    gLab.facesRooms = measure([] { gameFacesPrepare(); }, [] { gameFacesRooms(); });
    gameFacesPrepare();
    gameFacesMaster();
    gameFacesRooms();
    gLab.facesBad = gameFacesCheck();

    int32_t end = 0;
    uint32_t slave = 0;
    gLab.splitMaster = measure([] { gameFlushPrepare(); }, [&end, &slave] {
        uint32_t cycles;
        end = gameFlush(true, &cycles);
        slave += cycles;
    });
    gLab.splitSlave = slave / RUNS;
    gLab.splitBad = gameCommandsCheck(gCmds, gGouraud, end);
    gLab.flush = measure([] { gameFlushPrepare(); }, [&end] { end = gameFlush(false); });
    gLab.flushBad = gameCommandsCheck(gCmds, gGouraud, end);

    // the faces given: the meshes' quads and triangles, the shadows' quads,
    // the rooms'
    const DrawScene* draws = sceneArray<const DrawScene>("DRAW");
    const uint32_t* meshes = sceneArray<const uint32_t>("MESH");
    const uint8_t* geometry = sceneArray<const uint8_t>("MGEO");
    gLab.meshFacesGiven = 0;
    for (uint32_t i = 0; i < info.draws; i++)
    {
        const DrawScene &d = draws[i];
        if (d.type == DRAW_MESH)
        {
            const uint8_t* mesh = geometry + meshes[d.mesh];
            gLab.meshFacesGiven += *(const int16_t*)(mesh + 12) + *(const int16_t*)(mesh + 14);
        }
        else if (d.type == DRAW_SHADOW)
        {
            gLab.meshFacesGiven += 3;
        }
    }
    const RoomScene* rooms = sceneArray<const RoomScene>("ROOM");
    gLab.roomFacesGiven = 0;
    for (uint32_t i = 0; i < info.rooms; i++) {
        gLab.roomFacesGiven += rooms[i].quadsCount + rooms[i].trianglesCount;
    }
}

static void stageFloor()
{
    const SceneInfo &info = sceneInfo();
    const FloorFace* faces = sceneArray<const FloorFace>("FLOR");
    static FloorXform xf;
    gLab.floor = measure([] {}, [faces, &info] {
        xf.vertices = gVertices;
        xf.cmd = gFloorCmds[1].w;
        xf.grd = gFloorGrd[1];
        xf.grdLink = ((VRAM_GOURAUD_BASE >> 3) + 1) << 16;
        xf.shadeGouraud = (const uint16_t*)(sceneArray<const uint8_t>("SHAD") + 128);
        floorAsm(faces, info.floorFaces, &xf);
    });
    gLab.floorBad = 0;
    for (uint32_t i = 0; i < info.floorFaces; i++) {
        if (faces[i].flags & FLOOR_EXACT) {
            gLab.floorBad += !gameCommandMatches(gFloorCmds[i + 1], gFloorGrd[i + 1], i + 1);
        }
    }
}

// A row: a name, ms, and SH-2 cycles a unit.
static void timeRow(Page &page, const char* name, uint32_t cycles, uint32_t units, const char* unit)
{
    char line[TEXT_COLUMNS + 1];
    char* p = put(line, name);
    p = pad(line, p, 17);
    p = putFixed(p, cyclesToHundredths(cycles), 2, 6);
    if (units)
    {
        p = putFixed(p, int32_t(uint64_t(cycles) * 10 / units), 1, 8);
        p = put(p, " ");
        put(p, unit);
    }
    page.add(line);
}

static Page gResults;

static void resultsPage()
{
    const SceneInfo &info = sceneInfo();
    Page &page = gResults;
    char line[TEXT_COLUMNS + 1];
    char* p;

    page.count = 0;
    p = put(line, "RENDER LAB  ");
    p = put(p, info.name);
    p = put(p, ":");
    p = putNumber(p, info.commands - 1, 5);
    p = put(p, " commands,");
    p = putNumber(p, info.rooms, 2);
    put(p, " rooms");
    page.add(line);
    p = put(line, "meshes:");
    p = putNumber(p, gLab.meshFacesGiven, 5);
    p = put(p, " faces given,");
    p = putNumber(p, info.masterFaces, 5);
    put(p, " kept");
    page.add(line);
    p = put(line, "rooms: ");
    p = putNumber(p, gLab.roomFacesGiven, 5);
    p = put(p, " faces given,");
    p = putNumber(p, info.slaveFaces, 5);
    put(p, " kept");
    page.add(line);
    page.add("");
    page.add("STAGE 3, FACES TO COMMANDS: THE GAME'S");
    page.add("master, run alone     ms  cycles a");
    uint32_t commands = info.commands - 1;
    timeRow(page, "faces: meshes", gLab.facesMeshes, gLab.meshFacesGiven, "face given");
    timeRow(page, "faces: rooms", gLab.facesRooms, gLab.roomFacesGiven, "face given");
    timeRow(page, "flush", gLab.flush, commands, "command");
    timeRow(page, "all three", gLab.facesMeshes + gLab.facesRooms + gLab.flush, commands, "command");
    timeRow(page, "floor", gLab.floor, commands, "command");
    page.add("");
    page.add("flush split as the game splits it:");
    timeRow(page, "  master", gLab.splitMaster, 0, nullptr);
    timeRow(page, "  slave", gLab.splitSlave, 0, nullptr);
    page.add("");

    p = put(line, "faces ");
    p = put(p, gLab.facesBad ? "OFF" : "exact");
    p = put(p, ", commands ");
    p = put(p, gLab.flushBad || gLab.splitBad ? "OFF" : "exact");
    page.add(line);
    p = put(line, "floor exact on");
    p = putNumber(p, info.floorExact - gLab.floorBad, 5);
    p = put(p, " of");
    p = putNumber(p, info.floorExact, 5);
    page.add(line);
    p = put(line, "VDP1 ms: game's list");
    p = putFixed(p, cyclesToHundredths(gLab.vdp1Game), 2, 7);
    p = put(p, ", lab's");
    putFixed(p, cyclesToHundredths(gLab.vdp1Lab), 2, 7);
    page.add(line);
    page.add("");
    page.add("R, L: the frames from the lists");
}

enum { PAGE_RESULTS, PAGE_GAME, PAGE_LAB, PAGES };

static int32_t gLabEnd;     // the lab's list's end

static void show(int32_t page)
{
    if (page == PAGE_RESULTS)
    {
        textPage();
        for (int32_t r = 0; r < gResults.count; r++) {
            textRow(r, gResults.rows[r]);
        }
        return;
    }
    textFrame();
    if (page == PAGE_GAME)
    {
        const SceneInfo &info = sceneInfo();
        upload(sceneArray<const Command>("CMDS"), (const uint32_t (*)[2])sceneChunk("GRDS"), info.commands);
        textRow(0, "THE GAME'S LIST");
    }
    else
    {
        upload(gCmds, gGouraud, gLabEnd);
        textRow(0, "THE LAB'S LIST");
    }
    vdp1Show();
}

int main()
{
    platformInit();
    textPage();
    textRow(0, "RENDER LAB  loading " SCENE_FILE);
    const char* error = sceneLoad(SCENE_FILE);
    if (error)
    {
        textRow(1, error);
        while (1);
    }
    textRow(1, "measuring...");
    gameInit();

    stageThree();
    stageFloor();

    // the lists drawn, the game's then the lab's (the split flush's, as the
    // game's is)
    const SceneInfo &info = sceneInfo();
    upload(sceneArray<const Command>("CMDS"), (const uint32_t (*)[2])sceneChunk("GRDS"), info.commands);
    gLab.vdp1Game = vdp1Show();
    gameFlushPrepare();
    gLabEnd = gameFlush(true);
    upload(gCmds, gGouraud, gLabEnd);
    gLab.vdp1Lab = vdp1Show();
    gLab.magic = LAB_MAGIC;

    resultsPage();
    int32_t page = PAGE_RESULTS;
    show(page);
    uint32_t last = 0;
    while (1)
    {
        uint32_t buttons = padVblank();
        uint32_t pressed = buttons & ~last;
        last = buttons;
        if (pressed & PAD_R) {
            page = (page + 1) % PAGES;
        } else if (pressed & PAD_L) {
            page = (page + PAGES - 1) % PAGES;
        } else {
            continue;
        }
        show(page);
    }
}
