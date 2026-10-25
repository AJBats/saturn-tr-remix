// Full-resolution room texture cache (see detail.h).

#include "detail.h"
#include "common.h"
#include "hw.h"
#include "system.h"

// High work RAM, so SCU DMA can read it (it can't reach low work RAM).
#define DATA_MAX        DETAIL_DATA_MAX
#define MAX_KEYS        DETAIL_MAX_KEYS
#define MAX_SLOTS       128
#define MAX_UPLOADS     32      // per frame, 2 KB each
#define NO_KEY          0xFFFF
#define IN_USE          0xFFFFFFFF  // DetailSlot.frame of a slot under the item textures
#define KEY_QUADS       0x8000  // DetailSlot.key: the texture as quadrants
#define ITEM_DATA_MAX   (72 * 1024) // LEVEL4's are 70 KB

struct Upload
{
    uint32_t src;
    uint32_t dst;
    uint32_t size;
    uint16_t w, h;      // quadrant size, when it uploads as quadrants
};

static uint8_t gData[DATA_MAX] __attribute__((aligned(16)));
static uint32_t gDataSize;

uint8_t gDetailKeys[2][MAX_KEYS];
DetailSlot gDetailSlots[MAX_SLOTS];
static int32_t gSlotCount;
uint32_t gDetailVramStart;
uint32_t gDetailFrame;

static Upload gUploads[MAX_UPLOADS];
static int32_t gUploadCount;

static uint8_t gItemData[ITEM_DATA_MAX] __attribute__((aligned(16)));
static uint32_t gItemDataSize;
static int32_t gItemSlots;  // slots under the item textures while they are in VRAM
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

    memset(gDetailKeys, 0, sizeof(gDetailKeys));

    gDetailFrame = 1;
    for (int32_t i = 0; i < gSlotCount; i++)
    {
        gDetailSlots[i].key = NO_KEY;
        gDetailSlots[i].frame = 0;
    }

    gUploadCount = 0;
    gItemSlots = 0;
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

static void emptySlots()
{
    memset(gDetailKeys, 0, sizeof(gDetailKeys));
    for (int32_t i = 0; i < gSlotCount; i++)
    {
        gDetailSlots[i].key = NO_KEY;
        gDetailSlots[i].frame = 0;
    }
}

uint32_t detailReserveTop(uint32_t size)
{
    int32_t slots = (size + DETAIL_SLOT_SIZE - 1) / DETAIL_SLOT_SIZE;
    if (slots > gSlotCount / 2)
        return 0;

    gSlotCount -= slots;
    emptySlots();
    return detailSlotAddr(gSlotCount);
}

void detailSuspend()
{
    if (gItemSlots)
        return;

    gItemUpload = true;
    gItemSlots = (gItemDataSize + DETAIL_SLOT_SIZE - 1) / DETAIL_SLOT_SIZE;
    if (gItemSlots > gSlotCount) {
        gItemSlots = gSlotCount;
    }
    if (!gItemSlots)
        return;

    for (int32_t i = 0; i < gItemSlots; i++)
    {
        DetailSlot &slot = gDetailSlots[i];
        if (slot.key != NO_KEY) {
            gDetailKeys[slot.key >> 15][slot.key & ~KEY_QUADS] = 0;
        }
        slot.key = NO_KEY;
        slot.frame = IN_USE;
    }

    // and the uploads queued into them
    uint32_t end = VDP1_VRAM + detailSlotAddr(gItemSlots);
    int32_t kept = 0;
    for (int32_t i = 0; i < gUploadCount; i++)
    {
        if (gUploads[i].dst >= end) {
            gUploads[kept++] = gUploads[i];
        }
    }
    gUploadCount = kept;
}

void detailResume()
{
    for (int32_t i = 0; i < gItemSlots; i++) {
        gDetailSlots[i].frame = 0;
    }
    gItemSlots = 0;
}

int32_t detailSlotMiss(uint32_t key, uint32_t texSize, bool quads)
{
    uint32_t w = (texSize >> 8) * 8;
    uint32_t h = texSize & 0xFF;
    uint32_t size = w * h * 2; // four w x h quadrants, 4 bits a texel
    if (key >= MAX_KEYS || key * DETAIL_UNIT + size > gDataSize)
        return -1;

    uint8_t* keySlot = gDetailKeys[quads];

    if (gUploadCount == MAX_UPLOADS)
        return -1;

    // least recently drawn slot not in use this frame
    int32_t victim = -1;
    uint32_t oldest = gDetailFrame;
    for (int32_t i = 0; i < gSlotCount; i++)
    {
        if (gDetailSlots[i].frame < oldest)
        {
            oldest = gDetailSlots[i].frame;
            victim = i;
        }
    }

    if (victim < 0)
        return -1;

    DetailSlot &slot = gDetailSlots[victim];
    if (slot.key != NO_KEY) {
        gDetailKeys[slot.key >> 15][slot.key & ~KEY_QUADS] = 0;
    }
    slot.key = key | (quads ? KEY_QUADS : 0);
    slot.frame = gDetailFrame;
    keySlot[key] = victim + 1;

    Upload &upload = gUploads[gUploadCount++];
    upload.src = uint32_t(gData + key * DETAIL_UNIT);
    upload.dst = VDP1_VRAM + detailSlotAddr(victim);
    upload.size = size;
    upload.w = quads ? w : 0;
    upload.h = h;

    return victim;
}

// One texture's block, the 2w x 2h texture, to dst as four w x h quadrants,
// clockwise from top left.
static void quadrants(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t h)
{
    static const uint8_t ox[4] = { 0, 1, 1, 0 };
    static const uint8_t oy[4] = { 0, 0, 1, 1 };

    uint32_t row = w / 2; // bytes in a quadrant's row
    uint32_t quad = row * h;
    if (quad * 4 > DETAIL_SLOT_SIZE)
        return;

    for (int32_t q = 0; q < 4; q++)
    {
        for (uint32_t r = 0; r < h; r++)
        {
            uint32_t quadOffset = q * quad + r * row;
            uint32_t wholeOffset = (oy[q] * h + r) * row * 2 + ox[q] * row;
            memcpy(dst + quadOffset, src + wholeOffset, row);
        }
    }
}

void detailFlush()
{
    if (gItemUpload && gItemDataSize)
    {
        dmaCopy(gItemData, VDP1_VRAM + gDetailVramStart, gItemDataSize);
        gItemUpload = false;
    }

    // a texture as quadrants is rearranged into one of two buffers, the one
    // the transfer before last read from
    static uint8_t staging[2][DETAIL_SLOT_SIZE] __attribute__((aligned(16)));
    int32_t buffer = 0;

    for (int32_t i = 0; i < gUploadCount; i++)
    {
        const Upload &upload = gUploads[i];
        const uint8_t* src = (const uint8_t*)upload.src;
        if (upload.w)
        {
            quadrants(staging[buffer], src, upload.w, upload.h);
            src = staging[buffer];
            buffer ^= 1;
        }
        dmaCopy(src, upload.dst, upload.size);
    }
    gStats.detailUploads = gUploadCount;
    gUploadCount = 0;
    gDetailFrame++;
}
