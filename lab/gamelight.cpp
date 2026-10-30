// Stage 1 the game's way, the light (game.h): each item's as openlara/draw.h
// drawModel sets it before its nodes. An item without an intensity of its
// own takes its room's brightest light at its first frame's box centre,
// turned as the item (calcLightingDynamic at ItemObj::getRelative); one with
// takes that (calcLightingStatic). Either way the fog darkens it by the
// item's depth. Compiled as the game compiles draw.h and item.h (main.cpp,
// -Os: the Makefile), on common.cpp's matrices and square root.
#include "game.h"
#include "gamematrix.h"
#include "scene.h"

#include <string.h>

#define FOG_MIN     (1024 * 12)
#define FOG_SHIFT   0
#define X_SQR(x)    ((x) * (x))
#define X_MIN(a, b) ((a) < (b) ? (a) : (b))

struct vec3i
{
    int32_t x, y, z;
};

struct Light
{
    int16_t x, y, z;
    uint8_t radius;
    uint8_t intensity;
};

static const PoseItem* gItems;
static uint32_t gCount;
static const uint8_t* gSceneFrames;
static const uint8_t* gFrames;
static const PoseLight* gRooms;
static const Light* gSceneLights;
static const Light* gLights;
static const int16_t* gViewRotation;
static const DrawXform* gXforms;
static GameLight* gOut;

static int32_t gLightAmbient;
static int32_t gLightRange;
static int16_t gLightView[3];

// item.h's: a point in the item's space turned and moved as the item
vec3i getRelative(const PoseItem* item, const vec3i &point)
{
    matrixPush();

    Matrix &m = matrixGet();

    matrixSetIdentity();
    matrixRotateYXZ(item->angle[0], item->angle[1], item->angle[2]);

    vec3i p;
    p.x = item->pos[0] + ((m.e00 * point.x + m.e01 * point.y + m.e02 * point.z) >> FIXED_SHIFT);
    p.y = item->pos[1] + ((m.e10 * point.x + m.e11 * point.y + m.e12 * point.z) >> FIXED_SHIFT);
    p.z = item->pos[2] + ((m.e20 * point.x + m.e21 * point.y + m.e22 * point.z) >> FIXED_SHIFT);

    matrixPop();

    return p;
}

void calcLightingDynamic(const PoseLight* room, const vec3i &point)
{
    gLightAmbient = (room->ambient << 5);
    gLightRange = 0;

    if (room->lights)
    {
        int32_t ambient = 8191 - gLightAmbient;
        int32_t maxLum = 0;
        vec3i dir = { 0, 0, 0 };

        for (int i = 0; i < room->lights; i++)
        {
            const Light* light = gLights + room->first + i;

            vec3i pos;
            pos.x = light->x + (room->roomX << 8);
            pos.y = light->y;
            pos.z = light->z + (room->roomZ << 8);

            int32_t radius = light->radius << 8;
            int32_t intensity = light->intensity << 5;

            vec3i d = { point.x - pos.x, point.y - pos.y, point.z - pos.z };
            int32_t dist = (d.x * d.x + d.y * d.y + d.z * d.z) >> 12;
            int32_t att = X_SQR(radius) >> 12;

            int32_t lum = (intensity * att) / (dist + att) + ambient;

            if (lum > maxLum) {
                maxLum = lum;
                dir.x = pos.x - point.x;
                dir.y = pos.y - point.y;
                dir.z = pos.z - point.z;
            }
        }

        int32_t adder = (maxLum + ambient) >> 1;
        gLightAmbient = 8191 - adder;
        gLightRange = maxLum - adder;

        if (gLightRange > 0)
        {
            dir.x >>= 4;
            dir.y >>= 4;
            dir.z >>= 4;
            int32_t len = phd_sqrt(X_SQR(dir.x) + X_SQR(dir.y) + X_SQR(dir.z));

            if (len)
            {
                vec3i u;
                u.x = (dir.x << FIXED_SHIFT) / len;
                u.y = (dir.y << FIXED_SHIFT) / len;
                u.z = (dir.z << FIXED_SHIFT) / len;

                const int16_t* r = gViewRotation;
                gLightView[0] = (r[0] * u.x + r[1] * u.y + r[2] * u.z) >> FIXED_SHIFT;
                gLightView[1] = (r[3] * u.x + r[4] * u.y + r[5] * u.z) >> FIXED_SHIFT;
                gLightView[2] = (r[6] * u.x + r[7] * u.y + r[8] * u.z) >> FIXED_SHIFT;
            } else {
                gLightRange = 0;
            }
        }
    }

    Matrix &m = matrixGet();

    // the fog darkens the side facing the light to black too
    int32_t fogZ = m.e23 >> FIXED_SHIFT;
    if (fogZ > FOG_MIN) {
        gLightAmbient += (fogZ - FOG_MIN) << FOG_SHIFT;
        gLightAmbient = X_MIN(gLightAmbient, 8191 + gLightRange);
    }
}

void calcLightingStatic(int32_t intensity)
{
    gLightAmbient = intensity - 4096;
    gLightRange = 0;

    Matrix &m = matrixGet();

    int32_t fogZ = m.e23 >> FIXED_SHIFT;
    if (fogZ > FOG_MIN) {
        gLightAmbient += (fogZ - FOG_MIN) << FOG_SHIFT;
        gLightAmbient = X_MIN(gLightAmbient, 8191);
    }
}

// drawModel's light, the item placed (its depth for the fog).
void drawModelLight(const PoseItem* item, const PoseLight* room)
{
    matrixPush();
    matrixTranslateAbs(item->pos[0], item->pos[1], item->pos[2]);

    int32_t intensity = room->intensity << 5;

    if (intensity == 0) {
        const int16_t* box = (const int16_t*)(gFrames + item->frameA);
        vec3i center = { (box[1] + box[0]) >> 1, (box[3] + box[2]) >> 1, (box[5] + box[4]) >> 1 };
        vec3i point = getRelative(item, center);
        calcLightingDynamic(room, point);
    } else {
        calcLightingStatic(intensity);
    }

    matrixPop();
}

void gameLightRun(const PoseItem* items, const PoseLight* rooms, const void* lights, uint32_t count,
                  const uint8_t* frames, GameLight* out)
{
    gFrames = frames;
    gLights = (const Light*)lights;
    gMatrixPtr = gMatrixStack;
    for (uint32_t i = 0; i < count; i++)
    {
        drawModelLight(items + i, rooms + i);
        out[i].ambient = gLightAmbient;
        out[i].range = gLightRange;
        out[i].view[0] = gLightView[0];
        out[i].view[1] = gLightView[1];
        out[i].view[2] = gLightView[2];
    }
}

int32_t gameLightInit()
{
    uint32_t bytes;
    gItems = (const PoseItem*)sceneChunk("POSE", &bytes);
    gCount = bytes / sizeof(PoseItem);
    gSceneFrames = sceneArray<const uint8_t>("PFRM");
    gFrames = gSceneFrames;
    gRooms = sceneArray<const PoseLight>("PLIT");
    gSceneLights = sceneArray<const Light>("LGHT");
    gLights = gSceneLights;
    gViewRotation = sceneArray<const PoseCamera>("CAMR")->rotation;
    gXforms = sceneArray<const DrawXform>("XFRM");
    gOut = (GameLight*)sceneAlloc(gCount * sizeof(GameLight));
    return gCount;
}

void gameLight()
{
    gameLightRun(gItems, gRooms, gSceneLights, gCount, gSceneFrames, gOut);
}

const GameLight* gameLights()
{
    return gOut;
}

int32_t gameLightCheck()
{
    int32_t bad = 0;
    for (uint32_t i = 0; i < gCount; i++)
    {
        const PoseItem &item = gItems[i];
        const GameLight &light = gOut[i];
        int32_t drawn = 0;
        for (int32_t k = 0; k < item.count; k++) {
            drawn += item.lara || (item.visibleMask >> k & 1);
        }
        for (int32_t k = 0; k < drawn; k++)
        {
            const DrawXform &x = gXforms[item.firstDraw + k];
            bad += x.ambient != light.ambient || x.range != light.range ||
                   (light.range > 0 && memcmp(x.light, light.view, sizeof(light.view)));
        }
    }
    return bad;
}
