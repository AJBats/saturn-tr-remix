// CPU benchmark of engine hot paths, used to compare compilers and flags.
//
// Each kernel is timed with the SH-2 free-running timer at Pphi/128 with
// interrupts masked. Results go to low work RAM (unused by the program) so
// every build can be read the same way:
//   0x00200000  'BNCH' once finished
//   0x00200004  kernel count
//   0x00200008  timer ticks per kernel (x128 = CPU cycles), 0xFFFFFFFF on overflow
//   0x00200040  faces emitted by the last flush (sanity check)

#include "game.h"
#include "render.h"
#include "slave.h"
#include "system.h"

int32 fps;

#define FRT_FTCSR   (*(volatile uint8*)0xFFFFFE11)
#define FRT_FRCH    (*(volatile uint8*)0xFFFFFE12)
#define FRT_FRCL    (*(volatile uint8*)0xFFFFFE13)
#define FRT_TCR     (*(volatile uint8*)0xFFFFFE16)
#define FRT_OVF     0x02

#define RESULTS     ((volatile uint32*)0x20200000)
#define BENCH_MAGIC 0x424E4348

enum Kernel {
    K_MATRIX,       // view matrix + node transform chains
    K_FRAME_LERP,   // animation frame interpolation per bone
    K_TRANSFORM,    // room vertex transform + projection
    K_ROOM_FLUSH,   // transform + face bucketing + VDP1 command build
    K_MATH,         // atan / sqrt / angles / rng
    K_COUNT
};

extern int32 gRenderFaceCount;

#define GRID_X 16
#define GRID_Z 32

static RoomVertex gBenchVertices[GRID_X * GRID_Z];
static RoomQuad gBenchQuads[(GRID_X - 1) * (GRID_Z - 1)];
static RoomInfo gBenchInfo;
static Room gBenchRoom;
static uint32 gBenchAngles[2][JOINT_MAX];
static vec4s gBenchPos[JOINT_MAX];

static void timerStart()
{
    FRT_TCR = 2;    // Pphi / 128
    FRT_FRCH = 0;
    FRT_FRCL = 0;
    (void)FRT_FTCSR;
    FRT_FTCSR = 0;
}

static uint32 timerStop()
{
    uint32 hi = FRT_FRCH;
    uint32 lo = FRT_FRCL;
    if (FRT_FTCSR & FRT_OVF)
        return 0xFFFFFFFF;
    return (hi << 8) | lo;
}

static void initData()
{
    // a floor grid receding from the camera: 16 x 32 vertices, 256 units apart
    for (int32 z = 0; z < GRID_Z; z++)
    {
        for (int32 x = 0; x < GRID_X; x++)
        {
            RoomVertex &v = gBenchVertices[z * GRID_X + x];
            v.x = x;
            v.y = 0;
            v.z = z;
            v.g = (x + z) & 31;
        }
    }

    RoomQuad* q = gBenchQuads;
    for (int32 z = 0; z < GRID_Z - 1; z++)
    {
        for (int32 x = 0; x < GRID_X - 1; x++, q++)
        {
            int32 i = z * GRID_X + x;
            q->flags = 0;
            q->indices[0] = i;
            q->indices[1] = i + GRID_X;
            q->indices[2] = i + GRID_X + 1;
            q->indices[3] = i + 1;
        }
    }

    gBenchInfo.x = -8;          // in 256 units: room spans x -2048..1792
    gBenchInfo.z = 2;
    gBenchInfo.yTop = 768;      // floor below the camera
    gBenchInfo.verticesCount = GRID_X * GRID_Z;
    gBenchInfo.quadsCount = (GRID_X - 1) * (GRID_Z - 1);
    gBenchInfo.data.vertices = gBenchVertices;
    gBenchInfo.data.quads = gBenchQuads;

    gBenchRoom.info = &gBenchInfo;
    gBenchRoom.data = gBenchInfo.data;

    for (int32 i = 0; i < JOINT_MAX; i++)
    {
        gBenchAngles[0][i] = 0x01234567 * (i + 1);
        gBenchAngles[1][i] = 0x07654321 * (i + 1);
        gBenchPos[i].x = i * 13;
        gBenchPos[i].y = -i * 50;
        gBenchPos[i].z = i * 7;
    }
}

static void setView()
{
    gMatrixPtr = gMatrixStack;
    matrixSetView(_vec3i(0, 0, 0), 0, 0);
}

static void kernelMatrix()
{
    for (int32 i = 0; i < 1000; i++)
    {
        gMatrixPtr = gMatrixStack;
        matrixSetView(_vec3i(i, -i, i * 2), i * 64, i * 32);
        for (int32 j = 0; j < 6; j++)
        {
            matrixPush();
            matrixTranslateRel(j * 100, -j * 50, j * 25);
            matrixRotateYXZ(i * 16 + j, i * 8 - j, j * 300);
        }
        for (int32 j = 0; j < 6; j++)
        {
            matrixPop();
        }
    }
}

static void kernelFrameLerp()
{
    setView();
    for (int32 i = 0; i < 200; i++)
    {
        for (int32 j = 0; j < JOINT_MAX; j++)
        {
            matrixPush();
            matrixFrameLerp(gBenchPos + j, gBenchAngles[0] + j, gBenchAngles[1] + j, i & 7, 8);
            matrixPop();
        }
    }
}

static void kernelTransform()
{
    setView();
    matrixPush();
    matrixTranslateAbs(gBenchInfo.x << 8, gBenchInfo.yTop, gBenchInfo.z << 8);
    for (int32 i = 0; i < 50; i++)
    {
        // room transform and face setup, on the slave
        renderRoom(&gBenchRoom);
        RoomStats stats;
        Face** ot = roomsFinish(stats);
        for (int32 j = 0; j < OT_SIZE; j++) {
            ot[j] = NULL;
        }
    }
    matrixPop();
}

static void kernelRoomFlush()
{
    setView();
    for (int32 i = 0; i < 20; i++)
    {
        matrixPush();
        matrixTranslateAbs(gBenchInfo.x << 8, gBenchInfo.yTop, gBenchInfo.z << 8);
        renderRoom(&gBenchRoom);
        matrixPop();
        flush();
    }
}

static volatile int32 gSink;

static void kernelMath()
{
    int32 acc = 0;
    for (int32 i = 1; i <= 4000; i++)
    {
        int16 ax, ay;
        acc += phd_atan(i * 37 - 70000, i * 11 - 20000);
        acc += phd_sqrt(uint32(i) * 9973);
        anglesFromVector(i * 3 - 6000, i - 2000, 4000 - i * 2, ax, ay);
        acc += ax + ay + rand_logic();
    }
    gSink = acc;
}

typedef void (*KernelProc)();

static const KernelProc gKernels[K_COUNT] = {
    kernelMatrix,
    kernelFrameLerp,
    kernelTransform,
    kernelRoomFlush,
    kernelMath
};

int main()
{
    asm volatile("ldc %0, sr" : : "r"(0xF0));   // mask interrupts

    systemInit();
    slaveInit();
    renderInit();
    setViewport(RectMinMax(0, 0, FRAME_WIDTH, FRAME_HEIGHT));
    initData();

    RESULTS[0] = 0;
    RESULTS[1] = K_COUNT;

    for (int32 k = 0; k < K_COUNT; k++)
    {
        timerStart();
        gKernels[k]();
        RESULTS[2 + k] = timerStop();
    }

    RESULTS[16] = gRenderFaceCount;
    RESULTS[0] = BENCH_MAGIC;

    while (1)
    {
        waitVblank();
        renderSwap();
    }

    return 0;
}
