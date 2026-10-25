// A crash screen for real hardware, which has no debugger. A CPU exception on
// either CPU, or the game loop standing still, shows VDP2's bitmap in place
// of the frame, with numbers for a photo (crash.cpp lists them):
//   red      the master CPU took an exception
//   magenta  the slave CPU took an exception
//   blue     the game loop stopped for 6 seconds (the master is stuck)
#ifndef CRASH_H
#define CRASH_H

#include <stdint.h>

// Installs the exception handlers of both CPUs; before slaveInit.
void crashInit();

// From the vblank-in interrupt, with the interrupted context's stack.
void crashWatchdog(const uint32_t* irqStack);

// While set (level loads, videos), the game loop may stand still.
void crashHold(bool hold);

#endif
