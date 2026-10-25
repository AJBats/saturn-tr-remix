// Saturn platform entry: hardware setup, then OpenLara's game loop.

#include "game.h"
#include "cd.h"
#include "hw.h"
#include "input.h"
#include "system.h"

int32 fps;

static uint16 gPad;

static void updateInput()
{
    static const struct { uint16 pad; uint32 key; } map[] = {
        { PAD_UP,    IK_UP    }, { PAD_DOWN,  IK_DOWN  },
        { PAD_LEFT,  IK_LEFT  }, { PAD_RIGHT, IK_RIGHT },
        { PAD_A,     IK_A     }, { PAD_B,     IK_B     }, { PAD_C, IK_C },
        { PAD_X,     IK_X     }, { PAD_Y,     IK_Y     }, { PAD_Z, IK_Z },
        { PAD_L,     IK_L     }, { PAD_R,     IK_R     },
        { PAD_START, IK_START }
    };

    keys = 0;
    for (uint32 i = 0; i < sizeof(map) / sizeof(map[0]); i++)
    {
        if (gPad & map[i].pad) {
            keys |= map[i].key;
        }
    }
}

// INTBACK must be issued at least 300us into vblank (about 10 lines).
static void waitVblankLines(int32 lines)
{
    while (VDP2_VCNT < FRAME_HEIGHT + lines);
}

int main()
{
    systemInit();
    cdInit();

    gLevelID = LVL_TR1_1; // TODO title screen and menus

    gameInit();

    while (1)
    {
        updateInput();

        gameUpdate(1);

        gameRender();

        // game logic runs at 30 Hz
        waitVblank();
        renderSwap();
        waitVblankLines(12);
        gPad = padUpdate();
        waitVblank();
    }

    return 0;
}
