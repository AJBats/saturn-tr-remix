// OpenLara os* platform hooks.

#include "common.h"
#include "backup.h"
#include "cd.h"
#include "crash.h"
#include "detail.h"
#include "fmv.h"
#include "hw.h"
#include "plane.h"
#include "render.h"
#include "screen.h"
#include "sound.h"
#include "trace.h"
#include "vram.h"

// Packed levels load into low work RAM, which nothing else uses.
#define LEVEL_ADDR  ((uint8*)0x00200000)
#ifdef HW_LOG
#define LEVEL_MAX   (LOW_RAM_TOP - uint32(LEVEL_ADDR) - ENEMIES_EXTRA_BYTES)
#else
#define LEVEL_MAX   (LOW_RAM_TOP - uint32(LEVEL_ADDR) - ENEMIES_EXTRA_BYTES)
#endif

// The size of DATA/<name><ext>, or 0 if the disc has none.
static uint32 fileSize(const char* name, const char* ext)
{
    char path[32] = "DATA/";
    strcat(path, name);
    strcat(path, ext);
    uint32_t lba, size;
    return cdFind(path, &lba, &size) == 0 ? size : 0;
}

int32 osGetSystemTimeMS()
{
    return 0; // TODO FRT based timer
}

// Settings and the one saved game live in backup RAM.
#define SETTINGS_FILE   "TOMBR_CFG"
#define SAVE_FILE       "TOMBR_GAME"

// The level last loaded, and whether the next load skips its video: a saved
// game or a restart goes straight back in.
static LevelID gLoadedLevel = LVL_MAX;
static bool gSkipVideo;
static bool gGameComplete;

void osGameComplete()
{
    gGameComplete = true;
}

bool osSaveSettings()
{
    return backupWrite(SETTINGS_FILE, "Settings", &gSettings, sizeof(gSettings));
}

bool osLoadSettings()
{
    return backupRead(SETTINGS_FILE, &gSettings, sizeof(gSettings));
}

bool osCheckSave()
{
    return backupExists(SAVE_FILE);
}

bool osSaveGame()
{
    // the level's name shows in the BIOS memory manager
    char comment[11];
    strncpy(comment, (const char*)gLevelInfo[gLevelID].data, sizeof(comment));
    comment[sizeof(comment) - 1] = 0;

    return backupWrite(SAVE_FILE, comment, &gSaveFile, sizeof(SaveGame) + gSaveGame.dataSize);
}

bool osLoadGame()
{
    if (!backupRead(SAVE_FILE, &gSaveFile, sizeof(gSaveFile)))
        return false;

    gSkipVideo = true;
    return true;
}

void osJoyVibrate(int32 index, int32 L, int32 R)
{
}

void osSetPalette(const uint16* palette)
{
}

// The title's background: the inventory ring draws over it.
static uint8 gTitlePicture;

const void* osLoadScreen(LevelID id)
{
    traceStop();    // the picture stages over the trace
    screenShow("USAOPT", 352);
    traceResume();
    screenRelease(true);
    return &gTitlePicture;
}

// The original's videos, each before its level (as its own debug menu pairs
// them), and the ending's
static const char* levelVideo(LevelID id)
{
    switch (id)
    {
        case LVL_TR1_GYM : return "MANSION";
        case LVL_TR1_1   : return "SNOW";
        case LVL_TR1_4   : return "LIFT";
        case LVL_TR1_8A  : return "VISION";
        case LVL_TR1_10A : return "CANYON";
        case LVL_TR1_10B : return "PYRAMID";
        case LVL_TR1_10C : return "PRISON";
        default          : return NULL;
    }
}

static void playVideos(LevelID id)
{
    if (gGameComplete && id == LVL_TR1_TITLE)
    {
        gGameComplete = false;
        fmvPlay("END");
        fmvPicture("CRED224", 320, 60 * 8);
        fmvPicture("ENDPG224", 352, 60 * 8);
        return;
    }

    // the first load is the title, or a level started directly for testing
    bool fresh = (id != gLoadedLevel) && !gSkipVideo && (gLoadedLevel != LVL_MAX);
    gLoadedLevel = id;
    gSkipVideo = false;

    const char* video = levelVideo(id);
    if (fresh && video) {
        fmvPlay(video);
    }
}

// The original's loading pictures, one per area
static const char* loadingPicture(LevelID id)
{
    if (id == LVL_TR1_TITLE)
        return "USATIT";
    if (id == LVL_TR1_GYM)
        return "GYM224";
    if (id <= LVL_TR1_CUT_1)
        return "AZTEC224";  // Peru
    if (id <= LVL_TR1_CUT_2)
        return "GREEK224";
    if (id <= LVL_TR1_8C)
        return "EGYPT224";
    return "ATLAN224";
}

static uint32 loadOrHalt(const char* name, const char* ext, void* dst, uint32 maxSize)
{
    char path[32] = "DATA/";
    strcat(path, name);
    strcat(path, ext);

    int32 size = cdLoadFile(path, dst, maxSize);
    crashLog(ext + 1, size);
    if (size <= 0)
    {
        while (1); // TODO error screen
    }
    return size;
}

// High work RAM the program leaves (from its bss's end to the master's
// stack, MASTER_STACK of it kept for that): the level's, taken in order as it
// loads (its mesh store, its rooms' vertices, the renderer's tables, the
// rooms' faces as they fit), all of it again for the next.
extern "C" uint8 _bss_end[], _stack_top[];     // saturn.ld's __bss_end, __stack_top
#define MASTER_STACK    (8 * 1024)
static uint8* gHighFree;
static const void* gMeshStore;

static void highReset()
{
    gHighFree = (uint8*)((uint32(_bss_end) + 15) & ~15);
}

static uint32 highLeft()
{
    return uint32(_stack_top) - MASTER_STACK - uint32(gHighFree);
}

void* osHighAlloc(uint32 bytes)
{
    bytes = (bytes + 15) & ~15;
    if (bytes > highLeft())
        return nullptr;
    uint8* p = gHighFree;
    gHighFree += bytes;
    return p;
}

const void* osMeshStore()
{
    return gMeshStore;
}

// The rooms' vertices, which the DSP reads by DMA (from high work RAM only),
// one after another.
void osPlaceRooms()
{
    uint32 bytes = 0;
    for (int32 i = 0; i < level.roomsCount; i++) {
        bytes += rooms[i].info->verticesCount * sizeof(RoomVertex);
    }
    uint8* next = (uint8*)osHighAlloc(bytes);
    if (!next)
    {
        crashLog("ROOMVTX", bytes);
        while (1);  // TODO error screen
    }
    for (int32 i = 0; i < level.roomsCount; i++)
    {
        RoomData &data = rooms[i].data;
        uint32 size = rooms[i].info->verticesCount * sizeof(RoomVertex);
        memcpy(next, data.vertices, size);
        data.vertices = (const RoomVertex*)next;
        next += size;
    }
}

// The rooms' faces, which the CPUs read every frame: in high work RAM in the
// packer's order (level.roomsHigh: the rooms of the views that see the most
// faces first) as long as it lasts, the rest left in low work RAM, where the
// level holds them. A room with an alternate has room there for the larger
// of the two's faces, and holds whichever a flip map has in place.
static uint8* gFacesHigh[MAX_ROOMS];   // a room's place in high work RAM, or none

static uint32 facesSize(const RoomInfo* info)
{
    return info->quadsCount * sizeof(RoomQuad) + info->trianglesCount * sizeof(RoomTriangle);
}

// Room i's faces, from where they are, to its place in high work RAM.
static void facesHigh(int32 i)
{
    RoomData &data = rooms[i].data;
    const RoomInfo* info = rooms[i].info;
    uint8* next = gFacesHigh[i];
    uint32 quads = info->quadsCount * sizeof(RoomQuad);
    memcpy(next, data.quads, quads);
    memcpy(next + quads, data.triangles, info->trianglesCount * sizeof(RoomTriangle));
    data.quads = (const RoomQuad*)next;
    data.triangles = (const RoomTriangle*)(next + quads);
}

void osPlaceRoomFaces()
{
    int32 placed = 0;
    memset(gFacesHigh, 0, sizeof(gFacesHigh));
    for (int32 k = 0; k < level.roomsCount && level.roomsHigh[k] != NO_ROOM; k++)
    {
        int32 i = level.roomsHigh[k];
        uint32 size = facesSize(rooms[i].info);
        if (rooms[i].alternateRoom != NO_ROOM) {
            size = X_MAX(size, facesSize(rooms[rooms[i].alternateRoom].info));
        }
        gFacesHigh[i] = (uint8*)osHighAlloc(size);
        if (!gFacesHigh[i])
            continue;
        facesHigh(i);
        placed++;
    }
    crashLog("ROOMS", (placed << 24) | (level.roomsCount << 16) | (highLeft() >> 10));    // KB left
}

void osRoomFacesFlipped()
{
    for (int32 i = 0; i < level.roomsCount; i++)
    {
        int32 a = rooms[i].alternateRoom;
        if (!gFacesHigh[i] || a == NO_ROOM)
            continue;
        // the faces flipped out go back to the level's copy (read_PKD
        // relocates its offsets by its address); those flipped in move up
        const RoomData &home = rooms[a].info->data;
        rooms[a].data.quads = (const RoomQuad*)(LEVEL_ADDR + uint32(home.quads));
        rooms[a].data.triangles = (const RoomTriangle*)(LEVEL_ADDR + uint32(home.triangles));
        facesHigh(i);
    }
}

const void* osLoadLevel(LevelID id)
{
    const char* name = (const char*)gLevelInfo[id].data;

    crashHold(true);
    traceStop();    // the loading picture stages over the trace
    crashLog("LEVEL", id);
    playVideos(id);
    screenShow(loadingPicture(id), id == LVL_TR1_TITLE ? 352 : 320);

    // textures go straight to VDP1 VRAM, where the level's texture entries point
    uint32 texSize = loadOrHalt(name, ".TEX", (void*)(VDP1_VRAM + VRAM_TEX_BASE), VRAM_TEX_SIZE);

    // high work RAM: the meshes as the renderer reads them, first
    highReset();
    gMeshStore = gHighFree;
    osHighAlloc(loadOrHalt(name, ".MSH", gHighFree, highLeft()));

    // low work RAM: the level, then the full-res room texture set, which the
    // CPU reads only to upload, as the original lays them out
    uint32 levelSize = (loadOrHalt(name, ".PKD", LEVEL_ADDR, LEVEL_MAX) + 15) & ~15;
    uint8* subData = LEVEL_ADDR + levelSize;
    uint32 subSize = loadOrHalt(name, ".SUB", subData, LEVEL_MAX - levelSize);
    detailInit(VRAM_TEX_BASE + texSize, subData, subSize);

    // a floor and ceiling on VDP2, if the level has them: straight to bank B
    uint32 planeCells = fileSize(name, ".PLC");
    uint32 planeNames = 0;
    if (planeCells)
    {
        loadOrHalt(name, ".PLC", planeCellsAt(), planeCells);
        planeNames = loadOrHalt(name, ".PLN", planeNamesAt(), fileSize(name, ".PLN"));
    }
    planeLoaded(planeCells, planeNames);

    // VDP2's VRAM past the bitmap, at its top (hw.h): the inventory's item
    // textures, which the CPU copies to VDP1 VRAM when asked (some cutscenes
    // have none)
    uint32 itemSize = (fileSize(name, ".ITX") + 15) & ~15;
    uint8* items = planeItemsAt(itemSize);
    if (!items || itemSize > VDP2_BANK_B_END - VDP2_BANK_B)
    {
        crashLog("ITEMS", itemSize);
        while (1);  // TODO error screen
    }

    // sound effects straight to sound RAM, and inventory item textures (some
    // cutscenes have neither)
    char path[32] = "DATA/";
    strcat(path, name);
    strcat(path, ".SFX");
    sndStop();
    cdLoadFile(path, (void*)(SOUND_RAM + SOUND_SAMPLES_ADDR), SOUND_RAM_SIZE - SOUND_SAMPLES_ADDR);

    strcpy(path + strlen(path) - 3, "ITX");
    detailItemInit(items, X_MAX(0, cdLoadFile(path, items, itemSize)));
    if (planeLevel()) {
        traceLevel((const void*)VDP2_BITMAP_TAIL, (const void*)VDP2_BANK_B);
    } else {
        traceLevel((const void*)VDP2_BANK_B, items);
    }

    planeReady();
    screenPlaneLevel(planeLevel());
    screenRelease(false);
    crashHold(false);
    return LEVEL_ADDR;
}
