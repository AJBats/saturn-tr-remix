// A timeline of every frame, in HW_LOG builds: both CPUs, the master's
// interrupts and the SCU DSP note when each stage of the frame begins and
// ends, a ring of events each, in low work RAM above the level (a header at
// 0x002FFF00 says where). tools/trace.py turns a dump of the rings
// (Mednafen's) into a Chrome trace (chrome://tracing, ui.perfetto.dev) with
// VDP1 and the vblanks beside them; on a Saturn, the overlay's playback page
// shows the frames the rings hold one page each, for a video that
// tools/trace.py reads into the same trace.
//
// The master writes its lanes; the slave its own and the DSP's (it runs the
// DSP).
//
// An event is one longword: the writer's FRT count (ticks of 128 cycles, as
// timerTicks) in the top half, then the id (TRACE_END set on a span's end),
// then a byte of argument. The slave's counter runs at a fixed offset from the
// master's, measured at boot (traceInit).
#ifndef TRACE_H
#define TRACE_H

#include <stdint.h>
#include "system.h"

// tools/trace.py reads the names from this enum: keep one id a line.
enum TraceId
{
    // the master
    TR_TICK_WAIT = 1,   // waiting for the next 30 Hz tick
    TR_UPDATE,          // game logic, sound, input
    TR_DEBUG,           // the overlay's own work (debugUpdate)
    TR_RENDER,          // gameRender: the stages below, the rest its own
    TR_VIEW,            // the rooms the camera sees, walked (renderWalk)
    TR_DRAW_LIST,       // the frame's calls into the draw list (draws.s)
    TR_LARA,            // Lara's calls
    TR_OBJECTS,         // a room's sprites', static meshes' and items' calls (arg: room)
    TR_PLAN,            // the lists split, the hints begun, the frame planned
    TR_POSE,            // the master's items lit and posed, the statics placed
    TR_ITEM,            // an item's calls (arg: its type)
    TR_OVERLAY,         // the overlay's text drawn (osDrawDebug)
    TR_FLUSH,           // flush: the frame's stages on both CPUs and the DSP
    TR_SLAVE_WAIT,      // waiting for the slave's rooms, then its half of the join
    TR_POSE_WAIT,       // waiting for the items the slave poses
    TR_VDP1_WAIT,       // waiting for VDP1 to finish the last list
    TR_UPLOAD,          // textures, commands and gouraud table to VDP1
    TR_WALK_WAIT,       // waiting for the slave to lay out the rooms' jobs
    TR_FEED,            // the meshes fed to the DSP
    TR_CHUNKS,          // the master's chunks of the rooms' vertices projected
    TR_DRAWS,           // the draws: each mesh's vertices projected as the DSP hands them, its faces
    TR_JOIN,            // the far half of the buckets joined
    // the slave
    TR_JOBS = 32,       // the rooms' jobs laid out, the DSP started on them
    TR_PLAN_WAIT,       // waiting for the master's plan
    TR_SLAVE_POSE,      // its items lit and posed
    TR_ROOMS,           // its rooms: their chunks projected as the DSP hands them, their faces
    TR_DRAWS_WAIT,      // waiting for the master's draws
    TR_SLAVE_JOIN,      // the near half of the buckets joined
    // the SCU DSP, on the slave's clock: its runs as they start and end
    TR_DSP_RUN = 40,    // the rooms' vertices (arg 0), then the meshes' (arg 1)
    // the master's interrupts
    TR_IRQ_VBLANK_IN = 48,
    TR_IRQ_VBLANK_OUT,
    TR_IRQ_LINE,        // timer 0 at line 236: the pads
    // moments rather than spans
    TR_PLOT = 64,       // VDP1 starts on a list (arg: the plot's number)
    TR_DRAWN,           // VDP1 finished it
    TR_CHANGE,          // frame buffer change asked for, at a vblank-in
    TR_SHOWN,           // the change is on screen (arg: the plot's number)
    TR_ID_COUNT
};

#define TR_FIRST_MOMENT TR_PLOT
#define TRACE_END       0x80    // with the id: a span ends

// The playback's letter for each id, '.' for those it leaves out (tools/
// trace.py reads it): a span's, but TR_PLOT's is VDP1's from it to TR_DRAWN.
// No two alike in the font by fewer than 6 of their 64 pixels.
//                      0         1         2         3         4         5         6
//                      01234567890123456789012345678901234567890123456789012345678901234567
#define TRACE_LETTERS  ".TUGRVQLOSMIYFWABXDHNjJ.........bxfmkh..d.......z...............p..s"

enum TraceLane { TRACE_MASTER, TRACE_IRQ, TRACE_SLAVE, TRACE_DSP, TRACE_LANES };

#ifdef HW_LOG

// After slaveInit: measures the slave's counter against the master's.
void traceInit();

// The level's data ends at end in low work RAM: the rings record if they fit
// above it. traceStop stops them before a level loads over them; traceResume
// starts them afresh above the same level.
void traceLevel(const void* end);
void traceStop();
void traceResume();

// The overlay's Trace option: no events (each costs a Saturn a timer read,
// about 110 cycles, and a few stores), the stages, or the stages and the
// master's detail spans (TR_OBJECTS to TR_ITEM, some 130 more a frame).
enum TraceMode { TRACE_OFF, TRACE_STAGES, TRACE_DETAIL, TRACE_MODES };
void traceSetMode(int32_t mode);
int32_t traceMode();

extern bool gTraceOn;       // the rings record
extern bool gTraceDetail;   // and the detail spans too

// An event at ticks on lane, from the CPU (and context) that owns the lane.
// A lane's events must be written in time order (tools/trace.py unwraps the
// 16-bit counts by their differences): a span written after the work it
// times may hold no other event of its lane.
void traceAt(int32_t lane, uint32_t ticks, uint32_t code);

inline void traceBegin(int32_t lane, uint32_t id, uint32_t arg = 0)
{
    if (gTraceOn) {
        traceAt(lane, timerTicks(), (id << 8) | (arg & 0xFF));
    }
}

inline void traceEnd(int32_t lane, uint32_t id, uint32_t arg = 0)
{
    if (gTraceOn) {
        traceAt(lane, timerTicks(), ((id | TRACE_END) << 8) | (arg & 0xFF));
    }
}

inline void traceBeginAt(int32_t lane, uint32_t ticks, uint32_t id, uint32_t arg = 0)
{
    traceAt(lane, ticks, (id << 8) | (arg & 0xFF));
}

inline void traceEndAt(int32_t lane, uint32_t ticks, uint32_t id, uint32_t arg = 0)
{
    traceAt(lane, ticks, ((id | TRACE_END) << 8) | (arg & 0xFF));
}

// The detail spans, only while gTraceDetail.
inline void traceDetailBegin(uint32_t id, uint32_t arg = 0)
{
    if (gTraceDetail) {
        traceBegin(TRACE_MASTER, id, arg);
    }
}

inline void traceDetailEnd(uint32_t id, uint32_t arg = 0)
{
    if (gTraceDetail) {
        traceEnd(TRACE_MASTER, id, arg);
    }
}

// The overlay's playback page (debug.cpp) holds the game: the rings stop,
// and each tick tracePlayFrame shows the next of the frames they hold on the
// overlay, over and over (true once the last has been up), until
// tracePlayStop: then they record afresh. A page: a header row (the frame's
// number of how many, its start and length, its events shown and left out,
// and the tag tracePlayStart was given unless 0), then the events, five a
// row, as a letter (TRACE_LETTERS) and its begin and end from the frame's
// start, in tenths of a millisecond; the last character of each row checks
// the rest. tools/trace.py video reads the pages back.
void tracePlayStart(int32_t tag = 0);
bool tracePlayFrame();
void tracePlayStop();

// The rings record afresh from here (what they hold is left out of the
// next playback).
void traceRestart();

// The master's profiler samples (debug.cpp), in the same low work RAM, or
// NULL without room for them: a count for each 64 bytes of the program's
// code, traceProfileCount of them.
#define TRACE_PROFILE_START 0x06004000  // where the BIOS loads the program
#define TRACE_PROFILE_SHIFT 6
extern const char gTextEnd[] asm("__text_end");    // saturn.ld (C names take a "_")
inline uint32_t traceProfileCount()
{
    return (uintptr_t(gTextEnd) - TRACE_PROFILE_START + (1 << TRACE_PROFILE_SHIFT) - 1) >> TRACE_PROFILE_SHIFT;
}
uint16_t* traceProfileBlocks();

// Room for the overlay's text there (the bench table's, the sync test's),
// or NULL without room: high work RAM has none to spare in HW_LOG builds.
#define TRACE_TEXT_BYTES 1616
char* traceText();

// Counts the times traceLevel has set that memory up afresh: the profile's
// blocks and the text are empty again after each.
uint32_t traceGeneration();

#else

inline void traceInit() {}
inline void traceSetMode(int32_t) {}
inline int32_t traceMode() { return 0; }
inline void traceLevel(const void*) {}
inline void traceStop() {}
inline void traceResume() {}
inline void traceAt(int32_t, uint32_t, uint32_t) {}
inline void traceBegin(int32_t, uint32_t, uint32_t = 0) {}
inline void traceEnd(int32_t, uint32_t, uint32_t = 0) {}
inline void traceBeginAt(int32_t, uint32_t, uint32_t, uint32_t = 0) {}
inline void traceEndAt(int32_t, uint32_t, uint32_t, uint32_t = 0) {}
inline void traceDetailBegin(uint32_t, uint32_t = 0) {}
inline void traceDetailEnd(uint32_t, uint32_t = 0) {}
inline void tracePlayStart(int32_t = 0) {}
inline bool tracePlayFrame() { return false; }
inline void tracePlayStop() {}
inline void traceRestart() {}

#endif

#endif
