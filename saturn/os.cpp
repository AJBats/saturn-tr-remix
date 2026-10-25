// OpenLara os* platform hooks.

#include "common.h"
#include "cd.h"
#include "hw.h"
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

static void loadOrHalt(const char* name, const char* ext, void* dst, uint32 maxSize)
{
    char path[32] = "DATA/";
    strcat(path, name);
    strcat(path, ext);

    if (cdLoadFile(path, dst, maxSize) <= 0)
    {
        while (1); // TODO error screen
    }
}

const void* osLoadLevel(LevelID id)
{
    const char* name = (const char*)gLevelInfo[id].data;

    // textures go straight to VDP1 VRAM, where the level's texture entries point
    loadOrHalt(name, ".TEX", (void*)(VDP1_VRAM + VRAM_TEX_BASE), VRAM_TEX_SIZE);
    loadOrHalt(name, ".PKD", LEVEL_ADDR, LEVEL_MAX);

    return LEVEL_ADDR;
}
