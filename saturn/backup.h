// Files in the Saturn's backup memory (the system's, or a cartridge's),
// through the BIOS backup library.
#ifndef BACKUP_H
#define BACKUP_H

#include <stdint.h>
#include "common.h"

// A device's state for a file of size bytes (BackupState, common.h), and the
// blocks such a file takes there: device 0 the system memory, 1 a cartridge.
BackupState backupState(int32_t device, const char* name, uint32_t size, uint32_t* blocks);

// The device files go to from now on; -1: none, saving off.
void backupUse(int32_t device);
bool backupOn();

// Names are up to 11 characters, comments up to 10. Each returns false when
// saving is off, the memory is unformatted or full, the file doesn't exist,
// or a write doesn't read back the same.
bool backupRead(const char* name, void* data, uint32_t size);
bool backupWrite(const char* name, const char* comment, const void* data, uint32_t size);
bool backupExists(const char* name);

#endif
