// Full-resolution room texture cache (see detail.h).

#include "detail.h"
#include "common.h"
#include "hw.h"
#include "system.h"

// Largest full-res set is LEVEL8A's, 263 KB. High work RAM, so SCU DMA can
// read it (it can't reach low work RAM).
#define DATA_MAX        (272 * 1024)
#define MAX_KEYS        (DATA_MAX / DETAIL_UNIT)
#define MAX_SLOTS       128
#define MAX_UPLOADS     32      // per frame, 2 KB each: 8 normally, all with whole textures
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
static int32_t gUploadMax = 8;

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
    gUploadMax = 8;
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

    if (gUploadCount == gUploadMax)
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
    gStats.detailUploads = gUploadCount;
    gUploadCount = 0;
    gFrame++;
}

// One texture's block: four w x h quadrants, clockwise from top left, or the
// 2w x 2h texture they make.
static void relayout(uint8_t* block, uint32_t w, uint32_t h, bool toWhole)
{
    static uint8_t tmp[DETAIL_SLOT_SIZE];
    static const uint8_t ox[4] = { 0, 1, 1, 0 };
    static const uint8_t oy[4] = { 0, 0, 1, 1 };

    uint32_t row = w / 2; // bytes in a quadrant's row
    uint32_t quad = row * h;
    if (quad * 4 > sizeof(tmp))
        return;

    for (int32_t q = 0; q < 4; q++)
    {
        for (uint32_t r = 0; r < h; r++)
        {
            uint32_t quadOffset = q * quad + r * row;
            uint32_t wholeOffset = (oy[q] * h + r) * row * 2 + ox[q] * row;
            if (toWhole) {
                memcpy(tmp + wholeOffset, block + quadOffset, row);
            } else {
                memcpy(tmp + quadOffset, block + wholeOffset, row);
            }
        }
    }
    memcpy(block, tmp, quad * 4);
}

void detailSetWhole(bool whole, const Texture* textures, int32_t count)
{
    static uint8_t done[MAX_KEYS];
    memset(done, 0, sizeof(done));

    for (int32_t i = 0; i < count; i++)
    {
        const Texture &t = textures[i];
        uint32_t key = t.reserved - 1;
        if (!t.reserved || key >= MAX_KEYS || done[key])
            continue;
        done[key] = 1;
        relayout(gData + key * DETAIL_UNIT, (t.size >> 8) * 8, t.size & 0xFF, whole);
    }

    for (int32_t i = 0; i < gSlotCount; i++)
    {
        if (gSlots[i].key != NO_KEY) {
            gKeySlot[gSlots[i].key] = 0;
        }
        gSlots[i].key = NO_KEY;
        gSlots[i].frame = 0;
    }
    gUploadCount = 0;
    gUploadMax = whole ? MAX_UPLOADS : 8;
}
