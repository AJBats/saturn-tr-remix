// Room texture cache's pools (see detail.h).

#include "detail.h"
#include "frame.h"
#include "textures.h"
#include "hw.h"
#include "crash.h"

#include <string.h>

#define NO_KEY          0xFFFF
#define IN_USE          0xFFFFFFFF  // DetailSlot.frame of a slot under the item textures
#define VARIANT_SHIFT   12      // DetailSlot.key
#define KEY_MASK        0xFFF
#define UPLOAD_OFFSET   0x00FFFFFF
#define MIP_BYTES       (DETAIL_MIP_SLOTS * (DETAIL_SLOT_SIZE / 4 + DETAIL_SLOT_SIZE / 16))

static render::TextureSet gSet;
uint32_t gDetailPoolVram[DETAIL_LEVELS];

static const uint8_t* gItemData;
static uint32_t gItemDataSize;
static int32_t gItemSlots;  // slots under the item textures while they are in VRAM
static bool gItemUpload;    // item textures go to VRAM before the next frame's uploads

// Every slot empty.
static void emptySlots()
{
    memset(render::texturesKeys(), 0, render::DETAIL_VARIANTS * DETAIL_MAX_KEYS);
    render::DetailSlot* slots = render::texturesSlots();
    for (int32_t i = 0; i < DETAIL_SLOTS; i++)
    {
        slots[i].key = NO_KEY;
        slots[i].frame = 0;
    }
}

void detailInit(uint32_t vramStart, const uint8_t* data, uint32_t dataSize)
{
    uint32_t start = (vramStart + 31) & ~31; // as tools/pack_level.py places item textures

    // the full-res set and its mip levels, 1 + 1/4 + 1/16 of it
    uint32_t full = dataSize / 21 * 16;
    bool mips = full % DETAIL_UNIT == 0 && full / 16 * 21 == dataSize;
    if (!mips) {
        full = dataSize;
    }
    gSet.data = data;
    gSet.dataSize = full < DETAIL_DATA_MAX ? full : DETAIL_DATA_MAX;
    gSet.levels[0] = 0;
    gSet.levels[1] = full;
    gSet.levels[2] = full + full / 4;

    int32_t slots = (0x80000 - start) / DETAIL_SLOT_SIZE;
    if (mips && slots - MIP_BYTES / DETAIL_SLOT_SIZE >= DETAIL_MIN_FULL_SLOTS)
    {
        slots -= MIP_BYTES / DETAIL_SLOT_SIZE;
        gSet.pools[1] = gSet.pools[2] = DETAIL_MIP_SLOTS;
    }
    else
    {
        gSet.pools[1] = gSet.pools[2] = 0;
    }
    gSet.pools[0] = slots < DETAIL_FULL_SLOTS ? slots : DETAIL_FULL_SLOTS;
    gDetailPoolVram[0] = start;
    gDetailPoolVram[1] = start + slots * DETAIL_SLOT_SIZE;
    gDetailPoolVram[2] = gDetailPoolVram[1] + DETAIL_MIP_SLOTS * (DETAIL_SLOT_SIZE / 4);
    for (int32_t l = 0; l < DETAIL_LEVELS; l++) {
        gSet.poolVram[l] = gDetailPoolVram[l];
    }
    crashLog("DETAIL", (gSet.pools[0] << 16) | gSet.pools[1]);

    render::texturesInit(gSet);
    emptySlots();
    gItemSlots = 0;
    gItemUpload = false;
}

bool detailMips()
{
    return gSet.pools[1] != 0;
}

uint32_t detailReserveTop(uint32_t size)
{
    int32_t slots = (size + DETAIL_SLOT_SIZE - 1) / DETAIL_SLOT_SIZE;
    if (slots > gSet.pools[0] / 2)
        return 0;

    gSet.pools[0] -= slots;
    render::texturesInit(gSet);
    emptySlots();
    return gDetailPoolVram[0] + gSet.pools[0] * DETAIL_SLOT_SIZE;
}

uint32_t detailSpareBytes()
{
    return gSet.pools[0] > DETAIL_MIN_FULL_SLOTS ? (gSet.pools[0] - DETAIL_MIN_FULL_SLOTS) * DETAIL_SLOT_SIZE : 0;
}

void detailItemInit(const uint8_t* data, uint32_t size)
{
    gItemData = data;
    gItemDataSize = size;
}

void detailSuspend()
{
    if (gItemSlots)
        return;

    gItemUpload = true;
    gItemSlots = (gItemDataSize + DETAIL_SLOT_SIZE - 1) / DETAIL_SLOT_SIZE;
    if (gItemSlots > gSet.pools[0]) {
        gItemSlots = gSet.pools[0];
    }
    if (!gItemSlots)
        return;

    uint8_t (*keys)[DETAIL_MAX_KEYS] = render::texturesKeys();
    render::DetailSlot* slots = render::texturesSlots();
    for (int32_t i = 0; i < gItemSlots; i++)
    {
        render::DetailSlot &slot = slots[i];
        if (slot.key != NO_KEY) {
            keys[slot.key >> VARIANT_SHIFT][slot.key & KEY_MASK] = 0;
        }
        slot.key = NO_KEY;
        slot.frame = IN_USE;
    }

    // and the uploads queued into them
    uint32_t end = gDetailPoolVram[0] + gItemSlots * DETAIL_SLOT_SIZE;
    render::TextureQueue &queue = render::gFrameTextures;
    int32_t kept = 0;
    for (int32_t i = 0; i < queue.count; i++)
    {
        uint32_t dst = queue.uploads[i].dst & UPLOAD_OFFSET;
        if (dst >= end || dst < gDetailPoolVram[0]) {
            queue.uploads[kept++] = queue.uploads[i];
        }
    }
    queue.count = kept;
}

void detailResume()
{
    render::DetailSlot* slots = render::texturesSlots();
    for (int32_t i = 0; i < gItemSlots; i++) {
        slots[i].frame = 0;
    }
    gItemSlots = 0;
}

void detailItemUpload()
{
    if (!gItemUpload)
        return;
    gItemUpload = false;
    if (!gItemData || !gItemDataSize)
        return;
    const uint32_t* src = (const uint32_t*)gItemData;
    volatile uint32_t* dst = (volatile uint32_t*)(VDP1_VRAM + gDetailPoolVram[0]);
    for (uint32_t i = 0; i < (gItemDataSize + 3) / 4; i++) {
        dst[i] = src[i];
    }
}
