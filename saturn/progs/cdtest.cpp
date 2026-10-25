// CD self-test: loads files back off our own disc through saturn/cd.cpp.
//
// - CPY.TXT: small file, content checked.
// - 0MAIN.BIN: this program, compared with the .text the BIOS loaded.
// - DATA/LEVEL1.SAT: ~500 KB multi-sector read, FNV-1a checksum to compare
//   with the source file on the PC. loadStart()/loadEnd() are breakpoint
//   markers for timing it.
//
// Results in low work RAM:
//   0x00200000  'CDTS' once finished
//   0x00200004  cdInit result (0 = ok)
//   0x00200008  CPY.TXT size, or negative error
//   0x0020000C  CPY.TXT content check (1 = ok)
//   0x00200010  0MAIN.BIN size, or negative error
//   0x00200014  first mismatching offset in .text, or 0xFFFFFFFF if identical
//   0x00200018  DATA/LEVEL1.SAT size, or negative error
//   0x0020001C  DATA/LEVEL1.SAT FNV-1a

#include "common.h"
#include "cd.h"
#include "system.h"

#define RESULTS     ((volatile uint32*)0x20200000)
#define LOAD_BUF    ((uint8*)0x00210000)
#define LOAD_MAX    (0x000F0000)
#define CDTS_MAGIC  0x43445453

extern uint8 gTextEnd[] asm("__text_end"); // linker script symbol (no C prefix)

static uint8 gText[2048];

X_NOINLINE void loadStart() { asm volatile(""); }
X_NOINLINE void loadEnd() { asm volatile(""); }

static uint32 fnv1a(const uint8* data, uint32 size)
{
    uint32 h = 0x811C9DC5;
    for (uint32 i = 0; i < size; i++)
    {
        h = (h ^ data[i]) * 0x01000193;
    }
    return h;
}

int main()
{
    systemInit();

    RESULTS[0] = 0;
    RESULTS[1] = cdInit();

    int32 size = cdLoadFile("CPY.TXT", gText, sizeof(gText));
    RESULTS[2] = size;
    RESULTS[3] = (size > 0 && memcmp(gText, "Engine source GPL", 17) == 0);

    size = cdLoadFile("0MAIN.BIN", LOAD_BUF, LOAD_MAX);
    RESULTS[4] = size;

    const uint8* loaded = (const uint8*)0x06004000;
    uint32 textSize = gTextEnd - loaded;
    uint32 mismatch = 0xFFFFFFFF;
    for (uint32 i = 0; size > 0 && i < textSize; i++)
    {
        if (LOAD_BUF[i] != loaded[i])
        {
            mismatch = i;
            break;
        }
    }
    RESULTS[5] = mismatch;

    loadStart();
    size = cdLoadFile("DATA/LEVEL1.SAT", LOAD_BUF, LOAD_MAX);
    loadEnd();
    RESULTS[6] = size;
    RESULTS[7] = (size > 0) ? fnv1a(LOAD_BUF, size) : 0;

    RESULTS[0] = CDTS_MAGIC;

    while (1)
    {
        waitVblank();
    }

    return 0;
}
