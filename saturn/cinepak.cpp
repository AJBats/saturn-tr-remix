// Cinepak to 24-bit pixels (cinepak.h). The bitstream as saturn/fmv.cpp reads it, whose Cinepak decoder
// follows the structure of FFmpeg's libavcodec/cinepak.c (LGPL-2.1-or-later): a frame header (flags,
// size, width, height, strips), then strips, each a header (id, size, top, left, bottom, right) and
// chunks: codebooks, then the vectors.

#include "cinepak.h"

#define OPAQUE 0x80000000

// Each channel clamped to 0-255 and moved to its place in the pixel, indexed by value + 256: a codebook
// entry's sums run from -256 to 509.
static uint32_t gR[768], gG[768], gB[768];

void cpkInit()
{
    for (int32_t i = 0; i < 768; i++)
    {
        int32_t v = i - 256;
        uint32_t c = v < 0 ? 0 : v > 255 ? 255 : v;
        gR[i] = c;
        gG[i] = c << 8;
        gB[i] = OPAQUE | (c << 16);
    }
}

static inline uint32_t be16(const uint8_t* p) { return (p[0] << 8) | p[1]; }
static inline uint32_t be24(const uint8_t* p) { return (p[0] << 16) | (p[1] << 8) | p[2]; }
static inline uint32_t be32(const uint8_t* p) { return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

int32_t cpkStrips(const uint8_t* frame)
{
    return be16(frame + 8);
}

// Chunks 0x20-0x27, into book (V4 or V1 entries): every entry from 0 (even ids) or those a bit each picks
// (odd), 6-byte YUV entries or 4-byte grey ones (bit 2).
static void codebook(uint32_t (*book)[4], uint32_t id, const uint8_t* d, const uint8_t* end)
{
    const int32_t n = (id & 0x04) ? 4 : 6;
    uint32_t flag = 0, mask = 0;

    for (int32_t i = 0; i < 256; i++)
    {
        if (id & 0x01)
        {
            if (!(mask >>= 1))
            {
                if (d + 4 > end)
                    return;
                flag = be32(d);
                d += 4;
                mask = 0x80000000;
            }
            if (!(flag & mask))
                continue;
        }
        if (d + n > end)
            return;

        int32_t cr = 256, cg = 256, cb = 256;
        if (n == 6)
        {
            int32_t u = int8_t(d[4]);
            int32_t v = int8_t(d[5]);
            cr += v * 2;
            cg -= u / 2 + v;
            cb += u * 2;
        }
        uint32_t* e = book[i];
        for (int32_t k = 0; k < 4; k++)
        {
            int32_t y = d[k];
            e[k] = gR[y + cr] | gG[y + cg] | gB[y + cb];
        }
        d += n;
    }
}

#define REFILL(fail)                    \
    if (!(mask >>= 1))                  \
    {                                   \
        if (d + 4 > end)                \
            fail;                       \
        flag = be32(d);                 \
        d += 4;                         \
        mask = 0x80000000;              \
    }

// Chunks 0x30 (every block coded, a bit each: V4 if set), 0x31 (first a bit each for coded, kept from
// the last frame if clear) and 0x32 (every block V1). pic is the strip's top left.
static void vectors(const CpkBook* book, uint32_t id, const uint8_t* d, const uint8_t* end, uint32_t* pic,
                    int32_t pitch, int32_t width, int32_t height)
{
    uint32_t flag = 0, mask = 0;

    for (int32_t y = 0; y < height; y += 4, pic += pitch * 4)
    {
        uint32_t* r0 = pic;
        for (int32_t x = 0; x < width; x += 4, r0 += 4)
        {
            if (id & 0x01)
            {
                REFILL(return)
                if (!(flag & mask))
                    continue;
            }
            bool v4 = false;
            if (!(id & 0x02))
            {
                REFILL(return)
                v4 = flag & mask;
            }

            uint32_t* r1 = r0 + pitch;
            uint32_t* r2 = r1 + pitch;
            uint32_t* r3 = r2 + pitch;
            if (v4)
            {
                if (d + 4 > end)
                    return;
                const uint32_t* a = book->v4[d[0]];
                const uint32_t* b = book->v4[d[1]];
                const uint32_t* c = book->v4[d[2]];
                const uint32_t* e = book->v4[d[3]];
                d += 4;
                r0[0] = a[0]; r0[1] = a[1]; r0[2] = b[0]; r0[3] = b[1];
                r1[0] = a[2]; r1[1] = a[3]; r1[2] = b[2]; r1[3] = b[3];
                r2[0] = c[0]; r2[1] = c[1]; r2[2] = e[0]; r2[3] = e[1];
                r3[0] = c[2]; r3[1] = c[3]; r3[2] = e[2]; r3[3] = e[3];
            }
            else
            {
                if (d >= end)
                    return;
                const uint32_t* q = book->v1[*d++];
                uint32_t q0 = q[0], q1 = q[1], q2 = q[2], q3 = q[3];
                r0[0] = q0; r0[1] = q0; r0[2] = q1; r0[3] = q1;
                r1[0] = q0; r1[1] = q0; r1[2] = q1; r1[3] = q1;
                r2[0] = q2; r2[1] = q2; r2[2] = q3; r2[3] = q3;
                r3[0] = q2; r3[1] = q2; r3[2] = q3; r3[3] = q3;
            }
        }
    }
}

void cpkDecode(const uint8_t* frame, uint32_t size, uint32_t* pic, int32_t pitch, CpkBook* book,
               int32_t first, int32_t last)
{
    if (size < 10)
        return;

    const uint8_t* end = frame + size;
    const int32_t strips = be16(frame + 8);
    const uint8_t* d = frame + 10;
    int32_t y0 = 0;

    for (int32_t s = 0; s < strips && s < last && d + 12 <= end; s++)
    {
        const uint8_t* stripEnd = d + be24(d + 1);
        if (stripEnd > end) {
            stripEnd = end;
        }
        int32_t top = be16(d + 4);
        int32_t left = be16(d + 6);
        int32_t bottom = be16(d + 8);
        int32_t right = be16(d + 10);
        if (!top)
        {
            top = y0;   // a strip placed below the one before
            bottom += y0;
        }
        y0 = bottom;

        if (s >= first)
        {
            const uint8_t* c = d + 12;
            while (c + 4 <= stripEnd)
            {
                uint32_t id = c[0];
                const uint8_t* body = c + 4;
                const uint8_t* cend = c + be24(c + 1);
                if (cend > stripEnd || cend < body) {
                    cend = stripEnd;
                }
                if (id >= 0x20 && id <= 0x27) {
                    codebook((id & 0x02) ? book->v1 : book->v4, id, body, cend);
                } else if (id >= 0x30 && id <= 0x32) {
                    vectors(book, id, body, cend, pic + top * pitch + left, pitch, right - left, bottom - top);
                    break;
                }
                c = cend;
            }
        }
        d = stripEnd;
    }
}
