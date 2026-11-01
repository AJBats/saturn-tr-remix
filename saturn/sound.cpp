// Saturn sound. Music is CD audio, which the SCSP mixes in through its
// external input. Sound effects are the sound CPU's (sound.68k): this side
// keeps which of its voices plays what and posts it commands in sound RAM
// (SOUND_DRIVER_*), as the original's driver settings take them; it plays
// the level's samples from sound RAM, decoding the PlayStation's ADPCM ahead
// of where each voice plays.

#include "common.h"
#include "cd.h"
#include "crash.h"
#include "sound.h"
#include "system.h"

#include "sound_m68k.h"

#define SCSP_REG(x)     (*(volatile uint16*)(0x25B00000 + (x)))
#define SCSP_MIXER      SCSP_REG(0x400)     // MEM4MB (bit 9), DAC18B (bit 8), MVOL (bits 3-0)
#define SCSP_SLOT(slot, reg) SCSP_REG((slot) * 0x20 + (reg) * 2)
#define SCSP_SLOT_SEND(slot) SCSP_SLOT(slot, 11) // DISDL, DIPAN, EFSDL, EFPAN

// slot register 0
#define KYONEX          0x1000  // key execute: every slot follows its KYONB
#define KYONB           0x0800
#define LPCTL_LOOP      0x0020

#define SMPC_SNDON      0x06
#define SMPC_SNDOFF     0x07

// Effect sends of slots 16 and 17 carry the external input, CD audio left and
// right. EFPAN bit 4 picks the side attenuated by bits 3-0; 15 mutes it.
#define EXTS_LEVEL      (5 << 5)            // the original's level
#define EXTS_LEFT_ONLY  0x1F
#define EXTS_RIGHT_ONLY 0x0F

// sound.68k's: its shared words, command queue and voices
#define DRIVER_Q_HEAD   0x0A00
#define DRIVER_Q_TAIL   0x0A02
#define DRIVER_DONE     0x0A08
#define DRIVER_BLOCKS   0x0A10
#define DRIVER_LOW      0x0A14
#define DRIVER_SAMPLES  0x0A18
#define DRIVER_SHARED   0x80        // bytes from Q_HEAD cleared before it starts
#define DRIVER_QUEUE    0x0A80
#define DRIVER_Q_SIZE   32
#define OP_PLAY         1
#define OP_STOP         2
#define OP_LEVEL        3
#define FLAG_LOOP       1
#define VOICES          8           // slots 0-7

// The sound CPU's cycles a second for decoding ADPCM: its clock (11.29 MHz)
// less its timer interrupts and the voices' turns, the slots' reads of sound
// RAM (sndBench on the Saturn: 0.5% a slot) and room to spare. A voice
// decoding takes its sample's cycles a sample (tools/sfx.py) at its rate.
#define DECODE_BUDGET   9000000
#define DECODE_RATE     11025

// Audio tracks on the disc
#define TRACK_FIRST     2
#define TRACK_LAST      58

extern Level level;

struct Voice
{
    int32 sample;   // -1 when free
    uint8 gen;      // its play's, which DONE shows when it has played out
    uint8 tl;
    uint32 load;    // the sound CPU's cycles a second decoding it (0: none)
};

static Voice gVoices[VOICES];

struct Command
{
    uint8 op, voice, gen, flags;
    uint32 addr, length;
    uint16 pitch;
    uint8 tl, pan;
};
static_assert(sizeof(Command) == 16, "sound.68k's commands");

// OpenLara volume (1..64, 64 full) as SCSP total level: 16 steps per halving
static const uint8 gVolumeTL[65] = {
    255, 96, 80, 71, 64, 59, 55, 51, 48, 45, 43, 41, 39,
    37, 35, 33, 32, 31, 29, 28, 27, 26, 25, 24, 23, 22,
    21, 20, 19, 18, 17, 17, 16, 15, 15, 14, 13, 13, 12,
    11, 11, 10, 10, 9, 9, 8, 8, 7, 7, 6, 6, 5,
    5, 4, 4, 3, 3, 3, 2, 2, 1, 1, 1, 0, 0,
};

static uint32 entryOf(int32 index)
{
    return uint32(level.soundOffsets[index * 2]);
}

// The sample's total level at that volume, with its attenuation
static uint32 sampleTL(int32 index, int32 volume)
{
    uint32 atten = (entryOf(index) >> SOUND_ATTEN_SHIFT) & SOUND_ATTEN_MASK;
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

static const Rate gRates[3] = { { -3, 0, 5513 }, { -5, 834, 2500 }, { -2, 0, 11025 } };

// The rate at OpenLara pitch (128 = the sample's rate) as SCSP octave and
// frequency: the step 1 + FNS / 1024 between 1 and 2, the octave scaling it
static uint16 pitchOctFns(const Rate &rate, int32 pitch)
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
    return uint16((uint32(octave & 15) << 11) | (step - 1024));
}

// Every slot follows its KYONB: keyed-on slots start, keyed-off ones release.
static void keyExecute()
{
    SCSP_SLOT(0, 0) = SCSP_SLOT(0, 0) | KYONEX;
}

static volatile uint16* driverWord(uint32 addr)
{
    return (volatile uint16*)(SOUND_RAM + addr);
}

// Posts a command, waiting a little for room in the queue (the sound CPU
// takes a few milliseconds over a play); whether there was room.
static bool post(const Command &c)
{
    volatile uint16* head = driverWord(DRIVER_Q_HEAD);
    volatile uint16* tail = driverWord(DRIVER_Q_TAIL);
    uint32 h = *head;
    uint32 next = (h + 1) & (DRIVER_Q_SIZE - 1);
    uint32 v = gVblankCount;
    while (*tail == next)
    {
        if (gVblankCount - v > 2)
            return false;
    }
    volatile uint16* dst = driverWord(DRIVER_QUEUE + h * sizeof(Command));
    const uint16* src = (const uint16*)&c;
    for (uint32 i = 0; i < sizeof(Command) / 2; i++) {
        dst[i] = src[i];
    }
    *head = uint16(next);
    return true;
}

static uint8 driverDone(int32 v)
{
    return ((volatile uint8*)(SOUND_RAM + DRIVER_DONE))[v];
}

// The sound CPU stopped, its program and cleared shared words loaded, and
// started.
static void driverStart()
{
    smpcCommand(SMPC_SNDOFF);
    volatile uint16* ram = (volatile uint16*)SOUND_RAM;
    const uint16* image = (const uint16*)sound_m68k;
    for (uint32 i = 0; i < SOUND_M68K_SIZE / 2; i++) {
        ram[i] = image[i];
    }
    for (uint32 i = 0; i < DRIVER_SHARED / 2; i++) {
        driverWord(DRIVER_Q_HEAD)[i] = 0;
    }
    for (int32 v = 0; v < VOICES; v++)
    {
        gVoices[v].sample = -1;
        gVoices[v].gen = 0;
    }
    smpcCommand(SMPC_SNDON);
}

static void voiceStop(int32 v)
{
    Command c = {};
    c.op = OP_STOP;
    c.voice = uint8(v);
    c.gen = gVoices[v].gen;
    post(c);
    gVoices[v].sample = -1;
}

void sndInit()
{
    smpcCommand(SMPC_SNDOFF);

    SCSP_MIXER = (1 << 9) | 0xF;

    for (int32 s = 0; s < 32; s++) {
        SCSP_SLOT(s, 0) = 0;
    }
    keyExecute();

    SCSP_SLOT_SEND(16) = EXTS_LEVEL | EXTS_LEFT_ONLY;
    SCSP_SLOT_SEND(17) = EXTS_LEVEL | EXTS_RIGHT_ONLY;

    driverStart();
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

    smpcCommand(SMPC_SNDOFF);
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

    driverStart();
}

// The sound CPU's clock against a slot's own position: a silent slot plays
// at pitch from SOUND_SAMPLES_ADDR, and its call address (the position's
// bits 15-12, by the monitor register) steps every 4096 of its samples;
// the clock's samples between steps, the mean of 4 (0: no steps seen).
#define SCSP_MONITOR    SCSP_REG(0x408)     // MSLC (15-11) written, CA (10-7) read
#define CAL_SLOT        31

static uint32 driverSamples()
{
    volatile uint32* samples = (volatile uint32*)(SOUND_RAM + DRIVER_SAMPLES);
    uint32 a = *samples, b;
    while ((b = *samples) != a) {
        a = b;
    }
    return a;
}

static uint32 calibrate(uint16 pitch)
{
    SCSP_SLOT(CAL_SLOT, 0) = KYONEX;
    for (volatile int32 i = 0; i < 200; i++);
    SCSP_SLOT(CAL_SLOT, 0) = 0;
    SCSP_SLOT(CAL_SLOT, 1) = SOUND_SAMPLES_ADDR;
    SCSP_SLOT(CAL_SLOT, 2) = 0;
    SCSP_SLOT(CAL_SLOT, 3) = 0xFFFF;
    SCSP_SLOT(CAL_SLOT, 4) = 0x001F;
    SCSP_SLOT(CAL_SLOT, 5) = 0x001F;
    SCSP_SLOT(CAL_SLOT, 6) = 255;
    SCSP_SLOT(CAL_SLOT, 7) = 0;
    SCSP_SLOT(CAL_SLOT, 8) = pitch;
    SCSP_SLOT(CAL_SLOT, 9) = 0;
    SCSP_SLOT(CAL_SLOT, 10) = 0;
    SCSP_SLOT(CAL_SLOT, 11) = 0;
    SCSP_SLOT(CAL_SLOT, 0) = KYONB | KYONEX;
    SCSP_MONITOR = CAL_SLOT << 11;

    uint32 v = gVblankCount, steps = 0, first = 0, last = 0;
    uint32 ca = (SCSP_MONITOR >> 7) & 15;
    while (steps < 5 && gVblankCount - v < 240)
    {
        uint32 now = (SCSP_MONITOR >> 7) & 15;
        if (now != ca)
        {
            last = driverSamples();
            if (!steps) {
                first = last;
            }
            ca = now;
            steps++;
        }
    }
    SCSP_SLOT(CAL_SLOT, 0) = KYONEX;
    return steps == 5 ? (last - first) / 4 : 0;
}

void sndCalibrate()
{
    uint32 s0 = driverSamples(), v = gVblankCount;
    while (gVblankCount - v < 60);
    crashLog("SCLK", driverSamples() - s0);
    crashLog("SCAL0", calibrate((0xE << 11) | 0));
    crashLog("SCAL1", calibrate((0xE << 11) | 0x200));
}
#endif

#ifdef HW_LOG
void sndStats(int32 &low, uint32 &blocks)
{
    volatile int16* lowWord = (volatile int16*)(SOUND_RAM + DRIVER_LOW);
    low = *lowWord;
    *lowWord = 0x7FFF;
    blocks = *(volatile uint32*)(SOUND_RAM + DRIVER_BLOCKS);
}
#endif

void sndInitSamples()
{
}

void sndFreeSamples()
{
    sndStop();
}

// Frees the voices whose sound has played out.
void sndFrame()
{
    for (int32 v = 0; v < VOICES; v++)
    {
        Voice &voice = gVoices[v];
        if (voice.sample >= 0 && driverDone(v) == voice.gen) {
            voice.sample = -1;
        }
    }
}

void* sndPlaySample(int32 index, int32 volume, int32 pitch, int32 mode)
{
    if (!gSettings.audio_sfx || index < 0 || index >= level.soundOffsetsCount)
        return NULL;

    uint32 entry = entryOf(index);
    uint32 size = uint32(level.soundOffsets[index * 2 + 1]);
    uint32 length = size & SOUND_LENGTH_MASK;
    if (!length)
        return NULL;
    uint32 format = (entry >> SOUND_FORMAT_SHIFT) & SOUND_FORMAT_MASK;
    bool decoded = format == SOUND_FORMAT_ADPCM;
    uint32 load = decoded ? (size >> SOUND_COST_SHIFT) * 2 * DECODE_RATE * uint32(X_MAX(pitch, 1)) / 128 : 0;
    uint32 tl = sampleTL(index, volume);

    // already playing: its level updated, or restarted for REPLAY
    int32 v = 0;
    while (v < VOICES && gVoices[v].sample != index) {
        v++;
    }
    if (v < VOICES && mode != REPLAY)
    {
        if (gVoices[v].tl != tl)
        {
            Command c = {};
            c.op = OP_LEVEL;
            c.voice = uint8(v);
            c.tl = uint8(tl);
            post(c);
            gVoices[v].tl = uint8(tl);
        }
        return gVoices + v;
    }

    // past what the sound CPU can decode, the quietest voices decoding give
    // way to a louder sound, or it doesn't play
    if (load)
    {
        uint32 total = 0;
        for (int32 k = 0; k < VOICES; k++) {
            total += (k != v && gVoices[k].sample >= 0) ? gVoices[k].load : 0;
        }
        while (total + load > DECODE_BUDGET)
        {
            int32 quietest = -1;
            for (int32 k = 0; k < VOICES; k++)
            {
                if (k != v && gVoices[k].sample >= 0 && gVoices[k].load &&
                    (quietest < 0 || gVoices[k].tl > gVoices[quietest].tl)) {
                    quietest = k;
                }
            }
            if (quietest < 0 || gVoices[quietest].tl <= tl)
                return NULL;
            total -= gVoices[quietest].load;
            voiceStop(quietest);
        }
    }

    if (v == VOICES)
    {
        v = 0;
        while (v < VOICES && gVoices[v].sample >= 0) {
            v++;
        }
        if (v == VOICES)
            return NULL;
    }

    Voice &voice = gVoices[v];
    Command c = {};
    c.op = OP_PLAY;
    c.voice = uint8(v);
    c.gen = uint8(voice.gen + 1);
    c.flags = uint8((mode == LOOP ? FLAG_LOOP : 0) | (format << 1));
    c.addr = SOUND_SAMPLES_ADDR + (entry & SOUND_OFFSET_MASK);
    c.length = decoded ? length : (format == SOUND_FORMAT_PCM8 ? length : length >> 1);
    c.pitch = pitchOctFns(gRates[(entry >> SOUND_RATE_SHIFT) & SOUND_RATE_MASK], pitch);
    c.tl = uint8(tl);
    c.pan = 0;
    if (!post(c))
        return NULL;

    voice.sample = index;
    voice.gen = c.gen;
    voice.tl = uint8(tl);
    voice.load = load;
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
    for (int32 v = 0; v < VOICES; v++)
    {
        if (gVoices[v].sample == index) {
            voiceStop(v);
        }
    }
}

// Stops every voice and waits for the sound CPU to have (so the samples may
// be replaced).
void sndStop()
{
    sndStopTrack();

    for (int32 v = 0; v < VOICES; v++)
    {
        if (gVoices[v].sample >= 0) {
            voiceStop(v);
        }
    }
    uint32 t = gVblankCount;
    while (*driverWord(DRIVER_Q_TAIL) != *driverWord(DRIVER_Q_HEAD) && gVblankCount - t < 10);
}
