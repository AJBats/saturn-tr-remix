// openlara/common.cpp's matrix stack and arithmetic as the Saturn builds it,
// for stage 1 the game's way (gamepose.cpp): the C paths (USE_ASM is off),
// int32 entries (MATRIX_FIXED_SHIFT 0), rotations by mul16. gamematrix.cpp
// is compiled as common.cpp is (-O2), apart from its callers, as the game's
// are (main.cpp, -Os).
#ifndef GAMEMATRIX_H
#define GAMEMATRIX_H

#include <stdint.h>

#define FIXED_SHIFT     14
#define MAX_MATRICES    8

struct Matrix
{
    int32_t e00, e01, e02, e03;
    int32_t e10, e11, e12, e13;
    int32_t e20, e21, e22, e23;
};

extern Matrix gMatrixStack[MAX_MATRICES];
extern Matrix* gMatrixPtr;

// The table and the camera they read, the game's gSinCosTable and
// gCameraViewPos (and divTable, game.h).
extern const uint32_t* gSinCosTable;
extern const int32_t* gCameraViewPos;

#define matrixGet() *gMatrixPtr

#define DECODE_ANGLES(a,x,y,z)\
    x = (a & 0x3FF0) << 2;\
    y = (a & 0x000F) << 12 | ((a >> 16) & 0xFC00) >> 4;\
    z = ((a >> 16) & 0x03FF) << 6;

#define GET_FRAME_T(x,n) (divTable[n] * x)

void matrixPush_c();
void matrixSetBasis_c(Matrix &dst, const Matrix &src);
void matrixLerp_c(const Matrix &n, int32_t pmul, int32_t pdiv);
void matrixTranslateRel_c(int32_t x, int32_t y, int32_t z);
void matrixTranslateAbs_c(int32_t x, int32_t y, int32_t z);
void matrixRotateX_c(int32_t angle);
void matrixRotateY_c(int32_t angle);
void matrixRotateZ_c(int32_t angle);
void matrixRotateYXZ_c(int32_t angleX, int32_t angleY, int32_t angleZ);
void matrixFrame_c(const void* pos, const void* angles);
void matrixFrameLerp(const void* pos, const void* anglesA, const void* anglesB, int32_t delta, int32_t rate);

#define matrixPush          matrixPush_c
#define matrixPop()         gMatrixPtr--
#define matrixSetBasis      matrixSetBasis_c
#define matrixLerp          matrixLerp_c
#define matrixTranslateRel  matrixTranslateRel_c
#define matrixTranslateAbs  matrixTranslateAbs_c
#define matrixRotateX       matrixRotateX_c
#define matrixRotateY       matrixRotateY_c
#define matrixRotateZ       matrixRotateZ_c
#define matrixRotateYXZ     matrixRotateYXZ_c
#define matrixFrame         matrixFrame_c

#endif
