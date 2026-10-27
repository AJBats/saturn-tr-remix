// A timeline of every frame, in HW_LOG builds: both CPUs, the master's
// interrupts and the SCU DSP note when each stage of the frame begins and
// ends, a ring of events each, in low work RAM above the level (a header at
// 0x002FFF00 says where). The overlay's
// stages page sums them up on screen (debug.cpp); tools/trace.py turns a dump
// of the rings into a Chrome trace (chrome://tracing, ui.perfetto.dev) with
// VDP1 and the vblanks beside them.
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
    TR_VIEW,            // the camera's room, the rooms its portals show
    TR_QUEUE,           // the visible rooms queued for the slave
    TR_LARA,            // Lara drawn
    TR_OBJECTS,         // a room's sprites, static meshes and items (arg: room)
    TR_STATIC,          // a static mesh: its matrix, test and lighting
    TR_JOB_SETUP,       // a static mesh handed to the slave as a MeshJob
    TR_MESH,            // a static mesh drawn on the master
    TR_ITEM,            // an item drawn (arg: its type)
    TR_OVERLAY,         // the overlay's text drawn (osDrawDebug)
    TR_FLUSH,           // flush: the stages below, the rest command building
    TR_ROOMS_WAIT,      // waiting for the slave to finish rooms and meshes
    TR_FAR_WAIT,        // waiting for the slave's share of the commands
    TR_VDP1_WAIT,       // waiting for VDP1 to finish the last list
    TR_UPLOAD,          // textures, commands and gouraud table to VDP1
    // the slave
    TR_BATCH = 32,      // rooms and meshes as the master queues them
    TR_XFORM,           // a room's chunk of vertices (arg: its job)
    TR_FACES,           // a room's faces (arg: its job)
    TR_SLAVE_MESH,      // a static mesh's MeshJob (arg: its index)
    TR_SLAVE_FLUSH,     // its share of the commands
    // the SCU DSP, as the slave sees it start and finish
    TR_DSP_RUN = 40,    // a run of room chunks (arg: its job)
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

enum TraceLane { TRACE_MASTER, TRACE_IRQ, TRACE_SLAVE, TRACE_DSP, TRACE_LANES };

#ifdef HW_LOG

// After slaveInit: measures the slave's counter against the master's.
void traceInit();

// The level's data ends at end in low work RAM: the rings record if they fit
// above it. traceStop stops them before a level loads over them.
void traceLevel(const void* end);
void traceStop();

// The overlay's Trace option: no events (each costs a Saturn a timer read,
// about 110 cycles, and a few stores), the stages, or the stages and the
// master's detail spans (TR_OBJECTS to TR_ITEM, some 130 more a frame).
enum TraceMode { TRACE_OFF, TRACE_STAGES, TRACE_DETAIL, TRACE_MODES };
void traceSetMode(int32_t mode);
int32_t traceMode();

extern bool gTraceOn;       // the rings record
extern bool gTraceDetail;   // and the detail spans too

// An event at ticks on lane, from the CPU (and context) that owns the lane.
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

// The stages page: once a frame on the master, adds up the events written
// since (or, with sum false, only skips them, so that other pages don't pay
// for it); every TRACE_WINDOW vblanks, each id's own time (less the spans
// inside it) and each lane's time in spans become percentages of the time: a
// lane's add up to its busy time.
#define TRACE_WINDOW    60
struct TraceStages
{
    uint16_t self[TR_FIRST_MOMENT];     // percent of the time
    uint16_t busy[TRACE_LANES];         // time in spans, likewise
    uint16_t frames, vblanks;           // in the window
    uint16_t events;                    // a frame, all lanes
    uint16_t window;                    // counts the windows
    bool valid;
};
void traceTally(bool sum);
const TraceStages* traceStages();   // NULL while the rings are off

// The master's profiler samples (debug.cpp), in the same low work RAM, or
// NULL without room for them.
#define TRACE_PROFILE_BLOCKS 2432
uint16_t* traceProfileBlocks();

// Room for the stages page's text there, while traceStages() isn't NULL:
// high work RAM has none to spare in HW_LOG builds.
#define TRACE_TEXT_BYTES 320
char* traceText();

#else

inline void traceInit() {}
inline void traceSetMode(int32_t) {}
inline int32_t traceMode() { return 0; }
inline void traceLevel(const void*) {}
inline void traceStop() {}
inline void traceAt(int32_t, uint32_t, uint32_t) {}
inline void traceBegin(int32_t, uint32_t, uint32_t = 0) {}
inline void traceEnd(int32_t, uint32_t, uint32_t = 0) {}
inline void traceBeginAt(int32_t, uint32_t, uint32_t, uint32_t = 0) {}
inline void traceEndAt(int32_t, uint32_t, uint32_t, uint32_t = 0) {}
inline void traceDetailBegin(uint32_t, uint32_t = 0) {}
inline void traceDetailEnd(uint32_t, uint32_t = 0) {}

#endif

#endif
