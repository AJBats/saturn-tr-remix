// A crash screen for real hardware, which has no debugger. A CPU exception on
// either CPU, or the game loop standing still, shows VDP2's bitmap in place
// of the frame, with numbers for a photo (crash.cpp lists them):
//   red      the master CPU took an exception
//   magenta  the slave CPU took an exception
//   blue     the game loop stopped for 6 seconds (the master is stuck)
//   green    a video stopped for 4 seconds (fmv.cpp)
// and the last lines of a log of how far the program got.
#ifndef CRASH_H
#define CRASH_H

#include <stdint.h>

// Installs the exception handlers of both CPUs; after screenInit, before
// slaveInit. In HW_LOG builds (make disc HW_LOG=1) the log shows on screen
// from here, and over the loading pictures.
void crashInit();

// A line of the log: a label of capitals and digits, kept as given, and a
// number.
void crashLog(const char* label, uint32_t value);

// Draws the log over VDP2's bitmap in HW_LOG builds (after a picture).
void crashLogShow();

// From the vblank-in interrupt, with the interrupted context's stack.
void crashWatchdog(const uint32_t* irqStack);

// While set (from boot to the first level, level loads, videos), the game
// loop may stand still.
void crashHold(bool hold);

// A crash screen of named numbers (capitals and digits), in two columns of
// CRASH_REPORT_ROWS, under a title line: for a part of the game with its own
// watch (a video, fmv.cpp). Never returns.
#define CRASH_REPORT_ROWS 15
void crashReport(uint16_t color, const char* title, const char* const* labels, const uint32_t* values,
                 int32_t count);

#endif
