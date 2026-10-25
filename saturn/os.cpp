// OpenLara os* platform hooks.

#include "common.h"
#include "backup.h"
#include "cd.h"
#include "detail.h"
#include "fmv.h"
#include "hw.h"
#include "screen.h"
#include "sound.h"
#include "vram.h"

// Packed levels load into low work RAM, which nothing else uses.
#define LEVEL_ADDR  ((uint8*)0x00200000)
#define LEVEL_MAX   (1024 * 1024)

int32 osGetSystemTimeMS()
{
    return 0; // TODO FRT based timer
}

// Settings and the one saved game live in backup RAM; the game is a SaveGame
// followed by its item data.
#define SETTINGS_FILE   "TOMBR_CFG"
#define SAVE_FILE       "TOMBR_SAVE"

static uint8 gSaveBuffer[SAVEGAME_SIZE];

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
    Settings settings;
    if (!backupRead(SETTINGS_FILE, &settings, sizeof(settings)) || settings.version != SETTINGS_VER)
        return false;

    gSettings = settings;
    return true;
}

bool osCheckSave()
{
    return backupExists(SAVE_FILE);
}

bool osSaveGame()
{
    memcpy(gSaveBuffer, &gSaveGame, sizeof(gSaveGame));
    memcpy(gSaveBuffer + sizeof(gSaveGame), gSaveData, gSaveGame.dataSize);

    // the level's name shows in the BIOS memory manager
    char comment[11];
    strncpy(comment, (const char*)gLevelInfo[gLevelID].data, sizeof(comment));
    comment[sizeof(comment) - 1] = 0;

    return backupWrite(SAVE_FILE, comment, gSaveBuffer, sizeof(gSaveGame) + gSaveGame.dataSize);
}

bool osLoadGame()
{
    if (!backupRead(SAVE_FILE, gSaveBuffer, sizeof(gSaveBuffer)))
        return false;

    SaveGame save;
    memcpy(&save, gSaveBuffer, sizeof(save));
    if (save.version != SAVEGAME_VER || save.dataSize > sizeof(gSaveData))
        return false;

    gSaveGame = save;
    memcpy(gSaveData, gSaveBuffer + sizeof(save), save.dataSize);
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
    screenShow("USAOPT", 352);
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
    if (size <= 0)
    {
        while (1); // TODO error screen
    }
    return size;
}

const void* osLoadLevel(LevelID id)
{
    const char* name = (const char*)gLevelInfo[id].data;

    playVideos(id);
    screenShow(loadingPicture(id), id == LVL_TR1_TITLE ? 352 : 320);

    // textures go straight to VDP1 VRAM, where the level's texture entries point
    uint32 texSize = loadOrHalt(name, ".TEX", (void*)(VDP1_VRAM + VRAM_TEX_BASE), VRAM_TEX_SIZE);
    uint32 subSize = loadOrHalt(name, ".SUB", detailData(), detailDataMax());
    detailInit(VRAM_TEX_BASE + texSize, subSize);

    loadOrHalt(name, ".PKD", LEVEL_ADDR, LEVEL_MAX);

    // sound effects straight to sound RAM, and inventory item textures (some
    // cutscenes have neither)
    char path[32] = "DATA/";
    strcat(path, name);
    strcat(path, ".SFX");
    sndStop();
    cdLoadFile(path, (void*)(SOUND_RAM + SOUND_SAMPLES_ADDR), SOUND_RAM_SIZE - SOUND_SAMPLES_ADDR);

    strcpy(path + strlen(path) - 3, "ITX");
    detailItemInit(X_MAX(0, cdLoadFile(path, detailItemData(), detailItemDataMax())));

    screenRelease(false);
    return LEVEL_ADDR;
}
