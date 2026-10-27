// Saturn platform entry: hardware setup, then OpenLara's game loop.

#include "game.h"
#include "cd.h"
#include "crash.h"
#include "debug.h"
#include "fmv.h"
#include "hw.h"
#include "input.h"
#include "render.h"
#include "screen.h"
#include "slave.h"
#include "sound.h"
#include "system.h"
#include "trace.h"

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

int main()
{
    systemInit();
    screenInit();
    crashInit();
    slaveInit();
#ifdef HW_LOG
    traceInit();
    debugProfileInit();
    debugDspTest();
    debugSyncTest();
#endif
    crashLog("CD", cdInit());
    sndInit();
    crashLog("SOUND", 0);
    padInit();

    gLevelID = START_LEVEL;

    // the original's opening: publisher, developer, then the story's start
    if (gLevelID == LVL_TR1_TITLE)
    {
        fmvPicture("EIDOS224", 320, 60 * 3);
        fmvPlay("CORELOGO");
        fmvPlay("CAFE");
    }

    gameInit();

#ifdef START_YAW
    // testing: Lara's exact heading where LARA_START put her (items keep a
    // quarter turn)
    if (players[0]) {
        players[0]->angle.y = int16(START_YAW);
    }
#endif

    uint32 lastTick = gVblankCount >> 1;
    uint32 fpsStart = gVblankCount;
    uint32 fpsFrames = 0;

    while (1)
    {
        // Game logic runs in 30 Hz ticks: wait for the next one. The pads
        // are read from an interrupt, so a slow frame doesn't wait for theirs.
        uint32 tick;
        traceBegin(TRACE_MASTER, TR_TICK_WAIT);
        while ((tick = gVblankCount >> 1) == lastTick) {
            renderPoll();
        }
        gPad = padButtons();

        int32 delta = X_MIN(int32(tick - lastTick), MAX_UPDATE_FRAMES);
        lastTick = tick;

        uint32 t0 = timerTicks();
        traceEndAt(TRACE_MASTER, t0, TR_TICK_WAIT);
        traceBeginAt(TRACE_MASTER, t0, TR_UPDATE);
        updateInput();
        debugUpdate();
        renderPoll();
        gameUpdate(delta);
        renderPoll();
        sndFrame();
        uint32 t1 = timerTicks();
        traceEndAt(TRACE_MASTER, t1, TR_UPDATE);
        traceBeginAt(TRACE_MASTER, t1, TR_RENDER);
        gameRender();
        renderSwap();
        uint32 t2 = timerTicks();
        traceEndAt(TRACE_MASTER, t2, TR_RENDER);

        gStats.updateTicks = (t1 - t0) & 0xFFFF;
        gStats.renderTicks = (t2 - t1) & 0xFFFF;
        gStats.frames++;
#ifdef HW_LOG
        debugFrameDone();
#endif
        if (gStats.frames <= 3) {
            crashLog("FRAME", gStats.frames);
        }

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
