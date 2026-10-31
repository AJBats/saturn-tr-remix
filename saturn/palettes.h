// The remastered colour tables (DATA/<level>.PAL, tools/pack_level.py), for
// comparing them with the original ones in game.
#ifndef PALETTES_H
#define PALETTES_H

#include <stdint.h>

// After the level's textures are in VRAM: reads its remastered tables to dst
// (at most maxSize bytes) and puts them in if they were picked; the bytes
// they take there.
uint32_t palettesLoad(const char* level, void* dst, uint32_t maxSize);

// Swaps between the original and the remastered tables; the choice holds for
// later levels.
void palettesToggle();
bool palettesRemastered();

#endif
