#ifndef H_LEVEL
#define H_LEVEL

#include "common.h"
#include "stream.h"

Level level;

#ifdef __32X__
    extern uint8 gLightmap[256 * 32]; // SDRAM 8k at 0x6000000
#else
    #ifndef MODEHW
        IWRAM_DATA uint8 gLightmap[256 * 32]; // IWRAM 8k
    #endif
#endif

EWRAM_DATA ItemObj items[MAX_ITEMS];

#ifdef ROM_READ
EWRAM_DATA Texture textures[MAX_TEXTURES]; // animated textures require memory swap
EWRAM_DATA Sprite sprites[MAX_SPRITES];
EWRAM_DATA FixedCamera cameras[MAX_CAMERAS];
EWRAM_DATA Box boxes[MAX_BOXES];
#endif

EWRAM_DATA Room rooms[MAX_ROOMS];
EWRAM_DATA Model models[MAX_MODELS];
EWRAM_DATA const Mesh* meshes[MAX_MESHES];
EWRAM_DATA StaticMesh staticMeshes[MAX_STATIC_MESHES];

EWRAM_DATA ItemObj* ItemObj::sFirstActive;
EWRAM_DATA ItemObj* ItemObj::sFirstFree;

EWRAM_DATA int32 gBrightness;

#if (USE_FMT & (LVL_FMT_PHD | LVL_FMT_PSX))
    uint8 gLevelData[2 * 1024 * 1024];
#endif

#if (USE_FMT & LVL_FMT_PKD)
    #include "fmt/pkd.h"
#endif

#if (USE_FMT & LVL_FMT_PHD)
    #include "fmt/phd.h"
#endif

#if (USE_FMT & LVL_FMT_PSX)
    #include "fmt/psx.h"
#endif

bool readLevelStream(DataStream& f)
{
#ifdef CPU_BIG_ENDIAN
    f.bigEndian = true;
#endif

#if (USE_FMT & LVL_FMT_PKD)
    if (read_PKD(f))
        return true;
#endif

#if (USE_FMT & LVL_FMT_PHD)
    if (read_PHD(f))
        return true;
#endif

#if (USE_FMT & LVL_FMT_PSX)
    if (read_PSX(f))
        return true;
#endif

    //LOG("Unsupported level format\n");
    ASSERT(false);

    return false;
}

#ifdef USE_VRAM_MESH
struct MeshVRAM
{
    const Mesh* meshROM;
    const Mesh* meshVRAM;
};

MeshVRAM* vramMeshes = (MeshVRAM*)gSpheres; // use temporary memory
int32 vramMeshesCount;

uint8* pushToVRAM(uint8* ptr, Model* model)
{
    for (int32 i = 0; i < model->count; i++)
    {
        const Mesh* mesh = meshes[model->start + i];

        int32 vramMeshIndex = -1;

        for (int32 i = 0; i < vramMeshesCount; i++)
        {
            if (vramMeshes[i].meshROM == mesh)
            {
                vramMeshIndex = i;
                break;
            }
        }

        if (vramMeshIndex == -1)
        {
            int32 meshSize = sizeof(Mesh) + 
                mesh->vCount * sizeof(MeshVertex) +
                mesh->rCount * sizeof(MeshQuad) +
                mesh->tCount * sizeof(MeshTriangle);

            if (meshSize & 3)
            {
                meshSize += 2;
            }

            vramMeshes[vramMeshesCount].meshROM = mesh;
            vramMeshes[vramMeshesCount].meshVRAM = (Mesh*)ptr;
            vramMeshIndex = vramMeshesCount++;

            memcpy(ptr, mesh, meshSize);

            ptr += meshSize;
        }

        meshes[level.meshesCount + i] = vramMeshes[vramMeshIndex].meshVRAM;
    }
    model->start = level.meshesCount;
    level.meshesCount += model->count;

    return ptr;
}
#endif

void readLevel(const uint8* data)
{
//#ifdef ROM_READ
    dynSectorsCount = 0;
//#endif

    memset(&level, 0, sizeof(level));

    gAnimTexFrame = 0;

    DataStream f(data, 0);
    readLevelStream(f);
#ifdef __SATURN__
    level.meshes = (const Mesh**)osPlaceMeshes(level.meshes, (uint8*)level.meshOffsets - (uint8*)level.meshes);
#endif

    // prepare models // TODO prerocess
    memset(models, 0, sizeof(models));
    for (int32 i = 0; i < level.modelsCount; i++)
    {
        const Model* model = level.models + i;
        ASSERT(model->type < MAX_MODELS);
        models[model->type] = *model;
    }
    level.models = models;

    // prepare meshes
    for (int32 i = 0; i < level.meshesCount; i++)
    {
        meshes[i] = (Mesh*)((uint8*)level.meshes + level.meshOffsets[i]);
    }
    level.meshes = meshes;

    //  prepare static meshes // TODO preprocess
    memset(staticMeshes, 0, sizeof(staticMeshes));
    for (int32 i = 0; i < level.staticMeshesCount; i++)
    {
        const StaticMesh* staticMesh = level.staticMeshes + i;

        ASSERT(staticMesh->id < MAX_STATIC_MESHES);
        staticMeshes[staticMesh->id] = *staticMesh;
    }
    level.staticMeshes = staticMeshes;

    // prepare sprites // TODO preprocess
    for (int32 i = 0; i < level.spriteSequencesCount; i++)
    {
        const SpriteSeq* spriteSeq = level.spriteSequences + i;

        if (spriteSeq->type >= TR1_ITEM_MAX) // WTF?
            continue;

        Model* m = models + spriteSeq->type;
        m->count = int8(spriteSeq->count);
        m->start = spriteSeq->start;
    }

// experimental
#if defined(USE_VRAM_MESH) || defined(USE_VRAM_ROOM)
    vramPtr = (uint8*)0x06014000;
#endif

#ifdef USE_VRAM_MESH // should be per level or dynamic
    vramMeshesCount = 0;
    vramPtr = pushToVRAM(vramPtr, models + ITEM_LARA);
    vramPtr = pushToVRAM(vramPtr, models + ITEM_LARA_PISTOLS);
    vramPtr = pushToVRAM(vramPtr, models + ITEM_LARA_SHOTGUN);
    //vramPtr = pushToVRAM(vramPtr, models + ITEM_LARA_MAGNUMS);
    //vramPtr = pushToVRAM(vramPtr, models + ITEM_LARA_UZIS);
    vramPtr = pushToVRAM(vramPtr, models + ITEM_WOLF);
    vramPtr = pushToVRAM(vramPtr, models + ITEM_BAT);
    vramPtr = pushToVRAM(vramPtr, models + ITEM_BRIDGE_FLAT);
    vramPtr = pushToVRAM(vramPtr, models + ITEM_BRIDGE_TILT_1);
    vramPtr = pushToVRAM(vramPtr, models + ITEM_BRIDGE_TILT_2);
    //printf("%d\n", vramPtr - (uint8*)0x06014000);
#endif
}

void animTexturesShift()
{
    const uint16* data = level.animTexData;

    int32 texRangesCount = *data++;

    for (int32 i = 0; i < texRangesCount; i++)
    {
        int32 count = *data++;

        Texture tmp = level.textures[*data];
        while (count > 0)
        {
            level.textures[data[0]] = level.textures[data[1]];
            data++;
            count--;
        }
        level.textures[*data++] = tmp;
    }
}

#define FADING_RATE_SHIFT 4

void updateFading(int32 frames)
{
    if (gBrightness == 0)
        return;

    frames <<= FADING_RATE_SHIFT;

    if (gBrightness < 0)
    {
        gBrightness += frames;
        if (gBrightness > 0) {
            gBrightness = 0;
        }
    }

    if (gBrightness > 0)
    {
        gBrightness -= frames;
        if (gBrightness < 0) {
            gBrightness = 0;
        }
    }

    palSet(level.palette, gSettings.video_gamma << 4, gBrightness);
}

// Swaps every room that has an alternate with it: geometry, sectors and
// portals change, the items stay. Resting blocks raise the floor they stand on,
// so they lift it from the old version and put it on the new.
static void flipBlocks(Room* room, int32 offset)
{
    for (ItemObj* item = room->firstItem; item; item = item->nextItem)
    {
        if (item->type < ITEM_BLOCK_1 || item->type > ITEM_BLOCK_4)
            continue;

        uint32 status = item->flags & ITEM_FLAG_STATUS;
        if (status == ITEM_FLAG_STATUS_INVISIBLE || status == ITEM_FLAG_STATUS_ACTIVE)
            continue;

        ((Block*)item)->updateFloor(offset);
    }
}

void flipMap()
{
    for (int32 i = 0; i < level.roomsCount; i++)
    {
        Room* room = rooms + i;
        if (room->alternateRoom == NO_ROOM)
            continue;

        Room* alt = rooms + room->alternateRoom;

        flipBlocks(room, 1024);

        X_SWAP(room->info, alt->info);
        X_SWAP(room->sectors, alt->sectors);
        X_SWAP(room->data, alt->data);

        flipBlocks(room, -1024);
    }

    gSaveGame.flipped ^= 1;
}

void shakeCamera(int32 frames, bool add)
{
    for (int32 i = 0; i < MAX_PLAYERS; i++)
    {
        if (!players[i])
            continue;

        Camera &camera = players[i]->extraL->camera;
        camera.shake = add ? camera.shake + frames : frames;
    }
}

// Level effects, from triggers and animations (OpenLara's Level::setEffect
// and updateEffect), in 30 Hz frames.
static int32 gEffect = FX_NONE;
static int32 gEffectTimer;
static int32 gEffectIdx;

void setEffect(int32 effect, const ItemObj* item)
{
    gEffect = effect;
    gEffectTimer = 0;
    gEffectIdx = 0;

    switch (effect)
    {
        case FX_FLOOR_SHAKE:
        {
            // up to half a second, fading out 15 sectors away
            const int32 range = (15 * 1024) >> 4;
            for (int32 i = 0; i < MAX_PLAYERS; i++)
            {
                if (!players[i] || !item)
                    continue;

                Camera &camera = players[i]->extraL->camera;
                int32 dx = (item->pos.x - camera.view.pos.x) >> 4;
                int32 dy = (item->pos.y - camera.view.pos.y) >> 4;
                int32 dz = (item->pos.z - camera.view.pos.z) >> 4;
                if (X_MAX(X_MAX(abs(dx), abs(dy)), abs(dz)) >= range)
                    continue;

                int32 d2 = dx * dx + dy * dy + dz * dz;
                camera.shake = X_MAX(0, 15 * (range * range - d2) / (range * range));
            }
            gEffect = FX_NONE;
            break;
        }

        case FX_FLOOD:
            soundPlay(SND_FLOOD, NULL);
            break;

        case FX_STAIRS2SLOPE:
            soundPlay(SND_STAIRS2SLOPE, NULL);
            break;

        case FX_EXPLOSION:
            soundPlay(SND_TNT, NULL);
            shakeCamera(30, false);
            break;
    }
}

static void updateEffect(int32 frames)
{
    if (gEffect == FX_NONE)
        return;

    gEffectTimer += frames;

    switch (gEffect)
    {
        case FX_TR1_FLICKER:
        {
            // the lights go out and come back: the rooms flip back and forth
            static const uint8 times[] = { 90, 93, 105, 108, 123 };
            while (gEffectIdx < 5 && gEffectTimer > times[gEffectIdx])
            {
                gEffectIdx++;
                flipMap();
            }
            if (gEffectIdx == 5) {
                gEffect = FX_NONE;
            }
            break;
        }

        case FX_EARTHQUAKE:
        {
            static const struct { uint8 time; int16 sound; uint8 shake; } steps[] = {
                {  0, SND_ROCK,    30 },
                {  3, SND_STOMP,   0  },
                { 18, SND_BOULDER, 15 },
                { 33, SND_ROCK,    0  },
                { 48, SND_BOULDER, 15 },
                { 69, SND_BOULDER, 15 },
            };
            while (gEffectIdx < 6 && gEffectTimer > steps[gEffectIdx].time)
            {
                soundPlay(steps[gEffectIdx].sound, NULL);
                if (steps[gEffectIdx].shake) {
                    shakeCamera(steps[gEffectIdx].shake, gEffectIdx > 0);
                }
                gEffectIdx++;
            }
            if (gEffectIdx == 6) {
                gEffect = FX_NONE;
            }
            break;
        }

        default:
            gEffect = FX_NONE;
    }
}

void updateLevel(int32 frames)
{
    updateFading(frames);
    updateEffect(frames);

    gCausticsFrame += frames;

    gAnimTexFrame += frames;
    while (gAnimTexFrame > 5)
    {
        animTexturesShift();
        gAnimTexFrame -= 5;
    }
}

int32 getAmbientTrack()
{
    return gLevelInfo[gLevelID].track;
}

bool isCutsceneLevel()
{
    return (gLevelID == LVL_TR1_CUT_1) ||
           (gLevelID == LVL_TR1_CUT_2) ||
           (gLevelID == LVL_TR1_CUT_3) ||
           (gLevelID == LVL_TR1_CUT_4);
}

#endif
