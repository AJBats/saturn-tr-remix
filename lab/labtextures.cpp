// The room texture cache from the scene, and its checks (labrender.h).

#include "labrender.h"
#include "hw.h"

#include <string.h>

// The room texture set's layout (RTXI).
struct TextureLayout
{
    uint32_t levels[3];     // each level's offset in the set: full res, half, quarter
    uint32_t dataSize;      // the full-res set's bytes (detail.cpp gDataSize)
    int32_t pools[3];       // each level's pool's slots (gPoolCount)
};

static const uint8_t (*gKeysFound)[DETAIL_MAX_KEYS];    // as the frame found it: DKY0
static const DetailSlot* gSlotsFound;               // DSL0
static uint8_t (*gKeysLeft)[DETAIL_MAX_KEYS];       // as the game's frame left it (low work RAM)
static DetailSlot* gSlotsLeft;
static TextureQueue gCaptured;
static uint8_t* gStaging;          // a texture as staged, for the VRAM check (low work RAM)

void texturesLabInit()
{
    const SceneInfo &info = sceneInfo();
    const TextureLayout* layout = sceneArray<const TextureLayout>("RTXI");
    TextureSet set;
    set.data = sceneArray<const uint8_t>("RTEX");
    for (int32_t l = 0; l < 3; l++)
    {
        set.levels[l] = layout->levels[l];
        set.pools[l] = layout->pools[l];
        set.poolVram[l] = info.poolVram[l];
    }
    set.dataSize = layout->dataSize;
    texturesInit(set);
    memcpy(texturesKeys(), sceneChunk("DKEY"), DETAIL_VARIANTS * DETAIL_MAX_KEYS);
    memcpy(texturesSlots(), sceneChunk("DSLT"), DETAIL_SLOTS * sizeof(DetailSlot));
    gKeysFound = (const uint8_t (*)[DETAIL_MAX_KEYS])sceneChunk("DKY0");
    gSlotsFound = sceneArray<const DetailSlot>("DSL0");
    gKeysLeft = (uint8_t (*)[DETAIL_MAX_KEYS])sceneAllocLow(DETAIL_VARIANTS * DETAIL_MAX_KEYS);
    gSlotsLeft = (DetailSlot*)sceneAllocLow(DETAIL_SLOTS * sizeof(DetailSlot));
    memcpy(gKeysLeft, texturesKeys(), DETAIL_VARIANTS * DETAIL_MAX_KEYS);
    memcpy(gSlotsLeft, texturesSlots(), DETAIL_SLOTS * sizeof(DetailSlot));
    memcpy(&gCaptured, sceneChunk("DUPL"), sizeof(gCaptured));
    gStaging = (uint8_t*)sceneAllocLow(DETAIL_SLOT_SIZE);
}

void texturesReset()
{
    memcpy(texturesKeys(), gKeysFound, DETAIL_VARIANTS * DETAIL_MAX_KEYS);
    memcpy(texturesSlots(), gSlotsFound, DETAIL_SLOTS * sizeof(DetailSlot));
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
    const uint8_t (*keys)[DETAIL_MAX_KEYS] = texturesKeys();
    const DetailSlot* slots = texturesSlots();
    int32_t off = 0;
    for (int32_t v = 0; v < DETAIL_VARIANTS; v++) {
        for (int32_t k = 0; k < DETAIL_MAX_KEYS; k++) {
            off += keys[v][k] != gKeysLeft[v][k];
        }
    }
    for (int32_t i = 0; i < DETAIL_SLOTS; i++) {
        off += slots[i].key != gSlotsLeft[i].key || slots[i].frame != gSlotsLeft[i].frame;
    }
    return off;
}

int32_t texturesVramOff(const TextureQueue &queue)
{
    int32_t off = 0;
    for (int32_t i = 0; i < queue.count; i++)
    {
        const TextureUpload &upload = queue.uploads[i];
        uint32_t size = texturesStaged(gStaging, upload);
        const volatile uint32_t* vram = (const volatile uint32_t*)(VDP1_VRAM + (upload.dst & 0x00FFFFFF));
        const uint32_t* want = (const uint32_t*)gStaging;
        for (uint32_t k = 0; k < size / 4; k++) {
            off += vram[k] != want[k];
        }
    }
    return off;
}
