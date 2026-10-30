// Stage 1 the game's way, the light (game.h): each item's as openlara/draw.h
// drawModel sets it before its nodes; and the static meshes' and the items'
// shadows' transforms, as drawRoomStatic and drawShadow set them up. An item without an intensity of its
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

// --- the static meshes and the shadows ------------------------------------------

static const PoseStatic* gStatics;
static uint32_t gStaticCount;
static const PoseShadow* gShadows;
static uint32_t gShadowCount;
static const int32_t* gCamera;
static DrawXform* gStaticOut;

static void storeXform(DrawXform &out)
{
    memcpy(out.m, &matrixGet(), sizeof(out.m));
    out.ambient = gLightAmbient;
    out.range = gLightRange;
    memcpy(out.light, gLightView, sizeof(out.light));
}

// draw.h drawRoomStatic's: the view placed at the static, its quarter turn,
// its intensity's light (its visibility test aside)
void drawRoomStaticXform(const PoseStatic* mesh, DrawXform &out)
{
    matrixPush();
    matrixTranslateSet(mesh->x - gCamera[0], mesh->y - gCamera[1], mesh->z - gCamera[2]);
    matrixRotateYQ(mesh->quadrant);

    calcLightingStatic(mesh->intensity);
    storeXform(out);

    matrixPop();
}

// draw.h drawShadow's: the item's box as getBoundingBox(true) lerps it, the
// view placed at the item over the floor (the floor's lookup aside) and
// turned as it; the item's light, as drawModel left it
void drawShadowXform(const PoseItem* item, const PoseShadow* shadow, const GameLight &light, DrawXform &out)
{
    const int16_t* boxA = (const int16_t*)(gFrames + item->frameA);
    const int16_t* boxB = (const int16_t*)(gFrames + item->frameB);
    int16_t box[6];
    if (!item->frameDelta) {
        memcpy(box, boxA, sizeof(box));
    } else {
        int32_t d = divTable[item->frameRate] * item->frameDelta;
        for (int32_t k = 0; k < 6; k++) {
            box[k] = boxA[k] + ((boxB[k] - boxA[k]) * d >> 16);
        }
    }
    int32_t x = (box[1] + box[0]) >> 1;
    int32_t z = (box[5] + box[4]) >> 1;
    int32_t sx = (box[1] - box[0]) * 160 >> 10;
    int32_t sz = (box[5] - box[4]) * 160 >> 10;

    matrixPush();
    matrixTranslateAbs(item->pos[0], shadow->floor, item->pos[2]);
    matrixRotateY(item->angle[1]);

    memcpy(out.m, &matrixGet(), sizeof(out.m));
    out.ambient = light.ambient;
    out.range = light.range;
    memcpy(out.light, light.view, sizeof(out.light));
    out.shadow[0] = x;
    out.shadow[1] = z;
    out.shadow[2] = sx;
    out.shadow[3] = sz;

    matrixPop();
}

void gameStaticsInit()
{
    gStatics = sceneArray<const PoseStatic>("STAT", &gStaticCount);
    gShadows = sceneArray<const PoseShadow>("SHDW", &gShadowCount);
    gCamera = sceneArray<const PoseCamera>("CAMR")->pos;
    gStaticOut = (DrawXform*)sceneAlloc(sceneInfo().draws * sizeof(DrawXform));
}

void gameStaticsRun(const PoseStatic* statics, uint32_t statCount, const PoseShadow* shadows, uint32_t shadowCount,
                    const PoseItem* items, const GameLight* lights, const uint8_t* frames, DrawXform* out)
{
    gFrames = frames;
    gMatrixPtr = gMatrixStack;
    for (uint32_t i = 0; i < statCount; i++) {
        drawRoomStaticXform(statics + i, out[statics[i].draw]);
    }
    for (uint32_t i = 0; i < shadowCount; i++)
    {
        const PoseShadow &s = shadows[i];
        drawShadowXform(items + s.item, &s, lights[s.item], out[s.draw]);
    }
}

void gameStatics()
{
    gameStaticsRun(gStatics, gStaticCount, gShadows, gShadowCount, gItems, gOut, gSceneFrames, gStaticOut);
}

// Whether a transform differs: the matrix, the light (its direction where the
// range is above 0), and a shadow's place and size.
int32_t xformOff(const DrawXform &a, const DrawXform &b, bool shadow)
{
    return memcmp(a.m, b.m, sizeof(a.m)) || a.ambient != b.ambient || a.range != b.range ||
           (b.range > 0 && memcmp(a.light, b.light, sizeof(a.light))) ||
           (shadow && memcmp(a.shadow, b.shadow, sizeof(a.shadow)));
}

int32_t gameStaticsCheck(const DrawXform* made, int32_t &statics, int32_t &shadows)
{
    if (!made) {
        made = gStaticOut;
    }
    int32_t bad = 0;
    for (uint32_t i = 0; i < gStaticCount; i++) {
        bad += xformOff(made[gStatics[i].draw], gXforms[gStatics[i].draw], false);
    }
    for (uint32_t i = 0; i < gShadowCount; i++) {
        bad += xformOff(made[gShadows[i].draw], gXforms[gShadows[i].draw], true);
    }
    statics = gStaticCount;
    shadows = gShadowCount;
    return bad;
}
