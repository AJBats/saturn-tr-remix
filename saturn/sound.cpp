// Saturn sound. Music is CD audio, which the SCSP mixes in through its
// external input; sound effects are not implemented yet.

#include "common.h"
#include "cd.h"

#define SCSP_REG(x)     (*(volatile uint16*)(0x25B00000 + (x)))
#define SCSP_MIXER      SCSP_REG(0x400)     // MEM4MB (bit 9), DAC18B (bit 8), MVOL (bits 3-0)
#define SCSP_SLOT_SEND(slot) SCSP_REG((slot) * 0x20 + 0x16)  // DISDL, DIPAN, EFSDL, EFPAN

// Effect sends of slots 16 and 17 carry the external input, CD audio left and
// right. EFPAN bit 4 picks the side attenuated by bits 3-0; 15 mutes it.
#define EXTS_LEVEL      (5 << 5)            // the original's level
#define EXTS_LEFT_ONLY  0x1F
#define EXTS_RIGHT_ONLY 0x0F

// Audio tracks on the disc
#define TRACK_FIRST     2
#define TRACK_LAST      57

void sndInit()
{
    SCSP_MIXER = (1 << 9) | 0xF;
    SCSP_SLOT_SEND(16) = EXTS_LEVEL | EXTS_LEFT_ONLY;
    SCSP_SLOT_SEND(17) = EXTS_LEVEL | EXTS_RIGHT_ONLY;
}

void sndInitSamples()
{
}

void sndFreeSamples()
{
}

void* sndPlaySample(int32 index, int32 volume, int32 pitch, int32 mode)
{
    return NULL;
}

// Track numbers match the original disc up to 56 (the Caves play 5, as in the
// original). OpenLara's ambient tracks 57-59 are PC numbers whose Saturn
// equivalents are not known yet; they play nothing.
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
}

void sndStop()
{
    sndStopTrack();
}
