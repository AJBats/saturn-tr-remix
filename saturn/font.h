// The 3 x 5 pixel font of the crash screen and the debug overlay, drawn by
// the CPU into VDP2 bitmaps.
#ifndef FONT_H
#define FONT_H

// A character's glyph: 15 '0' and '1' characters, rows from the top. Lower
// case reads as capitals; NULL for a space or a character the font lacks.
const char* fontGlyph(char c);

#endif
