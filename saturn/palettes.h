// The remastered colour tables (DATA/<level>.PAL, tools/pack_level.py), for
// comparing them with the original ones in game.
#ifndef PALETTES_H
#define PALETTES_H

// After the level's textures are in VRAM: reads its remastered tables and
// puts them in if they were picked.
void palettesLoad(const char* level);

// Swaps between the original and the remastered tables; the choice holds for
// later levels.
void palettesToggle();
bool palettesRemastered();

#endif
