// The room texture cache's misses and uploads: saturn/detail.cpp's, which the
// renderer keeps as it is. A miss takes the least recently drawn slot of its
// level's pool not drawn this frame and queues its texture's upload, up to
// TEXTURE_UPLOADS a frame; the uploads go to VDP1 VRAM through a staging
// buffer in high work RAM (the SCU's DMA can't read the set in low work RAM),
// each texture whole, in quadrants or transposed.
#ifndef RENDER_TEXTURES_H
#define RENDER_TEXTURES_H

#include <stdint.h>

#include "types.h"

namespace render {

#define TEXTURE_UPLOADS 32
#ifndef DETAIL_SLOTS
#define DETAIL_SLOTS    (DETAIL_FULL_SLOTS + 2 * DETAIL_MIP_SLOTS)
#endif

struct TextureUpload
{
    uint32_t src;       // in the room texture set
    uint32_t dst;       // its slot's VDP1 VRAM offset, the variant << 24
    uint32_t size;      // the texture's width << 16 | height, at its level
};

struct TextureQueue
{
    int32_t count;
    TextureUpload uploads[TEXTURE_UPLOADS];
};

// The level's room texture set (in low work RAM) and its pools in VDP1 VRAM.
struct TextureSet
{
    const uint8_t* data;
    uint32_t levels[3];     // each level's offset in data: full res, half, quarter
    uint32_t dataSize;      // the full-res set's bytes
    int32_t pools[3];       // each level's pool's slots
    uint32_t poolVram[3];   // ... and its VDP1 VRAM offset
};

// The cache: each variant's slot by texture key (0 none, else its slot in its
// level's pool + 1; a key is Texture.reserved - 1), and the slots.
uint8_t (*texturesKeys())[DETAIL_MAX_KEYS];
DetailSlot* texturesSlots();

// The level's set; the cache as it stands.
void texturesInit(const TextureSet &set);

// A texture the cache hasn't, as of frame (texSize: Texture.size; variant:
// DetailVariant), into it: its slot in its level's pool, its upload queued;
// -1 when it can't be (past the set, the queue full, or every slot drawn this
// frame).
int32_t texturesMiss(TextureQueue &queue, uint32_t frame, uint32_t key, uint32_t texSize, uint32_t variant);

// The queue's textures into their slots. The SH-2 cycles the staging took
// (the DMA runs beside it).
uint32_t texturesUpload(const TextureQueue &queue);

// An upload's texture as its slot holds it, into dst: its bytes.
uint32_t texturesStaged(uint8_t* dst, const TextureUpload &upload);

}

#endif
