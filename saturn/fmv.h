// The original's full-motion video: FMV/<name>.CPK, Sega FILM files of
// Cinepak video and 16-bit stereo PCM, streamed from the CD.
#ifndef FMV_H
#define FMV_H

// Plays FMV/<name>.CPK to its end, or until START is pressed. Uses the sound
// RAM and low work RAM, so it runs between levels.
void fmvPlay(const char* name);

// Shows BINDATA/<name>.BIN for up to the given number of frames, or until
// START is pressed.
void fmvPicture(const char* name, int width, int frames);

#endif
