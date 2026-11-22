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
void fmvHostSlaveRun(void (*fn)(void*), void* arg); // the slave runs fn(arg), its cache purged first
void fmvHostSlaveWait();
void fmvHostSoundCpu(bool run);                     // the 68000 running or held
bool fmvHostSkip();                                 // START pressed (where a video may be skipped)
uint32_t fmvHostCycles();                           // the master's clock, for FmvStats

// Its memory, the host's: the ring takes the drive's sectors (2336 bytes each, as many as fit; in high work
// RAM, which SCU DMA reaches and low work RAM it doesn't), the frame buffer a frame at a time (the stream's
// largest), the picture 320 x lines 32-bit pixels (high work RAM: the decoder writes it).
struct FmvMemory
{
    uint8_t* ring;
    uint32_t ringBytes;
    uint8_t* frame;
    uint32_t frameBytes;
    uint32_t* picture;
    CpkBook* books;         // two: the master's, the slave's
};

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

// Plays a video from the index (FMV/INDEX.BIN); returns false if it isn't there or the disc fails. Leaves
// VDP2 in its 24-bit mode and the 68000 running fmvsound.68k: the host puts back its own.
bool fmvPlayStream(const char* name, const FmvMemory& mem, bool skippable, FmvStats* stats);

#endif
