// Saturn control pad on port 1, read through SMPC INTBACK (peripheral data
// only, no SMPC status), from an interrupt in every vblank: the SMPC collects
// the data at the end of vblank, so each read takes the result of the request
// the one before made. As retail's pad scan (06043720): the first digital pad
// (id 0x02; the 3D pad in its digital mode is one) or id 0x15 device (analog,
// 5 data bytes, the buttons first: the Mission Stick, by its id) on port 1,
// through a multitap if one is there; port 2's digital pad is the debug pad.
// The 3D pad in its analog mode (id 0x16) isn't one retail reads: its peripheral
// library files it under its own id (the generic handler, 06005956) and the
// scan passes over it, so with only that on port 1 retail sees no pad.

#include "input.h"
#include "hw.h"
#include "system.h"

#define INTBACK     0x10
#define IREG1_PEN   0x08    // peripheral data enable, 15-byte mode for both ports
#define IREG0_BREAK 0x40
#define SR_NPE      0x20    // more peripheral data remains

#define ID_DIGITAL  0x02    // standard pad, 2 data bytes
#define ID_ANALOG   0x15    // an analog device of 5 data bytes, the buttons first
#define ID_NONE     0xFF    // a multitap's empty port, no data

// INTBACK must be issued inside vblank, at least 300us after it starts (about
// 10 lines).
#define READ_LINE   (SCREEN_HEIGHT + 12)

static bool gPending;
static uint32_t gIssued;    // smpcCommands() when the pending request was made
static volatile uint16_t gButtons;
static volatile uint16_t gDebugButtons;
static volatile bool gPresent = true;   // till a read says otherwise: none has come back at boot
#ifdef HW_LOG
static volatile uint32_t gRaw;          // the last read's first four bytes, for the boot log
#endif

// A port's peripherals from OREG(at): the buttons of the first pad of one of
// the ids accepted (none: false); the next port's data after them in next.
static bool padPort(int32_t at, bool analog, uint16_t &buttons, int32_t &next)
{
    bool found = false;
    int32_t count = SMPC_OREG(at++) & 0x0F;     // the port's status: its peripherals
    for (int32_t i = 0; i < count && at < 32; i++)
    {
        uint8_t id = SMPC_OREG(at++);
        if (id == ID_NONE)
            continue;
        int32_t size = id & 0x0F;
        if (size == 0x0F) {
            size = SMPC_OREG(at++);
        }
        if (!found && (id == ID_DIGITAL || (analog && id == ID_ANALOG)) && at + 1 < 32)
        {
            buttons = ~((SMPC_OREG(at) << 8) | SMPC_OREG(at + 1)) & 0xFFF8;
            found = true;
        }
        at += size;
    }
    next = at;
    return found;
}

static void padRead()
{
    if (gPending)
    {
        if (SMPC_SF & 1)
            return; // not finished yet; keep the last state

        if (smpcCommands() != gIssued)
        {
            // another command came after it: its results are gone
        }
        else
        {
            uint16_t buttons = 0, debug = 0;
            int32_t next;
#ifdef HW_LOG
            gRaw = (SMPC_OREG(0) << 24) | (SMPC_OREG(1) << 16) | (SMPC_OREG(2) << 8) | SMPC_OREG(3);
#endif
            gPresent = padPort(0, true, buttons, next);
            padPort(next, false, debug, next);     // port 2's data follows port 1's
            gButtons = buttons;
            gDebugButtons = debug;
        }

        if (SMPC_SR & SR_NPE)
        {
            SMPC_IREG(0) = IREG0_BREAK;
        }
        gPending = false;
    }

    SMPC_SF = 1;
    SMPC_IREG(0) = 0x00;
    SMPC_IREG(1) = IREG1_PEN;
    SMPC_IREG(2) = 0xF0;
    SMPC_COMREG = INTBACK;
    gPending = true;
    gIssued = smpcCommands();
}

void padInit()
{
    onLine(READ_LINE, padRead);
}

uint16_t padButtons()
{
    return gButtons;
}

bool padPresent()
{
    return gPresent;
}

#ifdef HW_LOG
uint32_t padRaw()
{
    return gRaw;
}
#endif

uint16_t padDebug()
{
    return gDebugButtons;
}
