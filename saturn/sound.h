// Saturn-side sound calls beyond OpenLara's snd* interface (sound.cpp).
#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

// Sound RAM address of the level's samples (DATA/<level>.SFX).
#define SOUND_SAMPLES_ADDR  0x2000
#define SOUND_RAM           0x25A00000
#define SOUND_RAM_SIZE      0x80000

// Once per frame: frees voices whose samples have played out.
void sndFrame();

#endif
