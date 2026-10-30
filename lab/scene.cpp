// The lab's scene (scene.h).

#include "scene.h"
#include "cd.h"
#include "hw.h"

#include <string.h>

#define LOW_RAM         0x00200000  // the file, as read: low work RAM is the lab's
#define LOW_RAM_SIZE    0x00100000
#define ARENA_SIZE      (288 * 1024)
#define MAX_CHUNKS      32

enum Where { WHERE_HIGH, WHERE_LOW, WHERE_VDP1 };

struct Chunk
{
    char id[4];
    uint32_t offset, size, where;
};

static uint8_t gArena[ARENA_SIZE] __attribute__((aligned(16)));
static const Chunk* gChunks;
static uint32_t gCount;
static uint8_t* gData[MAX_CHUNKS];
static uint32_t gUsed;

const char* sceneLoad(const char* path)
{
    if (cdInit() < 0)
        return "no CD";
    uint8_t* file = (uint8_t*)LOW_RAM;
    int32_t size = cdLoadFile(path, file, LOW_RAM_SIZE);
    if (size < 8)
        return "no scene file";
    if (((const uint32_t*)file)[0] != SCENE_MAGIC || ((const uint32_t*)file)[1] > MAX_CHUNKS)
        return "not a scene";

    gCount = ((const uint32_t*)file)[1];
    gChunks = (const Chunk*)(file + 8);
    uint32_t used = 0;
    for (uint32_t i = 0; i < gCount; i++)
    {
        const Chunk &c = gChunks[i];
        if (c.offset + c.size > uint32_t(size))
            return "a chunk past the file's end";
        uint8_t* src = file + c.offset;
        if (c.where == WHERE_VDP1)
        {
            if (c.size > 0x80000)
                return "VRAM chunk too big";
            volatile uint32_t* dst = (volatile uint32_t*)VDP1_VRAM;
            for (uint32_t k = 0; k < c.size / 4; k++) {
                dst[k] = ((const uint32_t*)src)[k];
            }
            gData[i] = nullptr;
        }
        else if (c.where == WHERE_LOW)
        {
            gData[i] = src;
        }
        else
        {
            if (used + c.size > ARENA_SIZE)
                return "scene too big for high work RAM";
            gData[i] = gArena + used;
            memcpy(gData[i], src, c.size);
            used = (used + c.size + 15) & ~15;
        }
    }
    gUsed = used;
    return nullptr;
}

void* sceneAlloc(uint32_t bytes)
{
    if (gUsed + bytes > ARENA_SIZE)
        return nullptr;
    void* p = gArena + gUsed;
    gUsed = (gUsed + bytes + 15) & ~15;
    return p;
}

const void* sceneChunk(const char* id, uint32_t* bytes)
{
    for (uint32_t i = 0; i < gCount; i++)
    {
        if (!memcmp(gChunks[i].id, id, 4) && gData[i])
        {
            if (bytes) {
                *bytes = gChunks[i].size;
            }
            return gData[i];
        }
    }
    if (bytes) {
        *bytes = 0;
    }
    return nullptr;
}
