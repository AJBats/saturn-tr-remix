// openlara/common.cpp's matrix functions, the Saturn's C paths (gamematrix.h).
// The game reaches the stack through its CPU's state (GBR), the lab through a
// global: a load either way.
#include "gamematrix.h"
#include "game.h"

Matrix gMatrixStack[MAX_MATRICES];
Matrix* gMatrixPtr;
const uint32_t* gSinCosTable;
const int32_t* gCameraViewPos;

// common.h: the product of two values that fit 16 bits, by muls.w
static inline int32_t mul16(int32_t a, int32_t b)
{
    int32_t r;
    asm("muls.w %1, %2\n\tsts macl, %0" : "=r"(r) : "r"(a), "r"(b) : "macl");
    return r;
}

#define sincos(x,s,c) {\
    uint32_t sc = gSinCosTable[uint32_t(x << 16) >> 20];\
    s = int32_t(sc) >> 16;\
    c = int32_t(sc) << 16 >> 16;\
}

void matrixPush_c()
{
    gMatrixPtr[1] = gMatrixPtr[0];
    gMatrixPtr++;
}

void matrixSetIdentity_c()
{
    Matrix &m = matrixGet();

    m.e00 = 0x4000;
    m.e01 = 0;
    m.e02 = 0;
    m.e03 = 0;

    m.e10 = 0;
    m.e11 = 0x4000;
    m.e12 = 0;
    m.e13 = 0;

    m.e20 = 0;
    m.e21 = 0;
    m.e22 = 0x4000;
    m.e23 = 0;
}

void matrixSetBasis_c(Matrix &dst, const Matrix &src)
{
    dst.e00 = src.e00;
    dst.e01 = src.e01;
    dst.e02 = src.e02;

    dst.e10 = src.e10;
    dst.e11 = src.e11;
    dst.e12 = src.e12;

    dst.e20 = src.e20;
    dst.e21 = src.e21;
    dst.e22 = src.e22;
}

#define LERP_1_2(a, b)   a = (b + a) >> 1
#define LERP_1_4(a, b)   a = a + ((b - a) >> 2)
#define LERP_3_4(a, b)   a = b - ((b - a) >> 2)
#define LERP_SLOW(a, b)  a = a + ((b - a) * t >> 8)

#define LERP_ROW(lerp_func, a, b, row) \
    lerp_func(a.e##row##0, b.e##row##0); \
    lerp_func(a.e##row##1, b.e##row##1); \
    lerp_func(a.e##row##2, b.e##row##2); \
    lerp_func(a.e##row##3, b.e##row##3);

#define LERP_MATRIX(lerp_func) \
    LERP_ROW(lerp_func, m, n, 0); \
    LERP_ROW(lerp_func, m, n, 1); \
    LERP_ROW(lerp_func, m, n, 2);

void matrixLerp_c(const Matrix &n, int32_t pmul, int32_t pdiv)
{
    Matrix &m = matrixGet();

    if ((pdiv == 2) || ((pdiv == 4) && (pmul == 2))) {
        LERP_MATRIX(LERP_1_2);
    } else if (pdiv == 4) {

        if (pmul == 1) {
            LERP_MATRIX(LERP_1_4);
        } else {
            LERP_MATRIX(LERP_3_4);
        }

    } else {
        int32_t t = pmul * divTable[pdiv] >> 8;
        LERP_MATRIX(LERP_SLOW);
    }
}

#define MATRIX_TRANS(x,y,z)\
    Matrix &m = matrixGet();\
    int32_t tx = m.e00 * x + m.e01 * y + m.e02 * z;\
    int32_t ty = m.e10 * x + m.e11 * y + m.e12 * z;\
    int32_t tz = m.e20 * x + m.e21 * y + m.e22 * z;

#define MATRIX_ROTXY(x,y,s,c) {\
    int32_t _x = (mul16(x, c) - mul16(y, s)) >> FIXED_SHIFT;\
    int32_t _y = (mul16(y, c) + mul16(x, s)) >> FIXED_SHIFT;\
    x = _x;\
    y = _y;\
}

#define MATRIX_DP33(ax,ay,az,bx,by,bz) (mul16(ax, bx) + mul16(ay, by) + mul16(az, bz))

void matrixTranslateRel_c(int32_t x, int32_t y, int32_t z)
{
    Matrix &m = matrixGet();
    int32_t tx = MATRIX_DP33(m.e00, m.e01, m.e02, x, y, z);
    int32_t ty = MATRIX_DP33(m.e10, m.e11, m.e12, x, y, z);
    int32_t tz = MATRIX_DP33(m.e20, m.e21, m.e22, x, y, z);
    m.e03 += tx;
    m.e13 += ty;
    m.e23 += tz;
}

void matrixTranslateAbs_c(int32_t x, int32_t y, int32_t z)
{
    x -= gCameraViewPos[0];
    y -= gCameraViewPos[1];
    z -= gCameraViewPos[2];
    MATRIX_TRANS(x, y, z);
    m.e03 = tx;
    m.e13 = ty;
    m.e23 = tz;
}

void matrixRotateX_c(int32_t angle)
{
    int32_t s, c;
    sincos(angle, s, c);

    Matrix &m = matrixGet();
    MATRIX_ROTXY(m.e02, m.e01, s, c);
    MATRIX_ROTXY(m.e12, m.e11, s, c);
    MATRIX_ROTXY(m.e22, m.e21, s, c);
}

void matrixRotateY_c(int32_t angle)
{
    int32_t s, c;
    sincos(angle, s, c);

    Matrix &m = matrixGet();
    MATRIX_ROTXY(m.e00, m.e02, s, c);
    MATRIX_ROTXY(m.e10, m.e12, s, c);
    MATRIX_ROTXY(m.e20, m.e22, s, c);
}

void matrixRotateZ_c(int32_t angle)
{
    int32_t s, c;
    sincos(angle, s, c);

    Matrix &m = matrixGet();
    MATRIX_ROTXY(m.e01, m.e00, s, c);
    MATRIX_ROTXY(m.e11, m.e10, s, c);
    MATRIX_ROTXY(m.e21, m.e20, s, c);
}

void matrixTranslateSet_c(int32_t x, int32_t y, int32_t z)
{
    MATRIX_TRANS(x, y, z);
    m.e03 = tx;
    m.e13 = ty;
    m.e23 = tz;
}

void matrixRotateYQ_c(int32_t quadrant)
{
    if (quadrant == 2)
        return;

    Matrix &m = matrixGet();

    if (quadrant == 0) {
        m.e00 = -m.e00;
        m.e10 = -m.e10;
        m.e20 = -m.e20;
        m.e02 = -m.e02;
        m.e12 = -m.e12;
        m.e22 = -m.e22;
    } else if (quadrant == 1) {
        int32_t e0 = m.e02;
        int32_t e1 = m.e12;
        int32_t e2 = m.e22;

        m.e02 = -m.e00;
        m.e12 = -m.e10;
        m.e22 = -m.e20;

        m.e00 = e0;
        m.e10 = e1;
        m.e20 = e2;
    } else {
        int32_t e0 = m.e02;
        int32_t e1 = m.e12;
        int32_t e2 = m.e22;

        m.e02 = m.e00;
        m.e12 = m.e10;
        m.e22 = m.e20;

        m.e00 = -e0;
        m.e10 = -e1;
        m.e20 = -e2;
    }
}

void matrixRotateYXZ_c(int32_t angleX, int32_t angleY, int32_t angleZ)
{
    if (angleY) matrixRotateY(angleY);
    if (angleX) matrixRotateX(angleX);
    if (angleZ) matrixRotateZ(angleZ);
}

void matrixFrame_c(const void* pos, const void* angles)
{
    int16_t aX, aY, aZ;
    DECODE_ANGLES(*(uint32_t*)angles, aX, aY, aZ);

    uint32_t xy = ((uint32_t*)pos)[0];
    uint32_t zu = ((uint32_t*)pos)[1];

    int32_t posX = int16_t(xy >> 16);
    int32_t posY = int16_t(xy & 0xFFFF);
    int32_t posZ = int16_t(zu >> 16);

    matrixTranslateRel(posX, posY, posZ);
    matrixRotateYXZ(aX, aY, aZ);
}

void matrixFrameLerp(const void* pos, const void* anglesA, const void* anglesB, int32_t delta, int32_t rate)
{
    int16_t aX, aY, aZ;
    int16_t bX, bY, bZ;

    DECODE_ANGLES(*(uint32_t*)anglesA, aX, aY, aZ);
    DECODE_ANGLES(*(uint32_t*)anglesB, bX, bY, bZ);

    uint32_t xy = ((uint32_t*)pos)[0];
    uint32_t zu = ((uint32_t*)pos)[1];

    int32_t posX = int16_t(xy >> 16);
    int32_t posY = int16_t(xy & 0xFFFF);
    int32_t posZ = int16_t(zu >> 16);

    matrixTranslateRel(posX, posY, posZ);

    matrixPush();
    matrixRotateYXZ(bX, bY, bZ);
    matrixPop();

    matrixRotateYXZ(aX, aY, aZ);

    matrixLerp(*(gMatrixPtr + 1), delta, rate);
}

// common.cpp's square root, floor(sqrt(x)) a bit pair at a time.
uint32_t phd_sqrt(uint32_t x)
{
    uint32_t m = 0x40000000;
    uint32_t y = 0;
    uint32_t z = 0;

    do {
        y += m;

        if (y > x) {
            y = z;
        } else {
            x -= y;
            y = z + m;
        }

        z = y >> 1;
    } while (m >>= 2);

    return y;
}
