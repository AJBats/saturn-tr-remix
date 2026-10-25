// Saturn platform entry: hardware setup, then OpenLara's game loop.

#include "game.h"
#include "cd.h"
#include "detail.h"
#include "fmv.h"
#include "hw.h"
#include "input.h"
#include "palettes.h"
#include "render.h"
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
static uint16 gDebugPad;

static const char* gDebugMessage;
static uint32 gDebugMessageEnd;

static void debugMessage(const char* text)
{
    gDebugMessage = text;
    gDebugMessageEnd = gVblankCount + 120;
}

static char* appendStat(char* p, const char* name, uint32 value)
{
    while (*name) {
        *p++ = *name++;
    }
    int2str(int32(value), p);
    while (*p) {
        p++;
    }
    *p++ = ' ';
    *p++ = ' ';
    *p = 0;
    return p;
}

// Share of a 30 fps frame (FRT ticks at the master's clock / 128).
static uint32 framePercent(uint32 ticks)
{
    return ticks * 100 / 7457;
}

const char* osDebugMessage(int32 line)
{
    if (!gTexelView && !gNoTextureLod)
        return line == 0 && int32(gDebugMessageEnd - gVblankCount) > 0 ? gDebugMessage : NULL;

    static char text[64];
    char* p = text;
    if (line == 0)
    {
        if (int32(gDebugMessageEnd - gVblankCount) > 0)
            return gDebugMessage;

        // room faces drawn full res; left at quarter res for want of a cache
        // slot or upload; near ones without a full-res texture; near triangles
        p = appendStat(p, "full ", gStats.detailFaces);
        p = appendStat(p, "missed ", gStats.detailMisses);
        p = appendStat(p, "none ", gStats.nearNoDetail);
        appendStat(p, "tri ", gStats.nearTriangles);
    }
    else
    {
        p = appendStat(p, "vdp1 ", framePercent(gStats.vdp1Ticks));
        p = appendStat(p, "cpu ", framePercent(gStats.updateTicks + gStats.renderTicks - gStats.waitTicks));
        appendStat(p, "uploads ", gStats.detailUploads);
    }
    return text;
}

// The debug pad on port 2 (Mednafen: P is its A, H its B, G its C): A swaps
// the colour tables, B cycles the texture views, C turns texture LOD off.
static void updateDebug()
{
    uint16 pad = padDebug();
    uint16 pressed = pad & ~gDebugPad;
    gDebugPad = pad;

    if (pressed & PAD_A)
    {
        palettesToggle();
        debugMessage(palettesRemastered() ? "Remastered palette" : "Saturn palette");
    }

    if (pressed & PAD_B)
    {
        static const char* const names[TEXEL_VIEW_COUNT] = {
            "Normal view", "Texture resolution view", "Texel size view"
        };
        gTexelView = (gTexelView + 1) % TEXEL_VIEW_COUNT;
        debugMessage(names[gTexelView]);
    }

    if (pressed & PAD_C)
    {
        gNoTextureLod = !gNoTextureLod;
        detailSetWhole(gNoTextureLod, level.textures, level.texturesCount);
        debugMessage(gNoTextureLod ? "Full-res textures everywhere" : "Saturn texture LOD");
    }
}

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
        updateDebug();
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
