// The clean-room renderer's lab: a program of its own (make lab) that loads
// one frame of the game, captured (scene.h), renders it a stage at a time,
// the game's way and the new, checks each stage against what the game made
// of the frame, draws it with VDP1 and shows the times. A time is the
// master's, in SH-2 cycles at 28.6 MHz: the mean of RUNS runs, the cache
// emptied before each (a frame's data never stays in a 4 KB cache).
//
// Stage 1, pose, the game's way (game.h): the matrices of the items' nodes,
// from their frames, checked against those the game drew with.
//
// Stage 2, vertices, the game's way (game.h): the rooms' (xform.s) and the
// master's draws' (meshxform.s), each set up as the game sets it up, checked
// against the vertices the game drew with.
//
// Stage 2 the new way (vertices.h): the DSP's rotations (and the meshes'
// light), the meshes' feeding and the CPU's projection of what the DSP
// made, checked against the game's.
//
// Stage 3, faces to VDP1 commands, the game's way (game.h): the master's
// draws' faces (faces.s), the rooms' (faces.s and the C it calls), then flush
// (flush.s and the C it calls), on the master alone and split with the slave
// as the game splits it. The floor (floor.s): the same commands from words
// made ready and their corners, nothing decided.
//
// Stage 3 the new way (faces.h): the master's draws' faces straight to
// commands in one pass (facesmesh.s), chained by depth bucket, the rooms'
// the same way, the buckets joined; the list VDP1 follows checked against the
// game's as VDP1 draws it.
//
// Pages, L and R: the game's way, the pose, the new way's vertices and
// commands, the frame from the game's own list, the frame from the new.

#include "lab.h"
#include "faces.h"
#include "game.h"
#include "pose.h"
#include "rooms.h"
#include "vertices.h"
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
    uint32_t meshFlush;         // cycles: the game's flush of the master's faces alone
    uint32_t roomFlush;         // ... of the rooms'
    uint32_t newMeshes;         // ... the new way's master's draws
    uint32_t newRooms;          // ... its rooms
    uint32_t newJoin;           // ... its join
    int32_t newBad;             // commands of the new list VDP1 draws not as the game's
    int32_t newBadC;            // ... with the faces in C
    int32_t newVisited;         // commands VDP1 follows in the new list
    int32_t newShaded;          // ... the same as the game's but for a shaded copy of a colour table
                                // where the other has gouraud of its value, or the other way
    int32_t newAsmBad;          // what the scene seldom gives the new way (every draw and room under
                                // water, the rooms a frame on): commands its assembly makes not as its C
    int32_t newFrames;          // the frames the new way ran at this view before its list settled
    uint32_t meshCommands;      // the master's draws' commands
    uint32_t roomCommands;      // the rooms'
    uint32_t vdp1New;           // cycles VDP1 drew the new list in
    uint32_t vertsRooms;        // cycles: stage 2 the game's way, the rooms' vertices
    uint32_t vertsMeshes;       // ... the master's draws'
    int32_t vertsBad;           // vertices not the game's
    int32_t roomVertices;       // the rooms' vertices
    int32_t meshVertices;       // the draws'
    uint32_t newVertsRooms;     // cycles: stage 2 the new way, the rooms' projection
    int32_t newVertsBad;        // vertices not the game's
    uint32_t newVertsMeshes;    // ... the draws' projection and shades
    int32_t newVertsSeldom;     // the meshes' vertices not the game's projected as though none lay
                                // inside the frame, or each worked out at the near plane; the
                                // rooms' made up through every path not as the model's
    uint32_t dspRooms;          // cycles the DSP took over the rooms' rotations
    int32_t dspRoomsBad;        // its rotations not xform.s's arithmetic
    uint32_t newVertsFeed;      // cycles: the meshes fed to the DSP
    uint32_t dspMeshes;         // cycles the DSP took over the meshes
    int32_t dspMeshesBad;       // its output not meshxform.s's arithmetic
    uint32_t dspMeshes3;        // ... meshlight3.dsp (the bench's v3) took, from data laid out for it
    int32_t dspMeshes3Bad;
    int32_t feedBad;            // the jobs the feeding's assembly makes not as its C
    uint32_t together;          // cycles: stage 2 the new way on both CPUs and the DSP, start to end
    uint32_t togetherMaster;    // ... the master at work
    uint32_t togetherMasterWait;
    uint32_t togetherSlave;     // ... the slave at work
    uint32_t togetherSlaveWait;
    int32_t togetherBad;        // vertices not the game's
    uint32_t togetherDspRooms;  // ... the DSP's rooms, the gap to its meshes, its meshes
    uint32_t togetherDspGap;
    uint32_t togetherDspMeshes;
    uint32_t stages;            // cycles: stages 2 and 3 the new way together, start to the list joined
    uint32_t stagesMaster;      // ... the master at work
    uint32_t stagesMasterWait;
    uint32_t stagesSlave;       // ... the slave at work
    uint32_t stagesSlaveWait;
    int32_t stagesBad;          // commands VDP1 follows not as the game's, and vertices not the game's
    uint32_t pose;              // cycles: stage 1 the game's way, the items' node matrices
    int32_t poseBad;            // matrices not the game's
    int32_t poseMatrices;       // the matrices made, a drawn mesh each
    int32_t poseItems;          // the items posed
    uint32_t poseNew;           // cycles: stage 1 the new way
    int32_t poseNewBad;         // its matrices not the game's
    int32_t poseSeldom;         // ... on made-up inputs, against the game's way on them (and paths not taken)
    uint32_t light;             // cycles: stage 1 the game's way, the items' light
    int32_t lightBad;           // draws lit not as the game's
    uint32_t lightNew;          // cycles: the new way's
    int32_t lightNewBad;
    int32_t lightSeldom;        // ... on made-up inputs, against the game's way on them (and paths not taken)
    uint32_t statics;           // cycles: stage 1 the game's way, the static meshes' and shadows' transforms
    int32_t staticsBad;         // those not the game's
    int32_t staticCount, shadowCount;
    uint32_t staticsNew;        // cycles: the new way's
    int32_t staticsNewBad;
    int32_t staticsSeldom;      // ... on made-up inputs, against the game's way on them (and paths not taken)
    uint32_t rooms;             // cycles: stage 1 the game's way, the rooms seen (the portal walk, the rooms' matrices)
    int32_t roomsBad;           // rooms not as the game's (or a list of another length)
    int32_t roomsSeen;          // the rooms the frame queued
    int32_t roomsVoids;         // the fills past the draw distance the walk made
    uint32_t roomsNew;          // cycles: the new way's
    int32_t roomsNewBad;
    int32_t roomsSeldom;        // ... on made-up views, against the game's way on them (and paths not taken)
    uint32_t frame;             // cycles: stages 1, 2 and 3 the new way together (the frame so far), start to the
                                // list joined: the items' light and pose on the master before its feed
    uint32_t frameMaster;       // ... the master at work
    uint32_t frameMasterWait;
    uint32_t frameSlave;        // ... the slave at work
    uint32_t frameSlaveWait;
    int32_t frameBad;           // commands VDP1 follows not as the game's, vertices not the game's, draws whose
                                // transform the pose made not as the game's
    uint32_t arenaUsed;         // bytes of high work RAM the scene and the stages took (sceneUsed)
    uint32_t arenaSize;
};

extern "C" LabResults gLab;
LabResults gLab;

// The new way's command list (its gouraud tables beside), and before the new
// way runs the floor's.
static ALIGN16 Command gNewCmds[MAX_CMDS];
static ALIGN16 uint32_t gNewGrd[MAX_CMDS][2];

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

// The shaded copies of colour tables in VDP1 VRAM: the game's, as the scene
// has them, and the new way's once its frames have run; each list drawn
// with its own.
#define SHADE_TABLES    (256 * SHADE_TABLE_SIZE)
static uint32_t (*gGameTables)[SHADE_TABLES / 4];  // in low work RAM
static uint32_t (*gNewTables)[SHADE_TABLES / 4];

static void shadeTables(uint32_t (*tables)[SHADE_TABLES / 4], bool save)
{
    for (int32_t cpu = 0; cpu < 2; cpu++)
    {
        const ShadeCache* cache = gameShadeCache(cpu);
        if (!cache->vram)
            continue;
        volatile uint32_t* vram = (volatile uint32_t*)(VDP1_VRAM + cache->vram);
        for (int32_t i = 0; i < SHADE_TABLES / 4; i++)
        {
            if (save) {
                tables[cpu][i] = vram[i];
            } else {
                vram[i] = tables[cpu][i];
            }
        }
    }
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

static void stagePose()
{
    gLab.poseItems = gamePoseInit();
    gLab.pose = measure([] {}, [] { gamePose(); });
    gamePose();
    gLab.poseBad = gamePoseCheck(gLab.poseMatrices);
    gameLightInit();
    gLab.light = measure([] {}, [] { gameLight(); });
    gameLight();
    gLab.lightBad = gameLightCheck();
    gLab.roomsSeen = gameRoomsInit();
    gLab.rooms = measure([] {}, [] { gameRooms(); });
    gameRooms();
    gLab.roomsBad = gameRoomsCheck(nullptr, 0, gLab.roomsVoids);
    gameStaticsInit();
    gLab.statics = measure([] {}, [] { gameStatics(); });
    gameStatics();
    gLab.staticsBad = gameStaticsCheck(nullptr, gLab.staticCount, gLab.shadowCount);

    poseInit();
    gLab.lightNew = measure([] {}, [] { poseLight(); });
    poseLight();
    gLab.lightNewBad = poseLightCheck();
    gLab.lightSeldom = poseLightSeldomCheck();
    gLab.poseNew = measure([] {}, [] { pose(); });
    posePoison();
    pose();
    gLab.poseNewBad = poseCheck();
    gLab.poseSeldom = poseSeldomCheck();
    gLab.staticsNew = measure([] {}, [] { poseStatics(); poseShadows(); });
    poseStatics();
    poseShadows();
    gLab.staticsNewBad = poseStaticsCheck();
    gLab.staticsSeldom = poseStaticsSeldomCheck();
    roomsInit();
    gLab.roomsNew = measure([] {}, [] { roomsSeen(); });
    roomsSeen();
    gLab.roomsNewBad = roomsSeenCheck();
    gLab.roomsSeldom = roomsSeldomCheck();
}

static void stageTwo()
{
    gameVerticesInit();
    gLab.vertsRooms = measure([] { gameVerticesPrepare(); }, [] { gameVerticesRooms(); });
    gLab.vertsMeshes = measure([] { gameVerticesPrepare(); }, [] { gameVerticesMeshes(); });
    gameVerticesPrepare();
    gameVerticesRooms();
    gameVerticesMeshes();
    gLab.vertsBad = gameVerticesCheck(gLab.roomVertices, gLab.meshVertices);
}

static void stageTwoNew()
{
    const char* error = verticesInit();
    if (error)
    {
        textRow(1, error);
        while (1);
    }
    gLab.dspRooms = verticesRoomsDsp();
    gLab.dspRoomsBad = verticesRoomsDspCheck();
    gLab.newVertsRooms = measure([] { gameVerticesPrepare(); }, [] { verticesRooms(); });
    gLab.feedBad = verticesFeedCheck();
    gLab.newVertsFeed = measure([] {}, [] { verticesFeed(); });
    gLab.dspMeshes3 = verticesMeshesDsp3();
    gLab.dspMeshes3Bad = verticesMeshesDspCheck();
    gLab.dspMeshes = verticesMeshesDsp();
    gLab.dspMeshesBad = verticesMeshesDspCheck();
    gLab.newVertsMeshes = measure([] { verticesMeshesDsp(); }, [] { verticesMeshes(); });
    gameVerticesPrepare();
    verticesRooms();
    verticesMeshesDsp();
    verticesMeshes();
    int32_t rooms, meshes;
    gLab.newVertsBad = verticesRoomsCheck(rooms) + verticesMeshesCheck(meshes);
    gLab.newVertsSeldom = verticesFallbacksCheck() + verticesRoomsSeldomCheck();

    Together sum = {};
    for (int32_t r = 0; r < RUNS; r++)
    {
        gameVerticesPrepare();
        verticesPoison();
        cachePurge();
        Together t = verticesTogether();
        sum.wall += t.wall;
        sum.masterWork += t.masterWork;
        sum.masterWait += t.masterWait;
        sum.slaveWork += t.slaveWork;
        sum.slaveWait += t.slaveWait;
        sum.dspRooms += t.dspRooms;
        sum.dspGap += t.dspGap;
        sum.dspMeshes += t.dspMeshes;
    }
    gLab.togetherDspRooms = sum.dspRooms / RUNS;
    gLab.togetherDspGap = sum.dspGap / RUNS;
    gLab.togetherDspMeshes = sum.dspMeshes / RUNS;
    gLab.together = sum.wall / RUNS;
    gLab.togetherMaster = sum.masterWork / RUNS;
    gLab.togetherMasterWait = sum.masterWait / RUNS;
    gLab.togetherSlave = sum.slaveWork / RUNS;
    gLab.togetherSlaveWait = sum.slaveWait / RUNS;
    cachePurge();
    gLab.togetherBad = verticesRoomsCheck(rooms) + verticesMeshesCheck(meshes);
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
        xf.cmd = gNewCmds[1].w;
        xf.grd = gNewGrd[1];
        xf.grdLink = ((VRAM_GOURAUD_BASE >> 3) + 1) << 16;
        xf.shadeGouraud = (const uint16_t*)(sceneArray<const uint8_t>("SHAD") + 128);
        floorAsm(faces, info.floorFaces, &xf);
    });
    gLab.floorBad = 0;
    for (uint32_t i = 0; i < info.floorFaces; i++) {
        if (faces[i].flags & FLOOR_EXACT) {
            gLab.floorBad += !gameCommandMatches(gNewCmds[i + 1], gNewGrd[i + 1], i + 1);
        }
    }
}

// The new way: a command list of the rooms' (the slave's, with its cache of
// shaded tables) and one of the master's draws, in the commands after as many
// as the game's rooms took (gNewCmds).
static uint16_t gHeads[2][OT_SIZE + 1] __attribute__((aligned(4)));
static uint16_t gTails[2][OT_SIZE];
static CommandList gRoomList, gMeshList;
static CommandList* const gLists[2] = { &gRoomList, &gMeshList };
static int32_t gMeshFirst;  // the master's first command
static int32_t gNewEnd;     // the new list's end

#define FACE_WATER  (1u << 30)
#define MAX_ROOMS   64

// The scene's draws and what they draw, its rooms and the room texture cache,
// for the new way.
static const DrawScene* gDraws;
static uint32_t gDrawCount;
static const uint8_t* gFaceSets;
static const Face* gGameFaces;
static const Sprite* gSprites;
static const RoomScene* gRoomScenes;
static FaceRoom gRooms[MAX_ROOMS];
static uint32_t gRoomCount;
static RoomTextures gRoomTextures;
static RoomSplit gSplit;
#define MAX_ROOM_TEXTURES   512
static RoomMaterial gRoomMaterials[MAX_ROOM_TEXTURES];
static RoomRun gRoomRun;
static bool gRoomTexturesShort;     // the rooms take more textures than that

static void newPrepare()
{
    gameFlushPrepare();
    const Material* materials = sceneArray<const Material>("NMAT");
    const uint8_t* shadeLevel = sceneArray<const uint8_t>("SHAD");
    facesListInit(gRoomList, gNewCmds, gNewGrd, 1, gMeshFirst, gHeads[0], gTails[0], materials,
                  gameShadeGouraud(), shadeLevel, gameShadeCache(1));
    facesListInit(gMeshList, gNewCmds, gNewGrd, gMeshFirst, MAX_CMDS - 1, gHeads[1], gTails[1], materials,
                  gameShadeGouraud(), shadeLevel, gameShadeCache(0));
    gSplit.top = MAX_VERTICES;
    gSplit.bottom = sceneInfo().bottom;

    // the materials' characters looked up afresh, as in a frame of its own
    for (int32_t i = 0; i < MAX_ROOM_TEXTURES; i++) {
        gRoomMaterials[i].frame = ~gRoomTextures.frame;
    }
}

// The rooms in the order the slave faced them, through facesRoom or (c)
// facesRoomC.
static void newRooms(bool c)
{
    for (uint32_t i = 0; i < gRoomCount; i++)
    {
        const RoomScene &scene = gRoomScenes[i];
        if (scene.dsp) {
            cacheDrop(gRooms[i].vertices, scene.verticesCount * sizeof(Vertex));  // the DSP wrote some
        }
        if (c) {
            facesRoomC(gRooms[i], gRoomTextures, gSplit, gRoomList);
        } else {
            if (!i) {
                facesRoomsBegin(gRoomRun, gRoomTextures, gSplit, gRoomList);
            }
            facesRoom(gRooms[i], gRoomRun);
        }
    }
}

// The cache holds every texture the frame's faces drew with: one it misses,
// the game failed to take into it.
static int32_t roomTextureMiss(uint32_t, uint32_t, uint32_t)
{
    return -1;
}

// The rooms as facesRoom* takes them, and the cache as the frame found it.
static void newRoomsInit()
{
    const SceneInfo &info = sceneInfo();
    gRoomScenes = sceneArray<const RoomScene>("ROOM");
    uint8_t* geometry = (uint8_t*)sceneChunk("RGEO");
    gRoomCount = info.rooms < MAX_ROOMS ? info.rooms : MAX_ROOMS;
    for (uint32_t i = 0; i < gRoomCount; i++)
    {
        const RoomScene &scene = gRoomScenes[i];
        FaceRoom &room = gRooms[i];
        room.quads = (RoomQuad*)(geometry + scene.quads);
        room.triangles = (const RoomQuad*)(geometry + scene.triangles);
        room.points = (const RoomVertex*)(geometry + scene.vertices);
        room.vertices = gVertices + scene.base;
        room.quadsCount = scene.quadsCount;
        room.trianglesCount = scene.trianglesCount;
        for (int32_t k = 0; k < 6; k++) {
            room.facing[k] = scene.facing[k];
        }
        for (int32_t k = 0; k < 3; k++)
        {
            room.camera[k] = scene.camera[k];
            room.m[k * 3] = scene.m[k * 3];
            room.m[k * 3 + 1] = scene.m[k * 3 + 1];
            room.m[k * 3 + 2] = scene.m[k * 3 + 2];
        }
        room.t[0] = scene.tx;
        room.t[1] = scene.ty;
        room.t[2] = scene.tz;
        for (int32_t k = 0; k < 4; k++) {
            room.clip[k] = scene.clip[k];
        }
        room.flags = scene.wet ? FACE_WATER : 0;
        room.ripple = scene.ripple;
        room.phase = scene.causticsFrame;
    }

    RoomTextures &t = gRoomTextures;
    t.textures = sceneArray<const Texture>("TEXS");
    t.palette = sceneArray<const uint16_t>("PALT");
    t.keys = (uint8_t (*)[DETAIL_MAX_KEYS])sceneChunk("DKEY");
    t.slots = (DetailSlot*)sceneChunk("DSLT");
    t.frame = info.detailFrame;
    t.lod = info.lod;
    t.pmod = info.texturePmod;
    t.pools[0] = info.detailVram;
    t.pools[1] = info.poolVram[1];
    t.pools[2] = info.poolVram[2];
    t.miss = roomTextureMiss;

    // the materials of the textures the rooms' faces take
    uint32_t textures = 0;
    for (uint32_t i = 0; i < gRoomCount; i++)
    {
        const FaceRoom &room = gRooms[i];
        for (int32_t k = 0; k < room.quadsCount; k++) {
            uint32_t n = (room.quads[k].flags & 0x3FFF) + 1;
            textures = n > textures ? n : textures;
        }
        for (int32_t k = 0; k < room.trianglesCount; k++) {
            uint32_t n = (room.triangles[k].flags & 0x3FFF) + 1;
            textures = n > textures ? n : textures;
        }
    }
    gRoomTexturesShort = textures > MAX_ROOM_TEXTURES;
    facesRoomMaterials(t, gRoomMaterials, gRoomTexturesShort ? MAX_ROOM_TEXTURES : textures);
    t.materials = gRoomMaterials;
    gSplit.vertices = gVertices;
    gSplit.divTable = sceneArray<const uint16_t>("DIVT");
    gMeshFirst = 1 + info.slaveFaces;
}

// The master's draws in order, their faces through facesMeshAsm or (c)
// facesMeshC; with water, every draw under water.
static void newMeshes(bool c, bool water = false)
{
    const DrawScene* draws = gDraws;
    const uint8_t* sets = gFaceSets;
    for (uint32_t i = 0; i < gDrawCount; i++)
    {
        const DrawScene &d = draws[i];
        const Vertex* vertices = gVertices + d.base;
        if (d.type == DRAW_COPY)
        {
            facesOther(gGameFaces[d.first], gVertices, gSprites, d.bucket, gMeshList);
            continue;
        }
        const FaceSet* set = d.type == DRAW_SHADOW ? facesShadow()
                                                   : (const FaceSet*)(sets + ((const uint32_t*)sets)[d.mesh]);
        uint32_t mode = (d.inside ? MESH_INSIDE : 0) | (water || (d.flags & FACE_WATER) ? MESH_WATER : 0);
        if (c) {
            facesMeshC(set, vertices, mode, gMeshList);
        } else {
            facesMeshAsm(set, vertices, mode, &gMeshList);
        }
    }
}

// The buckets joined (facesJoinAsm, or facesJoin), after the game's clear;
// the end after the last command.
static Command gClear;

static void newJoin(bool c)
{
    gNewEnd = facesSlot(gMeshList.link);
    gNewCmds[0] = gClear;
    gNewCmds[gNewEnd].w[0] = CMD_END << 16;
    if (c) {
        facesJoin(gLists, 2, gNewCmds, 0, gNewEnd);
    } else {
        facesJoinRoomsMeshes(gRoomList, gMeshList, gNewCmds, 0, gNewEnd);
    }
}

// Stages 2 and 3 the new way together, as a frame would run them: the DSP
// turns the rooms, then the meshes; the slave takes each room in order as
// soon as the DSP has turned it, projects it and adds its faces (the room
// list); the master feeds the meshes meanwhile and projects the chunks of
// rooms the plan gives it (verticesPlan), then takes the draws in order,
// each mesh job projected once the DSP has made it and its faces added (the
// mesh list); then, both done, each joins half the lists' buckets (the
// slave the near half, from a field of its own the master then links the
// far half's last command to). Each CPU's work and waits.
// Room faces stay on the slave: they share the room texture cache and the
// materials' memo, and keep the game's order in the room list.
struct Stages
{
    uint32_t wall, masterWork, masterWait, slaveWork, slaveWait;
    int32_t late;       // cycles the master's draws ended after the slave's rooms (or before: < 0)
};

// Between the CPUs (each read and written uncached, and volatile, so that
// a wait on it stays a wait): the slave's rooms done, the master's draws
// done; the near half's first chain (its head, for the far half's last
// command to link to) and the link field its last goes into; the slave's
// work and waits.
#define JOIN_SPLIT  (JOIN_PAIRS / 2)    // the far half's nearest pair

struct SlaveStages
{
    volatile uint32_t roomsDone, drawsDone;
    uint16_t* volatile nearLast;
    volatile uint16_t nearHead;
    uint16_t pad;
    volatile uint32_t work, wait;
    volatile uint32_t poses;    // the slave poses its items (gPoseSlave) first
    volatile uint32_t posed;
} __attribute__((aligned(16)));
static SlaveStages gSlaveStages;

// The frame's split between the CPUs, beside the rooms' chunks: the items the
// slave poses.
#define MAX_POSE_ITEMS  64
#define X_MAX(a, b)     ((a) > (b) ? (a) : (b))
static uint8_t gPoseSlave[MAX_POSE_ITEMS];

static void slaveRooms(void*)
{
    uint32_t t0 = ticks(), wait = 0;
    SlaveStages* shared = uncached(&gSlaveStages);
    if (shared->poses)
    {
        poseShare(1, gPoseSlave);
        shared->posed = 1;
    }
    for (uint32_t i = 0; i < gRoomCount; i++)
    {
        verticesRoom(i, wait);
        if (!i) {
            facesRoomsBegin(gRoomRun, gRoomTextures, gSplit, gRoomList);
        }
        facesRoom(gRooms[i], gRoomRun);
    }
    *uncached(&gSlaveStages.roomsDone) = 1;
    uint32_t w0 = ticks();
    while (!*uncached(&gSlaveStages.drawsDone));
    wait += ticksCycles(w0, ticks());
    cachePurge();       // the master's mesh list
    shared->nearLast = facesJoinPairs(&gRoomList, &gMeshList, facesLinkBase(gNewCmds),
                                      (uint16_t*)&shared->nearHead, JOIN_SPLIT - 1);
    shared->work = ticksCycles(t0, ticks()) - wait;
    shared->wait = wait;
}

// Stages 2 and 3 together; posed: stage 1 too, the items' light and pose
// shared as the plan shares them (gPoseSlave), their transforms the feed's.
// The slave poses its items, then starts its rooms; the master poses its
// own, feeds the meshes before the first of the slave's, and the rest once
// the slave has posed them.
static Stages stagesTogether(bool posed)
{
    Stages s = {};
    SlaveStages* shared = uncached(&gSlaveStages);
    shared->roomsDone = shared->drawsDone = shared->posed = 0;
    uint32_t items = posed ? poseCount() : 0, slaveItems = 0;
    int32_t part = verticesJobs();
    for (uint32_t i = 0; i < items; i++)
    {
        slaveItems += gPoseSlave[i];
        if (gPoseSlave[i] && poseDraws(i) && part == verticesJobs()) {
            part = verticesJobOfDraw(poseFirstDraw(i));
        }
    }
    shared->poses = slaveItems;
    uint32_t t0 = ticks();
    verticesRoomsBegin();
    verticesClaimMasters();
    slaveRun(slaveRooms, nullptr);
    if (posed)
    {
        poseShare(0, gPoseSlave);
        poseStatics();
    }
    verticesMeshesFeed(0, part);
    if (slaveItems)
    {
        uint32_t w0 = ticks();
        while (!shared->posed);
        s.masterWait += ticksCycles(w0, ticks());
        poseDropShare(gPoseSlave);
        verticesMeshesFeed(part);
    }
    bool meshes = false;
    verticesMasterRooms(s.masterWait, meshes);
    if (!meshes) {
        verticesMeshesStart(s.masterWait);
    }
    const uint8_t* sets = gFaceSets;
    uint32_t slaveEnd = 0;
    for (uint32_t i = 0; i < gDrawCount; i++)
    {
        if (!slaveEnd && shared->roomsDone) {
            slaveEnd = ticksCycles(t0, ticks());
        }
        const DrawScene &d = gDraws[i];
        if (d.type == DRAW_COPY)
        {
            facesOther(gGameFaces[d.first], gVertices, gSprites, d.bucket, gMeshList);
            continue;
        }
        const Vertex* vertices = verticesDraw(i, s.masterWait);
        const FaceSet* set = d.type == DRAW_SHADOW ? facesShadow()
                                                   : (const FaceSet*)(sets + ((const uint32_t*)sets)[d.mesh]);
        uint32_t mode = (d.inside ? MESH_INSIDE : 0) | (d.flags & FACE_WATER ? MESH_WATER : 0);
        facesMeshAsm(set, vertices, mode, &gMeshList);
    }
    gNewEnd = facesSlot(gMeshList.link);
    gNewCmds[0] = gClear;
    gNewCmds[gNewEnd].w[0] = CMD_END << 16;
    shared->drawsDone = 1;
    uint32_t w0 = ticks();
    uint32_t drawsEnd = ticksCycles(t0, w0);
    while (!shared->roomsDone);
    s.masterWait += ticksCycles(w0, ticks());
    s.late = int32_t(drawsEnd) - int32_t(slaveEnd ? slaveEnd : ticksCycles(t0, ticks()));
    cachePurge();       // the slave's room list
    uint16_t* farLast = facesJoinPairs(&gRoomList, &gMeshList, facesLinkBase(gNewCmds),
                                       facesLinkField(gNewCmds, 0), JOIN_SPLIT << 16 | (JOIN_PAIRS - 1));
    w0 = ticks();
    slaveWait();
    s.masterWait += ticksCycles(w0, ticks());
    cachePurge();       // the link fields the slave's half set, of commands this CPU made
    uint16_t* nearLast = shared->nearLast;
    if (nearLast == &uncached(&gSlaveStages)->nearHead || nearLast == &gSlaveStages.nearHead) {
        *farLast = facesLink(gNewEnd);      // the near half had no commands
    }
    else
    {
        *farLast = shared->nearHead;
        *nearLast = facesLink(gNewEnd);
    }
    verticesEnd();
    s.wall = ticksCycles(t0, ticks());
    s.masterWork = s.wall - s.masterWait;
    s.slaveWork = *uncached(&gSlaveStages.work);
    s.slaveWait = *uncached(&gSlaveStages.wait);
    return s;
}

// The plan for a frame (verticesPlan), with the items' pose (gPoseSlave: the
// slave's): the heaviest first, each that leaves the master's predicted work
// no less than the slave's, and its own pose no less than the slave's (so
// it has its own to do while the slave poses), the slave's wait for the
// DSP's first room free for it; the rooms' chunks then even out what is
// left.
static void framePlan(int32_t bias, bool posed)
{
    memset(gPoseSlave, 0, sizeof(gPoseSlave));
    if (!posed || poseCount() > MAX_POSE_ITEMS)
    {
        verticesPlan(bias);
        return;
    }
    int32_t master, slave, masterPose = 0, slavePose = 0, idle = verticesSlaveIdle();
    verticesPredicted(master, slave);
    master += bias;
    for (uint32_t i = 0; i < poseCount(); i++) {
        masterPose += posePrice(i);
    }
    while (1)
    {
        int32_t best = -1, price = 0;
        for (uint32_t i = 0; i < poseCount(); i++)
        {
            int32_t p = posePrice(i);
            if (!gPoseSlave[i] && p > price && master + masterPose - p >= slave + X_MAX(slavePose + p - idle, 0) &&
                masterPose - p >= slavePose + p - idle)
            {
                best = i;
                price = p;
            }
        }
        if (best < 0)
            break;
        gPoseSlave[best] = 1;
        masterPose -= price;
        slavePose += price;
    }
    verticesPlan(bias + masterPose - X_MAX(slavePose - idle, 0), masterPose);
}

// The stages run together, RUNS times after the frames that settle the
// split (each moves the plan by half how late the master's draws ended after
// the slave's rooms): their mean, and the list and vertices against the
// game's.
static Stages togetherMeasured(bool posed, int32_t &bad)
{
    int32_t bias = 0;
    for (int32_t f = 0; f < 6; f++)
    {
        framePlan(bias, posed);
        newPrepare();
        verticesPoison();
        if (posed) {
            posePoison();
        }
        cachePurge();
        bias += stagesTogether(posed).late / 2;
    }
    framePlan(bias, posed);
    Stages sum = {};
    for (int32_t r = 0; r < RUNS; r++)
    {
        newPrepare();
        verticesPoison();
        if (posed) {
            posePoison();
        }
        cachePurge();
        Stages s = stagesTogether(posed);
        sum.wall += s.wall;
        sum.masterWork += s.masterWork;
        sum.masterWait += s.masterWait;
        sum.slaveWork += s.slaveWork;
        sum.slaveWait += s.slaveWait;
    }
    sum.wall /= RUNS;
    sum.masterWork /= RUNS;
    sum.masterWait /= RUNS;
    sum.slaveWork /= RUNS;
    sum.slaveWait /= RUNS;
    int32_t visited, shaded, rooms, meshes;
    bad = gameListCheck(gNewCmds, gNewGrd, visited, shaded) + verticesRoomsCheck(rooms) +
          verticesMeshesCheck(meshes) + (posed ? poseCheck() : 0);
    return sum;
}

static void stageTogether()
{
    Stages s = togetherMeasured(false, gLab.stagesBad);
    gLab.stages = s.wall;
    gLab.stagesMaster = s.masterWork;
    gLab.stagesMasterWait = s.masterWait;
    gLab.stagesSlave = s.slaveWork;
    gLab.stagesSlaveWait = s.slaveWait;
}

// The frame so far: stages 1, 2 and 3 together, the meshes' transforms the
// new pose's.
static void stageFrame()
{
    verticesXforms(poseXforms());
    Stages s = togetherMeasured(true, gLab.frameBad);
    verticesXforms(sceneArray<const DrawXform>("XFRM"));
    gLab.frame = s.wall;
    gLab.frameMaster = s.masterWork;
    gLab.frameMasterWait = s.masterWait;
    gLab.frameSlave = s.slaveWork;
    gLab.frameSlaveWait = s.slaveWait;
}

// A command's words and gouraud table, hashed.
static uint32_t commandHash(int32_t slot)
{
    uint32_t h = 0;
    for (int32_t i = 0; i < 8; i++) {
        h = ((h << 5) | (h >> 27)) ^ gNewCmds[slot].w[i];
    }
    return ((h << 5) | (h >> 27)) ^ gNewGrd[slot][0] ^ (gNewGrd[slot][1] << 1);
}

// The new way's frames at this view, its caches of shaded tables settling on
// what it draws (a copy drawn is kept two frames): until its list is the
// frame before's, at most 8. The frames run.
static int32_t newSettle()
{
    static uint32_t hashes[MAX_CMDS];
    for (int32_t f = 1; f <= 8; f++)
    {
        gameShadeCache(0)->frame++;
        gameShadeCache(1)->frame++;
        newPrepare();
        newRooms(false);
        newMeshes(false);
        int32_t rooms = facesSlot(gRoomList.link), meshes = facesSlot(gMeshList.link);
        bool settled = f > 1;
        for (int32_t i = 1; i < meshes; i++)
        {
            if (i == rooms) {
                i = gMeshFirst;
            }
            uint32_t h = commandHash(i);
            settled = settled && h == hashes[i];
            hashes[i] = h;
        }
        if (settled)
            return f;
    }
    return 8;
}

// The quads' flags, their kept turns and mip levels in the top byte, saved
// (save) or put back.
static void roomQuadFlags(bool save)
{
    static uint32_t flags[2048];
    int32_t n = 0;
    for (uint32_t i = 0; i < gRoomCount; i++)
    {
        for (int32_t k = 0; k < gRooms[i].quadsCount && n < 2048; k++, n++)
        {
            if (save) {
                flags[n] = gRooms[i].quads[k].flags;
            } else {
                gRooms[i].quads[k].flags = flags[n];
            }
        }
    }
}

// The room texture cache as the frame found it, for the loader below.
static uint8_t gKeys[DETAIL_VARIANTS][DETAIL_MAX_KEYS];

// A loader taking a texture into the cache as the frame had it.
static int32_t roomTextureReload(uint32_t key, uint32_t, uint32_t variant)
{
    gRoomTextures.keys[variant][key] = gKeys[variant][key];
    return int32_t(gKeys[variant][key]) - 1;
}

// The new way in C, then in assembly, on what a scene seldom gives it: every
// draw and room under water; the rooms a frame on, the quads that kept their
// turn and mip level deciding them and the others keeping theirs; the cache
// missing every other texture, its loader taking them. The commands that
// differ.
static int32_t newAsmCheck()
{
    static uint32_t hashes[MAX_CMDS];
    roomQuadFlags(true);
    memcpy(gKeys, gRoomTextures.keys, sizeof(gKeys));
    int32_t bad = 0;
    for (int32_t pass = 0; pass < 3; pass++)
    {
        bool wet = pass == 0;
        for (uint32_t i = 0; i < gRoomCount; i++) {
            gRooms[i].flags = wet || gRoomScenes[i].wet ? FACE_WATER : 0;
        }
        gRoomTextures.frame = sceneInfo().detailFrame + (pass == 1);
        gRoomTextures.miss = pass == 2 ? roomTextureReload : roomTextureMiss;
        int32_t rooms = 0, meshes = 0;
        for (int32_t c = 1; c >= 0; c--)
        {
            roomQuadFlags(false);
            if (pass == 2)
            {
                for (int32_t v = 0; v < DETAIL_VARIANTS; v++) {
                    for (int32_t k = 1; k < DETAIL_MAX_KEYS; k += 2) {
                        gRoomTextures.keys[v][k] = 0;
                    }
                }
            }
            newPrepare();
            newRooms(c);
            newMeshes(c, wet);
            int32_t roomEnd = facesSlot(gRoomList.link), meshEnd = facesSlot(gMeshList.link);
            if (c)
            {
                rooms = roomEnd;
                meshes = meshEnd;
            }
            else
            {
                bad += (roomEnd != rooms) + (meshEnd != meshes);
            }
            for (int32_t i = 1; i < meshes; i++)
            {
                if (i == rooms) {
                    i = gMeshFirst;
                }
                if (c) {
                    hashes[i] = commandHash(i);
                } else {
                    bad += commandHash(i) != hashes[i];
                }
            }
        }
    }
    for (uint32_t i = 0; i < gRoomCount; i++) {
        gRooms[i].flags = gRoomScenes[i].wet ? FACE_WATER : 0;
    }
    gRoomTextures.frame = sceneInfo().detailFrame;
    gRoomTextures.miss = roomTextureMiss;
    memcpy(gRoomTextures.keys, gKeys, sizeof(gKeys));
    roomQuadFlags(false);
    return bad;
}

static void stageNew()
{
    const SceneInfo &info = sceneInfo();
    gDraws = sceneArray<const DrawScene>("DRAW");
    gDrawCount = info.draws;
    gFaceSets = sceneArray<const uint8_t>("NMSH");
    gGameFaces = sceneArray<const Face>("MPOL");
    gSprites = sceneArray<const Sprite>("SPRS");
    gClear = sceneArray<const Command>("CMDS")[0];
    gClear.w[0] |= 0x1000 << 16;    // jumping to the first bucket's
    newRoomsInit();

    // the game's way: adding the faces, then flush's work on the master's
    // alone, and on the rooms'
    gLab.meshFlush = measure([] { gameFlushPrepare(FLUSH_MESHES); }, [] { gameFlush(false); });
    gLab.roomFlush = measure([] { gameFlushPrepare(FLUSH_ROOMS); }, [] { gameFlush(false); });

    // the new way at this view as a run of frames shows it, its own copies of
    // shaded tables made (the game's kept for its list)
    shadeTables(gGameTables, true);
    gLab.newFrames = newSettle();

    // in C, then in assembly (which finds the caches of shaded tables warm, as
    // a frame does after the one before)
    newPrepare();
    newRooms(true);
    newMeshes(true);
    newJoin(true);
    gLab.newBadC = gameListCheck(gNewCmds, gNewGrd, gLab.newVisited, gLab.newShaded);
    gLab.newRooms = measure([] { newPrepare(); }, [] { newRooms(false); });
    gLab.roomCommands = facesSlot(gRoomList.link) - 1;
    gLab.newMeshes = measure([] { newPrepare(); }, [] { newMeshes(false); });
    gLab.meshCommands = facesSlot(gMeshList.link) - gMeshFirst;
    gLab.newAsmBad = newAsmCheck();
    gLab.newJoin = measure([] { newPrepare(); newRooms(false); newMeshes(false); }, [] { newJoin(false); });
    gLab.newBad = gameListCheck(gNewCmds, gNewGrd, gLab.newVisited, gLab.newShaded) + gRoomTexturesShort;
    shadeTables(gNewTables, true);
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

// A CPU's work and its waits, ms.
static void workRow(Page &page, const char* name, uint32_t work, uint32_t waits)
{
    char line[TEXT_COLUMNS + 1];
    char* p = put(line, name);
    p = pad(line, p, 17);
    p = putFixed(p, cyclesToHundredths(work), 2, 6);
    putFixed(p, cyclesToHundredths(waits), 2, 7);
    page.add(line);
}

static Page gResults, gPosePage, gVerticesPage, gNewResults, gFramePage;

static void framePage()
{
    const SceneInfo &info = sceneInfo();
    Page &page = gFramePage;
    char line[TEXT_COLUMNS + 1];
    char* p;

    page.count = 0;
    p = put(line, "RENDER LAB  ");
    p = put(p, info.name);
    put(p, ": the frame so far");
    page.add(line);
    page.add("");
    page.add("THE GAME'S WAY, ONE CPU          ms");
    uint32_t pose = gLab.pose + gLab.light + gLab.statics;
    uint32_t vertices = gLab.vertsRooms + gLab.vertsMeshes;
    uint32_t faces = gLab.facesMeshes + gLab.facesRooms + gLab.flush;
    timeRow(page, "1 pose, light...", pose, 0, nullptr);
    timeRow(page, "2 vertices", vertices, 0, nullptr);
    timeRow(page, "3 faces, commands", faces, 0, nullptr);
    timeRow(page, "  all three", pose + vertices + faces, 0, nullptr);
    page.add("");
    page.add("THE NEW WAY, ONE CPU");
    uint32_t newPose = gLab.lightNew + gLab.poseNew + gLab.staticsNew;
    timeRow(page, "1 pose, light...", newPose, 0, nullptr);
    page.add("");
    page.add("THE NEW WAY TOGETHER, ms   work  waits");
    page.add("2+3, master, slave, DSP:");
    timeRow(page, "  start to end", gLab.stages, 0, nullptr);
    workRow(page, "  master", gLab.stagesMaster, gLab.stagesMasterWait);
    workRow(page, "  slave", gLab.stagesSlave, gLab.stagesSlaveWait);
    page.add("1+2+3: pose shared as planned:");
    timeRow(page, "  start to end", gLab.frame, 0, nullptr);
    workRow(page, "  master", gLab.frameMaster, gLab.frameMasterWait);
    workRow(page, "  slave", gLab.frameSlave, gLab.frameSlaveWait);
    p = put(line, "  list, vertices, transforms ");
    p = put(p, gLab.frameBad ? "OFF (" : "exact (");
    p = putNumber(p, gLab.frameBad, 0);
    put(p, " off)");
    page.add(line);
    page.add("");
    page.add("R, L: the other pages");
}

static void posePage()
{
    const SceneInfo &info = sceneInfo();
    Page &page = gPosePage;
    char line[TEXT_COLUMNS + 1];
    char* p;

    page.count = 0;
    p = put(line, "RENDER LAB  ");
    p = put(p, info.name);
    put(p, ": pose");
    page.add(line);
    p = put(line, "items:");
    p = putNumber(p, gLab.poseItems, 3);
    p = put(p, ", matrices:");
    putNumber(p, gLab.poseMatrices, 4);
    page.add(line);
    page.add("");
    page.add("STAGE 1, POSE: THE GAME'S");
    page.add("master, run alone     ms  cycles a");
    timeRow(page, "items' nodes", gLab.pose, gLab.poseMatrices, "matrix");
    timeRow(page, "items' light", gLab.light, gLab.poseItems, "item");
    timeRow(page, "statics, shadows", gLab.statics, gLab.staticCount + gLab.shadowCount, "each");
    timeRow(page, "rooms seen", gLab.rooms, gLab.roomsSeen, "room");
    p = put(line, gLab.roomsBad ? "OFF: rooms seen" : "exact: rooms seen");
    p = putNumber(p, gLab.roomsBad, 2);
    p = put(p, " off, voids");
    putNumber(p, gLab.roomsVoids, 2);
    page.add(line);
    p = put(line, gLab.poseBad || gLab.lightBad ? "OFF: matrices" : "exact: matrices");
    p = putNumber(p, gLab.poseBad, 2);
    p = put(p, " off, light");
    p = putNumber(p, gLab.lightBad, 2);
    put(p, " off");
    page.add(line);
    p = put(line, gLab.staticsBad ? "OFF: statics, shadows" : "exact: statics, shadows");
    p = putNumber(p, gLab.staticsBad, 2);
    put(p, " off");
    page.add(line);
    page.add("");
    page.add("STAGE 1, POSE: THE NEW WAY");
    page.add("master, run alone     ms  cycles a");
    timeRow(page, "items' nodes", gLab.poseNew, gLab.poseMatrices, "matrix");
    timeRow(page, "items' light", gLab.lightNew, gLab.poseItems, "item");
    timeRow(page, "statics, shadows", gLab.staticsNew, gLab.staticCount + gLab.shadowCount, "each");
    timeRow(page, "rooms seen", gLab.roomsNew, gLab.roomsSeen, "room");
    p = put(line, gLab.roomsNewBad ? "OFF: rooms seen" : "exact: rooms seen");
    p = putNumber(p, gLab.roomsNewBad, 2);
    p = put(p, " off, seldom");
    putNumber(p, gLab.roomsSeldom, 2);
    page.add(line);
    p = put(line, gLab.poseNewBad || gLab.lightNewBad ? "OFF: matrices" : "exact: matrices");
    p = putNumber(p, gLab.poseNewBad, 2);
    p = put(p, " off, light");
    p = putNumber(p, gLab.lightNewBad, 2);
    put(p, " off");
    page.add(line);
    p = put(line, gLab.poseSeldom || gLab.lightSeldom ? "seldom OFF: matrices" : "seldom as game's: matrices");
    p = putNumber(p, gLab.poseSeldom, 2);
    p = put(p, ", light");
    putNumber(p, gLab.lightSeldom, 2);
    page.add(line);
    p = put(line, gLab.staticsNewBad ? "OFF: statics, shadows" : "exact: statics, shadows");
    p = putNumber(p, gLab.staticsNewBad, 2);
    p = put(p, " off, seldom");
    putNumber(p, gLab.staticsSeldom, 2);
    page.add(line);
    page.add("");
    page.add("R, L: the other pages");
}

static void verticesPage()
{
    const SceneInfo &info = sceneInfo();
    Page &page = gVerticesPage;
    char line[TEXT_COLUMNS + 1];
    char* p;

    page.count = 0;
    p = put(line, "RENDER LAB  ");
    p = put(p, info.name);
    put(p, ": vertices");
    page.add(line);
    p = put(line, "rooms':");
    p = putNumber(p, gLab.roomVertices, 5);
    p = put(p, ", draws':");
    putNumber(p, gLab.meshVertices, 5);
    page.add(line);
    page.add("");
    page.add("STAGE 2, VERTICES: THE NEW WAY");
    page.add("master, run alone     ms  cycles a");
    timeRow(page, "rooms: game's", gLab.vertsRooms, gLab.roomVertices, "vertex");
    timeRow(page, "  DSP", gLab.dspRooms, gLab.roomVertices, "vertex");
    timeRow(page, "  projected", gLab.newVertsRooms, gLab.roomVertices, "vertex");
    timeRow(page, "meshes: game's", gLab.vertsMeshes, gLab.meshVertices, "vertex");
    timeRow(page, "  DSP", gLab.dspMeshes, gLab.meshVertices, "vertex");
    timeRow(page, "  DSP, v3's data", gLab.dspMeshes3, gLab.meshVertices, "vertex");
    timeRow(page, "  fed", gLab.newVertsFeed, gLab.meshVertices, "vertex");
    timeRow(page, "  projected", gLab.newVertsMeshes, gLab.meshVertices, "vertex");
    p = put(line, "vertices ");
    p = put(p, gLab.newVertsBad ? "OFF" : "exact");
    p = put(p, " (");
    p = putNumber(p, gLab.newVertsBad, 0);
    p = put(p, " off), seldom paths");
    p = putNumber(p, gLab.newVertsSeldom, 2);
    put(p, " off");
    page.add(line);
    p = put(line, "DSP as modelled: rooms");
    p = putNumber(p, gLab.dspRoomsBad, 2);
    p = put(p, " off, meshes");
    p = putNumber(p, gLab.dspMeshesBad, 2);
    put(p, " off");
    page.add(line);
    p = put(line, "  v3 as modelled:");
    p = putNumber(p, gLab.dspMeshes3Bad, 2);
    put(p, " off");
    page.add(line);
    p = put(line, "feeding's asm as its C:");
    p = putNumber(p, gLab.feedBad, 2);
    put(p, " off");
    page.add(line);
    page.add("TOGETHER, ms       work  waits");
    timeRow(page, "  start to end", gLab.together, 0, nullptr);
    workRow(page, "  master", gLab.togetherMaster, gLab.togetherMasterWait);
    workRow(page, "  slave", gLab.togetherSlave, gLab.togetherSlaveWait);
    p = put(line, "  DSP rooms");
    p = putFixed(p, cyclesToHundredths(gLab.togetherDspRooms), 2, 6);
    p = put(p, ", gap");
    p = putFixed(p, cyclesToHundredths(gLab.togetherDspGap), 2, 5);
    p = put(p, ", meshes");
    putFixed(p, cyclesToHundredths(gLab.togetherDspMeshes), 2, 6);
    page.add(line);
    p = put(line, "  vertices ");
    p = put(p, gLab.togetherBad ? "OFF (" : "exact (");
    p = putNumber(p, gLab.togetherBad, 0);
    put(p, " off)");
    page.add(line);
    page.add("");
    page.add("R, L: the other pages");
}

static void newPage()
{
    const SceneInfo &info = sceneInfo();
    Page &page = gNewResults;
    char line[TEXT_COLUMNS + 1];
    char* p;

    page.count = 0;
    p = put(line, "RENDER LAB  ");
    p = put(p, info.name);
    put(p, ": commands");
    page.add(line);
    p = put(line, "draws':");
    p = putNumber(p, gLab.meshCommands, 5);
    p = put(p, ", rooms':");
    putNumber(p, gLab.roomCommands, 5);
    page.add(line);
    page.add("");
    page.add("STAGE 3, FACES TO COMMANDS: THE NEW WAY");
    timeRow(page, "draws: game's", gLab.facesMeshes + gLab.meshFlush, gLab.meshCommands, "command");
    timeRow(page, "  new", gLab.newMeshes, gLab.meshCommands, "command");
    timeRow(page, "rooms: game's", gLab.facesRooms + gLab.roomFlush, gLab.roomCommands, "command");
    timeRow(page, "  new", gLab.newRooms, gLab.roomCommands, "command");
    timeRow(page, "join, all", gLab.newJoin, info.commands - 1, "command");
    timeRow(page, "floor's rate", gLab.floor, info.commands - 1, "command");
    page.add("");
    p = put(line, "new list ");
    p = put(p, gLab.newBad || gLab.newBadC ? "OFF" : "exact");
    p = put(p, " (C ");
    p = putNumber(p, gLab.newBadC, 0);
    p = put(p, ", asm ");
    p = putNumber(p, gLab.newBad, 0);
    put(p, " off),");
    page.add(line);
    p = putNumber(line, gLab.newVisited, 0);
    p = put(p, " followed,");
    p = putNumber(p, gLab.newShaded, 4);
    put(p, " shaded another way");
    page.add(line);
    p = put(line, "(its shaded copies settled in");
    p = putNumber(p, gLab.newFrames, 2);
    put(p, " frames)");
    page.add(line);
    p = put(line, "wet, a frame on, misses: asm ");
    p = put(p, gLab.newAsmBad ? "OFF" : "as C");
    p = put(p, " (");
    p = putNumber(p, gLab.newAsmBad, 0);
    put(p, " off)");
    page.add(line);
    p = put(line, "VDP1 ms: game's list");
    p = putFixed(p, cyclesToHundredths(gLab.vdp1Game), 2, 7);
    p = put(p, ", new");
    putFixed(p, cyclesToHundredths(gLab.vdp1New), 2, 7);
    page.add(line);
    page.add("STAGES 2+3 TOGETHER, ms  work  waits");
    timeRow(page, "  start to end", gLab.stages, 0, nullptr);
    workRow(page, "  master", gLab.stagesMaster, gLab.stagesMasterWait);
    workRow(page, "  slave", gLab.stagesSlave, gLab.stagesSlaveWait);
    p = put(line, "  list and vertices ");
    p = put(p, gLab.stagesBad ? "OFF (" : "exact (");
    p = putNumber(p, gLab.stagesBad, 0);
    put(p, " off)");
    page.add(line);
    page.add("");
    page.add("R, L: the other pages");
}

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
    page.add("STAGE 2, VERTICES: THE GAME'S");
    page.add("master, run alone     ms  cycles a");
    timeRow(page, "rooms", gLab.vertsRooms, gLab.roomVertices, "vertex");
    timeRow(page, "meshes", gLab.vertsMeshes, gLab.meshVertices, "vertex");
    p = put(line, "vertices ");
    if (gLab.vertsBad)
    {
        p = put(p, "OFF (");
        p = putNumber(p, gLab.vertsBad, 0);
        put(p, ")");
    }
    else
    {
        put(p, "exact");
    }
    page.add(line);
    page.add("");
    page.add("STAGE 3, FACES TO COMMANDS: THE GAME'S");
    uint32_t commands = info.commands - 1;
    timeRow(page, "faces: meshes", gLab.facesMeshes, gLab.meshFacesGiven, "face given");
    timeRow(page, "faces: rooms", gLab.facesRooms, gLab.roomFacesGiven, "face given");
    timeRow(page, "flush", gLab.flush, commands, "command");
    timeRow(page, "all three", gLab.facesMeshes + gLab.facesRooms + gLab.flush, commands, "command");
    timeRow(page, "floor", gLab.floor, commands, "command");
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
    page.add("R, L: the other pages");
}

enum { PAGE_GAME_WAY, PAGE_POSE, PAGE_NEW_VERTICES, PAGE_NEW_WAY, PAGE_FRAME, PAGE_GAME, PAGE_NEW, PAGES };

static void show(int32_t page)
{
    if (page < PAGE_GAME)
    {
        const Page &text = page == PAGE_GAME_WAY ? gResults : page == PAGE_POSE ? gPosePage
                         : page == PAGE_NEW_VERTICES ? gVerticesPage : page == PAGE_NEW_WAY ? gNewResults
                         : gFramePage;
        textPage();
        for (int32_t r = 0; r < text.count; r++) {
            textRow(r, text.rows[r]);
        }
        return;
    }
    textFrame();
    if (page == PAGE_GAME)
    {
        const SceneInfo &info = sceneInfo();
        shadeTables(gGameTables, false);
        upload(sceneArray<const Command>("CMDS"), (const uint32_t (*)[2])sceneChunk("GRDS"), info.commands);
        textRow(0, "THE GAME'S LIST");
    }
    else
    {
        shadeTables(gNewTables, false);
        upload(gNewCmds, gNewGrd, gNewEnd);
        textRow(0, "THE NEW LIST");
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
    gGameTables = (uint32_t (*)[SHADE_TABLES / 4])sceneAllocLow(2 * SHADE_TABLES);
    gNewTables = (uint32_t (*)[SHADE_TABLES / 4])sceneAllocLow(2 * SHADE_TABLES);

    stagePose();
    stageTwo();
    stageTwoNew();
    stageThree();
    stageFloor();

    // the lists drawn: the game's and the lab's of the game's way (the split
    // flush's, as the game's is), before the new way's frames make shaded
    // copies of their own; then the new
    const SceneInfo &info = sceneInfo();
    upload(sceneArray<const Command>("CMDS"), (const uint32_t (*)[2])sceneChunk("GRDS"), info.commands);
    gLab.vdp1Game = vdp1Show();
    gameFlushPrepare();
    int32_t end = gameFlush(true);
    upload(gCmds, gGouraud, end);
    gLab.vdp1Lab = vdp1Show();
    stageNew();
    stageTogether();
    stageFrame();
    shadeTables(gNewTables, false);
    upload(gNewCmds, gNewGrd, gNewEnd);
    gLab.vdp1New = vdp1Show();
    gLab.arenaUsed = sceneUsed(&gLab.arenaSize);
    gLab.magic = LAB_MAGIC;

    resultsPage();
    posePage();
    verticesPage();
    newPage();
    framePage();
    int32_t page = PAGE_GAME_WAY;
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
