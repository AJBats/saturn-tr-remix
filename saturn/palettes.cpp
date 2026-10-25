// Each entry in DATA/<level>.PAL is a colour table's offset in the level's
// textures and its other version. Swapping exchanges what VDP1 VRAM holds with
// the buffer, so one swap goes either way.

#include "palettes.h"
#include "render.h"
#include "cd.h"
#include "hw.h"
#include "vram.h"

struct PaletteEntry
{
    uint32 offset;  // from VRAM_TEX_BASE
    uint16 colors[16];
};

#define MAX_PALETTES 1280  // LEVEL4 has 1112

static struct
{
    uint32 count;
    PaletteEntry entries[MAX_PALETTES];
} gPalettes;

static bool gRemastered = false; // the player's pick
static bool gInVram;            // whether VRAM holds the remastered tables

static void swap()
{
    for (uint32 i = 0; i < gPalettes.count; i++)
    {
        PaletteEntry &e = gPalettes.entries[i];
        volatile uint16* vram = (volatile uint16*)(VDP1_VRAM + VRAM_TEX_BASE + e.offset);
        for (int32 c = 0; c < 16; c++)
        {
            uint16 t = vram[c];
            vram[c] = e.colors[c];
            e.colors[c] = t;
        }
    }
    gInVram = !gInVram;
}

void palettesLoad(const char* level)
{
    char path[32] = "DATA/";
    strcat(path, level);
    strcat(path, ".PAL");

    gPalettes.count = 0;
    gInVram = false;
    if (cdLoadFile(path, &gPalettes, sizeof(gPalettes)) < 4 || gPalettes.count > MAX_PALETTES) {
        gPalettes.count = 0;
    }

    if (gRemastered) {
        swap();
    }
}

void palettesToggle()
{
    gRemastered = !gRemastered;
    if (gRemastered != gInVram)
    {
        swap();
        renderPalettesSwapped();
    }
}

bool palettesRemastered()
{
    return gRemastered;
}
