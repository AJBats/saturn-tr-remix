// Saturn-side sound calls beyond OpenLara's snd* interface (sound.cpp).
#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

// Sound RAM address of the level's samples (DATA/<level>.SFX).
#define SOUND_SAMPLES_ADDR  0x2000
#define SOUND_RAM           0x25A00000
#define SOUND_RAM_SIZE      0x80000

// Each entry of level.soundOffsets (tools/pack_level.py): the sample's offset
// in the .SFX file, its format, and the extra attenuation that undoes its
// normalisation, in SCSP total level steps (0.375 dB). The entry after the
// last sample holds the file's size alone.
#define SOUND_OFFSET_MASK   0x00FFFFFF
#define SOUND_ATTEN_SHIFT   24          // bits 30-24
#define SOUND_ATTEN_MASK    0x7F
#define SOUND_PCM8          0x80000000  // 8-bit PCM at 11025 Hz; else 16-bit at 5512.5 Hz

// Once per frame: frees voices whose samples have played out.
void sndFrame();

#endif
