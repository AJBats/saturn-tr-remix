// Saturn-side sound calls beyond OpenLara's snd* interface (sound.cpp).
#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

// Sound RAM address of the level's samples (DATA/<level>.SFX); below it the
// sound CPU's program (sound.68k).
#define SOUND_SAMPLES_ADDR  0x2000
#define SOUND_RAM           0x25A00000
#define SOUND_RAM_SIZE      0x80000

// level.soundOffsets (tools/sfx.py), two words a sample: its offset in the
// .SFX file, rate, attenuation and format; then its length in bytes (0: the
// level has none).
#define SOUND_OFFSET_MASK   0x003FFFFF
#define SOUND_RATE_SHIFT    22          // bits 23-22: the rate it plays at
#define SOUND_RATE_MASK     3
#define SOUND_RATE_NORMAL   0           // 5512.5 Hz
#define SOUND_RATE_SLOW     1           // 2500 Hz (the rate the original made its slow sounds at)
#define SOUND_RATE_FAST     2           // 11025 Hz: the PlayStation's
#define SOUND_ATTEN_SHIFT   24          // bits 29-24: SCSP total level steps (0.375 dB)
#define SOUND_ATTEN_MASK    0x3F
#define SOUND_FORMAT_SHIFT  30          // bits 31-30
#define SOUND_FORMAT_MASK   3
#define SOUND_FORMAT_PCM16  0
#define SOUND_FORMAT_PCM8   1
#define SOUND_FORMAT_ADPCM  2           // the PlayStation's SPU ADPCM, which the sound CPU decodes

// Once per frame: frees voices whose samples have played out.
void sndFrame();

#ifdef SOUND_BENCH
// The sound CPU's decoding speed (soundbench.68k), in the boot log: samples
// a second for each format (PS1, SAT), with 0, 16 and 30 slots playing.
void sndBench();
#endif

#endif
