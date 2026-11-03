// A Sega FILM file is a header (FDSC: the formats, STAB: a table of chunks,
// each with its offset, size and time) followed by the chunks: Cinepak frames
// and blocks of 16-bit audio, the left channel's samples then the right's.
//
// The file streams from the CD into a ring in low work RAM. Audio chunks go
// to two looping SCSP slots, one per channel, ahead of where they play; the
// sound's play position is the clock. Once its time comes each frame is decoded
// over the last in work RAM (Cinepak keeps blocks from frame to frame) and
// copied to the VDP2 bitmap whole, while the beam is elsewhere.

#include "fmv.h"
#include "render.h"
#include "cd.h"
#ifdef HW_LOG
#include "font.h"
#endif
#include "hw.h"
#include "input.h"
#include "screen.h"
#include "sound.h"
#include "system.h"
#include "render.h"

#include <string.h>

void sndStop(); // OpenLara's (sound.cpp)

// low work RAM, free between levels
#define RING            ((uint8_t*)0x00200000)
#define RING_SIZE       (768 * 1024)
#define PICTURE         ((uint32_t*)0x002C0000)     // 128 KB: the largest video's 320 x 200
#define HEADER          ((uint8_t*)0x002E0000)
#define HEADER_MAX      (64 * 1024)
#define SCRATCH         ((uint8_t*)0x002F0000)  // a chunk that wraps the ring
#define SCRATCH_MAX     (64 * 1024)
#define PREROLL         (128 * 1024)    // in the ring before the sound starts

#define SCSP_REG(x)     (*(volatile uint16_t*)(0x25B00000 + (x)))
#define SCSP_SLOT(slot, reg) SCSP_REG((slot) * 0x20 + (reg) * 2)
#define KYONEX          0x1000
#define KYONB           0x0800
#define LPCTL_LOOP      0x0020

// A channel's ring of samples in sound RAM, played by a looping slot at
// 22265 Hz (octave -1 + 10/1024), as near as the SCSP gets to the files' 22254;
// the left's slot and the right's after it, clear of the sound CPU's voices
// (sound.68k) and the CD audio's.
#define AUDIO_SLOT      24
#define AUDIO_RING      32768
#define AUDIO_LEFT      0x10000
#define AUDIO_RIGHT     0x20000
#define AUDIO_PITCH     ((0xF << 11) | 10)
#define SAMPLES_PER_100_VBL 37146 // at 59.94 vblanks a second
#define AUDIO_MARGIN    2048    // samples kept clear of the play position
#define FILE_RATE       22254
#define TIME_BASE       30      // chunk times are in 1/30 s

#define AUDIO_CHUNK     0xFFFFFFFF  // a chunk's time field when it holds sound

static uint32_t be16(const uint8_t* p) { return (p[0] << 8) | p[1]; }
static uint32_t be24(const uint8_t* p) { return (p[0] << 16) | (p[1] << 8) | p[2]; }
static uint32_t be32(const uint8_t* p) { return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

// --- Cinepak -------------------------------------------------------------

#define MAX_STRIPS 4

// Codebooks as the 15-bit pixels they draw: a V1 entry fills a 4x4 block from
// its 2x2 pixels doubled (one pixel pair per quarter), a V4 entry one 2x2
// quarter of a block (its top and bottom pair).
struct Codebooks
{
    uint32_t v1[256][4];
    uint32_t v4[256][2];
};

// In the frame's command buffer while a video plays (renderScratch)
static Codebooks* gBooks;

static uint32_t rgb(int32_t r, int32_t g, int32_t b)
{
    r = (r < 0) ? 0 : (r > 255) ? 255 : r;
    g = (g < 0) ? 0 : (g > 255) ? 255 : g;
    b = (b < 0) ? 0 : (b > 255) ? 255 : b;
    return 0x8000 | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3);
}

// Chunks 0x20-0x27: full (even) or partial (odd, with a bit per entry)
// updates, of 6-byte YUV entries or 4-byte greyscale ones (bit 2).
static void decodeCodebook(Codebooks* books, uint32_t id, const uint8_t* data, const uint8_t* end)
{
    bool v4 = (id & 0x02) == 0;
    uint32_t n = (id & 0x04) ? 4 : 6;
    uint32_t flag = 0, mask = 0;

    for (uint32_t i = 0; i < 256; i++)
    {
        if ((id & 0x01) && !(mask >>= 1))
        {
            if (data + 4 > end)
                return;
            flag = be32(data);
            data += 4;
            mask = 0x80000000;
        }

        if ((id & 0x01) && !(flag & mask))
            continue;

        if (data + n > end)
            return;

        uint32_t p[4];
        if (n == 6)
        {
            int32_t u = int8_t(data[4]);
            int32_t v = int8_t(data[5]);
            for (int32_t k = 0; k < 4; k++)
            {
                int32_t y = data[k];
                p[k] = rgb(y + v * 2, y - u / 2 - v, y + u * 2);
            }
        }
        else
        {
            for (int32_t k = 0; k < 4; k++) {
                p[k] = rgb(data[k], data[k], data[k]);
            }
        }
        data += n;

        if (v4)
        {
            books->v4[i][0] = (p[0] << 16) | p[1];
            books->v4[i][1] = (p[2] << 16) | p[3];
        }
        else
        {
            for (int32_t k = 0; k < 4; k++) {
                books->v1[i][k] = p[k] | (p[k] << 16);
            }
        }
    }
}

// Chunks 0x30 (every block coded, a bit each for V1 or V4), 0x31 (a bit for
// coded or kept first) and 0x32 (V1 only). dst is the strip's top left in
// the picture, stride its pixel pairs a line.
static void decodeVectors(const Codebooks* books, uint32_t id, const uint8_t* data, const uint8_t* end,
                          uint32_t* dst, int32_t stride, int32_t width, int32_t height)
{
    uint32_t flag = 0, mask = 0;

    for (int32_t y = 0; y < height; y += 4, dst += stride * 4)
    {
        uint32_t* row = dst;

        for (int32_t x = 0; x < width; x += 4, row += 2)
        {
            if ((id & 0x01) && !(mask >>= 1))
            {
                if (data + 4 > end)
                    return;
                flag = be32(data);
                data += 4;
                mask = 0x80000000;
            }

            if ((id & 0x01) && !(flag & mask))
                continue; // kept from the last frame

            if (!(id & 0x02) && !(mask >>= 1))
            {
                if (data + 4 > end)
                    return;
                flag = be32(data);
                data += 4;
                mask = 0x80000000;
            }

            if ((id & 0x02) || !(flag & mask))
            {
                if (data >= end)
                    return;
                const uint32_t* e = books->v1[*data++];
                row[0] = e[0]; row[1] = e[1];
                row[stride] = e[0]; row[stride + 1] = e[1];
                row[stride * 2] = e[2]; row[stride * 2 + 1] = e[3];
                row[stride * 3] = e[2]; row[stride * 3 + 1] = e[3];
            }
            else
            {
                if (data + 4 > end)
                    return;
                const uint32_t* a = books->v4[data[0]];
                const uint32_t* b = books->v4[data[1]];
                const uint32_t* c = books->v4[data[2]];
                const uint32_t* d = books->v4[data[3]];
                data += 4;
                row[0] = a[0]; row[1] = b[0];
                row[stride] = a[1]; row[stride + 1] = b[1];
                row[stride * 2] = c[0]; row[stride * 2 + 1] = d[0];
                row[stride * 3] = c[1]; row[stride * 3 + 1] = d[1];
            }
        }
    }
}

static void decodeFrame(const uint8_t* data, uint32_t size, uint32_t* dst, int32_t stride)
{
    if (size < 12)
        return;

    const uint8_t* end = data + size;
    uint32_t flags = data[0];
    uint32_t strips = be16(data + 8);

    // Sega's Cinepak has extra bytes after the frame header: 6 in a couple of
    // files, 2 in the rest.
    data += (data[10] == 0xFE && data[11] == 0x00) ? 16 : 12;

    int32_t y0 = 0;
    for (uint32_t i = 0; i < strips && i < MAX_STRIPS && data + 12 <= end; i++)
    {
        const uint8_t* stripEnd = data + be24(data + 1);
        if (stripEnd > end) {
            stripEnd = end;
        }

        int32_t top = be16(data + 4);
        int32_t bottom = be16(data + 8);
        int32_t left = be16(data + 6);
        int32_t right = be16(data + 10);
        if (!top)
        {
            top = y0;
            bottom += y0;
        }
        y0 = bottom;

        // strips start from the one before's codebooks unless told otherwise
        if (i > 0 && !(flags & 1)) {
            gBooks[i] = gBooks[i - 1];
        }

        const uint8_t* chunk = data + 12;
        while (chunk + 4 <= stripEnd)
        {
            uint32_t id = chunk[0];
            const uint8_t* body = chunk + 4;
            const uint8_t* bodyEnd = chunk + be24(chunk + 1);
            if (bodyEnd > stripEnd || bodyEnd < body) {
                bodyEnd = stripEnd;
            }

            if (id >= 0x20 && id <= 0x27) {
                decodeCodebook(gBooks + i, id, body, bodyEnd);
            } else if (id >= 0x30 && id <= 0x32) {
                decodeVectors(gBooks + i, id, body, bodyEnd, dst + top * stride + (left >> 1), stride, right - left,
                              bottom - top);
                break;
            }
            chunk = bodyEnd;
        }

        data = stripEnd;
    }
}

// --- Playback -------------------------------------------------------------

static uint16_t gPad;

// START newly pressed
static bool skipPressed()
{
    uint16_t old = gPad;
    gPad = padButtons();
    return (gPad & PAD_START) && !(old & PAD_START);
}

static void audioStart()
{
    static const uint32_t addr[2] = { AUDIO_LEFT, AUDIO_RIGHT };
    static const uint16_t pan[2] = { 0x1F << 8, 0x0F << 8 }; // left only, right only

    for (int32_t c = 0; c < 2; c++)
    {
        int32_t s = AUDIO_SLOT + c;
        SCSP_SLOT(s, 0) = LPCTL_LOOP | ((addr[c] >> 16) & 15);
        SCSP_SLOT(s, 1) = addr[c];
        SCSP_SLOT(s, 2) = 0;
        SCSP_SLOT(s, 3) = AUDIO_RING - 1;
        SCSP_SLOT(s, 4) = 0x001F;
        SCSP_SLOT(s, 5) = 0x001F;
        SCSP_SLOT(s, 6) = 0;
        SCSP_SLOT(s, 7) = 0;
        SCSP_SLOT(s, 8) = AUDIO_PITCH;
        SCSP_SLOT(s, 9) = 0;
        SCSP_SLOT(s, 10) = 0;
        SCSP_SLOT(s, 11) = (7 << 13) | pan[c];
    }
    SCSP_SLOT(AUDIO_SLOT, 0) = SCSP_SLOT(AUDIO_SLOT, 0) | KYONB;
    SCSP_SLOT(AUDIO_SLOT + 1, 0) = SCSP_SLOT(AUDIO_SLOT + 1, 0) | KYONB | KYONEX;
}

static void audioStop()
{
    SCSP_SLOT(AUDIO_SLOT, 0) = SCSP_SLOT(AUDIO_SLOT, 0) & ~(KYONB | KYONEX);
    SCSP_SLOT(AUDIO_SLOT + 1, 0) = (SCSP_SLOT(AUDIO_SLOT + 1, 0) & ~KYONB) | KYONEX;
}

// copies count samples of one channel into its ring from sample pos
static void audioWrite(uint32_t ring, uint32_t pos, const uint8_t* src, uint32_t count)
{
    volatile uint16_t* dst = (volatile uint16_t*)(SOUND_RAM + ring);
    const uint16_t* s = (const uint16_t*)src;

    for (uint32_t i = 0; i < count; i++) {
        dst[(pos + i) & (AUDIO_RING - 1)] = s[i];
    }
}

// The picture to the bitmap, begun as the beam passes below it: copied before
// the beam comes round to it again.
static void showPicture(volatile uint32_t* dst, int32_t width, int32_t height, int32_t top)
{
    const int32_t below = top + height;
    const uint32_t start = gVblankCount;
    for (;;)
    {
        int32_t line = VDP2_VCNT & 0x1FF;
        if ((line >= below && line < below + 8) || gVblankCount - start > 1)
            break;
    }
    const uint32_t* src = PICTURE;
    const int32_t pairs = width / 2;
    for (int32_t y = 0; y < height; y++, dst += 256, src += pairs)
    {
        for (int32_t x = 0; x < pairs; x++) {
            dst[x] = src[x];
        }
    }
}

#ifdef HW_LOG

// Every sector of a video summed as it arrives (kept in VDP2's second bank, which
// nothing else uses between levels), again as a frame is decoded from it, and
// again read a second time when the video ends: what the boxes in frames on the
// Saturn come from. The counts show over the picture.
#define SUMS            ((volatile uint32_t*)VDP2_BANK_B)
#define SUMS_MAX        (0x3FF00 / 4)

static uint32_t gArrived;       // sectors summed
static uint32_t gChanged, gChangedAt;   // differing when decoded; the first
static uint32_t gMisread, gMisreadAt;   // differing when read again; the first

static uint32_t sectorSum(const uint8_t* sector)
{
    const uint32_t* p = (const uint32_t*)sector;
    uint32_t sum = 0;
    for (int32_t i = 0; i < CD_SECTOR_SIZE / 4; i++) {
        sum = (sum << 1) + (sum >> 31) + p[i];
    }
    return sum;
}

static void checkArrived(const uint8_t* sectors, int32_t count)
{
    for (int32_t i = 0; i < count && gArrived < SUMS_MAX; i++) {
        SUMS[gArrived++] = sectorSum(sectors + i * CD_SECTOR_SIZE);
    }
}

// The chunk at ring offset .. offset + size, sectors counted from the ring's first.
static void checkDecoded(uint32_t offset, uint32_t size)
{
    for (uint32_t k = offset / CD_SECTOR_SIZE; k <= (offset + size - 1) / CD_SECTOR_SIZE && k < gArrived; k++)
    {
        if (sectorSum(RING + (k * CD_SECTOR_SIZE) % RING_SIZE) != SUMS[k])
        {
            if (!gChanged++) {
                gChangedAt = k;
            }
        }
    }
}

static void checkText(volatile uint16_t* bitmap, int32_t x, int32_t y, const char* text)
{
    for (; *text; text++, x += 8)
    {
        const uint8_t* glyph = gFont8[*text - FONT8_FIRST];
        for (int32_t r = 0; r < 8; r++)
        {
            for (int32_t c = 0; c < 8; c++) {
                bitmap[(y + r) * 512 + x + c] = (glyph[r] >> c) & 1 ? 0xFFFF : 0x8000;
            }
        }
    }
}

static void checkNumber(char* at, uint32_t value)
{
    for (int32_t i = 5; i >= 0; i--, value /= 10) {
        at[i] = '0' + value % 10;
    }
}

// Reads what arrived again, and shows the counts: until START if anything differed.
static void checkEnd(volatile uint16_t* bitmap, uint32_t lba)
{
    static char changed[] = "CHANGED IN RAM 000000 AT 000000";
    static char misread[] = "MISREAD ON CD  000000 AT 000000";
    static char checked[] = "CHECKED        000000";
    gMisread = 0;
    gPad = 0xFFFF;
    checkText(bitmap, 16, 8, "CHECKING... START SKIPS");
    uint32_t k = 0;
    while (k < gArrived && !skipPressed())
    {
        uint32_t n = gArrived - k < 64 ? gArrived - k : 64;
        if (cdRead(lba + k, n * CD_SECTOR_SIZE, RING) != 0)
            break;
        for (uint32_t i = 0; i < n; i++)
        {
            if (sectorSum(RING + i * CD_SECTOR_SIZE) != SUMS[k + i])
            {
                if (!gMisread++) {
                    gMisreadAt = k + i;
                }
            }
        }
        k += n;
    }
    checkNumber(changed + 15, gChanged);
    checkNumber(changed + 25, gChangedAt);
    checkNumber(misread + 15, gMisread);
    checkNumber(misread + 25, gMisreadAt);
    checkNumber(checked + 15, k);
    checkText(bitmap, 16, 8, changed);
    checkText(bitmap, 16, 16, misread);
    checkText(bitmap, 16, 24, checked);
    gPad = 0xFFFF;
    const uint32_t start = gVblankCount;
    while (!skipPressed() && ((gChanged || gMisread) || gVblankCount - start < 180));
}
#endif

void fmvPlay(const char* name)
{
    gBooks = (Codebooks*)renderScratch(sizeof(Codebooks) * MAX_STRIPS);
    char path[32] = "FMV/";
    strcat(path, name);
    strcat(path, ".CPK");

    uint32_t lba, fileSize;
    if (cdFind(path, &lba, &fileSize) != 0)
        return;

    gBooks = (Codebooks*)renderScratch(sizeof(Codebooks) * MAX_STRIPS);

    // the header
    if (cdRead(lba, CD_SECTOR_SIZE, HEADER) != 0 || memcmp(HEADER, "FILM", 4) != 0)
        return;
    uint32_t headerSize = be32(HEADER + 4);
    if (headerSize > HEADER_MAX || cdRead(lba, headerSize, HEADER) != 0)
        return;

    const uint8_t* fdsc = HEADER + 16;
    if (memcmp(fdsc, "FDSC", 4) != 0 || memcmp(fdsc + 8, "cvid", 4) != 0)
        return;
    int32_t height = be32(fdsc + 12);
    int32_t width = be32(fdsc + 16);

    const uint8_t* stab = fdsc + be32(fdsc + 4);
    if (memcmp(stab, "STAB", 4) != 0)
        return;
    uint32_t count = be32(stab + 12);
    const uint8_t* table = stab + 16;

    // the picture area, centred
    volatile uint16_t* bitmap = screenBlank();
    const int32_t pictureTop = (SCREEN_HEIGHT - height) / 2;
    volatile uint32_t* frameDst = (volatile uint32_t*)bitmap + pictureTop * 256 + ((SCREEN_WIDTH - width) / 2) / 2;
    memset(PICTURE, 0, width * height * 2);

    // sound
    sndStop();
    for (uint32_t i = 0; i < AUDIO_RING; i++)
    {
        ((volatile uint16_t*)(SOUND_RAM + AUDIO_LEFT))[i] = 0;
        ((volatile uint16_t*)(SOUND_RAM + AUDIO_RIGHT))[i] = 0;
    }

    // stream from the sector holding the first chunk; ring offsets count
    // from that sector's start
    uint32_t firstSector = headerSize / CD_SECTOR_SIZE;
    uint32_t sectorsLeft = (fileSize + CD_SECTOR_SIZE - 1) / CD_SECTOR_SIZE - firstSector;
    uint32_t base = firstSector * CD_SECTOR_SIZE;
    if (cdStreamStart(lba + firstSector, sectorsLeft) != 0)
        return;

    uint32_t received = 0;      // bytes in the ring since base
    uint32_t written = 0;       // samples per channel sent to the sound rings
    uint32_t audioStartVbl = 0;
    bool audioOn = false;
    bool started = false;

    gPad = 0xFFFF; // a START still held from before doesn't skip
#ifdef HW_LOG
    gArrived = gChanged = gMisread = 0;
#endif

    for (uint32_t index = 0; index < count; )
    {
        if (skipPressed())
            break;

        const uint8_t* entry = table + index * 16;
        uint32_t offset = headerSize + be32(entry) - base;
        uint32_t size = be32(entry + 4);
        uint32_t time = be32(entry + 8);

        // keep the ring filled, up to the chunk we're on
        uint32_t keep = offset & ~(CD_SECTOR_SIZE - 1);
        while (sectorsLeft > 0)
        {
            uint32_t pos = received % RING_SIZE;
            uint32_t space = (RING_SIZE - (received - keep)) / CD_SECTOR_SIZE;
            uint32_t untilEnd = (RING_SIZE - pos) / CD_SECTOR_SIZE;
            uint32_t want = space < untilEnd ? space : untilEnd;
            if (want > sectorsLeft) {
                want = sectorsLeft;
            }
            if (!want)
                break;

            int32_t got = cdStreamRead(RING + pos, want);
            if (got <= 0)
                break;
#ifdef HW_LOG
            checkArrived(RING + pos, got);
#endif
            received += got * CD_SECTOR_SIZE;
            sectorsLeft -= got;
        }

        // a start the sound won't outrun
        if (!started && received < PREROLL && sectorsLeft > 0)
            continue;
        started = true;

        if (offset + size > received)
        {
            if (sectorsLeft == 0)
                break; // the file ended short
            continue;
        }

        const uint8_t* data = RING + offset % RING_SIZE;
        if (offset % RING_SIZE + size > RING_SIZE)
        {
            if (size > SCRATCH_MAX)
                break;
            uint32_t first = RING_SIZE - offset % RING_SIZE;
            memcpy(SCRATCH, data, first);
            memcpy(SCRATCH + first, RING, size - first);
            data = SCRATCH;
        }

        uint32_t played = audioOn ? (gVblankCount - audioStartVbl) * SAMPLES_PER_100_VBL / 100 : 0;

        if (time == AUDIO_CHUNK)
        {
            uint32_t samples = size / 4;
            if (audioOn && written + samples > played + AUDIO_RING - AUDIO_MARGIN)
                continue; // no room yet

            audioWrite(AUDIO_LEFT, written, data, samples);
            audioWrite(AUDIO_RIGHT, written, data + samples * 2, samples);
            written += samples;

            if (!audioOn)
            {
                audioStart();
                audioStartVbl = gVblankCount;
                audioOn = true;
            }
        }
        else
        {
            // a frame shows once the sound reaches its time
            uint32_t due = (time & 0x7FFFFFFF) * FILE_RATE / TIME_BASE;
            if (audioOn && played < due)
                continue;
#ifdef HW_LOG
            checkDecoded(offset, size);
#endif
            decodeFrame(data, size, PICTURE, width / 2);
            showPicture(frameDst, width, height, pictureTop);
        }

        index++;
    }

    // let the sound play out, unless skipped
    while (audioOn && !skipPressed())
    {
        uint32_t played = (gVblankCount - audioStartVbl) * SAMPLES_PER_100_VBL / 100;
        if (played >= written)
            break;
    }

    if (audioOn) {
        audioStop();
    }
#ifdef HW_LOG
    checkEnd(bitmap, lba + firstSector);
#endif
}

void fmvPicture(const char* name, int width, int frames)
{
    screenShow(name, width);

    gPad = 0xFFFF;
    uint32_t start = gVblankCount;
    while (int32_t(gVblankCount - start) < frames && !skipPressed());
}
