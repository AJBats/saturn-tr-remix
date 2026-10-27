// Room textures. The level's full-resolution set stays in low work RAM, and
// the VDP1 VRAM left over after the texture blob (which holds only their
// colour tables) is a cache of slots holding the textures in use. A face whose
// texture misses the cache is drawn in its mean colour that frame.
//
// The set loads as whole textures. Faces crossing the near plane split 2x2,
// each quarter drawn with one quadrant of the texture: four tiles half its
// size each way, clockwise from top left, which a slot holds instead. A face
// drawn with its lines turned (see rooms.cpp) takes the texture transposed,
// made as it uploads.
//
// After the full-res set come two mip levels (tools/pack_level.py): each
// texture at half its size each way, at a quarter of its full-res offset, and
// at a quarter of its size, at a sixteenth. A face small on the frame takes
// one (faces.s), so VDP1 reads about a texel a pixel along its lines rather
// than skipping through the full texture. Each level has a pool of slots its
// size at the top of the cache.
#ifndef DETAIL_H
#define DETAIL_H

#include <stdint.h>

#define DETAIL_UNIT         256     // Texture.reserved = full-res offset / DETAIL_UNIT + 1
#define DETAIL_SLOT_SIZE    2048    // four 32x32 4-bit quadrants
#define DETAIL_DATA_MAX     (272 * 1024)    // LEVEL8A's full-res set is 263 KB
#define DETAIL_MAX_KEYS     (DETAIL_DATA_MAX / DETAIL_UNIT)
#define DETAIL_LEVELS       3       // full res, half, quarter
#define DETAIL_FULL_SLOTS   128     // the most full-res slots; the mip pools' slots follow
#define DETAIL_MIP_SLOTS    64      // each mip level's
#define DETAIL_SLOTS        (DETAIL_FULL_SLOTS + 2 * DETAIL_MIP_SLOTS)
#define DETAIL_MIN_FULL_SLOTS 40    // kept for full-res textures: near faces take
                                    // them, and the inventory's item textures 36

// After loading the level's room texture set (data, size): the cache starts
// at this VDP1 VRAM offset and runs to the end of VRAM.
void detailInit(uint32_t vramStart, const uint8_t* data, uint32_t dataSize);

// How a slot holds its texture: the level, and whole, in quadrants or
// transposed. faces.s numbers the variants it uses by level * 2 + turned.
enum DetailVariant
{
    DETAIL_WHOLE,
    DETAIL_QUADS,
    DETAIL_TURNED,      // whole, transposed
    DETAIL_MIP1,        // half size
    DETAIL_MIP1_TURNED,
    DETAIL_MIP2,        // quarter size
    DETAIL_MIP2_TURNED,
    DETAIL_VARIANTS
};

// The cache's slots and which texture each holds, for detailSlot's lookup:
// the full-res pool's, then the mip pools' (each level's from
// detailPoolStart).
struct DetailSlot
{
    uint16_t key;       // the texture's key, its variant << 12
    uint32_t frame;     // last frame drawn with it
};
extern uint8_t gDetailKeys[DETAIL_VARIANTS][DETAIL_MAX_KEYS]; // slot in its pool + 1, 0 when not cached
extern DetailSlot gDetailSlots[DETAIL_SLOTS];
extern uint32_t gDetailFrame;

inline uint32_t detailLevel(uint32_t variant)
{
    return variant < DETAIL_MIP1 ? 0 : (variant - DETAIL_MIP1) / 2 + 1;
}

inline uint32_t detailPoolStart(uint32_t level)
{
    return level ? DETAIL_FULL_SLOTS + (level - 1) * DETAIL_MIP_SLOTS : 0;
}

int32_t detailSlotMiss(uint32_t key, uint32_t texSize, uint32_t variant);

// Cache slot (in its level's pool) for the texture with this key
// (Texture.reserved - 1) and quadrant size (Texture.size), as the variant,
// queueing it for upload if it is not cached. -1 when every slot of the pool
// is in use this frame or the frame's upload budget is spent.
inline int32_t detailSlot(uint32_t key, uint32_t texSize, uint32_t variant)
{
    if (key < DETAIL_MAX_KEYS)
    {
        uint32_t cached = gDetailKeys[variant][key];
        if (cached)
        {
            gDetailSlots[detailPoolStart(detailLevel(variant)) + cached - 1].frame = gDetailFrame;
            return cached - 1;
        }
    }
    return detailSlotMiss(key, texSize, variant);
}

// Takes at least 'size' bytes off the top of the full-res pool for other
// tables and empties the cache; their VDP1 VRAM offset, or 0 if they don't
// fit.
uint32_t detailReserveTop(uint32_t size);

// Bytes of the full-res pool beyond DETAIL_MIN_FULL_SLOTS, which optional
// tables may take.
uint32_t detailSpareBytes();

// VDP1 VRAM offset of a slot of the full-res pool, and of each pool.
extern uint32_t gDetailVramStart;
extern uint32_t gDetailPoolVram[DETAIL_LEVELS];
inline uint32_t detailSlotAddr(int32_t slot)
{
    return gDetailVramStart + slot * DETAIL_SLOT_SIZE;
}

// Faces may take mip levels: the set has them and the cache their pools.
bool detailMips();

// The w x h 4-bit texture at src to dst transposed, h x w: texel (x, y) to
// (y, x). Each a multiple of 8; both 32-bit aligned.
void detailTranspose(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t h);

// The frame's faces are all added: its uploads wait for detailUpload, and the
// next frame starts queueing its own. The last frame's must be uploaded.
void detailFrameEnd();

// Uploads the textures the last ended frame queued. VDP1 must have drawn the
// list before that frame's: slots replaced may hold textures it drew.
void detailUpload();

// Inventory item textures (DATA/<level>.ITX) use the cache's first slots
// while the inventory is open: suspending empties those and puts the item
// textures there at the next detailUpload, and the rest of the cache goes on
// serving the scene behind the inventory.
uint8_t* detailItemData();
uint32_t detailItemDataMax();
void detailItemInit(uint32_t size);
void detailSuspend();
void detailResume();

#endif
