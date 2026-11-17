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
EWRAM_DATA int32 ItemObj::sFreeCount;

EWRAM_DATA int32 gBrightness;

// What the updates changed that the renderer reads, held for the frame's top
// (levelCommit), where no frame is being drawn from it: the animated
// textures' shifts, and the flips' rooms as the renderer has them.
int32 gTexShiftsDue;
int32 gFlipsDue;

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
    memset(&level, 0, sizeof(level));

    gAnimTexFrame = 0;
    gTexShiftsDue = 0;
    gFlipsDue = 0;

    DataStream f(data, 0);
    readLevelStream(f);
#ifdef __SATURN__
    osPlaceRooms();
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
#ifdef __SATURN__
    renderTexturesShifted();
#endif
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

        alterFloor(item, offset);
    }
}

void flipMap()
{
    soundLoopsStop();

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
    gFlipsDue++;
}

// The texture shifts and flips the updates made since the last call, applied
// to what the renderer reads. The game's main loop calls it at the frame's
// top: on the Saturn the next frame's update runs on the slave while this
// frame is drawn (saturn/pipeline.cpp).
void levelCommit()
{
    for (; gTexShiftsDue > 0; gTexShiftsDue--) {
        animTexturesShift();
    }
    if (gFlipsDue)
    {
        gFlipsDue = 0;
#ifdef __SATURN__
        osRoomFacesFlipped();
        renderRoomsFlipped();
#endif
    }
}

void cameraBounce(int32 bounce)
{
    for (int32 i = 0; i < MAX_PLAYERS; i++)
    {
        if (players[i]) {
            players[i]->extraL->camera.bounce = bounce;
        }
    }
}

// The camera dropping as something heavy lands (retail's DinoStomp, 060159b8):
// by up to 100 for a tick, less with the square of its distance from the
// camera, none 16 sectors away.
void floorShake(const ItemObj* item)
{
    for (int32 i = 0; i < MAX_PLAYERS; i++)
    {
        if (!players[i])
            continue;

        Camera &camera = players[i]->extraL->camera;
        int32 dx = item->pos.x - camera.view.pos.x;
        int32 dy = item->pos.y - camera.view.pos.y;
        int32 dz = item->pos.z - camera.view.pos.z;
        if (abs(dx) >= 16 * 1024 || abs(dy) >= 16 * 1024 || abs(dz) >= 16 * 1024)
            continue;

        int32 d2 = (dx * dx + dy * dy + dz * dz) / 256;
        camera.bounce = (0x100000 - d2) * 100 / 0x100000;
    }
}

// The level's running effect (retail's flip effect, 06060c08, and its timer
// 06060c0c): set by a trigger along with a flip of the rooms, then run once a
// tick until it ends (retail runs it from SoundEffects, 06014fbc; its routines
// are at 0x06060c64).
static int32 gEffect = FX_NONE;
static int32 gEffectTimer;

void setEffect(int32 effect, const ItemObj* item)
{
    if (effect == FX_FLOOR_SHAKE)   // an animation's, at once
    {
        if (item) {
            floorShake(item);
        }
        return;
    }

    gEffect = effect;
    gEffectTimer = 0;
}

// A sound rising away below the camera's target, 100 a tick (drop sand,
// 06015c70; power up, 06015ce4).
static void soundBelowTarget(int16 id, int32 t)
{
    vec3i pos = players[0]->extraL->camera.target.pos;
    pos.y += t * 100;
    soundPlay(id, &pos);
}

static void updateEffect()
{
    if (gEffect == FX_NONE || !players[0])
        return;

    int32 t = gEffectTimer++;
    bool done = false;

    switch (gEffect)
    {
        case FX_EARTHQUAKE:     // 06015ac0
        {
            if (t == 0) {
                soundPlay(SND_ROCK, NULL);
                cameraBounce(-250);
            } else if (t == 3) {
                soundPlay(SND_STOMP, NULL);
            } else if (t == 35) {
                soundPlay(SND_ROCK, NULL);
            } else if (t == 20 || t == 50 || t == 70) {
                soundPlay(SND_BOULDER, NULL);
            }
            done = (t == 104);
            break;
        }

        case FX_FLOOD:          // 06015b44
        {
            // the roar for 4 s at Lara, from 3000 below the camera's target up
            // to it in a second, then down again, so it swells and fades
            done = (t >= 121);
            if (!done)
            {
                vec3i pos = players[0]->pos;
                pos.y = players[0]->extraL->camera.target.pos.y + 100 * ((t < 30) ? 30 - t : t - 30);
                soundPlay(SND_FLOOD, &pos);
            }
            break;
        }

        case FX_RAISING_BLOCK:  // 06015bbc
        {
            soundPlay(SND_RAISING_BLOCK, NULL);
            done = true;
            break;
        }

        case FX_STAIRS2SLOPE:   // 06015c34
        {
            if (t == 5) {
                soundPlay(SND_STAIRS2SLOPE, NULL);
                done = true;
            }
            break;
        }

        case FX_DROP_SAND:      // 06015c70
        {
            done = (t >= 121);
            if (!done)
            {
                if (t == 0) {
                    soundPlay(SND_SAND_DROP, NULL);
                }
                soundBelowTarget(SND_SAND, t);
            }
            break;
        }

        case FX_POWER_UP:       // 06015ce4
        {
            done = (t >= 121);
            if (!done) {
                soundBelowTarget(SND_POWER_UP, t);
            }
            break;
        }

        case FX_EXPLOSION:      // 06015d44
        {
            soundPlay(SND_TNT, NULL);
            cameraBounce(-75);
            done = true;
            break;
        }

        case FX_TR1_CHAIN_BLOCK:    // 06015be4
        {
            if (t == 0) {
                soundPlay(SND_CHAIN_BLOCK, NULL);
            }
            if (t == 54) {
                soundPlay(SND_SPLASH, NULL);
                done = true;
            }
            break;
        }

        case FX_TR1_FLICKER:    // OpenLara's (PC TR1's; not in the Saturn's table)
        {
            // the lights go out and come back: the rooms flip back and forth
            done = (t == 123);
            if (t == 90 || t == 93 || t == 105 || t == 108 || done) {
                flipMap();
            }
            break;
        }

        default:
            done = true;
    }

    if (done) {
        gEffect = FX_NONE;
    }
}

void updateLevel(int32 frames)
{
    updateFading(frames);

    gCausticsFrame += frames;

    gAnimTexFrame += frames;
    while (gAnimTexFrame > 5)
    {
        gTexShiftsDue++;    // levelCommit's
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
