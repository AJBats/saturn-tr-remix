// Saturn platform entry: hardware setup, then OpenLara's game loop.

#include "game.h"
#include "cd.h"
#include "debug.h"
#include "fmv.h"
#include "hw.h"
#include "input.h"
#include "screen.h"
#include "slave.h"
#include "sound.h"
#include "system.h"

// make START_LEVEL=LVL_TR1_2 ... starts in a level, for testing
#ifndef START_LEVEL
    #define START_LEVEL LVL_TR1_TITLE
#endif

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
        { PAD_START, IK_SELECT }    // inventory, as in the original (IK_START is a free camera)
    };

    keys = 0;
    for (uint32 i = 0; i < sizeof(map) / sizeof(map[0]); i++)
    {
        if (gPad & map[i].pad) {
            keys |= map[i].key;
        }
    }
}

// Pad reads (SMPC INTBACK) must be issued inside vblank, at least 300us
// after it starts (about 10 lines).
static bool inPadWindow()
{
    return (VDP2_TVSTAT & TVSTAT_VBLANK) && vdp2Line() >= FRAME_HEIGHT + 12;
}

int main()
{
    systemInit();
    screenInit();
    slaveInit();
    cdInit();
    sndInit();

    gLevelID = START_LEVEL;

    // the original's opening: publisher, developer, then the story's start
    if (gLevelID == LVL_TR1_TITLE)
    {
        fmvPicture("EIDOS224", 320, 60 * 3);
        fmvPlay("CORELOGO");
        fmvPlay("CAFE");
    }

    gameInit();

    uint32 lastTick = gVblankCount >> 1;
    uint32 lastPad = gVblankCount;
    uint32 fpsStart = gVblankCount;
    uint32 fpsFrames = 0;

    while (1)
    {
        // Game logic runs in 30 Hz ticks; wait for the next one, reading the
        // pad when the vblank window comes round.
        uint32 tick;
        while ((tick = gVblankCount >> 1) == lastTick)
        {
            if (gVblankCount != lastPad && inPadWindow()) {
                gPad = padUpdate();
                lastPad = gVblankCount;
            }
        }

        // a slow frame skipped the window: read the pad before going on
        if (gVblankCount - lastPad >= 2)
        {
            while (!inPadWindow());
            gPad = padUpdate();
            lastPad = gVblankCount;
        }

        int32 delta = X_MIN(int32(tick - lastTick), MAX_UPDATE_FRAMES);
        lastTick = tick;

        uint32 t0 = timerTicks();
        updateInput();
        debugUpdate();
        gameUpdate(delta);
        sndFrame();
        uint32 t1 = timerTicks();
        gameRender();
        renderSwap();
        uint32 t2 = timerTicks();

        gStats.updateTicks = (t1 - t0) & 0xFFFF;
        gStats.renderTicks = (t2 - t1) & 0xFFFF;
        gStats.frames++;

        fpsFrames++;
        if (gVblankCount - fpsStart >= 60)
        {
            fps = gStats.fps = fpsFrames;
            fpsFrames = 0;
            fpsStart += 60;
        }
    }

    return 0;
}
