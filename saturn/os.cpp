// OpenLara os* platform hooks.

#include "common.h"

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
    return NULL; // TODO load from CD
}
