// CD access: file lookup on the ISO9660 data track and sector reads.
#ifndef CD_H
#define CD_H

#include <stdint.h>

#define CD_SECTOR_SIZE 2048

// Returns 0, or a negative error from the CD block.
int cdInit();

// Looks up an ISO9660 path such as "DATA/LEVEL1.PKD" (no ";1" version).
// Returns 0 and the file's first sector (LBA) and byte size, or -1.
int cdFind(const char* path, uint32_t* lba, uint32_t* size);

// Reads size bytes starting at sector lba into dst (2-byte aligned).
int cdRead(uint32_t lba, uint32_t size, void* dst);

// cdFind + cdRead. Returns the file size, or a negative error.
int32_t cdLoadFile(const char* path, void* dst, uint32_t maxSize);

#endif
