// Full-resolution room textures for faces near the camera, after the original
// game. The level's texture blob in VDP1 VRAM holds room textures at quarter
// resolution; the full-resolution set stays in work RAM, and the VRAM left
// over after the blob is a cache of slots holding the textures in use.
//
// A full-res texture is four quadrants, clockwise from top left, each the size
// of the quarter-res tile and drawn with its colour table.
#ifndef DETAIL_H
#define DETAIL_H

#include <stdint.h>

#define DETAIL_UNIT         512     // Texture.reserved = full-res offset / DETAIL_UNIT + 1
#define DETAIL_SLOT_SIZE    2048    // four 32x32 4-bit quadrants

// Buffer the level's full-res textures load into.
uint8_t* detailData();
uint32_t detailDataMax();

// After loading: the cache starts at this VDP1 VRAM offset and runs to the
// end of VRAM.
void detailInit(uint32_t vramStart, uint32_t dataSize);

// Cache slot for the texture with this key (Texture.reserved - 1), queueing
// its 'size' bytes for upload if it is not cached. -1 when every slot is in
// use this frame or the frame's upload budget is spent.
int32_t detailSlot(uint32_t key, uint32_t size);

// VDP1 VRAM offset of a slot.
uint32_t detailSlotAddr(int32_t slot);

// Uploads the textures queued this frame and starts the next frame. VDP1 must
// be idle: slots replaced this frame may hold textures it drew last frame.
void detailFlush();

#endif
