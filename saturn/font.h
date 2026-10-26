// Fonts the CPU draws with into VDP2: the crash screen's 3 x 5 pixels, and
// the debug overlay's 8 x 8 (libyaul's, see font.cpp).
#ifndef FONT_H
#define FONT_H

#include <stdint.h>

// A character's 3 x 5 glyph: 15 '0' and '1' characters, rows from the top.
// Lower case reads as capitals; NULL for a space or a character the font
// lacks.
const char* fontGlyph(char c);

// 8 x 8 glyphs of printable ASCII, FONT8_FIRST on: a byte a row from the top,
// bit 0 the leftmost pixel.
#define FONT8_FIRST ' '
#define FONT8_COUNT 95
extern const uint8_t gFont8[FONT8_COUNT][8];

#endif
