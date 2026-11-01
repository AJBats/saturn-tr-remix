// Saturn-side sound calls beyond OpenLara's snd* interface (sound.cpp).
#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

// Sound RAM address of the level's samples (DATA/<level>.SFX).
#define SOUND_SAMPLES_ADDR  0x2000
#define SOUND_RAM           0x25A00000
#define SOUND_RAM_SIZE      0x80000

// Each entry of level.soundOffsets (tools/sfx.py): the sample's offset in
// the .SFX file, its format and rate, and the extra attenuation that undoes
// its normalisation, in SCSP total level steps (0.375 dB). The entry after
// the last sample holds the file's size alone.
#define SOUND_OFFSET_MASK   0x003FFFFF
#define SOUND_RATE_SHIFT    22          // bits 23-22: the rate it plays at
#define SOUND_RATE_MASK     3
#define SOUND_RATE_NORMAL   0           // 5512.5 Hz
#define SOUND_RATE_SLOW     1           // 2971.6 Hz: the original's for a sample made at 2500 Hz
#define SOUND_RATE_FAST     2           // 11025 Hz
#define SOUND_RATE_RAISED   3           // 13103 Hz: an 11025 Hz sample as the original plays its 2500 Hz one
#define SOUND_ATTEN_SHIFT   24          // bits 30-24
#define SOUND_ATTEN_MASK    0x7F
#define SOUND_PCM8          0x80000000  // 8-bit PCM; else 16-bit

// Once per frame: frees voices whose samples have played out.
void sndFrame();

#ifdef SOUND_BENCH
// The sound CPU's decoding speed (soundbench.68k), in the boot log: samples
// a second for each format (PS1, SAT), with 0, 16 and 30 slots playing.
void sndBench();
#endif

#endif
