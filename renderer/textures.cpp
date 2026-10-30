// The room texture cache's misses and uploads (textures.h): saturn/detail.cpp
// detailSlotMiss and detailUpload.

#include "textures.h"
#include "platform.h"

#include <string.h>

namespace render {

#define DETAIL_UNIT     256     // Texture.reserved = full-res offset / DETAIL_UNIT + 1
#define NO_KEY          0xFFFF
#define VARIANT_SHIFT   12      // DetailSlot.key
#define KEY_MASK        0xFFF
#define UPLOAD_OFFSET   0x00FFFFFF
#define UPLOAD_VARIANT  24

static const uint32_t kSlotSize[3] = { DETAIL_SLOT_SIZE, DETAIL_SLOT_SIZE / 4, DETAIL_SLOT_SIZE / 16 };

static TextureSet gSet;
static uint8_t gKeys[DETAIL_VARIANTS][DETAIL_MAX_KEYS];
static DetailSlot gSlots[DETAIL_SLOTS];

// Two staging buffers: one filled while the SCU's DMA reads the other.
static uint8_t gStaging[2][DETAIL_SLOT_SIZE] __attribute__((aligned(16)));

static inline uint32_t detailLevel(uint32_t variant)
{
    return variant < DETAIL_MIP1 ? 0 : (variant - DETAIL_MIP1) / 2 + 1;
}

static inline uint32_t detailPoolStart(uint32_t level)
{
    return level ? DETAIL_FULL_SLOTS + (level - 1) * DETAIL_MIP_SLOTS : 0;
}

uint8_t (*texturesKeys())[DETAIL_MAX_KEYS]
{
    return gKeys;
}

DetailSlot* texturesSlots()
{
    return gSlots;
}

void texturesInit(const TextureSet &set)
{
    gSet = set;
}

int32_t texturesMiss(TextureQueue &queue, uint32_t frame, uint32_t key, uint32_t texSize, uint32_t variant)
{
    uint32_t w = (texSize >> 8) * 16;   // the whole texture's
    uint32_t h = (texSize & 0xFF) * 2;
    if (key >= DETAIL_MAX_KEYS || key * DETAIL_UNIT + w * h / 2 > gSet.dataSize)
        return -1;

    uint32_t level = detailLevel(variant);
    w >>= level;
    h >>= level;
    if (queue.count == TEXTURE_UPLOADS)
        return -1;

    // least recently drawn slot of the level's pool not in use this frame
    DetailSlot* pool = gSlots + detailPoolStart(level);
    int32_t victim = -1;
    uint32_t oldest = frame;
    for (int32_t i = 0; i < gSet.pools[level]; i++)
    {
        if (pool[i].frame < oldest)
        {
            oldest = pool[i].frame;
            victim = i;
        }
    }
    if (victim < 0)
        return -1;

    DetailSlot &slot = pool[victim];
    if (slot.key != NO_KEY) {
        gKeys[slot.key >> VARIANT_SHIFT][slot.key & KEY_MASK] = 0;
    }
    slot.key = key | (variant << VARIANT_SHIFT);
    slot.frame = frame;
    gKeys[variant][key] = victim + 1;

    TextureUpload &upload = queue.uploads[queue.count++];
    upload.src = gSet.levels[level] + ((key * DETAIL_UNIT) >> (2 * level));
    upload.dst = (gSet.poolVram[level] + victim * kSlotSize[level]) | (variant << UPLOAD_VARIANT);
    upload.size = (w << 16) | h;
    return victim;
}

// One texture's block, the 2w x 2h texture, to dst as four w x h quadrants,
// clockwise from top left (detail.cpp quadrants).
static void quadrants(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t h)
{
    static const uint8_t ox[4] = { 0, 1, 1, 0 };
    static const uint8_t oy[4] = { 0, 0, 1, 1 };
    uint32_t row = w / 2;
    uint32_t quad = row * h;
    if (quad * 4 > DETAIL_SLOT_SIZE)
        return;
    for (int32_t q = 0; q < 4; q++)
    {
        for (uint32_t r = 0; r < h; r++) {
            memcpy(dst + q * quad + r * row, src + (oy[q] * h + r) * row * 2 + ox[q] * row, row);
        }
    }
}

// The w x h 4-bit texture transposed, by 8 x 8 blocks (detail.cpp
// detailTranspose).
static void transpose(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t h)
{
    const uint32_t* s = (const uint32_t*)src;
    uint32_t* d = (uint32_t*)dst;
    uint32_t srcRow = w / 8;
    uint32_t dstRow = h / 8;
    for (uint32_t by = 0; by < h / 8; by++)
    {
        for (uint32_t bx = 0; bx < w / 8; bx++)
        {
            uint32_t r[8];
            for (int32_t i = 0; i < 8; i++) {
                r[i] = s[(by * 8 + i) * srcRow + bx];
            }
            for (int32_t i = 0; i < 4; i++)
            {
                uint32_t a = r[i], b = r[i + 4];
                r[i] = (a & 0xFFFF0000) | (b >> 16);
                r[i + 4] = (a << 16) | (b & 0xFFFF);
            }
            static const uint8_t upper[4] = { 0, 1, 4, 5 };
            for (int32_t k = 0; k < 4; k++)
            {
                int32_t i = upper[k];
                uint32_t a = r[i], b = r[i + 2];
                r[i] = (a & 0xFF00FF00) | ((b >> 8) & 0x00FF00FF);
                r[i + 2] = ((a << 8) & 0xFF00FF00) | (b & 0x00FF00FF);
            }
            for (int32_t i = 0; i < 8; i += 2)
            {
                uint32_t a = r[i], b = r[i + 1];
                r[i] = (a & 0xF0F0F0F0) | ((b >> 4) & 0x0F0F0F0F);
                r[i + 1] = ((a << 4) & 0xF0F0F0F0) | (b & 0x0F0F0F0F);
            }
            for (int32_t i = 0; i < 8; i++) {
                d[(bx * 8 + i) * dstRow + by] = r[i];
            }
        }
    }
}

uint32_t texturesStaged(uint8_t* dst, const TextureUpload &upload)
{
    const uint8_t* src = gSet.data + upload.src;
    uint32_t variant = upload.dst >> UPLOAD_VARIANT;
    uint32_t w = upload.size >> 16, h = upload.size & 0xFFFF;
    uint32_t size = w * h / 2;
    if (variant == DETAIL_QUADS) {
        quadrants(dst, src, w / 2, h / 2);
    } else if (variant == DETAIL_TURNED || variant == DETAIL_MIP1_TURNED || variant == DETAIL_MIP2_TURNED) {
        transpose(dst, src, w, h);
    } else {
        memcpy(dst, src, size);
    }
    return size;
}

uint32_t texturesUpload(const TextureQueue &queue)
{
    uint32_t cycles = 0;
    int32_t buffer = 0;
    for (int32_t i = 0; i < queue.count; i++)
    {
        const TextureUpload &upload = queue.uploads[i];
        uint32_t t0 = ticks();
        uint32_t size = texturesStaged(gStaging[buffer], upload);
        cycles += ticksCycles(t0, ticks());
        dmaToVdp1(gStaging[buffer], upload.dst & UPLOAD_OFFSET, size);
        buffer ^= 1;
    }
    dmaWait();
    return cycles;
}

}
