// Room textures. The level's full-resolution set stays in low work RAM, and
// the VDP1 VRAM left over after the texture blob (which holds only their
// colour tables) is a cache of slots holding the textures in use. A face whose
// texture misses the cache is drawn in its mean colour that frame.
//
// The set loads as whole textures. Faces crossing the near plane split 2x2,
// each quarter drawn with one quadrant of the texture: four tiles half its
// size each way, clockwise from top left, which a slot holds instead. A face
// drawn with its lines turned takes the texture transposed, made as it
// uploads.
//
// After the full-res set come two mip levels (tools/pack_level.py): each
// texture at half its size each way, at a quarter of its full-res offset, and
// at a quarter of its size, at a sixteenth. A face small on the frame takes
// one, so VDP1 reads about a texel a pixel along its lines rather than
// skipping through the full texture. Each level has a pool of slots its size
// at the top of the cache.
//
// The cache itself, its misses and uploads are the renderer's
// (renderer/textures.h); this lays out its pools, hands them to it, and lends
// the full-res pool's first slots to the inventory's item textures.
#ifndef DETAIL_H
#define DETAIL_H

#include <stdint.h>

#include "types.h"

#define DETAIL_UNIT         256     // Texture.reserved = full-res offset / DETAIL_UNIT + 1
#define DETAIL_DATA_MAX     (DETAIL_MAX_KEYS * DETAIL_UNIT)     // LEVEL8A's full-res set is 263 KB
#define DETAIL_LEVELS       3       // full res, half, quarter
#define DETAIL_MIN_FULL_SLOTS 40    // kept for full-res textures: near faces take
                                    // them, and the inventory's item textures 36

// After loading the level's room texture set (data, size): the cache starts
// at this VDP1 VRAM offset and runs to the end of VRAM; it starts empty.
void detailInit(uint32_t vramStart, const uint8_t* data, uint32_t dataSize);

// Takes at least 'size' bytes off the top of the full-res pool for other
// tables and empties the cache; their VDP1 VRAM offset, or 0 if they don't
// fit.
uint32_t detailReserveTop(uint32_t size);

// Bytes of the full-res pool beyond DETAIL_MIN_FULL_SLOTS, which optional
// tables may take.
uint32_t detailSpareBytes();

// Each pool's VDP1 VRAM offset (the full-res pool's first).
extern uint32_t gDetailPoolVram[DETAIL_LEVELS];

// Faces may take mip levels: the set has them and the cache their pools.
bool detailMips();

// Inventory item textures (DATA/<level>.ITX, in low work RAM) use the
// cache's first slots while the inventory is open: suspending empties those
// (and drops the uploads queued into them) and puts the item textures there
// at the next detailItemUpload, and the rest of the cache goes on serving the
// scene behind the inventory.
void detailItemInit(const uint8_t* data, uint32_t size);
void detailSuspend();
void detailResume();

// Before the frame's uploads: the item textures, if suspending asked for
// them (the CPU copies them: the SCU's DMA can't read low work RAM).
void detailItemUpload();

#endif
