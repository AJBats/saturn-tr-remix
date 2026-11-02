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

// The pad as the game's keys: the master's at the frame's top, the slave's
// before the next frame's update.
static void updateInput()
{
    uint16 pad = padButtons();
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
        if (pad & map[i].pad) {
            keys |= map[i].key;
        }
    }
}

// Frames overlap on the two CPUs in play. Once frame N's draw list is made
// (what N draws of the game is recorded by then), the slave runs frame N+1's
// update and walks N+1's rooms (renderer/frame.h frameNextWork) while the
// master finishes N; the master waits for it at N+1's top. The inventory, the
// title, cutscenes and a level ending keep the update on the master at the
// frame's top. What an update changes that the renderer reads waits for the
// frame's top (level.h levelCommit). The pad is read just before the update,
// about two thirds of a frame earlier than at the frame's top.
static bool gNextHanded;        // the master's: the slave updates the next frame in this one's render
static bool gNextDone;          // the master's: this frame's update is the slave's
static uint32 gNextRan;         // uncached: the slave ran it
static uint32 gNextTick;        // the tick the last update ran to

static void nextFrame()
{
    cachePurge();       // the game as the master left it
    traceBegin(TRACE_SLAVE, TR_SLAVE_UPDATE);
    updateInput();
    if (debugOwnsPad()) {
        keys = 0;
    }
    uint32 tick = gVblankCount >> 1;
    int32 delta = X_MAX(X_MIN(int32(tick - gNextTick), MAX_UPDATE_FRAMES), 1);
    gNextTick = tick;
    gameUpdateItems(delta);
    sndFrame();
    traceEnd(TRACE_SLAVE, TR_SLAVE_UPDATE);
    // a flip's rooms aren't the renderer's yet (levelCommit): the master walks them
    if (gameInPlay() && !gFlipsDue)
    {
        traceBegin(TRACE_SLAVE, TR_SLAVE_WALK);
        renderWalkAhead(players[0]->extraL->camera.view.room);
        traceEnd(TRACE_SLAVE, TR_SLAVE_WALK);
    }
    *uncached(&gNextRan) = 1;
}

// At the frame's top: the slave's update of this frame waited for, if it was
// given one. Returns the wait, in ticks.
static uint32 nextFrameWait()
{
    if (!gNextHanded)
        return 0;
    gNextHanded = false;
    uint32 t0 = timerTicks();
    traceBeginAt(TRACE_MASTER, t0, TR_UPDATE_WAIT);
    slaveWait();
    uint32 t1 = timerTicks();
    traceEndAt(TRACE_MASTER, t1, TR_UPDATE_WAIT);
    cachePurge();       // the game as the slave left it
    gNextDone = *uncached(&gNextRan);
    *uncached(&gNextRan) = 0;
    return (t1 - t0) & 0xFFFF;
}

// The frame's update: what the slave didn't do of it, what updates changed
// given to the renderer; then the next frame's handed to the slave if in play.
static void frameUpdate(int32 delta, uint32 tick)
{
    bool done = gNextDone;
    gNextDone = false;
    levelCommit();
    gameUpdate(delta, !done);
    renderPoll();
    if (!done) {
        sndFrame();
    }
    levelCommit();
    gNextHanded = gameInPlay();
    if (gNextHanded && !done) {
        gNextTick = tick;
    }
    renderNextWork(gNextHanded ? nextFrame : nullptr);
}

int main()
{
    cpuInit(false);
    systemInit();
    screenInit();
    crashInit();
    slaveInit();
#ifdef HW_LOG
    traceInit();
    debugProfileInit();
#ifdef SYNC_TEST
    debugSyncTest();
#endif
#endif
    crashLog("CD", cdInit());
    sndInit();
#if defined(HW_LOG) && defined(SOUND_BENCH)
    sndBench();
    sndCalibrate();
#endif
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

        int32 delta = X_MIN(int32(tick - lastTick), MAX_UPDATE_FRAMES);
        lastTick = tick;

        uint32 t0 = timerTicks();
        traceEndAt(TRACE_MASTER, t0, TR_TICK_WAIT);
        traceBeginAt(TRACE_MASTER, t0, TR_UPDATE);
        uint32 waited = nextFrameWait();
        updateInput();
        debugUpdate();
#ifdef HW_LOG
        if (debugHeld())
        {
            debugPlayFrame();
            continue;
        }
#endif
        renderPoll();
        frameUpdate(delta, tick);
#ifdef HW_LOG
        if (const ItemObj* lara = (const ItemObj*)(const void*)players[0]) {
            tracePlace(uint32(lara->room - rooms), lara->pos.x, lara->pos.y, lara->pos.z, uint16(lara->angle.y));
        }
#endif
        uint32 t1 = timerTicks();
        traceEndAt(TRACE_MASTER, t1, TR_UPDATE);
        traceBeginAt(TRACE_MASTER, t1, TR_RENDER);
        gameRender();
        renderSwap();
        uint32 t2 = timerTicks();
        traceEndAt(TRACE_MASTER, t2, TR_RENDER);

        gStats.updateTicks = ((t1 - t0) & 0xFFFF) - waited;
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
