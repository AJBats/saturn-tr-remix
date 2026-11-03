// The frame (frame.h): the stages on both CPUs and the DSP at once.
#include "frame.h"
#include "draws.h"
#include "platform.h"
#include "pose.h"
#include "rooms.h"
#include "vertices.h"
#include "dsp.h"
#include "hw.h"

#include <string.h>

namespace render {

#define FACE_WATER  (1u << 30)
#define FACE_LINE_H (8u << 14)  // FaceType FACE_TYPE_LINE_H: renderRect's line, a fill's
#define X_MAX(a, b) ((a) > (b) ? (a) : (b))

ALIGN16 Command gFrameCmds[MAX_CMDS];
ALIGN16 uint32_t gFrameGrd[MAX_CMDS][2];
int32_t gFrameEnd;
int32_t gMeshFirst;
CommandList gRoomList, gMeshList;
FaceRoom* gFrameRooms;
uint32_t gFrameRoomCount;
RoomTextures gRoomTextures;
RoomMaterial gRoomMaterials[MAX_ROOM_TEXTURES];
TextureQueue gFrameTextures;
RoomSplit gSplit;
RoomRun gRoomRun;

static uint16_t gHeads[2][OT_SIZE + 1] __attribute__((aligned(4)));
static uint16_t gTails[2][OT_SIZE];
static FrameLevel gLevel;
static Command gClear;          // the list's first command, jumping to the first bucket's

#ifdef RENDER_LAB
// The rooms' vertices' top: the walk places them from MASTER_VERTICES up, a
// room's rounded up to 16 (rooms.cpp placeRooms' gBottom).
static int32_t roomsBottom()
{
    int32_t bottom = MASTER_VERTICES;
    for (uint32_t i = 0; i < gFrameRoomCount; i++)
    {
        const FaceRoom &room = gFrameRooms[i];
        int32_t top = int32_t(room.vertices - gSplit.vertices) + ((room.verticesCount + 15) & ~15);
        bottom = top > bottom ? top : bottom;
    }
    return bottom;
}
#endif

void frameInit(const FrameLevel &level)
{
    gLevel = level;
    gClear = level.clear;
    gClear.w[0] |= 0x1000 << 16;

    RoomTextures &t = gRoomTextures;
    t.textures = level.textures;
    t.palette = level.palette;
    t.keys = texturesKeys();
    t.slots = texturesSlots();
    t.frame = level.frame;
    t.lod = level.lod;
    t.pmod = level.pmod;
    for (int32_t l = 0; l < 3; l++) {
        t.pools[l] = level.pools[l];
    }
    t.miss = frameTextureMiss;
    facesRoomMaterials(t, gRoomMaterials, level.roomTextures < MAX_ROOM_TEXTURES ? level.roomTextures
                                                                                  : MAX_ROOM_TEXTURES);
    t.materials = gRoomMaterials;
#ifdef RENDER_LAB
    gSplit.vertices = level.vertices;
#endif
    gSplit.divTable = divTable;
    gMeshFirst = 1;
    frameLists();
}

void frameTexturesShifted(const uint16_t* ranges)
{
    int32_t count = *ranges++;
    for (int32_t i = 0; i < count; i++)
    {
        int32_t length = *ranges++;
        RoomMaterial first = gRoomMaterials[*ranges];
        for (; length > 0; length--, ranges++) {
            gRoomMaterials[ranges[0]] = gRoomMaterials[ranges[1]];
        }
        gRoomMaterials[*ranges++] = first;
    }
}

void frameLists()
{
    facesListInit(gRoomList, gFrameCmds, gFrameGrd, 1, gMeshFirst, gHeads[0], gTails[0], gLevel.materials,
                  gLevel.shadeGouraud, gLevel.shadeLevel, gLevel.shade[1], gLevel.sprites);
    facesListInit(gMeshList, gFrameCmds, gFrameGrd, gMeshFirst, MAX_CMDS - 1, gHeads[1], gTails[1], gLevel.materials,
                  gLevel.shadeGouraud, gLevel.shadeLevel, gLevel.shade[0], gLevel.sprites);
#ifdef RENDER_LAB
    gSplit.top = MAX_VERTICES;
    gSplit.bottom = roomsBottom();
#endif
}

void frameSplit(const DrawScene* draws, uint32_t count, int32_t fills)
{
    int32_t meshes = verticesFaces() + fills, rooms = verticesRoomFaces(), slots = MAX_CMDS - 2;
    for (uint32_t i = 0; i < count; i++) {
        meshes += draws[i].type == DRAW_COPY;
    }
    gMeshFirst = rooms + meshes <= slots ? MAX_CMDS - 1 - meshes : 1 + int32_t(int64_t(slots) * rooms / (rooms + meshes));
}

static uint16_t gFillColour;

void frameFillColour(uint16_t colour)
{
    gFillColour = colour;
}

#ifdef HW_LOG
static int16_t gRoomSlots[MAX_FRAME_ROOMS][2];  // each room's first command's slot, and past its last
static volatile int32_t gPaintRoom = -1;

void framePaintRoom(int32_t room)
{
    gPaintRoom = room;
}

static void paintRoom()
{
    int32_t room = gPaintRoom;
    if (room < 0)
        return;
    for (uint32_t i = 0; i < gFrameRoomCount; i++)
    {
        if (gFrameRooms[i].room != room)
            continue;
        for (int32_t s = gRoomSlots[i][0]; s < gRoomSlots[i][1]; s++)
        {
            Command &c = gFrameCmds[s];
            if (c.w[0] & (0x4000u << 16))
                continue;               // skipped
            c.w[0] = (c.w[0] & 0xFFFF) | (0x1004u << 16);   // a polygon, jumping as it did
            c.w[1] = (0x00C0u << 16) | 0xFC1F;              // magenta, nothing else
        }
    }
}
#else
void framePaintRoom(int32_t) {}
#endif

// The rooms' list on to its slots, and the split corners free.
static void roomListStart()
{
    facesListStart(gRoomList, gFrameCmds, gFrameGrd, 1, gMeshFirst);
#ifdef RENDER_LAB
    gSplit.top = MAX_VERTICES;
    gSplit.bottom = roomsBottom();
#endif
}

void frameListsStart(bool rooms)
{
    if (rooms) {
        roomListStart();
    }
    facesListStart(gMeshList, gFrameCmds, gFrameGrd, gMeshFirst, MAX_CMDS - 1);
}

int32_t frameTextureMiss(uint32_t key, uint32_t texSize, uint32_t variant)
{
    return texturesMiss(gFrameTextures, gRoomTextures.frame, key, texSize, variant);
}

// --- the draw order hints ----------------------------------------------------

static HeldCommand gHeld[MAX_HELD];
static int32_t gHeldCount;
static bool gHintsWait;
static uint32_t gHintsWaited;
static HintedDraw gHinted[MAX_HELD];
static const HintedDraw* gHintedList;
static int32_t gHintedCount, gHintedNext;
static uint32_t gNextHinted;

void frameHintsBegin()
{
    static uint16_t rooms[MAX_FRAME_ROOMS];
    for (uint32_t i = 0; i < gFrameRoomCount; i++) {
        rooms[i] = gFrameRooms[i].room;
    }
    hintsBegin(rooms, gFrameRoomCount);
    gHeldCount = 0;
}

static bool hintRoom(uint32_t room, HintRoom &out)
{
    for (uint32_t i = 0; i < gFrameRoomCount; i++)
    {
        const FaceRoom &r = gFrameRooms[i];
        if (r.room != room)
            continue;
        if (gHintsWait) {
            verticesRoomWait(i, gHintsWaited);
        }
        out = { r.quads, r.triangles, r.quadsCount, r.trianglesCount, r.points, r.vertices, r.camera };
        return true;
    }
    return false;
}

void frameHintsPlace(bool wait, uint32_t &waited)
{
    gHintsWait = wait;
    gHintsWaited = 0;
    for (int32_t i = 0; i < gHeldCount; i++) {
        facesLinkHeld(gMeshList, gHeld[i], hintsPlace(gHeld[i].hint, gHeld[i].depth, hintRoom));
    }
    gHeldCount = 0;
    waited += gHintsWaited;
}

void frameHinted(const HintedDraw* hinted, int32_t count)
{
    gHintedList = hinted;
    gHintedCount = count;
    gHintedNext = 0;
    gNextHinted = count ? hinted[0].draw : ~0u;
}

bool frameMeshHinted(uint32_t i, const FaceSet* set, const Vertex* vertices, uint32_t mode)
{
    if (i != gNextHinted)
        return false;
    HintFaces hints;
    hintsOf(gHintedList[gHintedNext++].thing, hints);
    gNextHinted = gHintedNext < gHintedCount ? gHintedList[gHintedNext].draw : ~0u;
    facesMeshHinted(set, vertices, mode, gMeshList, hints, gHeld, gHeldCount, MAX_HELD);
    return true;
}

void frameMesh(uint32_t i, const FaceSet* set, const Vertex* vertices, uint32_t mode)
{
    if (set->cards) {
        facesMeshCardsAsm(set, const_cast<Vertex*>(vertices), mode, gSplit.divTable);
    }
    if (!frameMeshHinted(i, set, vertices, mode)) {
        facesMeshAsm(set, vertices, mode, &gMeshList);
    }
}

// --- the frame ---------------------------------------------------------------

// Between the CPUs (each read and written uncached, and volatile, so that
// a wait on it stays a wait): the slave's rooms done, the master's draws
// done; the near half's first chain (its head, for the far half's last
// command to link to) and the link field its last goes into; the slave's
// work and waits; the items it poses, done; the plan made and the master's
// chunks claimed; whether it walks the rooms (or takes the master's walk)
// and starts the DSP on them first, done; whether frameRoomsStart started
// it, and the last list gone from the commands; the master's draw list made,
// and its hints placed (or none to place); whether the slave joins the near
// half in a frame with next-frame work, and has.
#define JOIN_SPLIT  (JOIN_PAIRS / 2)    // the far half's nearest pair

struct SlaveStages
{
    volatile uint32_t roomsDone, drawsDone;
    uint16_t* volatile nearLast;
    volatile uint16_t nearHead;
    uint16_t pad;
    volatile uint32_t work, wait;
    volatile uint32_t poses;
    volatile uint32_t posed;
    volatile uint32_t roomsReady;
    volatile uint32_t walks;
    volatile uint32_t walked;
    volatile uint32_t early, listFree;
    volatile uint32_t drawListDone, hintsFree;
    volatile uint32_t slaveJoins, joined;
} __attribute__((aligned(16)));
static SlaveStages gSlaveStages;

// The next frame begun in this one (frameNextWork): the slave's work, whether
// it walked the next frame's rooms (the slave's own), whether the slave laid
// those out and started the DSP on them (uncached: the master's too) and the
// jobs it laid out, and the cycles the work took after the slave's rooms, the
// last time it ran (0: in them).
static void (*volatile gNextWork)();
static bool gNextWalked;
static volatile uint32_t gNextRooms;
static FaceRoom* gNextJobs;
static uint32_t gNextJobCount;
static volatile int32_t gNextAfter;

static int32_t gMeshesUsed = MAX_CMDS / 2;  // the draws' commands, the last frame's

// The list built, as frameUpload takes it: the rooms' slots, the draws'
// first and the end's (the next frame's rooms may start before it goes).
static int32_t gListRooms, gListMeshFirst, gListEnd;
enum { WALK_NONE, WALK_SLAVE, WALK_MASTER };   // SlaveStages.walks

// The frame's split between the CPUs, beside the rooms' chunks: the items the
// slave poses.
static uint8_t gPoseSlave[MAX_DRAW_ITEMS];

// The next frame's rooms (walked ahead) laid out and the DSP started on them,
// this frame's rooms done (every chunk projected from the DSP's rotations)
// and the master's hints placed (they wait on this frame's chunks).
static void nextRoomsStart()
{
    SlaveStages* shared = uncached(&gSlaveStages);
    stageBegin(STAGE_JOBS);
    while (!shared->hintsFree || dspBusy()) {
        busPause();
    }
    gNextJobs = roomsJobsAhead(gNextJobCount);
    if (gNextJobCount > MAX_FRAME_ROOMS) {
        gNextJobCount = MAX_FRAME_ROOMS;
    }
    verticesRoomsSet(gNextJobs, gNextJobCount);
    verticesRoomsStart();
    *uncached(&gNextRooms) = 1;
    stageEnd(STAGE_JOBS);
}

void frameNextWork(void (*work)())
{
    *uncached(&gNextWork) = work;
}

void frameNextWalked()
{
    gNextWalked = true;
}

void frameNextDrop()
{
    if (!*uncached(&gNextRooms))
        return;
    while (dspBusy()) {
        busPause();
    }
    *uncached(&gNextRooms) = 0;
}

static void slaveRooms(void*)
{
    uint32_t t0 = ticks(), wait = 0;
    SlaveStages* shared = uncached(&gSlaveStages);
    if (shared->walks)
    {
        // the rooms seen, their jobs, laid out, the DSP started on them in order
        stageBegin(STAGE_JOBS);
        if (shared->walks == WALK_SLAVE) {
            roomsSeen();
        }
        if (*uncached(&gNextRooms))
        {
            // laid out and on the DSP since the frame before's rooms were done
            gFrameRooms = gNextJobs;
            gFrameRoomCount = gNextJobCount;
            *uncached(&gNextRooms) = 0;
        }
        else
        {
            gFrameRooms = roomsJobs(gFrameRoomCount);
            if (gFrameRoomCount > MAX_FRAME_ROOMS) {
                gFrameRoomCount = MAX_FRAME_ROOMS;
            }
            verticesRoomsSet(gFrameRooms, gFrameRoomCount);
            verticesRoomsStart();
        }
        if (shared->early) {
            roomListStart();
        }
        shared->walked = 1;
        stageEnd(STAGE_JOBS);
    }
    if (!shared->roomsReady)
    {
        stageBegin(STAGE_PLAN_WAIT);
        uint32_t w0 = ticks();
        while (!shared->roomsReady) {
            busPause();
        }
        wait += ticksCycles(w0, ticks());
        cachePurge();   // the master's draw list and plan
        stageEnd(STAGE_PLAN_WAIT);
    }
    if (shared->poses)
    {
        stageBegin(STAGE_SLAVE_POSE);
        poseShare(1, gPoseSlave);
        shared->posed = 1;
        stageEnd(STAGE_SLAVE_POSE);
    }
    stageBegin(STAGE_ROOMS);
    void (*work)() = *uncached(&gNextWork);
    uint32_t projected = 0;     // rooms projected, ahead of their faces while the last list holds the commands
    for (uint32_t i = 0; i < gFrameRoomCount; i++)
    {
        if (work && i >= projected && i && shared->drawListDone && verticesRoomMasters(i))
        {
            // the next frame's work while the master projects this room's chunks
            stageEnd(STAGE_ROOMS);
            work();
            work = nullptr;
            stageBegin(STAGE_ROOMS);
        }
        if (i >= projected)
        {
            verticesRoom(i, wait);
            projected = i + 1;
        }
        if (!i)
        {
            // the last list still in the commands (VDP1 drawing the one
            // before it): the next rooms projected meanwhile, up to one the
            // master has chunks of; faces wait for the commands
            while (!shared->listFree && projected < gFrameRoomCount && !verticesRoomMasters(projected))
            {
                verticesRoom(projected, wait);
                projected++;
            }
            if (!shared->listFree)
            {
                // the last list still in the commands, VDP1 drawing the one
                // before it
                stageEnd(STAGE_ROOMS);
                stageBegin(STAGE_PLAN_WAIT);
                uint32_t w0 = ticks();
                while (!shared->listFree) {
                    busPause();
                }
                wait += ticksCycles(w0, ticks());
                cachePurge();   // the caches' frames, on as it left
                stageEnd(STAGE_PLAN_WAIT);
                stageBegin(STAGE_ROOMS);
            }
            facesRoomsBegin(gRoomRun, gRoomTextures, gSplit, gRoomList);
        }
#ifdef HW_LOG
        gRoomSlots[i][0] = facesSlot(gRoomList.link);
#endif
        facesRoom(gFrameRooms[i], gRoomRun);
#ifdef HW_LOG
        gRoomSlots[i][1] = facesSlot(gRoomList.link);
#endif
    }
    stageEnd(STAGE_ROOMS);
    if (*uncached(&gNextWork))
    {
        // the frame's numbers before the next frame's work (the master reads
        // them as it joins); the near half the slave's if the work is done
        shared->work = ticksCycles(t0, ticks()) - wait;
        shared->wait = wait;
        shared->slaveJoins = !work;
        shared->roomsDone = 1;
        int32_t after = 0;
        if (work)
        {
            while (!shared->drawListDone) {
                busPause();
            }
            uint32_t a = ticks();
            work();
            after = ticksCycles(a, ticks());
        }
        *uncached(&gNextAfter) = after;
        if (gNextWalked)
        {
            gNextWalked = false;
            nextRoomsStart();
        }
        if (!work)
        {
            stageBegin(STAGE_DRAWS_WAIT);
            while (!shared->drawsDone) {
                busPause();
            }
            cachePurge();       // the master's mesh list
            stageEnd(STAGE_DRAWS_WAIT);
            stageBegin(STAGE_SLAVE_JOIN);
            shared->nearLast = facesJoinPairs(&gRoomList, &gMeshList, facesLinkBase(gFrameCmds),
                                              (uint16_t*)&shared->nearHead, JOIN_SPLIT - 1);
            stageEnd(STAGE_SLAVE_JOIN);
            shared->joined = 1;
        }
        return;
    }
    *uncached(&gSlaveStages.roomsDone) = 1;
    stageBegin(STAGE_DRAWS_WAIT);
    uint32_t w0 = ticks();
    while (!*uncached(&gSlaveStages.drawsDone)) {
        busPause();
    }
    wait += ticksCycles(w0, ticks());
    cachePurge();       // the master's mesh list
    stageEnd(STAGE_DRAWS_WAIT);
    stageBegin(STAGE_SLAVE_JOIN);
    shared->nearLast = facesJoinPairs(&gRoomList, &gMeshList, facesLinkBase(gFrameCmds),
                                      (uint16_t*)&shared->nearHead, JOIN_SPLIT - 1);
    stageEnd(STAGE_SLAVE_JOIN);
    shared->work = ticksCycles(t0, ticks()) - wait;
    shared->wait = wait;
}

// The pose's share and the feed's: the slave's items (gPoseSlave), and the
// master's mesh jobs to feed before the slave's first.
static uint32_t slaveShare(int32_t &part)
{
    uint32_t items = poseCount(), slaveItems = 0;
    part = verticesJobs();
    for (uint32_t i = 0; i < items; i++)
    {
        slaveItems += gPoseSlave[i];
        if (gPoseSlave[i] && poseDraws(i) && part == verticesJobs()) {
            part = verticesJobOfDraw(poseFirstDraw(i));
        }
    }
    return slaveItems;
}

// Stages 1, 2 and 3 together (calls: the draw list made and posed here, the
// slave walking meanwhile), or 2 and 3 on draws given.
static FrameTimes stagesTogether(const FrameCalls* calls, const FrameDraws* given, int32_t bias)
{
    bool posed = calls != nullptr;
    bool early = posed && calls->started;   // the slave on the rooms already
    FrameTimes s = {};
    SlaveStages* shared = uncached(&gSlaveStages);
    if (!early)
    {
        shared->roomsDone = shared->drawsDone = shared->posed = shared->walked = 0;
        shared->drawListDone = shared->hintsFree = shared->slaveJoins = shared->joined = 0;
        shared->roomsReady = !posed;
        shared->walks = !posed ? WALK_NONE : calls->walked ? WALK_MASTER : WALK_SLAVE;
        shared->poses = 0;
        shared->early = 0;
        shared->listFree = 1;
        if (!*uncached(&gNextRooms)) {      // else the frame before's flush cleared them, read
            dspEndsClear();
        }
    }
    int32_t part = verticesJobs();
    uint32_t slaveItems = 0;
    const DrawList &list = drawsList();
    uint32_t t0 = ticks();
    if (posed)
    {
        poseMatrices((const PoseMatrix*)calls->matrices, calls->matrixLights);
        if (!early) {
            slaveRun(slaveRooms, nullptr);
        }
        stageBegin(STAGE_DRAW_LIST);
        roomsItemsTest(calls->walked);     // the walk's entries there to test the items by
        draws(calls->calls, calls->count, calls->items, calls->base);
        verticesDrawsSet(list.draws, list.drawCount, poseXforms());
        poseUse(list);
        shared->drawListDone = 1;
        stageEnd(STAGE_DRAW_LIST);
        stageBegin(STAGE_WALK_WAIT);
        uint32_t w0 = ticks();
        while (!shared->walked) {
            busPause();
        }
        s.masterWait += ticksCycles(w0, ticks());
        cachePurge();   // the rooms the slave walked
        stageEnd(STAGE_WALK_WAIT);
        stageBegin(STAGE_PLAN);
        int32_t fills;
        roomsFills(fills);
        if (!early) {
            frameSplit(list.draws, list.drawCount, fills);
        }
        frameListsStart(!early);
        frameHintsBegin();
        framePlan(bias, !early, true);
        slaveItems = slaveShare(part);
        shared->poses = slaveItems;
        verticesClaimMasters();
        shared->roomsReady = 1;
        stageEnd(STAGE_PLAN);
    }
    else
    {
        shared->drawListDone = 1;
        frameHintsBegin();
        verticesRoomsBegin();
        verticesClaimMasters();
        slaveRun(slaveRooms, nullptr);
    }
    if (posed)
    {
        stageBegin(STAGE_POSE);
        poseShare(0, gPoseSlave);
        poseStatics();
        stageEnd(STAGE_POSE);
    }
    stageBegin(STAGE_FEED);
    verticesMeshesFeed(0, part);
    stageEnd(STAGE_FEED);
    if (slaveItems)
    {
        stageBegin(STAGE_POSE_WAIT);
        uint32_t w0 = ticks();
        while (!shared->posed) {
            busPause();
        }
        s.masterWait += ticksCycles(w0, ticks());
        poseDropShare(gPoseSlave);
        stageEnd(STAGE_POSE_WAIT);
        stageBegin(STAGE_FEED);
        verticesMeshesFeed(part);
        stageEnd(STAGE_FEED);
    }
    stageBegin(STAGE_CHUNKS);
    bool meshes = false;
    verticesMasterRooms(s.masterWait, meshes);
    if (!meshes) {
        verticesMeshesStart(s.masterWait);
    }
    stageEnd(STAGE_CHUNKS);
    stageBegin(STAGE_DRAWS);
    const uint8_t* sets = gLevel.faceSets;
    uint32_t slaveEnd = 0;
    if (posed && calls->inventory) {
        gMeshList.shade = nullptr;
    }
    const DrawScene* draws = posed ? list.draws : given->draws;
    uint32_t drawCount = posed ? list.drawCount : given->count;
    if (posed) {
        frameHinted(gHinted, hintsDraws(list.hinted, list.hintedCount, gHinted, MAX_HELD));
    } else {
        frameHinted(given->hinted, given->hintedCount);
    }
    shared->hintsFree = !gHintedCount;
    if (posed)
    {
        // the fills past the draw distance the walk found (render.cpp
        // renderVoid: a rectangle of colour 0 behind everything), before the
        // draws
        int32_t fills;
        const int32_t (*fill)[4] = roomsFills(fills);
        for (int32_t k = 0; k < fills; k++)
        {
            Vertex v[2] = {};
            v[0].x = fill[k][0];
            v[0].y = fill[k][1];
            v[0].z = int16_t(gFillColour);
            v[1].x = fill[k][2] - fill[k][0];
            v[1].y = fill[k][3] - fill[k][1];
            const Face face = { FACE_LINE_H, nullptr, { 0 } };
            facesOtherAsm(&face, v, OT_SIZE - 1, &gMeshList);
        }
    }
    for (uint32_t i = 0; i < drawCount; i++)
    {
        if (!slaveEnd && shared->roomsDone) {
            slaveEnd = ticksCycles(t0, ticks());
        }
        const DrawScene &d = draws[i];
        if (d.type == DRAW_COPY)
        {
            // a sprite's or rectangle's face: the draw list's made, or given
            if (posed) {
                facesOtherAsm(list.faces + d.first, list.vertices + 2 * d.first, d.bucket, &gMeshList);
            } else {
                facesOtherAsm(given->copies + d.first, given->copyVertices, d.bucket, &gMeshList);
            }
            continue;
        }
        const Vertex* vertices = verticesDraw(i, s.masterWait);
        const FaceSet* set = d.type == DRAW_SHADOW ? facesShadow()
                                                   : (const FaceSet*)(sets + ((const uint32_t*)sets)[d.mesh]);
        int32_t inside = posed ? verticesInsideOf(i) : d.inside;
        uint32_t mode = (inside ? MESH_INSIDE : 0) | (d.flags & FACE_WATER ? MESH_WATER : 0);
        frameMesh(i, set, vertices, mode);
    }
    frameHintsPlace(true, s.masterWait);
    shared->hintsFree = 1;
    frameEnds();
    gMeshList.shade = gLevel.shade[0];
    shared->drawsDone = 1;
    stageEnd(STAGE_DRAWS);
    stageBegin(STAGE_SLAVE_WAIT);
    uint32_t w0 = ticks();
    uint32_t drawsEnd = ticksCycles(t0, w0);
    while (!shared->roomsDone) {
        busPause();
    }
    s.masterWait += ticksCycles(w0, ticks());
    s.late = int32_t(drawsEnd) - int32_t(slaveEnd ? slaveEnd : ticksCycles(t0, ticks()));
    uint32_t roomsSeen = ticks();
    cachePurge();       // the slave's room list
    stageEnd(STAGE_SLAVE_WAIT);
    stageBegin(STAGE_JOIN);
    uint16_t* farLast = facesJoinPairs(&gRoomList, &gMeshList, facesLinkBase(gFrameCmds),
                                       facesLinkField(gFrameCmds, 0), JOIN_SPLIT << 16 | (JOIN_PAIRS - 1));
    stageEnd(STAGE_JOIN);
    if (!*uncached(&gNextWork))
    {
        stageBegin(STAGE_SLAVE_WAIT);
        w0 = ticks();
        slaveWait();
        s.masterWait += ticksCycles(w0, ticks());
        stageEnd(STAGE_SLAVE_WAIT);
    }
    else if (shared->slaveJoins)
    {
        stageBegin(STAGE_SLAVE_WAIT);
        w0 = ticks();
        while (!shared->joined) {
            busPause();
        }
        s.masterWait += ticksCycles(w0, ticks());
        stageEnd(STAGE_SLAVE_WAIT);
    }
    else
    {
        // the near half too: the slave is on the next frame's work
        stageBegin(STAGE_JOIN);
        shared->nearLast = facesJoinPairs(&gRoomList, &gMeshList, facesLinkBase(gFrameCmds),
                                          (uint16_t*)&shared->nearHead, JOIN_SPLIT - 1);
        stageEnd(STAGE_JOIN);
    }
    cachePurge();       // the link fields the slave's half set, of commands this CPU made
    uint16_t* nearLast = shared->nearLast;
    if (nearLast == &uncached(&gSlaveStages)->nearHead || nearLast == &gSlaveStages.nearHead) {
        *farLast = facesLink(gFrameEnd);    // the near half had no commands
    }
    else
    {
        *farLast = shared->nearHead;
        *nearLast = facesLink(gFrameEnd);
    }
#ifdef HW_LOG
    paintRoom();
#endif
    verticesEnd(uncached(&gNextRooms));
    gListRooms = facesSlot(gRoomList.link);
    gListMeshFirst = gMeshFirst;
    gListEnd = gFrameEnd;
    // the slave's next-frame work after its rooms (the last time it ran) ends
    // the slave's part of the frame: the master's draws may end that much
    // later than the slave's rooms, less what the master does after them
    int32_t after = *uncached(&gNextAfter) - int32_t(ticksCycles(roomsSeen, ticks()));
    if (after > 0) {
        s.late -= after;
    }
    s.wall = ticksCycles(t0, ticks());
    uint32_t ends[2], roomsAt, meshesAt;
    if (dspEnds(ends, 2) == 2)
    {
        verticesDspStarts(roomsAt, meshesAt);
        if (posed) {
            roomsAt = slaveTickOnMaster(roomsAt);    // the slave started them
        }
        s.dsp = ticksCycles(roomsAt, ends[0]) + ticksCycles(meshesAt, ends[1]);
    }
    s.masterWork = s.wall - s.masterWait;
    s.slaveWork = *uncached(&gSlaveStages.work);
    s.slaveWait = *uncached(&gSlaveStages.wait);
    return s;
}

void frameTexels(int32_t lod)
{
    gRoomTextures.lod = lod;
}

void frameFar(int32_t far)
{
    roomsFar(far);
    verticesFar(far);
    facesFar(far);
    drawsFar(far);
}

void frameRoomsStart(bool listFree)
{
    int32_t meshes = gMeshesUsed + gMeshesUsed / 4 + 32;
    if (meshes > MAX_CMDS - 3) {
        meshes = MAX_CMDS - 3;
    }
    gMeshFirst = MAX_CMDS - 1 - meshes;
    SlaveStages* shared = uncached(&gSlaveStages);
    shared->roomsDone = shared->drawsDone = shared->posed = shared->walked = 0;
    shared->drawListDone = shared->hintsFree = shared->slaveJoins = shared->joined = 0;
    shared->roomsReady = 1;
    shared->walks = WALK_MASTER;
    shared->poses = 0;
    shared->early = 1;
    shared->listFree = listFree;
    if (!*uncached(&gNextRooms)) {      // else the frame before's flush cleared them, read
        dspEndsClear();
    }
    slaveRun(slaveRooms, nullptr);
}

void frameListFree()
{
    *uncached(&gSlaveStages.listFree) = 1;
}

void frameEnds()
{
    gFrameEnd = facesSlot(gMeshList.link);
    gMeshesUsed = gFrameEnd + 1 - gMeshFirst;
    gFrameCmds[0] = gClear;
    gFrameCmds[gFrameEnd].w[0] = CMD_END << 16;
}

FrameTimes frameRun(const FrameCalls &calls, int32_t bias)
{
    return stagesTogether(&calls, nullptr, bias);
}

FrameTimes frameDraws(const FrameDraws &draws, int32_t bias)
{
    return stagesTogether(nullptr, &draws, bias);
}

void framePlan(int32_t bias, bool posed, bool started)
{
    memset(gPoseSlave, 0, sizeof(gPoseSlave));
    if (!posed || poseCount() > MAX_DRAW_ITEMS)
    {
        verticesPlan(bias, 0, started);
        return;
    }
    int32_t master, slave, masterPose = 0, slavePose = 0, idle = verticesSlaveIdle();
    verticesPredicted(master, slave);
    master += bias;
    int32_t prices[MAX_DRAW_ITEMS];
    for (uint32_t i = 0; i < poseCount(); i++)
    {
        prices[i] = posePrice(i);
        masterPose += prices[i];
    }
    while (1)
    {
        int32_t best = -1, price = 0;
        for (uint32_t i = 0; i < poseCount(); i++)
        {
            int32_t p = prices[i];
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
    verticesPlan(bias + masterPose - X_MAX(slavePose - idle, 0), masterPose, started);
}

uint32_t frameUpload()
{
    texturesUpload(gFrameTextures);
    int32_t rooms = gListRooms, first = gListMeshFirst, meshes = gListEnd + 1 - first;
    dmaToVdp1(gFrameCmds, VRAM_FRAME_CMDS, rooms * sizeof(Command));
    dmaToVdp1(gFrameCmds + first, VRAM_FRAME_CMDS + first * sizeof(Command), meshes * sizeof(Command));
    dmaToVdp1(gFrameGrd, VRAM_GOURAUD_BASE, rooms * sizeof(gFrameGrd[0]));
    if (meshes > 1)     // (a frame without draws has the end alone, which takes no table)
    {
        dmaToVdp1(gFrameGrd + first, VRAM_GOURAUD_BASE + first * sizeof(gFrameGrd[0]),
                  (meshes - 1) * sizeof(gFrameGrd[0]));
    }
    dmaWait();
    return (rooms + meshes) * sizeof(Command) + (rooms + meshes - 1) * sizeof(gFrameGrd[0]);
}

}
