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
static uint32_t gVramStart;
static uint32_t gFrame;

static Upload gUploads[MAX_UPLOADS];
static int32_t gUploadCount;

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
    gVramStart = (vramStart + 7) & ~7;
    gDataSize = dataSize;

    gSlotCount = (0x80000 - gVramStart) / DETAIL_SLOT_SIZE;
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
}

int32_t detailSlot(uint32_t key, uint32_t size)
{
    if (key >= MAX_KEYS || key * DETAIL_UNIT + size > gDataSize)
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

uint32_t detailSlotAddr(int32_t slot)
{
    return gVramStart + slot * DETAIL_SLOT_SIZE;
}

void detailFlush()
{
    for (int32_t i = 0; i < gUploadCount; i++) {
        dmaCopy((const void*)gUploads[i].src, gUploads[i].dst, gUploads[i].size);
    }
    gUploadCount = 0;
    gFrame++;
}
