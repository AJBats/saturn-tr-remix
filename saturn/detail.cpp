// Room texture cache (see detail.h).

#include "detail.h"
#include "common.h"
#include "hw.h"
#include "system.h"
#include "crash.h"

#define MAX_KEYS        DETAIL_MAX_KEYS
#define MAX_UPLOADS     DETAIL_MAX_UPLOADS  // per frame, 2 KB at most each
#define NO_KEY          0xFFFF
#define IN_USE          0xFFFFFFFF  // DetailSlot.frame of a slot under the item textures
#define VARIANT_SHIFT   12      // DetailSlot.key
#define KEY_MASK        0xFFF
#define ITEM_DATA_MAX   (72 * 1024) // LEVEL4's are 70 KB
#define MIP_BYTES       (DETAIL_MIP_SLOTS * (DETAIL_SLOT_SIZE / 4 + DETAIL_SLOT_SIZE / 16))

struct Upload
{
    uint32_t src;
    uint32_t dst;       // VDP1 VRAM offset, the variant in the top byte
    uint16_t w, h;      // the texture's at its level
};
#define UPLOAD_OFFSET   0x00FFFFFF
#define UPLOAD_VARIANT  24

// The set's levels, in low work RAM, which SCU DMA can't read; each holds a
// texture at its full-res offset >> 2 * level.
static const uint8_t* gData[DETAIL_LEVELS];
static uint32_t gDataSize;  // the full-res set's

uint8_t gDetailKeys[DETAIL_VARIANTS][MAX_KEYS];
DetailSlot gDetailSlots[DETAIL_SLOTS];
static int32_t gPoolCount[DETAIL_LEVELS];
static const uint32_t gPoolSlotSize[DETAIL_LEVELS] = { DETAIL_SLOT_SIZE, DETAIL_SLOT_SIZE / 4, DETAIL_SLOT_SIZE / 16 };
uint32_t gDetailVramStart;
uint32_t gDetailPoolVram[DETAIL_LEVELS];
uint32_t gDetailFrame;

// The uploads the frame's faces queue, and the last ended frame's, which wait
// for VDP1 to finish the list before theirs (detailUpload).
static Upload gUploads[2][MAX_UPLOADS];
static int32_t gUploadCount[2];
static int32_t gUploadList;     // the faces'; the other is the last frame's

static uint8_t gItemData[ITEM_DATA_MAX] __attribute__((aligned(16)));
static uint32_t gItemDataSize;
static int32_t gItemSlots;  // slots under the item textures while they are in VRAM
static bool gItemUpload;    // item textures go to VRAM at the next flush

void detailInit(uint32_t vramStart, const uint8_t* data, uint32_t dataSize)
{
    gDetailVramStart = (vramStart + 31) & ~31; // as tools/pack_level.py places item textures

    // the full-res set and its mip levels, 1 + 1/4 + 1/16 of it
    uint32_t full = dataSize / 21 * 16;
    bool mips = full % DETAIL_UNIT == 0 && full / 16 * 21 == dataSize;
    if (!mips) {
        full = dataSize;
    }
    gDataSize = X_MIN(full, uint32_t(DETAIL_DATA_MAX));
    gData[0] = data;
    gData[1] = data + full;
    gData[2] = gData[1] + full / 4;

    int32_t slots = (0x80000 - gDetailVramStart) / DETAIL_SLOT_SIZE;
    if (mips && slots - MIP_BYTES / DETAIL_SLOT_SIZE >= DETAIL_MIN_FULL_SLOTS)
    {
        slots -= MIP_BYTES / DETAIL_SLOT_SIZE;
        gPoolCount[1] = gPoolCount[2] = DETAIL_MIP_SLOTS;
    }
    else
    {
        gPoolCount[1] = gPoolCount[2] = 0;
    }
    gPoolCount[0] = X_MIN(slots, DETAIL_FULL_SLOTS);
    gDetailPoolVram[0] = gDetailVramStart;
    gDetailPoolVram[1] = gDetailVramStart + slots * DETAIL_SLOT_SIZE;
    gDetailPoolVram[2] = gDetailPoolVram[1] + DETAIL_MIP_SLOTS * gPoolSlotSize[1];
    crashLog("DETAIL", (gPoolCount[0] << 16) | gPoolCount[1]);

    memset(gDetailKeys, 0, sizeof(gDetailKeys));

    gDetailFrame = 1;
    for (int32_t i = 0; i < DETAIL_SLOTS; i++)
    {
        gDetailSlots[i].key = NO_KEY;
        gDetailSlots[i].frame = 0;
    }

    gUploadCount[0] = gUploadCount[1] = 0;
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

bool detailMips()
{
    return gPoolCount[1] != 0;
}

static void emptySlots()
{
    memset(gDetailKeys, 0, sizeof(gDetailKeys));
    for (int32_t i = 0; i < DETAIL_SLOTS; i++)
    {
        gDetailSlots[i].key = NO_KEY;
        gDetailSlots[i].frame = 0;
    }
}

uint32_t detailReserveTop(uint32_t size)
{
    int32_t slots = (size + DETAIL_SLOT_SIZE - 1) / DETAIL_SLOT_SIZE;
    if (slots > gPoolCount[0] / 2)
        return 0;

    gPoolCount[0] -= slots;
    emptySlots();
    return detailSlotAddr(gPoolCount[0]);
}

uint32_t detailSpareBytes()
{
    return gPoolCount[0] > DETAIL_MIN_FULL_SLOTS ? (gPoolCount[0] - DETAIL_MIN_FULL_SLOTS) * DETAIL_SLOT_SIZE : 0;
}

void detailSuspend()
{
    if (gItemSlots)
        return;

    gItemUpload = true;
    gItemSlots = (gItemDataSize + DETAIL_SLOT_SIZE - 1) / DETAIL_SLOT_SIZE;
    if (gItemSlots > gPoolCount[0]) {
        gItemSlots = gPoolCount[0];
    }
    if (!gItemSlots)
        return;

    for (int32_t i = 0; i < gItemSlots; i++)
    {
        DetailSlot &slot = gDetailSlots[i];
        if (slot.key != NO_KEY) {
            gDetailKeys[slot.key >> VARIANT_SHIFT][slot.key & KEY_MASK] = 0;
        }
        slot.key = NO_KEY;
        slot.frame = IN_USE;
    }

    // and the uploads queued into them
    uint32_t end = detailSlotAddr(gItemSlots);
    Upload* uploads = gUploads[gUploadList];
    int32_t kept = 0;
    for (int32_t i = 0; i < gUploadCount[gUploadList]; i++)
    {
        if ((uploads[i].dst & UPLOAD_OFFSET) >= end) {
            uploads[kept++] = uploads[i];
        }
    }
    gUploadCount[gUploadList] = kept;
}

void detailResume()
{
    for (int32_t i = 0; i < gItemSlots; i++) {
        gDetailSlots[i].frame = 0;
    }
    gItemSlots = 0;
}

int32_t detailSlotMiss(uint32_t key, uint32_t texSize, uint32_t variant)
{
    uint32_t w = (texSize >> 8) * 16;   // the whole texture's
    uint32_t h = (texSize & 0xFF) * 2;
    if (key >= MAX_KEYS || key * DETAIL_UNIT + w * h / 2 > gDataSize)
        return -1;

    uint32_t level = detailLevel(variant);
    w >>= level;
    h >>= level;

    int32_t &count = gUploadCount[gUploadList];
    if (count == MAX_UPLOADS)
        return -1;

    // least recently drawn slot of the level's pool not in use this frame
    DetailSlot* pool = gDetailSlots + detailPoolStart(level);
    int32_t victim = -1;
    uint32_t oldest = gDetailFrame;
    for (int32_t i = 0; i < gPoolCount[level]; i++)
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
        gDetailKeys[slot.key >> VARIANT_SHIFT][slot.key & KEY_MASK] = 0;
    }
    slot.key = key | (variant << VARIANT_SHIFT);
    slot.frame = gDetailFrame;
    gDetailKeys[variant][key] = victim + 1;

    Upload &upload = gUploads[gUploadList][count++];
    upload.src = uint32_t(gData[level] + ((key * DETAIL_UNIT) >> (2 * level)));
    upload.dst = (gDetailPoolVram[level] + victim * gPoolSlotSize[level]) | (variant << UPLOAD_VARIANT);
    upload.w = w;
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

// By 8 x 8 blocks, each eight 32-bit rows of eight texels, whose off-diagonal
// halves, then quarters, then single texels change places.
void detailTranspose(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t h)
{

    const uint32_t* s = (const uint32_t*)src;
    uint32_t* d = (uint32_t*)dst;
    uint32_t srcRow = w / 8; // words in a row
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

const void* detailQueued(int32_t &count)
{
    count = gUploadCount[gUploadList];
    return gUploads[gUploadList];
}

void detailFrameEnd()
{
    gUploadList ^= 1;
    gUploadCount[gUploadList] = 0;
    gDetailFrame++;
}

void detailUpload()
{
    if (gItemUpload && gItemDataSize)
    {
        dmaCopy(gItemData, VDP1_VRAM + gDetailVramStart, gItemDataSize);
        gItemUpload = false;
    }

    // each texture goes through one of two buffers in high work RAM (the one
    // the transfer before last read from), copied, rearranged into quadrants
    // or transposed
    static uint8_t staging[2][DETAIL_SLOT_SIZE] __attribute__((aligned(16)));
    int32_t buffer = 0;

    int32_t list = gUploadList ^ 1;
    for (int32_t i = 0; i < gUploadCount[list]; i++)
    {
        const Upload &upload = gUploads[list][i];
        const uint8_t* src = (const uint8_t*)upload.src;
        uint8_t* dst = staging[buffer];
        uint32_t variant = upload.dst >> UPLOAD_VARIANT;
        uint32_t size = upload.w * upload.h / 2;
        if (variant == DETAIL_QUADS) {
            quadrants(dst, src, upload.w / 2, upload.h / 2);
        } else if (variant == DETAIL_TURNED || variant == DETAIL_MIP1_TURNED || variant == DETAIL_MIP2_TURNED) {
            detailTranspose(dst, src, upload.w, upload.h);
        } else {
            memcpy(dst, src, size);
        }
        buffer ^= 1;
        dmaCopy(dst, VDP1_VRAM + (upload.dst & UPLOAD_OFFSET), size);
    }
    gStats.detailUploads = gUploadCount[list];
    gUploadCount[list] = 0;
}
