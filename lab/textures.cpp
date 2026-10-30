// The room texture cache's misses and uploads (textures.h): saturn/detail.cpp
// detailSlotMiss and detailUpload, on the scene's cache and room texture set.

#include "textures.h"
#include "game.h"
#include "hw.h"
#include "platform.h"
#include "scene.h"

#include <string.h>

#define DETAIL_UNIT     256     // Texture.reserved = full-res offset / DETAIL_UNIT + 1
#define DETAIL_SLOTS    (DETAIL_FULL_SLOTS + 2 * DETAIL_MIP_SLOTS)
#define NO_KEY          0xFFFF
#define VARIANT_SHIFT   12      // DetailSlot.key
#define KEY_MASK        0xFFF
#define UPLOAD_OFFSET   0x00FFFFFF
#define UPLOAD_VARIANT  24

// The room texture set's layout (RTXI).
struct TextureSet
{
    uint32_t levels[3];     // each level's offset in the set: full res, half, quarter
    uint32_t dataSize;      // the full-res set's bytes (detail.cpp gDataSize)
    int32_t pools[3];       // each level's pool's slots (gPoolCount)
};

static const uint32_t kSlotSize[3] = { DETAIL_SLOT_SIZE, DETAIL_SLOT_SIZE / 4, DETAIL_SLOT_SIZE / 16 };

static const uint8_t* gSet;             // RTEX, in low work RAM
static const TextureSet* gLayout;
static uint32_t gPoolVram[3];
static uint8_t (*gKeys)[DETAIL_MAX_KEYS];           // the cache: DKEY
static DetailSlot* gSlots;                          // DSLT
static const uint8_t (*gKeysFound)[DETAIL_MAX_KEYS];    // as the frame found it: DKY0
static const DetailSlot* gSlotsFound;               // DSL0
static uint8_t (*gKeysLeft)[DETAIL_MAX_KEYS];       // as the game's frame left it (low work RAM)
static DetailSlot* gSlotsLeft;
static TextureQueue gCaptured;

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

void texturesInit()
{
    const SceneInfo &info = sceneInfo();
    gSet = sceneArray<const uint8_t>("RTEX");
    gLayout = sceneArray<const TextureSet>("RTXI");
    for (int32_t l = 0; l < 3; l++) {
        gPoolVram[l] = info.poolVram[l];
    }
    gKeys = (uint8_t (*)[DETAIL_MAX_KEYS])sceneChunk("DKEY");
    gSlots = sceneArray<DetailSlot>("DSLT");
    gKeysFound = (const uint8_t (*)[DETAIL_MAX_KEYS])sceneChunk("DKY0");
    gSlotsFound = sceneArray<const DetailSlot>("DSL0");
    gKeysLeft = (uint8_t (*)[DETAIL_MAX_KEYS])sceneAllocLow(DETAIL_VARIANTS * DETAIL_MAX_KEYS);
    gSlotsLeft = (DetailSlot*)sceneAllocLow(DETAIL_SLOTS * sizeof(DetailSlot));
    memcpy(gKeysLeft, gKeys, DETAIL_VARIANTS * DETAIL_MAX_KEYS);
    memcpy(gSlotsLeft, gSlots, DETAIL_SLOTS * sizeof(DetailSlot));
    memcpy(&gCaptured, sceneChunk("DUPL"), sizeof(gCaptured));
}

void texturesReset()
{
    memcpy(gKeys, gKeysFound, DETAIL_VARIANTS * DETAIL_MAX_KEYS);
    memcpy(gSlots, gSlotsFound, DETAIL_SLOTS * sizeof(DetailSlot));
}

int32_t texturesMiss(TextureQueue &queue, uint32_t frame, uint32_t key, uint32_t texSize, uint32_t variant)
{
    uint32_t w = (texSize >> 8) * 16;   // the whole texture's
    uint32_t h = (texSize & 0xFF) * 2;
    if (key >= DETAIL_MAX_KEYS || key * DETAIL_UNIT + w * h / 2 > gLayout->dataSize)
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
    for (int32_t i = 0; i < gLayout->pools[level]; i++)
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
    upload.src = gLayout->levels[level] + ((key * DETAIL_UNIT) >> (2 * level));
    upload.dst = (gPoolVram[level] + victim * kSlotSize[level]) | (variant << UPLOAD_VARIANT);
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

// An upload's texture as its slot holds it, into dst: its bytes.
static uint32_t staged(uint8_t* dst, const TextureUpload &upload)
{
    const uint8_t* src = gSet + upload.src;
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
        uint32_t size = staged(gStaging[buffer], upload);
        cycles += ticksCycles(t0, ticks());
        dmaToVdp1(gStaging[buffer], upload.dst & UPLOAD_OFFSET, size);
        buffer ^= 1;
    }
    dmaWait();
    return cycles;
}

const TextureQueue& texturesCaptured()
{
    return gCaptured;
}

int32_t texturesQueueOff(const TextureQueue &a, const TextureQueue &b)
{
    if (a.count != b.count)
        return a.count > b.count ? a.count : b.count;
    int32_t off = 0;
    for (int32_t i = 0; i < a.count; i++) {
        off += memcmp(a.uploads + i, b.uploads + i, sizeof(TextureUpload)) != 0;
    }
    return off;
}

int32_t texturesStateOff()
{
    int32_t off = 0;
    for (int32_t v = 0; v < DETAIL_VARIANTS; v++) {
        for (int32_t k = 0; k < DETAIL_MAX_KEYS; k++) {
            off += gKeys[v][k] != gKeysLeft[v][k];
        }
    }
    for (int32_t i = 0; i < DETAIL_SLOTS; i++) {
        off += gSlots[i].key != gSlotsLeft[i].key || gSlots[i].frame != gSlotsLeft[i].frame;
    }
    return off;
}

int32_t texturesVramOff(const TextureQueue &queue)
{
    int32_t off = 0;
    for (int32_t i = 0; i < queue.count; i++)
    {
        const TextureUpload &upload = queue.uploads[i];
        uint32_t size = staged(gStaging[0], upload);
        const volatile uint32_t* vram = (const volatile uint32_t*)(VDP1_VRAM + (upload.dst & UPLOAD_OFFSET));
        const uint32_t* want = (const uint32_t*)gStaging[0];
        for (uint32_t k = 0; k < size / 4; k++) {
            off += vram[k] != want[k];
        }
    }
    return off;
}
