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
#include "draws.h"
#include "pose.h"
#include "rooms.h"
#include "hints.h"
#include "frame.h"
#include "labrender.h"
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
    int32_t orderOff;           // the game's list with its rooms in the walk's order: commands not the capture's
    uint32_t draws;             // cycles: the draw list the game's way (the renderer's side of the calls)
    int32_t drawsBad;           // ... its draws, items, statics, shadows and faces not the capture's
    uint32_t drawCalls;         // the calls it took
    uint32_t drawsNew;          // cycles: the draw list the new way
    int32_t drawsNewBad;        // ... not the game's way's
    int32_t drawsSeldom;        // ... on made-up calls, against the game's way on them (and paths not taken)
    uint32_t arenaUsed;         // bytes of high work RAM the scene and the stages took (sceneUsed)
    uint32_t arenaSize;
    uint32_t lowUsed;           // ... of low work RAM (sceneUsedLow)
    uint32_t lowSize;
    int32_t othersBad;          // made-up sprites, rectangles, lines and voids: facesOtherAsm not as
                                // facesOther; made-up chains of commands held and placed: not as their
                                // model (and paths not taken)
    uint32_t runFrames;         // the whole frame run in a row (stageRun): frames
    uint32_t runFrame;          // cycles a frame, plot to plot
    uint32_t runStages;         // ... the stages (the master, start to end)
    uint32_t runVdp1Wait;       // ... the master waiting for VDP1 to draw the last list
    uint32_t runVblank;         // ... then for the vertical blank and the frame buffers' change
    uint32_t runUpload;         // ... the list's upload
    uint32_t runVdp1;           // cycles VDP1 drew a list in, plot to drawn, over the frames it was waited for
    uint32_t runWaited;         // those frames
    uint32_t uploadBytes;       // the list's bytes uploaded
    int32_t uploadBad;          // words of the last list in VDP1 VRAM not as uploaded
    uint32_t stagesDsp;         // cycles the DSP ran in stages 2+3 together (Stages.dsp)
    uint32_t frameDsp;          // ... in the frame so far
    uint32_t runMaster;         // the whole frame in a row, a frame: the master's work in the stages
    uint32_t runSlave;          // ... the slave's
    uint32_t runDsp;            // ... the DSP's runs
    int32_t texturesQueued;     // room textures the game's way's faces queued for upload
    int32_t texturesGameOff;    // ... not as the game's (its uploads, the cache after, the textures as
                                // its upload left VDP1 VRAM)
    int32_t texturesUploadOff;  // longwords of the new way's uploads in VDP1 VRAM not as staged
};

extern "C" LabResults gLab;
LabResults gLab;

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
    gLab.roomsBad = gameRoomsCheck(nullptr, 0, gLab.roomsVoids) + gameRoomJobsCheck(nullptr, 0);
    gameStaticsInit();
    gLab.statics = measure([] {}, [] { gameStatics(); });
    gameStatics();
    gLab.staticsBad = gameStaticsCheck(nullptr, gLab.staticCount, gLab.shadowCount);

    poseLabInit();
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
    roomsLabInit();
    gLab.roomsNew = measure([] {}, [] { roomsSeen(); });
    roomsSeen();
    gLab.roomsNewBad = roomsSeenCheck();
    gLab.roomsSeldom = roomsSeldomCheck();

    // the draw list: after the walk's fills, from the frame's calls
    int32_t fills;
    roomsFills(fills);
    gLab.drawCalls = gameDrawsInit();
    gLab.draws = measure([] {}, [fills] { gameDraws(fills); });
    gameDraws(fills);
    gLab.drawsBad = gameDrawsCheck(gameDrawList(), fills);
    drawsLabInit();
    uint32_t masks[2];
    hintsMasks(masks);
    drawsHints(masks);
    gLab.drawsNew = measure([] {}, [fills] { drawsLab(fills); });
    drawsLab(fills);
    gLab.drawsNewBad = drawsCheck(gameDrawList()) + !drawsLabFits();
    gLab.drawsSeldom = drawsSeldomCheck();
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
    uint32_t jobCount;
    const FaceRoom* jobs = roomsJobs(jobCount);
    const char* error = verticesLabInit(jobs, jobCount);
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
    gLab.texturesQueued = gameTextureQueue().count;
    gLab.texturesGameOff = texturesQueueOff(gameTextureQueue(), texturesCaptured()) + texturesStateOff() +
                           texturesVramOff(gameTextureQueue());

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

    // flush on the faces the game's way made, in their own tables: the game's
    // list again (the way the lab makes the game's list for other room orders)
    gameFacesPrepare();
    gameFacesMaster();
    gameFacesRooms();
    gameFlushPrepare(FLUSH_MADE);
    end = gameFlush(false);
    gLab.flushBad += gameCommandsCheck(gCmds, gGouraud, end);
    gameFlushPrepare();

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
        xf.cmd = gFrameCmds[1].w;
        xf.grd = gFrameGrd[1];
        xf.grdLink = ((VRAM_GOURAUD_BASE >> 3) + 1) << 16;
        xf.shadeGouraud = (const uint16_t*)(sceneArray<const uint8_t>("SHAD") + 128);
        floorAsm(faces, info.floorFaces, &xf);
    });
    gLab.floorBad = 0;
    for (uint32_t i = 0; i < info.floorFaces; i++) {
        if (faces[i].flags & FLOOR_EXACT) {
            gLab.floorBad += !gameCommandMatches(gFrameCmds[i + 1], gFrameGrd[i + 1], i + 1);
        }
    }
}

// The scene's draws and what they draw, for the new way's pages (the frame
// module's state holds the rest: renderer/frame.h).
static const DrawScene* gDraws;
static uint32_t gDrawCount;
static const Face* gGameFaces;
static const DrawThing* gThings;    // DTHG: the scene's draws' things
static bool gRoomTexturesShort;     // the rooms take more textures than the frame keeps materials for
static bool gBottomOff;             // the split corners' bottom not the game's
static HintedDraw gSceneHinted[MAX_HELD];
static int32_t gSceneHintedCount;
static CommandList* const gLists[2] = { &gRoomList, &gMeshList };

#define FACE_WATER  (1u << 30)

// The game's list with its rooms in the walk's order (walkOrderList): what
// the new way's lists are checked against.
static GameList gWalkList;

static void newPrepare()
{
    gameFlushPrepare();
    frameLists();
    texturesReset();
    gFrameTextures.count = 0;

    // the materials' characters looked up afresh, as in a frame of its own
    for (int32_t i = 0; i < MAX_ROOM_TEXTURES; i++) {
        gRoomMaterials[i].frame = ~gRoomTextures.frame;
    }
}

// The rooms in the walk's order, through facesRoom or (c) facesRoomC.
static void newRooms(bool c)
{
    for (uint32_t i = 0; i < gFrameRoomCount; i++)
    {
        if (c) {
            facesRoomC(gFrameRooms[i], gRoomTextures, gSplit, gRoomList);
        } else {
            if (!i) {
                facesRoomsBegin(gRoomRun, gRoomTextures, gSplit, gRoomList);
            }
            facesRoom(gFrameRooms[i], gRoomRun);
        }
    }
}

// The frame module on the scene: the walk's jobs as its rooms, the scene's
// faces and materials, the cache as the frame found it, the split for the
// scene's draws.
static void newRoomsInit()
{
    const SceneInfo &info = sceneInfo();
    gFrameRooms = roomsJobs(gFrameRoomCount);
    if (gFrameRoomCount > MAX_ROOMS) {
        gFrameRoomCount = MAX_ROOMS;
    }

    // the textures the rooms' faces take
    uint32_t textures = 0;
    for (uint32_t i = 0; i < gFrameRoomCount; i++)
    {
        const FaceRoom &room = gFrameRooms[i];
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

    const MeshStore* store = sceneArray<const MeshStore>("MSTO");
    FrameLevel level;
    level.faceSets = (const uint8_t*)store + store->faceSets;
    level.materials = (const Material*)((const uint8_t*)store + store->materials);
    level.textures = sceneArray<const Texture>("TEXS");
    level.palette = sceneArray<const uint16_t>("PALT");
    level.sprites = sceneArray<const Sprite>("SPRS");
    level.shadeGouraud = gameShadeGouraud();
    level.shadeLevel = sceneArray<const uint8_t>("SHAD");
    level.shade[0] = gameShadeCache(0);
    level.shade[1] = gameShadeCache(1);
    level.pmod = info.texturePmod;
    level.lod = info.lod;
    level.pools[0] = info.detailVram;
    level.pools[1] = info.poolVram[1];
    level.pools[2] = info.poolVram[2];
    level.frame = info.detailFrame;
    level.roomTextures = textures;
    level.clear = sceneArray<const Command>("CMDS")[0];
    level.vertices = gVertices;
    frameInit(level);
    frameSplit(gDraws, gDrawCount, 0);
    frameLists();
    gBottomOff = gSplit.bottom != info.bottom;
}

// The master's draws in order, their faces through facesMeshAsm or (c)
// facesMeshC, a hinted mesh's held or moved; with water, every draw under
// water.
static void newMeshes(bool c, bool water = false)
{
    const DrawScene* draws = gDraws;
    const MeshStore* store = sceneArray<const MeshStore>("MSTO");
    const uint8_t* sets = (const uint8_t*)store + store->faceSets;
    frameHintsBegin();
    frameHinted(gSceneHinted, gSceneHintedCount);
    for (uint32_t i = 0; i < gDrawCount; i++)
    {
        const DrawScene &d = draws[i];
        const Vertex* vertices = gVertices + d.base;
        if (d.type == DRAW_COPY)
        {
            if (c) {
                facesOther(gGameFaces[d.first], gVertices, d.bucket, gMeshList);
            } else {
                facesOtherAsm(gGameFaces + d.first, gVertices, d.bucket, &gMeshList);
            }
            continue;
        }
        const FaceSet* set = d.type == DRAW_SHADOW ? facesShadow()
                                                   : (const FaceSet*)(sets + ((const uint32_t*)sets)[d.mesh]);
        uint32_t mode = (d.inside ? MESH_INSIDE : 0) | (water || (d.flags & FACE_WATER) ? MESH_WATER : 0);
        if (frameMeshHinted(i, set, vertices, mode))
            continue;
        if (c) {
            facesMeshC(set, vertices, mode, gMeshList);
        } else {
            facesMeshAsm(set, vertices, mode, &gMeshList);
        }
    }
    uint32_t waited = 0;
    frameHintsPlace(false, waited);
}

// The buckets joined (facesJoinAsm, or facesJoin), after the game's clear;
// the end after the last command.
static void newJoin(bool c)
{
    frameEnds();
    if (c) {
        facesJoin(gLists, 2, gFrameCmds, 0, gFrameEnd);
    } else {
        facesJoinRoomsMeshes(gRoomList, gMeshList, gFrameCmds, 0, gFrameEnd);
    }
}

// The frame's fills past the draw distance (the walk's at the scene's view):
// their vertices before the draw list's.
static int32_t gFills;

// Stages 2 and 3 together on the scene's draws, or (posed) the frame from
// its calls (renderer/frame.h): the pose's checks then held to its draw list.
static FrameTimes stagesTogether(bool posed, int32_t bias)
{
    if (!posed)
    {
        FrameDraws given = { gDraws, gDrawCount, gGameFaces, gVertices, gSceneHinted, gSceneHintedCount };
        return frameDraws(given, bias);
    }
    FrameCalls calls;
    calls.calls = sceneArray<const CallRecord>("CALL", &calls.count);
    calls.items = sceneArray<ItemRecord>("OITM");
    calls.base = gFills;
    FrameTimes s = frameRun(calls, bias);
    poseLabUse(&drawsList(), gFills);
    return s;
}

// The stages run together, RUNS times after the frames that settle the
// split (each moves the plan by half how late the master's draws ended after
// the slave's rooms): their mean, and the list and vertices against the
// game's.
static FrameTimes togetherMeasured(bool posed, int32_t &bad)
{
    int32_t bias = 0;
    for (int32_t f = 0; f < 6; f++)
    {
        if (!posed) {
            framePlan(bias, posed);
        }
        newPrepare();
        verticesPoison();
        if (posed)
        {
            posePoison();
            drawsPoison();
            roomsPoison();
        }
        cachePurge();
        bias += stagesTogether(posed, bias).late / 2;
    }
    if (!posed) {
        framePlan(bias, posed);
    }
    FrameTimes sum = {};
    for (int32_t r = 0; r < RUNS; r++)
    {
        newPrepare();
        verticesPoison();
        if (posed)
        {
            posePoison();
            drawsPoison();
            roomsPoison();
        }
        cachePurge();
        FrameTimes s = stagesTogether(posed, bias);
        sum.wall += s.wall;
        sum.masterWork += s.masterWork;
        sum.masterWait += s.masterWait;
        sum.slaveWork += s.slaveWork;
        sum.slaveWait += s.slaveWait;
        sum.dsp += s.dsp;
    }
    sum.wall /= RUNS;
    sum.masterWork /= RUNS;
    sum.masterWait /= RUNS;
    sum.slaveWork /= RUNS;
    sum.slaveWait /= RUNS;
    sum.dsp /= RUNS;
    int32_t visited, shaded, rooms, meshes;
    bad = gameListCheck(gFrameCmds, gFrameGrd, visited, shaded) + verticesRoomsCheck(rooms) +
          verticesMeshesCheck(meshes) + texturesQueueOff(gFrameTextures, gWalkList.textures) +
          (posed ? poseCheck() + roomsSeenCheck() + drawsCheck(gameDrawList()) : 0);
    return sum;
}

static void stageTogether()
{
    FrameTimes s = togetherMeasured(false, gLab.stagesBad);
    gLab.stages = s.wall;
    gLab.stagesMaster = s.masterWork;
    gLab.stagesMasterWait = s.masterWait;
    gLab.stagesSlave = s.slaveWork;
    gLab.stagesSlaveWait = s.slaveWait;
    gLab.stagesDsp = s.dsp;
}

// The frame so far: stages 1, 2 and 3 together, the meshes' transforms the
// new pose's.
static void stageFrame()
{
    roomsFills(gFills);
    FrameTimes s = togetherMeasured(true, gLab.frameBad);
    verticesDrawsSet(sceneArray<const DrawScene>("DRAW"), sceneInfo().draws, sceneArray<const DrawXform>("XFRM"));
    poseLabUse(nullptr);
    gLab.frame = s.wall;
    gLab.frameMaster = s.masterWork;
    gLab.frameMasterWait = s.masterWait;
    gLab.frameSlave = s.slaveWork;
    gLab.frameSlaveWait = s.slaveWait;
    gLab.frameDsp = s.dsp;
}

// The words of the uploaded list in VDP1 VRAM not as gFrameCmds and
// gFrameGrd hold them.
static int32_t uploadCheck()
{
    int32_t bad = 0;
    int32_t rooms = facesSlot(gRoomList.link);
    const volatile uint32_t* cmds = (const volatile uint32_t*)(VDP1_VRAM + VRAM_FRAME_CMDS);
    const volatile uint32_t* grd = (const volatile uint32_t*)(VDP1_VRAM + VRAM_GOURAUD_BASE);
    for (int32_t i = 0; i <= gFrameEnd; i++)
    {
        if (i == rooms) {
            i = gMeshFirst;
        }
        for (int32_t w = 0; w < 8; w++) {
            bad += cmds[8 * i + w] != gFrameCmds[i].w[w];
        }
        if (i < gFrameEnd) {
            bad += (grd[2 * i] != gFrameGrd[i][0]) + (grd[2 * i + 1] != gFrameGrd[i][1]);
        }
    }
    return bad;
}

// The whole frame, frame after frame as the game runs it, RUN_FRAMES frames:
// each frame's stages 1-3 (stagesTogether: the slave walks, the master makes
// the draw list, ...), the shaded copies' and room textures' frames on by
// one; once VDP1 has drawn the last list and that frame is shown (the frame
// buffers changed at a vblank-in), the new list uploaded and plotted, VDP1
// drawing it while the CPUs make the next. Each frame starts from an empty
// cache, as the stages were measured (the DSP and the other CPU wrote behind
// it). What it changes put back after, so the list and caches stay those the
// checks read.
#define RUN_FRAMES  1000

static void roomQuadFlags(bool save);

static void stageRun()
{
    // what the frames change: the list, the shaded copies' caches, the room
    // texture cache, the quads' kept decisions, the frames
    uint32_t slots = DETAIL_SLOTS;
    DetailSlot* detailSlots = texturesSlots();
    Command* cmds = (Command*)sceneAllocLow(sizeof(gFrameCmds));
    uint32_t (*grd)[2] = (uint32_t (*)[2])sceneAllocLow(sizeof(gFrameGrd));
    ShadeCache* caches = (ShadeCache*)sceneAllocLow(2 * sizeof(ShadeCache));
    uint8_t* keys = (uint8_t*)sceneAllocLow(DETAIL_VARIANTS * DETAIL_MAX_KEYS);
    DetailSlot* saved = (DetailSlot*)sceneAllocLow(slots * sizeof(DetailSlot));
    static TextureQueue uploads;
    if (!saved)
        return;
    uploads = gFrameTextures;
    memcpy(cmds, gFrameCmds, sizeof(gFrameCmds));
    memcpy(grd, gFrameGrd, sizeof(gFrameGrd));
    int32_t end = gFrameEnd;
    for (int32_t c = 0; c < 2; c++) {
        caches[c] = *gameShadeCache(c);
    }
    memcpy(keys, gRoomTextures.keys, DETAIL_VARIANTS * DETAIL_MAX_KEYS);
    memcpy(saved, detailSlots, slots * sizeof(DetailSlot));
    roomQuadFlags(true);
    uint32_t textureFrame = gRoomTextures.frame;

    textFrame();
    textRow(0, "THE WHOLE FRAME, RUNNING");
    uint32_t wait = 0, vblank = 0, stages = 0, upload = 0, vdp1 = 0, waited = 0, frames = 0;
    uint64_t master = 0, slave = 0, dsp = 0;
    uint32_t plotted = vdp1Plot();      // the list as the last measured frame left it
    uint64_t total = 0;
    int32_t bias = 0;
    for (int32_t f = 0; f < RUN_FRAMES; f++)
    {
        cachePurge();
        uint32_t t0 = ticks();
        gameShadeCache(0)->frame++;
        gameShadeCache(1)->frame++;
        gRoomTextures.frame++;
        gFrameTextures.count = 0;
        FrameTimes s = stagesTogether(true, bias);
        bias += s.late / 2;
        uint32_t t1 = ticks();
        stages += ticksCycles(t0, t1);
        master += s.masterWork;
        slave += s.slaveWork;
        dsp += s.dsp;
        uint32_t w = wait, drawn;
        if (!vdp1ShowDrawn(plotted, drawn, wait, vblank))
            break;
        if (wait > w)
        {
            vdp1 += drawn;
            waited++;
        }
        uint32_t u0 = ticks();
        gLab.uploadBytes = frameUpload();
        upload += ticksCycles(u0, ticks());
        uint32_t next = vdp1Plot();
        total += ticksCycles(plotted, next);
        plotted = next;
        frames++;
    }
    uint32_t drawn, w = 0, v = 0;
    vdp1ShowDrawn(plotted, drawn, w, v);
    gLab.uploadBad = uploadCheck();
    gLab.runFrames = frames;
    if (frames)
    {
        gLab.runFrame = uint32_t(total / frames);
        gLab.runStages = stages / frames;
        gLab.runVdp1Wait = wait / frames;
        gLab.runVblank = vblank / frames;
        gLab.runUpload = upload / frames;
        gLab.runMaster = uint32_t(master / frames);
        gLab.runSlave = uint32_t(slave / frames);
        gLab.runDsp = uint32_t(dsp / frames);
    }
    gLab.runVdp1 = waited ? vdp1 / waited : 0;
    gLab.runWaited = waited;

    memcpy(gFrameCmds, cmds, sizeof(gFrameCmds));
    memcpy(gFrameGrd, grd, sizeof(gFrameGrd));
    gFrameEnd = end;
    for (int32_t c = 0; c < 2; c++) {
        *gameShadeCache(c) = caches[c];
    }
    memcpy(gRoomTextures.keys, keys, DETAIL_VARIANTS * DETAIL_MAX_KEYS);
    memcpy(detailSlots, saved, slots * sizeof(DetailSlot));
    roomQuadFlags(false);
    gRoomTextures.frame = textureFrame;
    gFrameTextures = uploads;
    texturesUpload(gFrameTextures);
    verticesDrawsSet(sceneArray<const DrawScene>("DRAW"), sceneInfo().draws, sceneArray<const DrawXform>("XFRM"));
    poseLabUse(nullptr);
}

// A command's words and gouraud table, hashed.
static uint32_t commandHash(int32_t slot)
{
    uint32_t h = 0;
    for (int32_t i = 0; i < 8; i++) {
        h = ((h << 5) | (h >> 27)) ^ gFrameCmds[slot].w[i];
    }
    return ((h << 5) | (h >> 27)) ^ gFrameGrd[slot][0] ^ (gFrameGrd[slot][1] << 1);
}

// The new way's frames at this view, its caches of shaded tables settling on
// what it draws (a copy drawn is kept two frames): until its list is the
// frame before's, at most 8. The frames run.
static int32_t newSettle()
{
    static uint32_t* hashes = (uint32_t*)sceneAllocLow(MAX_CMDS * sizeof(uint32_t));
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
    static uint32_t* flags = (uint32_t*)sceneAllocLow(2048 * sizeof(uint32_t));
    int32_t n = 0;
    for (uint32_t i = 0; i < gFrameRoomCount; i++)
    {
        for (int32_t k = 0; k < gFrameRooms[i].quadsCount && n < 2048; k++, n++)
        {
            if (save) {
                flags[n] = gFrameRooms[i].quads[k].flags;
            } else {
                gFrameRooms[i].quads[k].flags = flags[n];
            }
        }
    }
}

// The room texture cache as the frame found it, for the loader below.
static uint8_t (*gKeys)[DETAIL_MAX_KEYS];

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
    static uint32_t* hashes = (uint32_t*)sceneAllocLow(MAX_CMDS * sizeof(uint32_t));
    static uint32_t water[MAX_ROOMS];
    for (uint32_t i = 0; i < gFrameRoomCount; i++) {
        water[i] = gFrameRooms[i].flags;
    }
    roomQuadFlags(true);
    if (!gKeys) {
        gKeys = (uint8_t (*)[DETAIL_MAX_KEYS])sceneAllocLow(DETAIL_VARIANTS * DETAIL_MAX_KEYS);
    }
    memcpy(gKeys, gRoomTextures.keys, DETAIL_VARIANTS * DETAIL_MAX_KEYS);
    int32_t bad = 0;
    for (int32_t pass = 0; pass < 3; pass++)
    {
        bool wet = pass == 0;
        for (uint32_t i = 0; i < gFrameRoomCount; i++) {
            gFrameRooms[i].flags = wet ? FACE_WATER : water[i];
        }
        gRoomTextures.frame = sceneInfo().detailFrame + (pass == 1);
        gRoomTextures.miss = pass == 2 ? roomTextureReload : frameTextureMiss;
        int32_t rooms = 0, meshes = 0;
        for (int32_t c = 1; c >= 0; c--)
        {
            roomQuadFlags(false);
            newPrepare();
            if (pass == 2)
            {
                for (int32_t v = 0; v < DETAIL_VARIANTS; v++) {
                    for (int32_t k = 1; k < DETAIL_MAX_KEYS; k += 2) {
                        gRoomTextures.keys[v][k] = 0;
                    }
                }
            }
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
    for (uint32_t i = 0; i < gFrameRoomCount; i++) {
        gFrameRooms[i].flags = water[i];
    }
    gRoomTextures.frame = sceneInfo().detailFrame;
    gRoomTextures.miss = frameTextureMiss;
    memcpy(gRoomTextures.keys, gKeys, DETAIL_VARIANTS * DETAIL_MAX_KEYS);
    roomQuadFlags(false);
    return bad;
}

// The game's list with its rooms in the walk's order (the new way's, not the
// order the game's slave and DSP happened to finish them): the game's own
// jobs (ROOM) in that order, their faces as the game adds them, flush on
// them. The new way's lists are checked against it; it against the
// capture's, the commands the order moves.
static void walkOrderList()
{
    uint32_t count, rooms;
    const FaceRoom* jobs = roomsJobs(count);
    const RoomScene* scene = sceneArray<const RoomScene>("ROOM", &rooms);
    RoomScene* ordered = (RoomScene*)sceneAllocLow(rooms * sizeof(RoomScene));
    gWalkList.cmds = (Command*)sceneAllocLow(MAX_CMDS * sizeof(Command));
    gWalkList.grd = (uint32_t (*)[2])sceneAllocLow(MAX_CMDS * sizeof(gWalkList.grd[0]));
    if (!ordered || !gWalkList.cmds || !gWalkList.grd)
    {
        textRow(1, "no low work RAM for the walk's list");
        while (1);
    }
    uint32_t n = 0;
    for (uint32_t k = 0; k < count; k++)
    {
        for (uint32_t i = 0; i < rooms; i++)
        {
            if (scene[i].room == jobs[k].room)
            {
                ordered[n++] = scene[i];
                break;
            }
        }
    }
    gameListMake(ordered, n, gWalkList);
    int32_t visited, shaded;
    gLab.orderOff = gameListCheck(gWalkList.cmds, gWalkList.grd, visited, shaded) + (n != rooms);
    gameListReference(&gWalkList);
}

static void stageNew()
{
    const SceneInfo &info = sceneInfo();
    walkOrderList();
    gDraws = sceneArray<const DrawScene>("DRAW");
    gDrawCount = info.draws;
    gGameFaces = sceneArray<const Face>("MPOL");
    gThings = sceneArray<const DrawThing>("DTHG");
    newRoomsInit();
    frameHintsBegin();
    gSceneHintedCount = hintsSceneDraws(gThings, gDrawCount, gSceneHinted, MAX_HELD);

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
    gLab.newBadC = gameListCheck(gFrameCmds, gFrameGrd, gLab.newVisited, gLab.newShaded);
    gLab.newRooms = measure([] { newPrepare(); }, [] { newRooms(false); });
    gLab.roomCommands = facesSlot(gRoomList.link) - 1;
    gLab.newMeshes = measure([] { newPrepare(); }, [] { newMeshes(false); });
    gLab.meshCommands = facesSlot(gMeshList.link) - gMeshFirst;
    gLab.newAsmBad = newAsmCheck();
    gLab.othersBad = facesOtherCheck(gameShadeGouraud()) + facesChainCheck();
    gLab.newJoin = measure([] { newPrepare(); newRooms(false); newMeshes(false); }, [] { newJoin(false); });
    gLab.newBad = gameListCheck(gFrameCmds, gFrameGrd, gLab.newVisited, gLab.newShaded) + gRoomTexturesShort + gBottomOff +
                  texturesQueueOff(gFrameTextures, gWalkList.textures);
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

static Page gResults, gPosePage, gVerticesPage, gNewResults, gFramePage, gRunPage;

// ms a frame and the frames a second it makes.
static void rateRow(Page &page, const char* name, uint32_t cycles)
{
    char line[TEXT_COLUMNS + 1];
    char* p = put(line, name);
    p = pad(line, p, 17);
    p = putFixed(p, cyclesToHundredths(cycles), 2, 6);
    p = put(p, " =");
    p = putFixed(p, cycles ? int32_t(uint64_t(28636364) * 100 / cycles) : 0, 2, 6);
    put(p, " fps");
    page.add(line);
}

static void runPage()
{
    const SceneInfo &info = sceneInfo();
    Page &page = gRunPage;
    char line[TEXT_COLUMNS + 1];
    char* p;

    page.count = 0;
    p = put(line, "RENDER LAB  ");
    p = put(p, info.name);
    put(p, ": the whole frame");
    page.add(line);
    p = putNumber(line, gLab.runFrames, 0);
    put(p, " frames in a row, VDP1 drawing each");
    page.add(line);
    page.add("list while the CPUs make the next");
    page.add("");
    page.add("A FRAME, ms              work");
    rateRow(page, "  plot to plot", gLab.runFrame);
    timeRow(page, "  stages 1-3", gLab.runStages, 0, nullptr);
    timeRow(page, "    master", gLab.runMaster, 0, nullptr);
    timeRow(page, "    slave", gLab.runSlave, 0, nullptr);
    timeRow(page, "    DSP", gLab.runDsp, 0, nullptr);
    timeRow(page, "  wait for VDP1", gLab.runVdp1Wait, 0, nullptr);
    timeRow(page, "  wait for vblank", gLab.runVblank, 0, nullptr);
    timeRow(page, "  upload", gLab.runUpload, 0, nullptr);
    p = put(line, "    (SCU DMA,");
    p = putNumber(p, (gLab.uploadBytes + 512) / 1024, 3);
    put(p, " KB)");
    page.add(line);
    page.add("");
    page.add("VDP1 A LIST, ms");
    timeRow(page, "  alone", gLab.vdp1New, 0, nullptr);
    timeRow(page, "  running", gLab.runVdp1, 0, nullptr);
    p = put(line, "    (the");
    p = putNumber(p, gLab.runWaited, 5);
    put(p, " frames waited for)");
    page.add(line);
    page.add("");
    p = put(line, "upload ");
    p = put(p, gLab.uploadBad ? "OFF (" : "exact (");
    p = putNumber(p, gLab.uploadBad, 0);
    put(p, " off)");
    page.add(line);
    page.add("");
    page.add("R, L: the other pages");
}

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
    workRow(page, "  DSP", gLab.stagesDsp, gLab.stages - gLab.stagesDsp);
    page.add("1+2+3: pose shared as planned:");
    timeRow(page, "  start to end", gLab.frame, 0, nullptr);
    workRow(page, "  master", gLab.frameMaster, gLab.frameMasterWait);
    workRow(page, "  slave", gLab.frameSlave, gLab.frameSlaveWait);
    workRow(page, "  DSP", gLab.frameDsp, gLab.frame - gLab.frameDsp);
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
    timeRow(page, "draw list", gLab.draws, gLab.drawCalls, "call");
    p = put(line, gLab.roomsBad ? "OFF: rooms seen" : "exact: rooms seen");
    p = putNumber(p, gLab.roomsBad, 2);
    p = put(p, " off, voids");
    putNumber(p, gLab.roomsVoids, 2);
    page.add(line);
    p = put(line, gLab.drawsBad ? "OFF: draw list" : "exact: draw list");
    p = putNumber(p, gLab.drawsBad, 2);
    put(p, " off");
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
    timeRow(page, "draw list", gLab.drawsNew, gLab.drawCalls, "call");
    p = put(line, gLab.roomsNewBad ? "OFF: rooms seen" : "exact: rooms seen");
    p = putNumber(p, gLab.roomsNewBad, 2);
    p = put(p, " off, seldom");
    putNumber(p, gLab.roomsSeldom, 2);
    page.add(line);
    p = put(line, gLab.drawsNewBad ? "OFF: draw list" : "exact: draw list");
    p = putNumber(p, gLab.drawsNewBad, 2);
    p = put(p, " off, seldom");
    putNumber(p, gLab.drawsSeldom, 2);
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
    p = put(line, "texture misses:");
    p = putNumber(p, gLab.texturesQueued, 3);
    p = put(p, gLab.texturesGameOff ? " game's OFF (" : " game's exact (");
    p = putNumber(p, gLab.texturesGameOff, 0);
    put(p, " off)");
    page.add(line);
    p = put(line, "  new's uploaded ");
    p = put(p, gLab.texturesUploadOff ? "OFF (" : "exact (");
    p = putNumber(p, gLab.texturesUploadOff, 0);
    put(p, " off)");
    page.add(line);
    p = put(line, "made up sprites, holds: ");
    p = put(p, gLab.othersBad ? "OFF" : "ok");
    p = put(p, " (");
    p = putNumber(p, gLab.othersBad, 0);
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
    p = put(line, "rooms in walk order:");
    p = putNumber(p, gLab.orderOff, 5);
    put(p, " commands move");
    page.add(line);
    p = put(line, "VDP1 ms: game's list");
    p = putFixed(p, cyclesToHundredths(gLab.vdp1Game), 2, 7);
    p = put(p, ", lab's");
    putFixed(p, cyclesToHundredths(gLab.vdp1Lab), 2, 7);
    page.add(line);
    page.add("R, L: the other pages");
}

enum { PAGE_GAME_WAY, PAGE_POSE, PAGE_NEW_VERTICES, PAGE_NEW_WAY, PAGE_FRAME, PAGE_RUN, PAGE_GAME, PAGE_NEW, PAGES };

static void show(int32_t page)
{
    if (page < PAGE_GAME)
    {
        const Page &text = page == PAGE_GAME_WAY ? gResults : page == PAGE_POSE ? gPosePage
                         : page == PAGE_NEW_VERTICES ? gVerticesPage : page == PAGE_NEW_WAY ? gNewResults
                         : page == PAGE_FRAME ? gFramePage : gRunPage;
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
        texturesUpload(texturesCaptured());
        textRow(0, "THE GAME'S LIST");
    }
    else
    {
        shadeTables(gNewTables, false);
        upload(gFrameCmds, gFrameGrd, gFrameEnd);
        texturesUpload(gFrameTextures);
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
    upload(gFrameCmds, gFrameGrd, gFrameEnd);
    texturesUpload(gFrameTextures);
    gLab.texturesUploadOff = texturesVramOff(gFrameTextures);
    gLab.vdp1New = vdp1Show();
    stageRun();
    shadeTables(gNewTables, false);
    upload(gFrameCmds, gFrameGrd, gFrameEnd);
    gLab.arenaUsed = sceneUsed(&gLab.arenaSize);
    gLab.lowUsed = sceneUsedLow(&gLab.lowSize);
    gLab.magic = LAB_MAGIC;

    resultsPage();
    posePage();
    verticesPage();
    newPage();
    framePage();
    runPage();
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
