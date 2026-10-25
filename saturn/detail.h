// Full-resolution room textures. The level's texture blob in VDP1 VRAM holds
// room textures at quarter resolution; the full-resolution set stays in work
// RAM, and the VRAM left over after the blob is a cache of slots holding the
// textures in use.
//
// The set loads as whole textures, drawn on faces at any distance. Faces near
// the camera split 2x2, each quarter drawn with one quadrant of the full-res
// texture: four tiles the size of the quarter-res one, clockwise from top
// left, all drawn with its colour table. The original's way (gSaturnLod) draws
// quarter-res textures but for those quarters, and keeps the set as quadrants.
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
// quarter-res size (Texture.size), whole or as quadrants, queueing it for
// upload if it is not cached. -1 when every slot is in use this frame or the
// frame's upload budget is spent.
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

// VDP1 VRAM offset of a slot.
extern uint32_t gDetailVramStart;
inline uint32_t detailSlotAddr(int32_t slot)
{
    return gDetailVramStart + slot * DETAIL_SLOT_SIZE;
}

// Rearranges the full-res set between whole textures and quadrants (for
// gSaturnLod, which draws only quadrants), and empties the cache. Slots in
// the layout the set is not in are rearranged as they upload. Each of the
// level's room textures names its block (reserved) and quadrant size.
struct Texture;
void detailSetWhole(bool whole, const Texture* textures, int32_t count);

// Uploads the textures queued this frame and starts the next frame. VDP1 must
// be idle: slots replaced this frame may hold textures it drew last frame.
void detailFlush();

// Inventory item textures (DATA/<level>.ITX) use the cache's VRAM while the
// inventory is open: suspending empties and stops the cache and puts them
// there at the next detailFlush.
uint8_t* detailItemData();
uint32_t detailItemDataMax();
void detailItemInit(uint32_t size);
void detailSuspend();
void detailResume();

#endif
