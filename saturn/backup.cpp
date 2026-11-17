// The BIOS copies its backup library into RAM we provide (BUP_Init), then
// publishes a table of entry points. The SMPC reset button is disabled
// around each call, as the library requires.

#include "backup.h"
#include "system.h"
#include "common.h"

#include <string.h>

#define BUP_INIT        (*(void (**)(uint32_t*, uint32_t*, BupConfig*))0x06000358)
#define BUP_VECTORS     (*(BupStub**)0x06000354)

#define SMPC_RESENAB    0x19
#define SMPC_RESDISA    0x1A

#define DEVICE_INTERNAL 0
#define WRITE_OVERWRITE 0
#define LANGUAGE_ENGLISH 1

struct BupConfig
{
    uint16_t unit;          // 0: not connected
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

struct BupStub
{
    uint32_t reserved;
    int32_t (*select)(uint32_t device, uint32_t partition);
    int32_t (*format)(uint32_t device);
    int32_t (*status)(uint32_t device, uint32_t size, void* stat);
    int32_t (*write)(uint32_t device, BupDir* dir, const void* data, uint32_t mode);
    int32_t (*read)(uint32_t device, const char* filename, void* data);
    int32_t (*remove)(uint32_t device, const char* filename);
    int32_t (*dir)(uint32_t device, const char* filename, uint32_t count, BupDir* dirs);
};

// The library's 16 KB and its 8 KB of work, at the top of low work RAM
// (common.h BACKUP_WORK_ADDR): copied there at each use, as a video between
// levels may have been there since.
static BupStub* gStub;

static bool init()
{
    uint32_t* area = (uint32_t*)BACKUP_WORK_ADDR;
    BupConfig config[3];
    smpcCommand(SMPC_RESDISA);
    BUP_INIT(area, area + 4096, config);
    smpcCommand(SMPC_RESENAB);

    gStub = BUP_VECTORS;
    return config[0].unit != 0;
}

// size of the named file, or -1
static int32_t fileSize(const char* name)
{
    BupDir dir;
    smpcCommand(SMPC_RESDISA);
    int32_t count = gStub->dir(DEVICE_INTERNAL, name, 1, &dir);
    smpcCommand(SMPC_RESENAB);
    return (count >= 1) ? int32_t(dir.size) : -1;
}

bool backupExists(const char* name)
{
    return init() && fileSize(name) >= 0;
}

bool backupRead(const char* name, void* data, uint32_t size)
{
    if (!init())
        return false;

    // the library reads the whole file: it must fit
    int32_t fsize = fileSize(name);
    if (fsize < 0 || uint32_t(fsize) > size)
        return false;

    smpcCommand(SMPC_RESDISA);
    int32_t status = gStub->read(DEVICE_INTERNAL, name, data);
    smpcCommand(SMPC_RESENAB);
    return status == 0;
}

bool backupWrite(const char* name, const char* comment, const void* data, uint32_t size)
{
    if (!init())
        return false;

    BupDir dir;
    memset(&dir, 0, sizeof(dir));
    strncpy(dir.filename, name, sizeof(dir.filename) - 1);
    strncpy(dir.comment, comment, sizeof(dir.comment) - 1);
    dir.language = LANGUAGE_ENGLISH;
    dir.size = size;

    smpcCommand(SMPC_RESDISA);
    int32_t status = gStub->write(DEVICE_INTERNAL, &dir, data, WRITE_OVERWRITE);
    smpcCommand(SMPC_RESENAB);
    return status == 0;
}
