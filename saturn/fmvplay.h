// The PlayStation's videos played on the Saturn (docs/ps1-fmv.md, "The Cinepak route"): tools/fmvpack.py's
// streams, from the disc's Mode 2 Form 2 track, in 24-bit, with the retail Saturn's sound decoded by the
// 68000 (saturn/fmvsound.68k).
#ifndef FMVPLAY_H
#define FMVPLAY_H

#include <stdint.h>
#include "cinepak.h"

// What the player needs from its host (the game or a bench):
uint32_t fmvHostVblanks();                          // vertical blanks so far
void fmvHostOnVblank(void (*fn)());                 // called in each vertical blank's interrupt (nullptr: none)
void fmvHostOnLine(void (*fn)(), int32_t line);     // called in an interrupt as each field reaches screen line
                                                    // line (an SCU timer; nullptr: none)
void fmvHostSlaveRun(void (*fn)(void*), void* arg); // the slave runs fn(arg), its cache purged first
void fmvHostSlaveWait();
void fmvHostSoundCpu(bool run);                     // the 68000 running or held
bool fmvHostSkip();                                 // START pressed (where a video may be skipped)
uint32_t fmvHostCycles();                           // the master's clock, for FmvStats

// Its memory, the host's: the ring takes the drive's sectors (2336 bytes each, as many as fit; filled by SCU
// DMA in high work RAM, by the SH-2's DMA controller in low work RAM, which SCU DMA can't reach), the frame
// buffer a frame at a time (the stream's largest), the picture 320 x lines 32-bit pixels (high work RAM: SCU
// DMA takes it to VDP2).
struct FmvMemory
{
    uint8_t* ring;
    uint32_t ringBytes;
    uint8_t* frame;
    uint32_t frameBytes;
    uint32_t* picture;
    void* books;            // two of FMV_BOOK_BYTES: the master's, the slave's
};

#define FMV_BOOK_BYTES (sizeof(CpkBook15) > sizeof(CpkBook) ? sizeof(CpkBook15) : sizeof(CpkBook))

struct FmvStats
{
    uint32_t frames;        // shown
    uint32_t late;          // shown after their vertical blank
    uint32_t mostLate;      // vertical blanks
    uint32_t decodeMost;    // cycles a frame, both CPUs
    uint32_t decodeSum;
    uint32_t copyMost;      // a frame out of the ring
    uint32_t vramMost;      // the picture to VRAM
    uint32_t moveSum;       // the drive's sectors moved into the ring
    uint32_t sectors;
    uint32_t ringMost;      // sectors waiting in the ring at most
    uint32_t blockMost;     // sectors waiting in the CD block at most
    int32_t soundLow;       // fewest samples decoded ahead of the slots (fmvsound.68k)
    uint32_t vblanks;       // from the first sector asked for to the end
    uint32_t cdFailed;      // transfers the CD block refused, retried at the next look
    int32_t cdFirstError;   // the first one's code (cd.cpp's ERR_), and the drive's status then
    int32_t cdFirstStatus;
    uint32_t cdFirstSector; // sectors in by then
    int32_t error;
};

// The player's state as its last vertical blank saw it, for a bench's freeze watch.
struct FmvLive
{
    uint32_t where;         // the main loop's place (FMV_AT_*)
    uint32_t arrived;       // sectors in the ring, from the stream's first
    uint32_t parse;         // the next item's stream offset
    uint32_t doneTo;
    uint32_t queueHead, queueTail;
    uint32_t soundWr;
    uint32_t lastReady;     // the CD block's sectors at the last look
    int32_t lastRet;        // the last transfer's return
    uint32_t cdFailed;
    uint32_t slaveIn, slaveOut;     // slave jobs begun, finished
    int32_t shown;
    uint32_t sounding;
    uint32_t vblank;
    uint32_t edcBad;        // sectors whose EDC (the disc's own check) or subheader came out wrong
    uint32_t edcFirst;      // the first of them, from the stream's first sector
    uint32_t edcLast;
};
enum { FMV_AT_LOOP = 1, FMV_AT_READY, FMV_AT_BEGIN, FMV_AT_DMA, FMV_AT_END, FMV_AT_PARSE, FMV_AT_COPY,
       FMV_AT_DECODE, FMV_AT_SLAVE, FMV_AT_VRAM, FMV_AT_REST };
const volatile FmvLive* fmvLive();

// Plays a video from the index (FMV/INDEX.BIN); returns false if it isn't there or the disc fails. A picture
// of up to 128 lines plays in 24-bit, double-buffered; a taller one (CORELOGO's 208) as tall says: in 24-bit
// with one buffer (FMV_TALL_24, the host's line interrupt needed), or in 15-bit converted as a CPK15_ value.
// Leaves VDP2 in the video's mode and the 68000 running fmvsound.68k: the host puts back its own.
enum { FMV_TALL_24 = CPK15_DITHER + 1 };
bool fmvHas(const char* name);      // whether the index has the video
bool fmvPlayStream(const char* name, const FmvMemory& mem, bool skippable, int32_t tall, FmvStats* stats);

#endif
