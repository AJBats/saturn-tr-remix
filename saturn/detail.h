// Room textures. The level's full-resolution set stays in work RAM, and the
// VDP1 VRAM left over after the texture blob (which holds only their colour
// tables) is a cache of slots holding the textures in use. A face whose
// texture misses the cache is drawn in its mean colour that frame.
//
// The set loads as whole textures. Faces crossing the near plane split 2x2,
// each quarter drawn with one quadrant of the texture: four tiles half its
// size each way, clockwise from top left, which a slot holds instead.
#ifndef DETAIL_H
#define DETAIL_H

#include <stdint.h>

#define DETAIL_UNIT         256     // Texture.reserved = full-res offset / DETAIL_UNIT + 1
#define DETAIL_SLOT_SIZE    2048    // four 32x32 4-bit quadrants
#define DETAIL_DATA_MAX     (272 * 1024)    // LEVEL8A's set is 263 KB
#define DETAIL_MAX_KEYS     (DETAIL_DATA_MAX / DETAIL_UNIT)

// Buffer the level's full-res textures load into.
uint8_t* detailData();
uint32_t detailDataMax();

// After loading: the cache starts at this VDP1 VRAM offset and runs to the
// end of VRAM.
void detailInit(uint32_t vramStart, uint32_t dataSize);

// The cache's slots and which texture each holds, for detailSlot's lookup.
struct DetailSlot
{
    uint16_t key;
    uint32_t frame;     // last frame drawn with it
};
extern uint8_t gDetailKeys[2][DETAIL_MAX_KEYS]; // whole, quadrants: slot + 1, 0 when not cached
extern DetailSlot gDetailSlots[];
extern uint32_t gDetailFrame;

int32_t detailSlotMiss(uint32_t key, uint32_t texSize, bool quads);

// Cache slot for the texture with this key (Texture.reserved - 1) and
// quadrant size (Texture.size), whole or as quadrants, queueing it for upload
// if it is not cached. -1 when every slot is in use this frame or the frame's
// upload budget is spent.
inline int32_t detailSlot(uint32_t key, uint32_t texSize, bool quads)
{
    if (key < DETAIL_MAX_KEYS)
    {
        uint32_t cached = gDetailKeys[quads][key];
        if (cached)
        {
            gDetailSlots[cached - 1].frame = gDetailFrame;
            return cached - 1;
        }
    }
    return detailSlotMiss(key, texSize, quads);
}

// Takes at least 'size' bytes off the top of the cache for other tables and
// empties it; their VDP1 VRAM offset, or 0 if they don't fit.
uint32_t detailReserveTop(uint32_t size);

// VDP1 VRAM offset of a slot.
extern uint32_t gDetailVramStart;
inline uint32_t detailSlotAddr(int32_t slot)
{
    return gDetailVramStart + slot * DETAIL_SLOT_SIZE;
}

// Uploads the textures queued this frame and starts the next frame. VDP1 must
// be idle: slots replaced this frame may hold textures it drew last frame.
void detailFlush();

// Inventory item textures (DATA/<level>.ITX) use the cache's first slots
// while the inventory is open: suspending empties those and puts the item
// textures there at the next detailFlush, and the rest of the cache goes on
// serving the scene behind the inventory.
uint8_t* detailItemData();
uint32_t detailItemDataMax();
void detailItemInit(uint32_t size);
void detailSuspend();
void detailResume();

#endif
