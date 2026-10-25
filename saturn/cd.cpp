// CD access: file lookup on the ISO9660 data track and sector reads.
//
// The CD-block command layer (cdCommand and the commands built on it, init and
// sector transfer sequences) is adapted from libyaul's scu/bus/a/cs2/cd-block:
//
//   Copyright (c) 2012-2023 Israel Jacquez <mrkotfw@gmail.com>
//   Theo Berkau, Romulo Fernandes Machado
//
//   Permission is hereby granted, free of charge, to any person obtaining a
//   copy of this software and associated documentation files (the "Software"),
//   to deal in the Software without restriction, including without limitation
//   the rights to use, copy, modify, merge, publish, distribute, sublicense,
//   and/or sell copies of the Software, and to permit persons to whom the
//   Software is furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in
//   all copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//   DEALINGS IN THE SOFTWARE.
//
// The ISO9660 path lookup is our own (same layout as tools/mkdisc.py writes).

#include "cd.h"
#include "hw.h"

#include <string.h>

#define CD_DTR      REG16(0x25890000)
#define CD_HIRQ     REG16(0x25890008)
#define CD_CR1      REG16(0x25890018)
#define CD_CR2      REG16(0x2589001C)
#define CD_CR3      REG16(0x25890020)
#define CD_CR4      REG16(0x25890024)

// HIRQ bits
#define CMOK    0x0001  // command can be issued
#define DRDY    0x0002  // data transfer ready
#define CSCT    0x0004  // one sector read
#define PEND    0x0010  // play ended
#define ESEL    0x0040  // selector setup done
#define EHST    0x0080  // host I/O done
#define EFLS    0x0200  // file system processing done

// Status code (high byte of CR1)
#define STATUS_CODE(cr1)    (((cr1) >> 8) & 0x0F)
#define STATUS_WAIT         0x80
#define STATUS_REJECT       0xFF
#define STATUS_OPEN         0x06
#define STATUS_NO_DISC      0x07
#define STATUS_ERROR        0x09
#define STATUS_FATAL        0x0A

#define TIMEOUT 0x240000

#define ERR_BUSY    -1
#define ERR_REJECT  -2
#define ERR_STATUS  -3
#define ERR_TIMEOUT -4

#define LBA_TO_FAD(lba) ((lba) + 150)

struct CdRegs
{
    uint16_t cr1, cr2, cr3, cr4;
};

static inline uint32_t irqMaskSet(uint32_t level)
{
    uint32_t sr;
    asm volatile("stc sr, %0" : "=r"(sr));
    asm volatile("ldc %0, sr" : : "r"((sr & ~0xF0) | (level << 4)));
    return sr;
}

static inline void srRestore(uint32_t sr)
{
    asm volatile("ldc %0, sr" : : "r"(sr));
}

static int waitHirq(uint16_t flags)
{
    for (uint32_t i = 0; i < TIMEOUT; i++)
    {
        if (CD_HIRQ & flags)
            return 0;
    }
    return ERR_TIMEOUT;
}

// Issues a command and waits for its response. Clears CMOK and the HIRQ bits
// in hirqMask first, so callers can then wait for the command's completion bit.
static int cdCommand(uint16_t hirqMask, const CdRegs &in, CdRegs* out)
{
    if (waitHirq(CMOK) != 0)
        return ERR_BUSY;

    uint32_t sr = irqMaskSet(15);

    CD_HIRQ = ~(CMOK | hirqMask);   // writing 0 clears a bit, 1 leaves it
    CD_CR1 = in.cr1;
    CD_CR2 = in.cr2;
    CD_CR3 = in.cr3;
    CD_CR4 = in.cr4;

    int ret = waitHirq(CMOK);

    CdRegs res;
    res.cr1 = CD_CR1;
    res.cr2 = CD_CR2;
    res.cr3 = CD_CR3;
    res.cr4 = CD_CR4;

    srRestore(sr);

    if (ret != 0)
        return ret;

    uint8_t status = res.cr1 >> 8;
    if (status == STATUS_REJECT || (status & STATUS_WAIT))
        return ERR_REJECT;

    if (out)
        *out = res;

    return 0;
}

static int statusCheck(const CdRegs &res)
{
    switch (STATUS_CODE(res.cr1))
    {
        case STATUS_OPEN:
        case STATUS_NO_DISC:
        case STATUS_ERROR:
        case STATUS_FATAL:
            return ERR_STATUS;
    }
    return 0;
}

static int cmdChecked(uint16_t hirqMask, uint16_t cr1, uint16_t cr2, uint16_t cr3, uint16_t cr4)
{
    CdRegs in = { cr1, cr2, cr3, cr4 };
    CdRegs res;
    int ret = cdCommand(hirqMask, in, &res);
    return ret ? ret : statusCheck(res);
}

static int cmdEndTransfer()
{
    CdRegs in = { 0x0600, 0, 0, 0 };
    int ret = cdCommand(0, in, NULL);
    CD_HIRQ = ~DRDY;
    return ret;
}

static int cmdResetSelector(uint8_t flags, uint8_t selector)
{
    int ret = cmdChecked(EFLS, 0x4800 | flags, 0, selector << 8, 0);
    if (ret)
        return ret;
    return waitHirq(ESEL);
}

static int cmdSetSectorLength2048()
{
    return cmdChecked(ESEL, 0x6000, 0, 0, 0);
}

static int cmdConnectCdDevice(uint8_t filter)
{
    return cmdChecked(ESEL, 0x3000, 0, filter << 8, 0);
}

static int cmdPlay(uint32_t fad, uint32_t sectors)
{
    CD_HIRQ = ~(PEND | CSCT);
    // bit 7 of the low byte marks the position as a FAD, not a track number
    return cmdChecked(0, 0x1080 | (fad >> 16), fad, 0x0080 | (sectors >> 16), sectors);
}

static int cmdSectorCount(uint8_t buffer)
{
    CdRegs in = { 0x5100, 0, uint16_t(buffer << 8), 0 };
    CdRegs res;
    if (cdCommand(0, in, &res) != 0)
        return 0;
    return res.cr4;
}

// Moves up to size bytes from the front of buffer 0 into dst, freeing the sectors.
static int transfer(uint8_t* dst, uint32_t size)
{
    uint32_t sectors = (size + CD_SECTOR_SIZE - 1) / CD_SECTOR_SIZE;

    int ret = cmdChecked(EHST, 0x6300, 0, 0, sectors); // get then delete sector data
    if (ret)
        return ret;

    ret = waitHirq(DRDY | EHST);
    if (ret)
        return ret;

    uint16_t* words = (uint16_t*)dst;
    for (uint32_t i = 0; i < size / 2; i++)
    {
        words[i] = CD_DTR;
    }

    if (size & 1)
    {
        dst[size - 1] = CD_DTR >> 8;
    }

    return cmdEndTransfer();
}

int cdInit()
{
    int ret;

    if ((ret = waitHirq(CMOK)) != 0) return ret;
    if ((ret = cmdChecked(EFLS, 0x7500, 0, 0, 0)) != 0) return ret; // abort file
    if ((ret = cmdChecked(0, 0x0400, 0x0002, 0, 0x0001)) != 0) return ret; // init CD system
    if ((ret = cmdEndTransfer()) != 0) return ret;
    if ((ret = cmdResetSelector(0xFC, 0)) != 0) return ret;
    if ((ret = cmdSetSectorLength2048()) != 0) return ret;

    return 0;
}

int cdRead(uint32_t lba, uint32_t size, void* dst)
{
    int ret;

    uint32_t sectors = (size + CD_SECTOR_SIZE - 1) / CD_SECTOR_SIZE;

    if ((ret = cmdResetSelector(0, 0)) != 0) return ret;
    if ((ret = cmdConnectCdDevice(0)) != 0) return ret;
    if ((ret = cmdPlay(LBA_TO_FAD(lba), sectors)) != 0) return ret;

    uint8_t* ptr = (uint8_t*)dst;
    while (size > 0)
    {
        uint32_t ready;
        uint32_t i = 0;
        while ((ready = cmdSectorCount(0)) == 0)
        {
            if (++i == TIMEOUT)
                return ERR_TIMEOUT;
        }

        uint32_t count = ready * CD_SECTOR_SIZE;
        if (count > size)
            count = size;

        if ((ret = transfer(ptr, count)) != 0)
            return ret;

        ptr += count;
        size -= count;
    }

    return 0;
}

static uint32_t be32(const uint8_t* p)
{
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static uint16_t gSector[CD_SECTOR_SIZE / 2];

// Searches one directory for name; on success returns its extent and size.
static int findInDir(uint32_t dirLba, uint32_t dirSize, const char* name, uint32_t nameLen,
                     bool wantDir, uint32_t* lba, uint32_t* size)
{
    const uint8_t* sector = (const uint8_t*)gSector;

    for (uint32_t offset = 0; offset < dirSize; offset += CD_SECTOR_SIZE)
    {
        if (cdRead(dirLba + offset / CD_SECTOR_SIZE, CD_SECTOR_SIZE, gSector) != 0)
            return -1;

        uint32_t pos = 0;
        while (pos < CD_SECTOR_SIZE && sector[pos] != 0)
        {
            const uint8_t* rec = sector + pos;
            pos += rec[0];

            uint32_t idLen = rec[32];
            const char* id = (const char*)rec + 33;
            bool isDir = (rec[25] & 2) != 0;

            // file names carry a ";1" version suffix
            if (!isDir && idLen > 2 && id[idLen - 2] == ';')
                idLen -= 2;

            if (isDir == wantDir && idLen == nameLen && memcmp(id, name, nameLen) == 0)
            {
                *lba = be32(rec + 6);
                *size = be32(rec + 14);
                return 0;
            }
        }
    }
    return -1;
}

int cdFind(const char* path, uint32_t* lba, uint32_t* size)
{
    if (cdRead(16, CD_SECTOR_SIZE, gSector) != 0)
        return -1;

    const uint8_t* root = (const uint8_t*)gSector + 156;
    uint32_t dirLba = be32(root + 6);
    uint32_t dirSize = be32(root + 14);

    while (true)
    {
        const char* slash = strchr(path, '/');
        uint32_t len = slash ? uint32_t(slash - path) : strlen(path);

        if (findInDir(dirLba, dirSize, path, len, slash != NULL, &dirLba, &dirSize) != 0)
            return -1;

        if (!slash)
        {
            *lba = dirLba;
            *size = dirSize;
            return 0;
        }
        path = slash + 1;
    }
}

int32_t cdLoadFile(const char* path, void* dst, uint32_t maxSize)
{
    uint32_t lba, size;

    if (cdFind(path, &lba, &size) != 0)
        return -1;

    if (size > maxSize)
        return -1;

    int ret = cdRead(lba, size, dst);
    return ret ? ret : int32_t(size);
}
