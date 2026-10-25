// OpenLara os* platform hooks.

#include "common.h"
#include "cd.h"

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

const void* osLoadLevel(LevelID id)
{
    char path[32] = "DATA/";
    strcat(path, (const char*)gLevelInfo[id].data);
    strcat(path, ".PKD");

    if (cdLoadFile(path, LEVEL_ADDR, LEVEL_MAX) <= 0)
    {
        while (1); // TODO error screen
    }

    return LEVEL_ADDR;
}
