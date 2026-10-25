// Saturn control pad on port 1, read through SMPC INTBACK (peripheral data
// only, no SMPC status). The SMPC collects the data at the end of vblank, so
// each call reads the result of the request made by the previous call.

#include "input.h"
#include "hw.h"

#define SMPC_REG(x) (*(volatile uint8_t*)(0x20100000 + (x)))
#define SMPC_IREG(n) SMPC_REG(0x01 + (n) * 2)
#define SMPC_COMREG SMPC_REG(0x1F)
#define SMPC_OREG(n) SMPC_REG(0x21 + (n) * 2)
#define SMPC_SR     SMPC_REG(0x61)
#define SMPC_SF     SMPC_REG(0x63)

#define INTBACK     0x10
#define IREG1_PEN   0x08    // peripheral data enable, 15-byte mode for both ports
#define IREG0_BREAK 0x40
#define SR_NPE      0x20    // more peripheral data remains

#define PORT_DIRECT 0xF1    // port status: one peripheral, no multitap
#define ID_DIGITAL  0x02    // standard pad, 2 data bytes

static bool gPending;
static uint16_t gButtons;

uint16_t padUpdate()
{
    if (gPending)
    {
        if (SMPC_SF & 1)
            return gButtons; // not finished yet; keep the last state

        if (SMPC_OREG(0) == PORT_DIRECT && SMPC_OREG(1) == ID_DIGITAL)
        {
            gButtons = ~((SMPC_OREG(2) << 8) | SMPC_OREG(3)) & 0xFFF8;
        }
        else
        {
            gButtons = 0;
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

    return gButtons;
}
