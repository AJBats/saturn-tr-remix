// Stage 1 the game's way (game.h): the items' node matrices, openlara/draw.h
// drawModel's (from the view matrix: the item's place and turn), then
// drawNodesLerp's or drawLaraNodesLerp's, on common.cpp's matrix arithmetic
// (gamematrix.h). Compiled as the game compiles draw.h (in main.cpp, -Os:
// the Makefile). Each drawMesh takes the matrix as it stands to the mesh's
// draw, where the game's renderMesh would read it.
#include "game.h"
#include "gamematrix.h"
#include "scene.h"

#include <string.h>

#define NODE_FLAG_POP       1
#define NODE_FLAG_PUSH      2
#define NODE_FLAG_ROTX      4
#define NODE_FLAG_ROTY      8
#define NODE_FLAG_ROTZ      16
#define LARA_ARM_R          0
#define LARA_ARM_L          1
#define LARA_ARM_MAX        2
#define JOINT_TORSO         7
#define JOINT_ARM_R1        8
#define JOINT_ARM_L1        11
#define WEAPON_SHOTGUN      3
#define DEF_TORSO_ANGLE_X   1216
#define DEF_TORSO_ANGLE_Y   -832
#define DEF_TORSO_ANGLE_Z   -192
#define NO_FRAME            0xFFFFFFFF

static_assert(sizeof(PoseItem) == 104, "tools/posepack.py POSE_ITEM, POSE_ARM");

// common.h's, as the game lays them out: int16 x y z, then the flags; a
// frame's box, place and spare, then a packed angle a node.
struct ModelNode
{
    int16_t x, y, z;
    uint16_t flags;
};

struct AnimFrame
{
    int16_t box[6];
    int16_t x, y, z;
    uint16_t angles[1];
};

static const PoseItem* gPoses;
static uint32_t gPoseCount;
static const ModelNode* gSceneNodes;
static const ModelNode* gNodes;
static const uint8_t* gSceneFrames;
static const uint8_t* gFrames;
static const DrawXform* gXforms;

static Matrix* gOut;            // the draws' matrices (DrawXform order)
Matrix* gDrawn;                 // the next drawn mesh's

uint32_t ZERO_POS[2] = { 0, 0 };

static inline const AnimFrame* frameAt(uint32_t offset)
{
    return (const AnimFrame*)(gFrames + offset);
}

void drawMesh()
{
    *gDrawn++ = matrixGet();
}

void drawNodes(const PoseItem* item, const AnimFrame* frameA)
{
    const ModelNode* node = gNodes + item->nodes / sizeof(ModelNode);
    int32_t meshCount = item->count;
    uint32_t visibleMask = item->visibleMask;

    const uint32_t* angles = (uint32_t*)(frameA->angles + 1);
    const int16_t* extraAngles = item->extraCount ? item->extra : nullptr;

    matrixFrame(&frameA->x, angles);
    if (visibleMask & 1) {
        drawMesh();
    }

    while (meshCount > 1)
    {
        visibleMask >>= 1;
        angles++;

        if (node->flags & NODE_FLAG_POP)  matrixPop();
        if (node->flags & NODE_FLAG_PUSH) matrixPush();

        matrixFrame(&node->x, angles);

        if (extraAngles)
        {
            if (node->flags & NODE_FLAG_ROTY) matrixRotateY(*extraAngles++);
            if (node->flags & NODE_FLAG_ROTX) matrixRotateX(*extraAngles++);
            if (node->flags & NODE_FLAG_ROTZ) matrixRotateZ(*extraAngles++);
        }

        if (visibleMask & 1) {
            drawMesh();
        }

        meshCount--;
        node++;
    }
}

void drawNodesLerp(const PoseItem* item, const AnimFrame* frameA, const AnimFrame* frameB, int32_t frameDelta,
                   int32_t frameRate)
{
    if (frameDelta == 0)
    {
        drawNodes(item, frameA);
        return;
    }

    const ModelNode* node = gNodes + item->nodes / sizeof(ModelNode);
    int32_t meshCount = item->count;
    uint32_t visibleMask = item->visibleMask;

    const uint32_t* anglesA = (uint32_t*)(frameA->angles + 1);
    const uint32_t* anglesB = (uint32_t*)(frameB->angles + 1);
    const int16_t* extraAngles = item->extraCount ? item->extra : nullptr;

    int32_t t = GET_FRAME_T(frameDelta, frameRate);

    alignas(4) int16_t posLerp[4];     // vec4s, read as two longwords
    posLerp[0] = frameA->x + ((frameB->x - frameA->x) * t >> 16);
    posLerp[1] = frameA->y + ((frameB->y - frameA->y) * t >> 16);
    posLerp[2] = frameA->z + ((frameB->z - frameA->z) * t >> 16);

    matrixFrameLerp(posLerp, anglesA, anglesB, frameDelta, frameRate);
    if (visibleMask & 1) {
        drawMesh();
    }

    while (meshCount > 1)
    {
        visibleMask >>= 1;
        anglesA++;
        anglesB++;

        if (node->flags & NODE_FLAG_POP)  matrixPop();
        if (node->flags & NODE_FLAG_PUSH) matrixPush();

        matrixFrameLerp(&node->x, anglesA, anglesB, frameDelta, frameRate);

        if (extraAngles)
        {
            if (node->flags & NODE_FLAG_ROTY) matrixRotateY(*extraAngles++);
            if (node->flags & NODE_FLAG_ROTX) matrixRotateX(*extraAngles++);
            if (node->flags & NODE_FLAG_ROTZ) matrixRotateZ(*extraAngles++);
        }

        if (visibleMask & 1) {
            drawMesh();
        }

        meshCount--;
        node++;
    }
}

void drawLaraNodes(const PoseItem* lara, const AnimFrame* frameA)
{
    const ModelNode* node = gNodes + lara->nodes / sizeof(ModelNode);

    const uint32_t* anglesArm[LARA_ARM_MAX];
    const uint32_t* angles = anglesArm[LARA_ARM_R] = anglesArm[LARA_ARM_L] = (uint32_t*)(frameA->angles + 1);

    int16_t torsoAngle[3] = { lara->torso[0], lara->torso[1], lara->torso[2] };

    for (int32_t i = 0; i < LARA_ARM_MAX; i++)
    {
        const PoseArm* arm = lara->arms + i;

        if (arm->frame != NO_FRAME)
        {
            const AnimFrame* frame = frameAt(arm->frame);
            anglesArm[i] = (uint32_t*)(frame->angles + 1);

            // additional torso animation for shotgun
            if (lara->weapon == WEAPON_SHOTGUN && i == LARA_ARM_R)
            {
                int32_t aX, aY, aZ;
                DECODE_ANGLES(*((uint32_t*)(frame->angles + 1) + JOINT_TORSO), aX, aY, aZ);
                torsoAngle[0] = torsoAngle[0] + aX - DEF_TORSO_ANGLE_X;
                torsoAngle[1] = torsoAngle[1] + aY - DEF_TORSO_ANGLE_Y;
                torsoAngle[2] = torsoAngle[2] + aZ - DEF_TORSO_ANGLE_Z;
            }
        }
    }

    anglesArm[LARA_ARM_R] += JOINT_ARM_R1;
    anglesArm[LARA_ARM_L] += JOINT_ARM_L1;

    const Matrix& basis = matrixGet();

    matrixPush();
    { // JOINT_HIPS
        matrixFrame(&frameA->x, angles++);
        drawMesh();

        for (int32_t i = 0; i < 2; i++) // draw Left & Right legs
        {
            matrixPush();
            { // JOINT_LEG_1
                matrixFrame(&(node++)->x, angles++);
                drawMesh();

                { // JOINT_LEG_2
                    matrixFrame(&(node++)->x, angles++);
                    drawMesh();

                    { // JOINT_LEG_3
                        matrixFrame(&(node++)->x, angles++);
                        drawMesh();
                    }
                }
            }
            matrixPop();
        }

        { // JOINT_TORSO
            matrixFrame(&(node++)->x, angles++);
            matrixRotateYXZ(torsoAngle[0], torsoAngle[1], torsoAngle[2]);
            drawMesh();

            for (int32_t i = 0; i < LARA_ARM_MAX; i++) // draw Right & Left arms
            {
                const PoseArm* arm = lara->arms + i;

                matrixPush();
                // JOINT_ARM_1
                matrixTranslateRel(node->x, node->y, node->z);
                node++;
                if (arm->useBasis) { // hands are rotated relative to the basis
                    matrixSetBasis(matrixGet(), basis);
                    matrixRotateYXZ(arm->angle[0], arm->angle[1], arm->angle[2]);
                }
                matrixFrame(ZERO_POS, anglesArm[i]++);
                drawMesh();

                { // JOINT_ARM_2
                    matrixFrame(&(node++)->x, anglesArm[i]++);
                    drawMesh();

                    { // JOINT_ARM_3
                        matrixFrame(&(node++)->x, anglesArm[i]);
                        drawMesh();
                    }
                }
                matrixPop();
            }

            { // JOINT_HEAD
                matrixFrame(&(node++)->x, angles + 3 * LARA_ARM_MAX);
                matrixRotateYXZ(lara->head[0], lara->head[1], lara->head[2]);
                drawMesh();
            }
        }
    }
    matrixPop();
}

void drawLaraNodesLerp(const PoseItem* lara, const AnimFrame* frameA, const AnimFrame* frameB, int32_t frameDelta,
                       int32_t frameRate)
{
    if (frameDelta == 0)
    {
        drawLaraNodes(lara, frameA);
        return;
    }

    const ModelNode* node = gNodes + lara->nodes / sizeof(ModelNode);

    const uint32_t* anglesArmA[LARA_ARM_MAX];
    const uint32_t* anglesArmB[LARA_ARM_MAX];
    const uint32_t* anglesA = anglesArmA[LARA_ARM_R] = anglesArmA[LARA_ARM_L] = (uint32_t*)(frameA->angles + 1);
    const uint32_t* anglesB = anglesArmB[LARA_ARM_R] = anglesArmB[LARA_ARM_L] = (uint32_t*)(frameB->angles + 1);
    int32_t frameRateArm[2];
    frameRateArm[0] = frameRateArm[1] = frameRate;

    int16_t torsoAngle[3] = { lara->torso[0], lara->torso[1], lara->torso[2] };

    for (int32_t i = 0; i < LARA_ARM_MAX; i++)
    {
        const PoseArm* arm = lara->arms + i;

        if (arm->frame != NO_FRAME)
        {
            const AnimFrame* frame = frameAt(arm->frame);
            anglesArmA[i] = anglesArmB[i] = (uint32_t*)(frame->angles + 1); // no lerp for armed hands (frameRate == 1)
            frameRateArm[i] = arm->frameRate;

            // additional torso animation for shotgun
            if (lara->weapon == WEAPON_SHOTGUN && i == LARA_ARM_R)
            {
                int32_t aX, aY, aZ;
                DECODE_ANGLES(*((uint32_t*)(frame->angles + 1) + JOINT_TORSO), aX, aY, aZ);
                torsoAngle[0] = torsoAngle[0] + aX - DEF_TORSO_ANGLE_X;
                torsoAngle[1] = torsoAngle[1] + aY - DEF_TORSO_ANGLE_Y;
                torsoAngle[2] = torsoAngle[2] + aZ - DEF_TORSO_ANGLE_Z;
            }
        }
    }

    anglesArmA[LARA_ARM_R] += JOINT_ARM_R1;
    anglesArmB[LARA_ARM_R] += JOINT_ARM_R1;
    anglesArmA[LARA_ARM_L] += JOINT_ARM_L1;
    anglesArmB[LARA_ARM_L] += JOINT_ARM_L1;

    const Matrix& basis = matrixGet();

    matrixPush();
    { // JOINT_HIPS
        int32_t t = GET_FRAME_T(frameDelta, frameRate);

        alignas(4) int16_t posLerp[4];     // vec4s, read as two longwords
        posLerp[0] = frameA->x + ((frameB->x - frameA->x) * t >> 16);
        posLerp[1] = frameA->y + ((frameB->y - frameA->y) * t >> 16);
        posLerp[2] = frameA->z + ((frameB->z - frameA->z) * t >> 16);

        matrixFrameLerp(posLerp, anglesA++, anglesB++, frameDelta, frameRate);
        drawMesh();

        for (int32_t i = 0; i < 2; i++) // draw Left & Right legs
        {
            matrixPush();
            { // JOINT_LEG_1
                matrixFrameLerp(&(node++)->x, anglesA++, anglesB++, frameDelta, frameRate);
                drawMesh();

                { // JOINT_LEG_2
                    matrixFrameLerp(&(node++)->x, anglesA++, anglesB++, frameDelta, frameRate);
                    drawMesh();

                    { // JOINT_LEG_3
                        matrixFrameLerp(&(node++)->x, anglesA++, anglesB++, frameDelta, frameRate);
                        drawMesh();
                    }
                }
            }
            matrixPop();
        }

        { // JOINT_TORSO
            matrixFrameLerp(&(node++)->x, anglesA++, anglesB++, frameDelta, frameRate);
            matrixRotateYXZ(torsoAngle[0], torsoAngle[1], torsoAngle[2]);
            drawMesh();

            for (int32_t i = 0; i < LARA_ARM_MAX; i++) // draw Right & Left arms
            {
                const PoseArm* arm = lara->arms + i;

                matrixPush();
                // JOINT_ARM_1
                matrixTranslateRel(node->x, node->y, node->z);
                node++;
                if (arm->useBasis) { // hands are rotated relative to the basis
                    matrixSetBasis(matrixGet(), basis);
                    matrixRotateYXZ(arm->angle[0], arm->angle[1], arm->angle[2]);
                }

                bool useLerp = frameRateArm[i] > 1; // armed hands always use frameRate == 1 (i.e. useLerp == false)

                if (useLerp) {
                    matrixFrameLerp(ZERO_POS, anglesArmA[i]++, anglesArmB[i]++, frameDelta, frameRate);
                } else {
                    matrixFrame(ZERO_POS, anglesArmA[i]++);
                }
                drawMesh();

                { // JOINT_ARM_2
                    if (useLerp) {
                        matrixFrameLerp(&(node++)->x, anglesArmA[i]++, anglesArmB[i]++, frameDelta, frameRate);
                    } else {
                        matrixFrame(&(node++)->x, anglesArmA[i]++);
                    }
                    drawMesh();

                    { // JOINT_ARM_3
                        if (useLerp) {
                            matrixFrameLerp(&(node++)->x, anglesArmA[i], anglesArmB[i], frameDelta, frameRate);
                        } else {
                            matrixFrame(&(node++)->x, anglesArmA[i]);
                        }
                        drawMesh();
                    }
                }
                matrixPop();
            }

            { // JOINT_HEAD
                matrixFrameLerp(&(node++)->x, anglesA + 3 * LARA_ARM_MAX, anglesB + 3 * LARA_ARM_MAX, frameDelta, frameRate);
                matrixRotateYXZ(lara->head[0], lara->head[1], lara->head[2]);
                drawMesh();
            }
        }
    }
    matrixPop();
}

const int32_t (*gGivenMatrices)[12];

void gameMatrices(const int32_t (*matrices)[12])
{
    gGivenMatrices = matrices;
}

// drawModel's matrices: the item's place and turn, then its nodes'; or the
// inventory's drawSlot's, its nodes under the matrix it set up.
void drawModel(const PoseItem* item)
{
    matrixPush();
    if (item->matrix)
    {
        memcpy(gMatrixPtr, gGivenMatrices[item->matrix - 1], sizeof(Matrix));
    }
    else
    {
        matrixTranslateAbs(item->pos[0], item->pos[1], item->pos[2]);
        matrixRotateYXZ(item->angle[0], item->angle[1], item->angle[2]);
    }

    if (item->lara) {
        drawLaraNodesLerp(item, frameAt(item->frameA), frameAt(item->frameB), item->frameDelta, item->frameRate);
    } else {
        drawNodesLerp(item, frameAt(item->frameA), frameAt(item->frameB), item->frameDelta, item->frameRate);
    }

    matrixPop();
}

// --- the stage --------------------------------------------------------------

int32_t gamePoseInit()
{
    uint32_t bytes;
    gPoses = (const PoseItem*)sceneChunk("POSE", &bytes);
    gPoseCount = bytes / sizeof(PoseItem);
    gSceneNodes = sceneArray<const ModelNode>("PNOD");
    gSceneFrames = sceneArray<const uint8_t>("PFRM");
    gSinCosTable = sceneArray<const uint32_t>("SINC");
    const PoseCamera* camera = sceneArray<const PoseCamera>("CAMR");
    gCameraViewPos = camera->pos;
    gXforms = sceneArray<const DrawXform>("XFRM");
    gOut = (Matrix*)sceneAlloc(sceneInfo().draws * sizeof(Matrix));

    Matrix &view = gMatrixStack[0];
    const int16_t* r = camera->rotation;
    view.e00 = r[0]; view.e01 = r[1]; view.e02 = r[2]; view.e03 = 0;
    view.e10 = r[3]; view.e11 = r[4]; view.e12 = r[5]; view.e13 = 0;
    view.e20 = r[6]; view.e21 = r[7]; view.e22 = r[8]; view.e23 = 0;
    return gPoseCount;
}

void gamePoseRun(const PoseItem* items, uint32_t count, const uint8_t* nodes, const uint8_t* frames, void* out)
{
    gNodes = (const ModelNode*)nodes;
    gFrames = frames;
    gMatrixPtr = gMatrixStack;
    for (uint32_t i = 0; i < count; i++)
    {
        const PoseItem* item = items + i;
        gDrawn = (Matrix*)out + item->firstDraw;
        drawModel(item);
    }
}

void gamePose()
{
    gamePoseRun(gPoses, gPoseCount, (const uint8_t*)gSceneNodes, gSceneFrames, gOut);
}

int32_t gamePoseCheck(int32_t &matrices)
{
    int32_t bad = 0;
    matrices = 0;
    for (uint32_t i = 0; i < gPoseCount; i++)
    {
        const PoseItem &item = gPoses[i];
        int32_t drawn = 0;
        for (int32_t k = 0; k < item.count; k++) {
            drawn += item.lara || (item.visibleMask >> k & 1);
        }
        for (int32_t k = 0; k < drawn; k++) {
            bad += memcmp(gOut + item.firstDraw + k, gXforms[item.firstDraw + k].m, sizeof(Matrix)) != 0;
        }
        matrices += drawn;
    }
    return bad;
}
