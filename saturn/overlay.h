// Debug text over the game's frame, on VDP2 (NBG0's bitmap, see screen.h): VDP1
// never draws it, so it takes nothing from the frame it measures. Text goes
// in rows, each written again only when it changes, in libyaul's 8 x 8 font,
// white on black.
#ifndef OVERLAY_H
#define OVERLAY_H

#include <stdint.h>

#define OVERLAY_ROWS    26
#define OVERLAY_COLUMNS 42      // characters a row holds; the rest is cut

enum OverlayAlign { OVERLAY_LEFT, OVERLAY_CENTRE, OVERLAY_RIGHT };

// Row 'row' (0 at the top) reads 'text' this frame.
void overlayRow(int32_t row, const char* text, OverlayAlign align = OVERLAY_LEFT);

// After the frame's rows: rows not given text this frame go blank, and the
// layer shows while any row has text.
void overlayFrame();

#endif
