// OpenLara os* platform hooks.

#include "common.h"
#include "cd.h"
#include "detail.h"
#include "hw.h"
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

const void* osLoadScreen(LevelID id)
{
    return NULL;
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

    // textures go straight to VDP1 VRAM, where the level's texture entries point
    uint32 texSize = loadOrHalt(name, ".TEX", (void*)(VDP1_VRAM + VRAM_TEX_BASE), VRAM_TEX_SIZE);
    uint32 subSize = loadOrHalt(name, ".SUB", detailData(), detailDataMax());
    detailInit(VRAM_TEX_BASE + texSize, subSize);

    loadOrHalt(name, ".PKD", LEVEL_ADDR, LEVEL_MAX);

    // sound effects straight to sound RAM (some cutscenes have none)
    char path[32] = "DATA/";
    strcat(path, name);
    strcat(path, ".SFX");
    sndStop();
    cdLoadFile(path, (void*)(SOUND_RAM + SOUND_SAMPLES_ADDR), SOUND_RAM_SIZE - SOUND_SAMPLES_ADDR);

    return LEVEL_ADDR;
}
