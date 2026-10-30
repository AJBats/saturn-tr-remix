// The lab's scene (scene.h).

#include "scene.h"
#include "cd.h"
#include "hw.h"

#include <string.h>

#define LOW_RAM         0x00200000  // the file's low chunks, as read: low work RAM is the lab's
#define LOW_RAM_SIZE    0x00100000
#define SECTOR          2048
#define ARENA_SIZE      (310 * 1024)
#define MAX_CHUNKS      64

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
static uint8_t* gLow;       // low work RAM past the file's low chunks

// The file holds its low chunks first (the chunk table among them), then its
// high ones, then its VDP1 one, each run from a sector's start
// (tools/labscene.py): each run is read straight to where it lives.
const char* sceneLoad(const char* path)
{
    if (cdInit() < 0)
        return "no CD";
    uint32_t lba, size;
    if (cdFind(path, &lba, &size) != 0 || size < SECTOR)
        return "no scene file";
    uint8_t* file = (uint8_t*)LOW_RAM;
    if (cdRead(lba, SECTOR, file) != 0)
        return "the scene file unread";
    if (((const uint32_t*)file)[0] != SCENE_MAGIC || ((const uint32_t*)file)[1] > MAX_CHUNKS)
        return "not a scene";

    // each run's span in the file
    gCount = ((const uint32_t*)file)[1];
    gChunks = (const Chunk*)(file + 8);
    uint32_t from[3] = { size, size, size }, to[3] = { 0, 0, 0 };
    from[WHERE_LOW] = 0;
    to[WHERE_LOW] = 8 + 16 * gCount;
    for (uint32_t i = 0; i < gCount; i++)
    {
        const Chunk &c = gChunks[i];
        if (c.offset + c.size > size || c.where > WHERE_VDP1)
            return "a chunk past the file's end";
        from[c.where] = c.offset < from[c.where] ? c.offset : from[c.where];
        to[c.where] = c.offset + c.size > to[c.where] ? c.offset + c.size : to[c.where];
    }
    if (to[WHERE_LOW] > LOW_RAM_SIZE)
        return "scene too big for low work RAM";
    if (to[WHERE_HIGH] > from[WHERE_HIGH] && to[WHERE_HIGH] - from[WHERE_HIGH] > ARENA_SIZE)
        return "scene too big for high work RAM";
    if (to[WHERE_VDP1] > from[WHERE_VDP1] && to[WHERE_VDP1] - from[WHERE_VDP1] > 0x80000)
        return "VRAM chunk too big";
    if (from[WHERE_HIGH] % SECTOR || from[WHERE_VDP1] % SECTOR)
        return "a run not at a sector's start";

    // the runs where they live
    if (cdRead(lba, to[WHERE_LOW], file) != 0)
        return "the scene file unread";
    gUsed = 0;
    if (to[WHERE_HIGH] > from[WHERE_HIGH])
    {
        if (cdRead(lba + from[WHERE_HIGH] / SECTOR, to[WHERE_HIGH] - from[WHERE_HIGH], gArena) != 0)
            return "the scene file unread";
        gUsed = (to[WHERE_HIGH] - from[WHERE_HIGH] + 15) & ~15;
    }
    if (to[WHERE_VDP1] > from[WHERE_VDP1] &&
        cdRead(lba + from[WHERE_VDP1] / SECTOR, to[WHERE_VDP1] - from[WHERE_VDP1], (void*)VDP1_VRAM) != 0)
        return "the scene file unread";
    for (uint32_t i = 0; i < gCount; i++)
    {
        const Chunk &c = gChunks[i];
        gData[i] = c.where == WHERE_LOW ? file + c.offset
                 : c.where == WHERE_HIGH ? gArena + (c.offset - from[WHERE_HIGH]) : nullptr;
    }
    gLow = file + ((to[WHERE_LOW] + 15) & ~15);
    return nullptr;
}

void* sceneAllocLow(uint32_t bytes)
{
    if (gLow + bytes > (uint8_t*)(LOW_RAM + LOW_RAM_SIZE))
        return nullptr;
    void* p = gLow;
    gLow += (bytes + 15) & ~15;
    return p;
}

void* sceneAlloc(uint32_t bytes)
{
    if (gUsed + bytes > ARENA_SIZE)
        return nullptr;
    void* p = gArena + gUsed;
    gUsed = (gUsed + bytes + 15) & ~15;
    return p;
}

uint32_t sceneUsedLow(uint32_t* size)
{
    if (size) {
        *size = LOW_RAM_SIZE;
    }
    return gLow - (uint8_t*)LOW_RAM;
}

uint32_t sceneUsed(uint32_t* size)
{
    if (size) {
        *size = ARENA_SIZE;
    }
    return gUsed;
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
