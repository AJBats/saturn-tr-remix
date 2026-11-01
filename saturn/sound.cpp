// Saturn sound. Music is CD audio, which the SCSP mixes in through its
// external input. Sound effects play the level's samples from sound RAM on
// SCSP slots driven straight from the SH-2 (the sound CPU is stopped), with
// the slot settings the original uses.

#include "common.h"
#include "cd.h"
#include "crash.h"
#include "sound.h"
#include "system.h"

#define SCSP_REG(x)     (*(volatile uint16*)(0x25B00000 + (x)))
#define SCSP_MIXER      SCSP_REG(0x400)     // MEM4MB (bit 9), DAC18B (bit 8), MVOL (bits 3-0)
#define SCSP_SLOT(slot, reg) SCSP_REG((slot) * 0x20 + (reg) * 2)
#define SCSP_SLOT_SEND(slot) SCSP_SLOT(slot, 11) // DISDL, DIPAN, EFSDL, EFPAN

// slot register 0
#define KYONEX          0x1000  // key execute: every slot follows its KYONB
#define KYONB           0x0800
#define LPCTL_LOOP      0x0020
#define PCM8B           0x0010  // 8-bit samples

#define SMPC_SNDON      0x06
#define SMPC_SNDOFF     0x07

// Effect sends of slots 16 and 17 carry the external input, CD audio left and
// right. EFPAN bit 4 picks the side attenuated by bits 3-0; 15 mutes it.
#define EXTS_LEVEL      (5 << 5)            // the original's level
#define EXTS_LEFT_ONLY  0x1F
#define EXTS_RIGHT_ONLY 0x0F

// Samples are 16-bit PCM at the original's rates (5512.5 Hz, octave -3; or
// for the sounds it lists, 2971.6 Hz, octave -4 at FNS 0x50, and 11025 Hz,
// octave -2), or 8-bit PCM made from the PlayStation's, at 11025 Hz or
// raised as the original raises the 2971.6 Hz ones. They are loaded at
// SOUND_SAMPLES_ADDR in sound RAM (sound.h has the offsets table's format).
#define VOICES          16      // slots 0-15; 16 and 17 carry the CD audio

// Audio tracks on the disc
#define TRACK_FIRST     2
#define TRACK_LAST      58

extern Level level;

struct Voice
{
    int32 sample;   // -1 when free
    bool loop;
    uint32 end;     // vblank count when a one-shot sample has played out
};

static Voice gVoices[VOICES];

// OpenLara volume (1..64, 64 full) as SCSP total level: 16 steps per halving
static const uint8 gVolumeTL[65] = {
    255, 96, 80, 71, 64, 59, 55, 51, 48, 45, 43, 41, 39,
    37, 35, 33, 32, 31, 29, 28, 27, 26, 25, 24, 23, 22,
    21, 20, 19, 18, 17, 17, 16, 15, 15, 14, 13, 13, 12,
    11, 11, 10, 10, 9, 9, 8, 8, 7, 7, 6, 6, 5,
    5, 4, 4, 3, 3, 3, 2, 2, 1, 1, 1, 0, 0,
};

// The sample's total level at that volume, attenuated by what undoes its
// normalisation
static uint32 sampleTL(int32 index, int32 volume)
{
    uint32 atten = (uint32(level.soundOffsets[index]) >> SOUND_ATTEN_SHIFT) & SOUND_ATTEN_MASK;
    return X_MIN(gVolumeTL[X_CLAMP(volume, 0, 64)] + atten, 255u);
}

// A sample's rate: its SCSP octave and frequency at OpenLara pitch 128, and
// in Hz, by SOUND_RATE_*
struct Rate
{
    int32 octave;
    uint32 fns;
    uint32 hz;
};

static const Rate gRates[4] = { { -3, 0, 5513 }, { -4, 0x50, 2972 }, { -2, 0, 11025 }, { -2, 193, 13103 } };

// The rate at OpenLara pitch (128 = the sample's rate) as SCSP octave and
// frequency: the step 1 + FNS / 1024 between 1 and 2, the octave scaling it
static uint32 pitchOctFns(const Rate &rate, int32 pitch)
{
    int32 octave = rate.octave;
    uint32 step = (1024 + rate.fns) * uint32(X_MAX(pitch, 1)) / 128;
    while (step >= 2048)
    {
        step >>= 1;
        octave++;
    }
    while (step < 1024)
    {
        step <<= 1;
        octave--;
    }
    return (uint32(octave & 15) << 11) | (step - 1024);
}

// Every slot follows its KYONB: keyed-on slots start, keyed-off ones release.
static void keyExecute()
{
    SCSP_SLOT(0, 0) = SCSP_SLOT(0, 0) | KYONEX;
}

static void voiceOff(int32 v)
{
    SCSP_SLOT(v, 0) = SCSP_SLOT(v, 0) & ~(KYONB | KYONEX);
    gVoices[v].sample = -1;
}

void sndInit()
{
    smpcCommand(SMPC_SNDOFF);

    SCSP_MIXER = (1 << 9) | 0xF;

    for (int32 v = 0; v < VOICES; v++)
    {
        SCSP_SLOT(v, 0) = 0;
        gVoices[v].sample = -1;
    }
    keyExecute();

    SCSP_SLOT_SEND(16) = EXTS_LEVEL | EXTS_LEFT_ONLY;
    SCSP_SLOT_SEND(17) = EXTS_LEVEL | EXTS_RIGHT_ONLY;
}

#ifdef SOUND_BENCH
#include "soundbench_m68k.h"

#define BENCH_COUNTER   0x0F00      // soundbench.68k's
#define BENCH_MODE      0x0F04
#define BENCH_RING      0x1000
#define BENCH_VBLANKS   60

// A slot reading sound RAM as a voice does, unheard: soundbench.68k's ring,
// looped, 16-bit at 44.1 kHz, total level 255, no send.
static void benchSlot(int32 slot)
{
    SCSP_SLOT(slot, 0) = LPCTL_LOOP;
    SCSP_SLOT(slot, 1) = BENCH_RING;
    SCSP_SLOT(slot, 2) = 0;
    SCSP_SLOT(slot, 3) = 511;
    SCSP_SLOT(slot, 4) = 0x001F;
    SCSP_SLOT(slot, 5) = 0x001F;
    SCSP_SLOT(slot, 6) = 255;
    SCSP_SLOT(slot, 7) = 0;
    SCSP_SLOT(slot, 8) = 0;
    SCSP_SLOT(slot, 9) = 0;
    SCSP_SLOT(slot, 10) = 0;
    SCSP_SLOT(slot, 11) = 0;
    SCSP_SLOT(slot, 0) = LPCTL_LOOP | KYONB;
}

// The bench's count, read until two reads agree (the 68000 writes it a
// word at a time).
static uint32 benchCount()
{
    volatile uint32* counter = (volatile uint32*)(SOUND_RAM + BENCH_COUNTER);
    uint32 a = *counter, b;
    while ((b = *counter) != a) {
        a = b;
    }
    return a;
}

void sndBench()
{
    static const char* const kLabels[2][3] = {
        { "PS1 0", "PS1 16", "PS1 30" },
        { "SAT 0", "SAT 16", "SAT 30" },
    };
    static const int32 kSlots[3] = { 0, 16, 30 };

    volatile uint16* ram = (volatile uint16*)SOUND_RAM;
    const uint16* image = (const uint16*)soundbench_m68k;
    for (uint32 i = 0; i < SOUNDBENCH_M68K_SIZE / 2; i++) {
        ram[i] = image[i];
    }

    for (int32 mode = 0; mode < 2; mode++)
    {
        for (int32 load = 0; load < 3; load++)
        {
            // slots 16 and 17 carry the CD audio
            for (int32 k = 0, slot = 0; k < kSlots[load]; k++, slot++)
            {
                if (slot == 16) {
                    slot = 18;
                }
                benchSlot(slot);
            }
            keyExecute();

            ram[BENCH_MODE / 2] = uint16(mode);
            smpcCommand(SMPC_SNDON);
            uint32 v = gVblankCount;
            while (gVblankCount - v < 4);
            uint32 c0 = benchCount();
            v = gVblankCount;
            while (gVblankCount - v < BENCH_VBLANKS);
            uint32 c1 = benchCount();
            smpcCommand(SMPC_SNDOFF);
            crashLog(kLabels[mode][load], (c1 - c0) * 60 / BENCH_VBLANKS);

            for (int32 slot = 0; slot < 32; slot++) {
                SCSP_SLOT(slot, 0) = 0;
            }
            keyExecute();
        }
    }
}
#endif

void sndInitSamples()
{
}

void sndFreeSamples()
{
    sndStop();
}

// Releases voices whose one-shot sample has ended, so they can be keyed on
// again (the SCSP restarts only slots it has seen keyed off).
void sndFrame()
{
    bool released = false;
    for (int32 v = 0; v < VOICES; v++)
    {
        Voice &voice = gVoices[v];
        if (voice.sample >= 0 && !voice.loop && int32(gVblankCount - voice.end) >= 0)
        {
            voiceOff(v);
            released = true;
        }
    }

    if (released) {
        keyExecute();
    }
}

void* sndPlaySample(int32 index, int32 volume, int32 pitch, int32 mode)
{
    if (!gSettings.audio_sfx || index < 0 || index >= level.soundOffsetsCount)
        return NULL;

    // already playing: update it, or restart it for REPLAY
    for (int32 v = 0; v < VOICES; v++)
    {
        if (gVoices[v].sample != index)
            continue;

        if (mode != REPLAY)
        {
            SCSP_SLOT(v, 6) = sampleTL(index, volume);
            return gVoices + v;
        }

        voiceOff(v);
        keyExecute();
        break;
    }

    int32 v = 0;
    while (v < VOICES && gVoices[v].sample >= 0) {
        v++;
    }
    if (v == VOICES)
        return NULL;

    // a slot keyed off this frame needs a sample period (23 us) to release
    // before a key on restarts it
    for (volatile int32 i = 0; i < 200; i++);

    uint32 entry = level.soundOffsets[index];
    uint32 offset = entry & SOUND_OFFSET_MASK;
    uint32 bytes = (level.soundOffsets[index + 1] & SOUND_OFFSET_MASK) - offset;
    bool pcm8 = (entry & SOUND_PCM8) != 0;
    uint32 addr = SOUND_SAMPLES_ADDR + offset;
    uint32 length = pcm8 ? bytes : bytes >> 1;
    if (!length)
        return NULL;
    const Rate &rate = gRates[(entry >> SOUND_RATE_SHIFT) & SOUND_RATE_MASK];
    bool loop = (mode == LOOP);

    uint16 reg0 = (loop ? LPCTL_LOOP : 0) | (pcm8 ? PCM8B : 0) | ((addr >> 16) & 15);
    SCSP_SLOT(v, 0) = reg0;
    SCSP_SLOT(v, 1) = addr;
    SCSP_SLOT(v, 2) = 0;                        // loop start
    SCSP_SLOT(v, 3) = length - 1;               // loop/sample end
    SCSP_SLOT(v, 4) = 0x001F;                   // attack 31, no decay
    SCSP_SLOT(v, 5) = 0x001F;                   // release 31
    SCSP_SLOT(v, 6) = sampleTL(index, volume);
    SCSP_SLOT(v, 7) = 0;                        // no modulation
    SCSP_SLOT(v, 8) = pitchOctFns(rate, pitch);
    SCSP_SLOT(v, 9) = 0;                        // no LFO
    SCSP_SLOT(v, 10) = 0;
    SCSP_SLOT(v, 11) = 7 << 13;                 // direct send, centre
    SCSP_SLOT(v, 0) = reg0 | KYONB;
    keyExecute();

    // samples per 60 Hz frame at this pitch: hz / 60 * pitch / 128
    Voice &voice = gVoices[v];
    voice.sample = index;
    voice.loop = loop;
    voice.end = gVblankCount + length * 7680 / (rate.hz * X_MAX(pitch, 1)) + 1;

    return &voice;
}

// OpenLara's track numbers are the original disc's (the Caves play 5 and the
// cutscenes 22-25, as in the original).
static bool trackOnDisc(int32 track)
{
    return track >= TRACK_FIRST && track < TRACK_LAST;
}

void sndPlayTrack(int32 track)
{
    if (!gSettings.audio_music)
        return;

    if (track == gCurTrack)
        return;

    gCurTrack = track;

    if (!trackOnDisc(track))
    {
        cdStopAudio();
        return;
    }

    cdPlayTrack(track);
}

bool sndTrackIsPlaying()
{
    // an unplayable track counts as playing, so the game doesn't retry it
    return cdAudioPlaying() || (gCurTrack != -1 && !trackOnDisc(gCurTrack));
}

void sndStopTrack()
{
    gCurTrack = -1;
    cdStopAudio();
}

void sndStopSample(int32 index)
{
    bool released = false;
    for (int32 v = 0; v < VOICES; v++)
    {
        if (gVoices[v].sample == index)
        {
            voiceOff(v);
            released = true;
        }
    }

    if (released) {
        keyExecute();
    }
}

void sndStop()
{
    sndStopTrack();

    for (int32 v = 0; v < VOICES; v++) {
        voiceOff(v);
    }
    keyExecute();
}
