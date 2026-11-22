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

// Streaming: cdStreamStart begins reading sectors from lba without waiting;
// cdStreamRead then moves up to maxSectors of those that have arrived into
// dst and returns how many (0 if none yet), or a negative error.
int cdStreamStart(uint32_t lba, uint32_t sectors);
int32_t cdStreamRead(void* dst, uint32_t maxSectors);
// The same from a frame address (FAD: LBA + 150), as the table of contents gives them.
int cdStreamStartFad(uint32_t fad, uint32_t sectors);
// Ends a stream, finished or not: the drive paused and what it had read dropped, so
// the next read starts as after any other (one issued while the drive still reads
// the stream's rest into a full buffer can wait a minute on a Saturn, and fail).
void cdStreamStop();

// The bytes the CD block hands over for each sector read from now on: CD_SECTOR_SIZE (the user data
// of Mode 1 and Mode 2 Form 1 sectors), or CD_SECTOR_MODE2: a Mode 2 sector's 8-byte subheader, then
// its 2328 bytes (in Form 2, 2324 of data and 4 of EDC). cdRead and cdFind need CD_SECTOR_SIZE.
#define CD_SECTOR_MODE2 2336
int cdSectorBytes(uint32_t bytes);

// The table of contents: toc[t - 1] for track t is its control and ADR nibbles << 24 | its first FAD
// (control bit 6 set: a data track), 0xFFFFFFFF where there's no track; toc[99..101] hold the first
// track, last track and lead-out. Returns 0 or a negative error.
int cdToc(uint32_t toc[102]);

// The transfer underneath cdStreamRead, for callers that move the data themselves: the sectors in the
// CD block's buffer; cdTransferBegin(n) readies the first n, whose bytes are then read 16 bits at a
// time from CD_DATA; cdTransferEnd() frees them.
#define CD_DATA (*(volatile uint16_t*)0x25890000)
uint32_t cdSectorsReady();

// The drive's status code (1 paused, 3 playing, ...) and the frame address it is at, or a negative error.
int cdStatus(uint32_t* fad);
int cdTransferBegin(uint32_t sectors);
int cdTransferEnd();

// Plays an audio track once, to its end; reads stop it.
int cdPlayTrack(uint32_t track);
void cdStopAudio();
bool cdWaitPlaying();    // till the drive reports PLAY (false: it didn't)
void cdPauseAudio();     // the pickup kept where it is
void cdResumeAudio();    // and playing on from there
bool cdAudioPlaying();

#endif
