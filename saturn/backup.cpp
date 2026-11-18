// The BIOS copies its backup library into RAM we provide (BUP_Init), then
// publishes a table of entry points. The SMPC reset button is disabled
// around each call, as the library requires. As retail's (06045c08 on): the
// system memory or a cartridge's, the one chosen at boot; a write is read
// back to check it (BUP_Verify) and dated from the clock.

#include "backup.h"
#include "system.h"
#include "common.h"

#include <string.h>

#define BUP_INIT        (*(void (**)(uint32_t*, uint32_t*, BupConfig*))0x06000358)
#define BUP_VECTORS     (*(BupStub**)0x06000354)

#define SMPC_RESENAB    0x19
#define SMPC_RESDISA    0x1A

#define WRITE_OVERWRITE 0
#define LANGUAGE_ENGLISH 1
#define BUP_UNFORMAT    2       // BUP_Stat's answer for memory not formatted

struct BupConfig
{
    uint16_t unit;          // a device's number + 1; 0: none
    uint16_t partitions;
};

struct BupDir
{
    char filename[12];
    char comment[11];
    uint8_t language;
    uint32_t date;
    uint32_t size;
    uint16_t blocks;
};

struct BupStat
{
    uint32_t totalSize;
    uint32_t totalBlocks;
    uint32_t blockSize;
    uint32_t freeSize;
    uint32_t freeBlocks;
    uint32_t files;         // of the size asked, that would fit
};

struct BupDate
{
    uint8_t year;           // since 1980
    uint8_t month;
    uint8_t day;
    uint8_t time;
    uint8_t minute;
    uint8_t week;
};

struct BupStub
{
    uint32_t reserved;
    int32_t (*select)(uint32_t device, uint32_t partition);
    int32_t (*format)(uint32_t device);
    int32_t (*status)(uint32_t device, uint32_t size, BupStat* stat);
    int32_t (*write)(uint32_t device, BupDir* dir, const void* data, uint32_t mode);
    int32_t (*read)(uint32_t device, const char* filename, void* data);
    int32_t (*remove)(uint32_t device, const char* filename);
    int32_t (*dir)(uint32_t device, const char* filename, uint32_t count, BupDir* dirs);
    int32_t (*verify)(uint32_t device, const char* filename, const void* data);
    void (*getDate)(uint32_t date, BupDate* out);
    uint32_t (*setDate)(const BupDate* in);
};

// The library's 16 KB and its 8 KB of work, at the top of low work RAM
// (common.h BACKUP_WORK_ADDR): copied there at each use, as a video between
// levels may have been there since.
static BupStub* gStub;
static BupConfig gConfig[3];
static int32_t gDevice;         // the device files go to; -1: none (saving off)

static void init()
{
    uint32_t* area = (uint32_t*)BACKUP_WORK_ADDR;
    smpcCommand(SMPC_RESDISA);
    BUP_INIT(area, area + 4096, gConfig);
    smpcCommand(SMPC_RESENAB);
    gStub = BUP_VECTORS;
}

static bool present(int32_t device)
{
    for (int32_t i = 0; i < 3; i++)
    {
        if (gConfig[i].unit == device + 1)
            return true;
    }
    return false;
}

// size of the named file on the device, or -1
static int32_t fileSize(int32_t device, const char* name)
{
    BupDir dir;
    smpcCommand(SMPC_RESDISA);
    int32_t count = gStub->dir(device, name, 1, &dir);
    smpcCommand(SMPC_RESENAB);
    return (count >= 1) ? int32_t(dir.size) : -1;
}

BackupState backupState(int32_t device, const char* name, uint32_t size, uint32_t* blocks)
{
    init();
    if (!present(device))
        return BACKUP_ABSENT;

    BupStat stat;
    smpcCommand(SMPC_RESDISA);
    int32_t status = gStub->status(device, size, &stat);
    smpcCommand(SMPC_RESENAB);
    if (status == BUP_UNFORMAT)
        return BACKUP_UNFORMATTED;

    // the blocks a file takes: each holds its block size less a 4-byte mark;
    // the first also the 30-byte entry, and the list of them, 2 bytes each
    // and 2 to end it. Retail counts size / 64, about 6% short in the
    // system memory, where 5,120 bytes take 89.
    uint32_t room = stat.blockSize - 6;
    *blocks = (size + 32 + room - 1) / room;

    // a file there is written over in place; for a new one the library's own
    // count of files that size that fit
    if (fileSize(device, name) < 0 && stat.files == 0)
        return BACKUP_FULL;
    return BACKUP_READY;
}

void backupUse(int32_t device)
{
    gDevice = device;
}

bool backupOn()
{
    return gDevice >= 0;
}

bool backupExists(const char* name)
{
    if (gDevice < 0)
        return false;
    init();
    return fileSize(gDevice, name) >= 0;
}

bool backupRead(const char* name, void* data, uint32_t size)
{
    if (gDevice < 0)
        return false;
    init();

    // the library reads the whole file: it must fit
    int32_t fsize = fileSize(gDevice, name);
    if (fsize < 0 || uint32_t(fsize) > size)
        return false;

    smpcCommand(SMPC_RESDISA);
    int32_t status = gStub->read(gDevice, name, data);
    smpcCommand(SMPC_RESENAB);
    return status == 0;
}

// BCD to binary
static uint32_t bcd(uint32_t v)
{
    return (v >> 4) * 10 + (v & 15);
}

bool backupWrite(const char* name, const char* comment, const void* data, uint32_t size)
{
    if (gDevice < 0)
        return false;

    // the clock (SMPC: year in 2 BCD bytes, day of the week and month, day,
    // hour, minute), read before the library's copy, which the read leaves be
    uint8_t clock[7];
    smpcClock(clock);
    init();

    BupDir dir;
    memset(&dir, 0, sizeof(dir));
    strncpy(dir.filename, name, sizeof(dir.filename) - 1);
    strncpy(dir.comment, comment, sizeof(dir.comment) - 1);
    dir.language = LANGUAGE_ENGLISH;
    dir.size = size;

    BupDate date;
    date.year = uint8_t(bcd(clock[0]) * 100 + bcd(clock[1]) - 1980);
    date.month = clock[2] & 15;
    date.week = clock[2] >> 4;
    date.day = uint8_t(bcd(clock[3]));
    date.time = uint8_t(bcd(clock[4]));
    date.minute = uint8_t(bcd(clock[5]));
    dir.date = gStub->setDate(&date);

    smpcCommand(SMPC_RESDISA);
    int32_t status = gStub->write(gDevice, &dir, data, WRITE_OVERWRITE);
    if (status == 0) {
        status = gStub->verify(gDevice, name, data);
    }
    smpcCommand(SMPC_RESENAB);
    return status == 0;
}
