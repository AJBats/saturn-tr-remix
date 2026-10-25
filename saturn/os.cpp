// OpenLara os* platform hooks.

#include "common.h"
#include "cd.h"
#include "detail.h"
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

bool osSaveSettings()
{
    return false;
}

bool osLoadSettings()
{
    return false;
}

bool osCheckSave()
{
    return false;
}

bool osSaveGame()
{
    return false;
}

bool osLoadGame()
{
    return false;
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
