// The PlayStation's video frames decoded on an SH-2 (mdec.h). Every step is tools/mdec.py's, in the same
// integer arithmetic.

#include "mdec.h"

// --- the AC codes (psx-spx) ------------------------------------------------------------

// A group of codes: its prefix, x bits indexing its MDEC values (run << 10 | level), a sign bit.
struct CodeGroup
{
    const char* prefix;
    uint8_t count;
    uint16_t values[16];
};

static const CodeGroup kCodes[] = {
    { "11", 1, { 0x0001 } },
    { "011", 1, { 0x0401 } },
    { "010", 2, { 0x0002, 0x0801 } },
    { "0011", 2, { 0x1001, 0x0C01 } },
    { "00101", 1, { 0x0003 } },
    { "00100", 8, { 0x3401, 0x0006, 0x3001, 0x2C01, 0x0C02, 0x0403, 0x0005, 0x2801 } },
    { "0001", 4, { 0x1C01, 0x1801, 0x0402, 0x1401 } },
    { "00001", 4, { 0x0802, 0x2401, 0x0004, 0x2001 } },
    { "0000001", 8, { 0x4001, 0x1402, 0x0007, 0x0803, 0x0404, 0x3C01, 0x3801, 0x1002 } },
    { "00000001", 16, { 0x000B, 0x2002, 0x1003, 0x000A, 0x0804, 0x1C02, 0x5401, 0x5001,
                        0x0009, 0x4C01, 0x4801, 0x0405, 0x0C03, 0x0008, 0x1802, 0x4401 } },
    { "000000001", 16, { 0x2802, 0x2402, 0x1403, 0x0C04, 0x0805, 0x0407, 0x0406, 0x000F,
                         0x000E, 0x000D, 0x000C, 0x6801, 0x6401, 0x6001, 0x5C01, 0x5801 } },
    { "0000000001", 16, { 0x001F, 0x001E, 0x001D, 0x001C, 0x001B, 0x001A, 0x0019, 0x0018,
                          0x0017, 0x0016, 0x0015, 0x0014, 0x0013, 0x0012, 0x0011, 0x0010 } },
    { "00000000001", 16, { 0x0028, 0x0027, 0x0026, 0x0025, 0x0024, 0x0023, 0x0022, 0x0021,
                           0x0020, 0x040E, 0x040D, 0x040C, 0x040B, 0x040A, 0x0409, 0x0408 } },
    { "000000000001", 16, { 0x0412, 0x0411, 0x0410, 0x040F, 0x1803, 0x4002, 0x3C02, 0x3802,
                            0x3402, 0x3002, 0x2C02, 0x7C01, 0x7801, 0x7401, 0x7001, 0x6C01 } },
};

#define SHORT_BITS  9
#define END_OF_BLOCK (2 | 63 << 4)      // "10": run 63 takes the block past its last value

// The code the next 9 bits start with: length | run << 4 | level << 10 (6 bits, signed); 0 for one with
// 5 or more leading zeros (the escape, or a long code).
static uint16_t gShort[1 << SHORT_BITS];
// Codes with 6 to 11 leading zeros: the MDEC values their x bits index.
static uint16_t gLong[6][16];
// v + 512 -> v clamped to -128..127, plus 128
static uint8_t gClamp[1024];

// the 64 quant table bytes for STR movies, in zigzag order (psx-spx)
static const uint8_t kQuant[64] = {
    2, 16, 16, 19, 16, 19, 22, 22, 22, 22, 22, 22, 26, 24, 26, 27,
    27, 27, 26, 26, 26, 26, 27, 27, 27, 29, 29, 29, 34, 34, 34, 29,
    29, 29, 27, 27, 29, 29, 32, 32, 34, 34, 37, 38, 37, 35, 35, 34,
    35, 38, 38, 40, 40, 40, 48, 48, 46, 46, 56, 56, 58, 69, 69, 83 };
// zigzag scan index -> place in the block (psx-spx's zagzig)
static const uint8_t kZagzig[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };
// Every product is 32 x 32 -> 64 bits with its middle 32 bits kept (the SH-2's DMULS.L and XTRCT): a
// product >> 16, rounded down. So the constants carry 16 fraction bits, the pixels 8 (FRAC) through the
// IDCT into the colour, rounded once there (tools/mdec.py).
#define FRAC    8
// x 8 values -> scalezag (psx-spx) x the dequantised value in FRAC fraction bits, by place
static const int32_t kPrescale[64] = {
    262144, 363604, 342508, 308249, 262144, 205965, 141871, 72325, 363604, 504333, 475072, 427553, 363604,
    285682, 196781, 100318, 342508, 475072, 447508, 402746, 342508, 269107, 185364, 94498, 308249, 427553,
    402746, 362462, 308249, 242189, 166823, 85046, 262144, 363604, 342508, 308249, 262144, 205965, 141871,
    72325, 205965, 285682, 269107, 242189, 205965, 161826, 111468, 56826, 141871, 196781, 185364, 166823,
    141871, 111468, 76780, 39142, 72325, 100318, 94498, 85046, 72325, 56826, 39142, 19955 };
#define K1414   92682
#define K1847   121095
#define K2613   171254
#define K1082   70936
// YUV to RGB: R = Y + 1.402 Cr, G = Y - 0.3437 Cb - 0.7143 Cr, B = Y + 1.772 Cb; a pixel's Y + term +
// BIAS (the clamp table's offset and the rounding) >> FRAC indexes the clamp table
#define CR_R    91881
#define CR_G    46812
#define CB_G    22525
#define CB_B    116130
#define BIAS    ((512 << FRAC) + (1 << (FRAC - 1)))

#define MB_COLS 20

static uint32_t bitsOf(const char* s, int32_t* length)
{
    uint32_t v = 0;
    int32_t n = 0;
    for (; s[n]; n++) {
        v = v << 1 | (s[n] == '1');
    }
    *length = n;
    return v;
}

static void fillShort(uint32_t code, int32_t length, uint16_t entry)
{
    uint32_t base = code << (SHORT_BITS - length);
    for (uint32_t i = 0; i < (1u << (SHORT_BITS - length)); i++) {
        gShort[base + i] = entry;
    }
}

void mdecInit()
{
    for (uint32_t i = 0; i < (1 << SHORT_BITS); i++) {
        gShort[i] = 0;
    }
    fillShort(2, 2, END_OF_BLOCK);
    for (const CodeGroup& g : kCodes)
    {
        int32_t plen;
        uint32_t prefix = bitsOf(g.prefix, &plen);
        int32_t xbits = g.count > 8 ? 4 : g.count > 4 ? 3 : g.count > 2 ? 2 : g.count > 1 ? 1 : 0;
        int32_t zeros = 0;
        while (g.prefix[zeros] == '0') {
            zeros++;
        }
        for (int32_t x = 0; x < g.count; x++)
        {
            if (zeros >= 6)
            {
                gLong[zeros - 6][x] = g.values[x];
                continue;
            }
            int32_t run = g.values[x] >> 10, level = g.values[x] & 0x3FF;
            for (int32_t s = 0; s < 2; s++)
            {
                int32_t length = plen + xbits + 1;
                uint32_t code = (prefix << xbits | x) << 1 | s;
                int32_t l = s ? -level : level;
                fillShort(code, length, uint16_t(length | run << 4 | (l & 63) << 10));
            }
        }
    }
    for (int32_t v = -512; v < 512; v++) {
        gClamp[v + 512] = uint8_t((v < -128 ? -128 : v > 127 ? 127 : v) + 128);
    }
}

// k x x >> 16, rounded down: DMULS.L and XTRCT
static inline int32_t mul(int32_t k, int32_t x)
{
    return int32_t((int64_t(x) * k) >> 16);
}

// --- the bitstream ---------------------------------------------------------------------

static inline void refill(MdecBits* b)
{
    if (b->cnt <= 16)
    {
        b->buf |= uint32_t(*b->p++) << (16 - b->cnt);
        b->cnt += 16;
        if (b->cnt <= 16)
        {
            b->buf |= uint32_t(*b->p++) << (16 - b->cnt);
            b->cnt += 16;
        }
    }
}

static inline void consume(MdecBits* b, int32_t n)
{
    b->buf <<= n;
    b->cnt -= n;
}

void mdecPartStart(const MdecRecord* record, int part, MdecBits* bits, MdecCoef* coefs)
{
    bits->buf = 0;
    bits->cnt = 0;
    bits->p = (const uint16_t*)((const uint8_t*)record + (part ? record->part1 : sizeof(MdecRecord)));
    refill(bits);
    for (int32_t k = 0; k < 64; k++)
    {
        coefs[k].scale = int16_t(kQuant[k] * record->quant);
        coefs[k].pre = kPrescale[kZagzig[k]];
        coefs[k].raster = kZagzig[k];
    }
}

// A block's DC and AC values, dequantised, saturated to -400h..3FFh and prescaled for the IDCT, into the
// (cleared) block at their places; whether there was any AC value
int mdecParseBlock(MdecBits* b, const MdecCoef* coefs, int32_t* block)
{
    for (int32_t i = 0; i < 64; i += 8)
    {
        block[i] = 0;
        block[i + 1] = 0;
        block[i + 2] = 0;
        block[i + 3] = 0;
        block[i + 4] = 0;
        block[i + 5] = 0;
        block[i + 6] = 0;
        block[i + 7] = 0;
    }
    refill(b);
    int32_t dc = int32_t(b->buf) >> 22;     // 10 bits, signed
    consume(b, 10);
    block[0] = mul(kPrescale[0], dc * kQuant[0] * 8);
    int32_t k = 0;
    for (;;)
    {
        refill(b);
        uint32_t e = gShort[b->buf >> (32 - SHORT_BITS)];
        int32_t run, level;
        if (e & 15)
        {
            run = (e >> 4) & 63;
            level = int32_t(int16_t(e)) >> 10;
            consume(b, e & 15);
        }
        else if (b->buf & 0x04000000)       // "000001": the escape, 16 bits as they are
        {
            consume(b, 6);
            refill(b);
            uint32_t raw = b->buf >> 16;
            consume(b, 16);
            run = raw >> 10;
            level = int32_t(raw << 22) >> 22;
        }
        else                                // 6 to 11 leading zeros
        {
            int32_t zeros = 6;
            while (!(b->buf & (0x80000000u >> zeros)) && zeros < 11) {
                zeros++;
            }
            int32_t xbits = zeros == 6 ? 3 : 4;
            uint32_t w = b->buf << (zeros + 1);
            uint32_t v = gLong[zeros - 6][w >> (32 - xbits)];
            run = v >> 10;
            level = (w << xbits) & 0x80000000 ? -int32_t(v & 0x3FF) : int32_t(v & 0x3FF);
            consume(b, zeros + xbits + 2);
        }
        k += run + 1;
        if (k > 63)
            return k > 64;          // the end of block straight after the DC: k = 64
        // psx-spx: (level x quant x scale + 4) / 8, saturated to -400h..3FFh; kept x 8, unrounded,
        // saturated to the same range x 8 (the prescale divides by the 8)
        const MdecCoef& c = coefs[k];
        int32_t v = level * c.scale;
        v = v < -0x2000 ? -0x2000 : v > 0x1FFF ? 0x1FFF : v;
        block[c.raster] = mul(c.pre, v);
    }
}

// --- the IDCT (psx-spx's fast_idct_core, in fixed point) ---------------------------------

// Vector i is in[i], in[8 + i] ... in[56 + i]; its result goes to out[i * 8] ... out[i * 8 + 7].
static void idctPass(const int32_t* in, int32_t* out)
{
    for (int32_t i = 0; i < 8; i++, in++, out += 8)
    {
        int32_t s0 = in[0], s1 = in[8], s2 = in[16], s3 = in[24];
        int32_t s4 = in[32], s5 = in[40], s6 = in[48], s7 = in[56];
        if (!(s1 | s2 | s3 | s4 | s5 | s6 | s7))
        {
            out[0] = out[1] = out[2] = out[3] = out[4] = out[5] = out[6] = out[7] = s0;
            continue;
        }
        int32_t z10 = s0 + s4, z11 = s0 - s4;
        int32_t z13 = s2 + s6, z12 = mul(K1414, s2 - s6) - z13;
        int32_t t0 = z10 + z13, t3 = z10 - z13, t1 = z11 + z12, t2 = z11 - z12;
        z13 = s3 + s5;
        z10 = s3 - s5;
        z11 = s1 + s7;
        z12 = s1 - s7;
        int32_t z5 = mul(K1847, z12 - z10);
        int32_t t7 = z11 + z13;
        int32_t t6 = mul(K2613, z10) + z5 - t7;
        int32_t t5 = mul(K1414, z11 - z13) - t6;
        int32_t t4 = mul(K1082, z12) - z5 + t5;
        out[0] = t0 + t7;
        out[1] = t1 + t6;
        out[2] = t2 + t5;
        out[3] = t3 - t4;
        out[4] = t3 + t4;
        out[5] = t2 - t5;
        out[6] = t1 - t6;
        out[7] = t0 - t7;
    }
}

void mdecIdct(int32_t* block)
{
    int32_t temp[64];
    idctPass(block, temp);
    idctPass(temp, block);
}

// --- YUV to RGB ---------------------------------------------------------------------------

// A macroblock's pixels from its blocks (Cr, Cb, Y1..Y4): one Cr/Cb value colours a 2x2 square.
void mdecColour(int32_t b[6][64], uint32_t* dst, int pitch)
{
    for (int32_t cy = 0; cy < 8; cy++)
    {
        for (int32_t cx = 0; cx < 8; cx++)
        {
            int32_t cr = b[0][cy * 8 + cx], cb = b[1][cy * 8 + cx];
            int32_t r = mul(CR_R, cr) + BIAS;
            int32_t g = mul(-CB_G, cb) + mul(-CR_G, cr) + BIAS;
            int32_t bl = mul(CB_B, cb) + BIAS;
            const int32_t* y = b[2 + (cy >> 2) * 2 + (cx >> 2)] + (cy & 3) * 16 + (cx & 3) * 2;
            uint32_t* d = dst + cy * 2 * pitch + cx * 2;
            for (int32_t dy = 0; dy < 2; dy++, y += 8, d += pitch)
            {
                for (int32_t dx = 0; dx < 2; dx++)
                {
                    uint32_t v = y[dx];
                    d[dx] = 0x80000000u | uint32_t(gClamp[((v + bl) >> FRAC) & 1023]) << 16
                          | uint32_t(gClamp[((v + g) >> FRAC) & 1023]) << 8 | gClamp[((v + r) >> FRAC) & 1023];
                }
            }
        }
    }
}

// --- a part --------------------------------------------------------------------------------

void mdecDecode(const MdecRecord* record, int part, uint32_t* picture, int pitch)
{
    MdecBits bits;
    MdecCoef coefs[64];
    int32_t blocks[6][64];
    mdecPartStart(record, part, &bits, coefs);
    int32_t m = part ? record->split : 0;
    int32_t end = part ? record->mbs : record->split;
    int32_t row = m / MB_COLS, col = m % MB_COLS;
    for (; m < end; m++)
    {
        for (int32_t i = 0; i < 6; i++)
        {
            if (mdecParseBlock(&bits, coefs, blocks[i]))
            {
                mdecIdctAsm(blocks[i]);
                continue;
            }
            int32_t* b = blocks[i];
            int32_t dc = b[0];
            for (int32_t j = 0; j < 64; j += 8)
            {
                b[j] = dc;
                b[j + 1] = dc;
                b[j + 2] = dc;
                b[j + 3] = dc;
                b[j + 4] = dc;
                b[j + 5] = dc;
                b[j + 6] = dc;
                b[j + 7] = dc;
            }
        }
        mdecColour(blocks, picture + row * 16 * pitch + col * 16, pitch);
        if (++col == MB_COLS)
        {
            col = 0;
            row++;
        }
    }
}
