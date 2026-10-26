// Saturn control pad on port 1, read through SMPC INTBACK (peripheral data
// only, no SMPC status), from an interrupt in every vblank: the SMPC collects
// the data at the end of vblank, so each read takes the result of the request
// the one before made.

#include "input.h"
#include "hw.h"
#include "system.h"

#define INTBACK     0x10
#define IREG1_PEN   0x08    // peripheral data enable, 15-byte mode for both ports
#define IREG0_BREAK 0x40
#define SR_NPE      0x20    // more peripheral data remains

#define PORT_DIRECT 0xF1    // port status: one peripheral, no multitap
#define ID_DIGITAL  0x02    // standard pad, 2 data bytes

// INTBACK must be issued inside vblank, at least 300us after it starts (about
// 10 lines).
#define READ_LINE   (SCREEN_HEIGHT + 12)

static bool gPending;
static uint32_t gIssued;    // smpcCommands() when the pending request was made
static volatile uint16_t gButtons;
static volatile uint16_t gDebugButtons;

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
        else if (SMPC_OREG(0) == PORT_DIRECT && SMPC_OREG(1) == ID_DIGITAL)
        {
            gButtons = ~((SMPC_OREG(2) << 8) | SMPC_OREG(3)) & 0xFFF8;

            // port 2's data follows port 1's
            if (SMPC_OREG(4) == PORT_DIRECT && SMPC_OREG(5) == ID_DIGITAL) {
                gDebugButtons = ~((SMPC_OREG(6) << 8) | SMPC_OREG(7)) & 0xFFF8;
            } else {
                gDebugButtons = 0;
            }
        }
        else
        {
            gButtons = 0;
            gDebugButtons = 0;
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

uint16_t padDebug()
{
    return gDebugButtons;
}
