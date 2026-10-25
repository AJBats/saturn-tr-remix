// Full-resolution room texture cache (see detail.h).

#include "detail.h"
#include "hw.h"
#include "system.h"

// Largest full-res set is LEVEL8A's, 263 KB. High work RAM, so SCU DMA can
// read it (it can't reach low work RAM).
#define DATA_MAX        (272 * 1024)
#define MAX_KEYS        (DATA_MAX / DETAIL_UNIT)
#define MAX_SLOTS       128
#define MAX_UPLOADS     8       // per frame, 2 KB each
#define NO_KEY          0xFFFF
#define ITEM_DATA_MAX   (72 * 1024) // LEVEL4's are 70 KB

struct Slot
{
    uint16_t key;
    uint32_t frame;     // last frame drawn with it
};

struct Upload
{
    uint32_t src;
    uint32_t dst;
    uint32_t size;
};

static uint8_t gData[DATA_MAX] __attribute__((aligned(16)));
static uint32_t gDataSize;

static uint8_t gKeySlot[MAX_KEYS];  // slot + 1, 0 when not cached
static Slot gSlots[MAX_SLOTS];
static int32_t gSlotCount;
uint32_t gDetailVramStart;
static uint32_t gFrame;

static Upload gUploads[MAX_UPLOADS];
static int32_t gUploadCount;

static uint8_t gItemData[ITEM_DATA_MAX] __attribute__((aligned(16)));
static uint32_t gItemDataSize;
static bool gSuspended;
static bool gItemUpload;    // item textures go to VRAM at the next flush

uint8_t* detailData()
{
    return gData;
}

uint32_t detailDataMax()
{
    return DATA_MAX;
}

void detailInit(uint32_t vramStart, uint32_t dataSize)
{
    gDetailVramStart = (vramStart + 31) & ~31; // as tools/pack_level.py places item textures
    gDataSize = dataSize;

    gSlotCount = (0x80000 - gDetailVramStart) / DETAIL_SLOT_SIZE;
    if (gSlotCount > MAX_SLOTS) {
        gSlotCount = MAX_SLOTS;
    }

    for (int32_t i = 0; i < MAX_KEYS; i++) {
        gKeySlot[i] = 0;
    }

    gFrame = 1;
    for (int32_t i = 0; i < gSlotCount; i++)
    {
        gSlots[i].key = NO_KEY;
        gSlots[i].frame = 0;
    }

    gUploadCount = 0;
    gSuspended = false;
    gItemUpload = false;
}

uint8_t* detailItemData()
{
    return gItemData;
}

uint32_t detailItemDataMax()
{
    return ITEM_DATA_MAX;
}

void detailItemInit(uint32_t size)
{
    gItemDataSize = size;
}

void detailSuspend()
{
    if (gSuspended)
        return;

    gSuspended = true;
    gItemUpload = true;
    gUploadCount = 0;

    for (int32_t i = 0; i < gSlotCount; i++)
    {
        if (gSlots[i].key != NO_KEY) {
            gKeySlot[gSlots[i].key] = 0;
        }
        gSlots[i].key = NO_KEY;
        gSlots[i].frame = 0;
    }
}

void detailResume()
{
    gSuspended = false;
}

int32_t detailSlot(uint32_t key, uint32_t size)
{
    if (gSuspended || key >= MAX_KEYS || key * DETAIL_UNIT + size > gDataSize)
        return -1;

    uint32_t cached = gKeySlot[key];
    if (cached)
    {
        gSlots[cached - 1].frame = gFrame;
        return cached - 1;
    }

    if (gUploadCount == MAX_UPLOADS)
        return -1;

    // least recently drawn slot not in use this frame
    int32_t victim = -1;
    uint32_t oldest = gFrame;
    for (int32_t i = 0; i < gSlotCount; i++)
    {
        if (gSlots[i].frame < oldest)
        {
            oldest = gSlots[i].frame;
            victim = i;
        }
    }

    if (victim < 0)
        return -1;

    Slot &slot = gSlots[victim];
    if (slot.key != NO_KEY) {
        gKeySlot[slot.key] = 0;
    }
    slot.key = key;
    slot.frame = gFrame;
    gKeySlot[key] = victim + 1;

    Upload &upload = gUploads[gUploadCount++];
    upload.src = uint32_t(gData + key * DETAIL_UNIT);
    upload.dst = VDP1_VRAM + detailSlotAddr(victim);
    upload.size = size;

    return victim;
}

void detailFlush()
{
    if (gItemUpload && gItemDataSize)
    {
        dmaCopy(gItemData, VDP1_VRAM + gDetailVramStart, gItemDataSize);
        gItemUpload = false;
    }

    for (int32_t i = 0; i < gUploadCount; i++) {
        dmaCopy((const void*)gUploads[i].src, gUploads[i].dst, gUploads[i].size);
    }
    gUploadCount = 0;
    gFrame++;
}
