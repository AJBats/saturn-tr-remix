// Files on the Saturn's internal backup RAM, through the BIOS backup library.
#ifndef BACKUP_H
#define BACKUP_H

#include <stdint.h>

// Names are up to 11 characters, comments up to 10. Each returns false when
// the backup RAM is missing, unformatted or full, or the file doesn't exist.
bool backupRead(const char* name, void* data, uint32_t size);
bool backupWrite(const char* name, const char* comment, const void* data, uint32_t size);
bool backupExists(const char* name);

#endif
