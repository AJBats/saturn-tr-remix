// What the two SH-2s can use to share work, tested at boot in HW_LOG builds
// made with SYNC_TEST=1 (see debug.h): whether TAS.B is atomic between them, whether a Peterson
// lock on uncached flags is, whether one CPU's stores reach the other in the
// order made, and whether GBR stays each CPU's own (per-CPU variables through
// __thread). Each test runs both CPUs at once, flat out, ROUNDS times.

#include "debug.h"
#include "common.h"
#include "render.h"
#include "slave.h"
#include "system.h"

#include <string.h>

// Saturn, 2026-10-27 (71e9556): adds lost raw 19963, TAS 0, Peterson 0; claims
// 0 errors (2320/1776); order 0; GBR 0, kept. Cycles: uncached access 8, add
// under TAS alone 36, contended TAS 66, Peterson 178.
#if defined(HW_LOG) && defined(SYNC_TEST)

#define ROUNDS      20000
#define TASKS       4096        // the claim test's
#define RING        64          // the order test's messages in flight
#define GBR_MASTER  0x4D415354
#define GBR_SLAVE   0x534C4156

SyncResults gSync;

static volatile uint32_t gGo, gReady;
static void (*gSlavePart)();
static volatile uint32_t gCounter;
static volatile uint8_t gLock;
static volatile uint32_t gFlag[2], gTurn;
static volatile uint32_t gNext;
static volatile uint32_t gHead, gTail;      // the order test's ring
static volatile uint32_t gGbrErrors[2];
static uint32_t gOrderErrors;

// the order test's ring and the claim test's marks, in the frame's command
// buffer (renderScratch): unused before a level
struct Message { uint32_t seq, a, b, c; };   // a cache line
static Message* ring() { return (Message*)((uintptr_t(renderScratch(0)) + 15) & ~15); }
static uint8_t* claimed(int32_t cpu) { return (uint8_t*)renderScratch(0) + sizeof(Message) * (RING + 1) + cpu * TASKS; }

static void tasLock()
{
    while (!tasClaim(uncached(&gLock)));
}

static void tasUnlock()
{
    asm volatile("" : : : "memory");
    *uncached(&gLock) = 0;
}

static void petersonLock(int32_t me)
{
    volatile uint32_t* flag = uncached(gFlag);
    volatile uint32_t* turn = uncached(&gTurn);
    flag[me] = 1;
    *turn = 1 - me;
    while (flag[1 - me] && *turn == uint32_t(1 - me));
}

static void petersonUnlock(int32_t me)
{
    uncached(gFlag)[me] = 0;
}

static void syncTask()
{
    *uncached(&gReady) = 1;
    while (!*uncached(&gGo));
    gSlavePart();
}

// Both parts at once; the master's time, in cycles a round.
static uint32_t both(void (*master)(), void (*slave)())
{
    gSlavePart = slave;
    *uncached(&gReady) = 0;
    *uncached(&gGo) = 0;
    slaveRun(syncTask);
    while (!*uncached(&gReady));
    uint32_t start = timerTicks();
    *uncached(&gGo) = 1;
    master();
    uint32_t ticks = (timerTicks() - start) & 0xFFFF;
    slaveWait();
    return ticks * 128 / ROUNDS;
}

static void raw()
{
    volatile uint32_t* c = uncached(&gCounter);
    for (int32_t i = 0; i < ROUNDS; i++) {
        *c = *c + 1;
    }
}

static void tas()
{
    volatile uint32_t* c = uncached(&gCounter);
    for (int32_t i = 0; i < ROUNDS; i++)
    {
        tasLock();
        *c = *c + 1;
        tasUnlock();
    }
}

// Each test that tells the CPUs apart takes me, 0 on the master, 1 on the slave.
static void __attribute__((noinline)) peterson(int32_t me)
{
    volatile uint32_t* c = uncached(&gCounter);
    for (int32_t i = 0; i < ROUNDS; i++)
    {
        petersonLock(me);
        *c = *c + 1;
        petersonUnlock(me);
    }
}

// Tasks taken one at a time under the TAS lock, each marked by its taker.
static void __attribute__((noinline)) claim(int32_t me)
{
    volatile uint8_t* mine = uncached(claimed(me));
    volatile uint32_t* next = uncached(&gNext);
    while (1)
    {
        tasLock();
        uint32_t i = *next;
        *next = i + 1;
        tasUnlock();
        if (i >= TASKS)
            break;
        mine[i] = 1;
    }
}

// The slave writes each message (through its cache, as the renderer does),
// then the head; the master reads the head, then the message, cached after
// purging its line, as rooms.cpp reads what the DSP wrote.
static void produce()
{
    Message* r = ring();
    volatile uint32_t* head = uncached(&gHead);
    volatile uint32_t* tail = uncached(&gTail);
    for (uint32_t seq = 1; seq <= ROUNDS; seq++)
    {
        while (seq - *tail > RING);
        Message &m = r[seq % RING];
        m.seq = seq;
        m.a = seq ^ 0x5A5A5A5A;
        m.b = ~seq;
        m.c = seq * 3;
        *head = seq;
    }
}

static void consume()
{
    Message* r = ring();
    volatile uint32_t* head = uncached(&gHead);
    volatile uint32_t* tail = uncached(&gTail);
    uint32_t errors = 0;
    for (uint32_t seq = 1; seq <= ROUNDS; seq++)
    {
        while (*head < seq);
        const volatile Message* v = r + seq % RING;
        *(volatile uint32_t*)(uintptr_t(v) | 0x40000000) = 0;   // purge the line
        errors += v->seq != seq || v->a != (seq ^ 0x5A5A5A5A) || v->b != ~seq || v->c != seq * 3;
        *tail = seq;
    }
    gOrderErrors = errors;
}

static void __attribute__((noinline)) gbr(int32_t me)
{
    uint32_t value = me ? GBR_SLAVE : GBR_MASTER;
    uint32_t saved;
    asm volatile("stc gbr, %0" : "=r"(saved));
    asm volatile("ldc %0, gbr" : : "r"(value));
    uint32_t errors = 0;
    for (int32_t i = 0; i < ROUNDS * 4; i++)
    {
        uint32_t g;
        asm volatile("stc gbr, %0" : "=r"(g));
        errors += g != value;
    }
    asm volatile("ldc %0, gbr" : : "r"(saved));
    uncached(gGbrErrors)[me] = errors;
}

static void petersonMaster() { peterson(0); }
static void petersonSlave() { peterson(1); }
static void claimMaster() { claim(0); }
static void claimSlave() { claim(1); }
static void gbrMaster() { gbr(0); }
static void gbrSlave() { gbr(1); }
static void none() {}

// Both add to the counter: the adds lost, and the master's cycles a round.
static uint32_t counted(void (*master)(), void (*slave)(), uint32_t &cycles)
{
    *uncached(&gCounter) = 0;
    cycles = both(master, slave);
    return (slave == none ? ROUNDS : 2 * ROUNDS) - *uncached(&gCounter);
}

void debugSyncTest()
{
    // a CPU alone: an uncached access, an add under the TAS lock
    uint32_t cycles;
    counted(raw, none, cycles);
    gSync.uncachedCycles = cycles / 2;
    counted(tas, none, gSync.lockCycles);

    // without a lock some adds must be lost, or the test can't see a race
    gSync.raw = counted(raw, raw, cycles);
    gSync.tas = counted(tas, tas, gSync.tasCycles);
    gSync.peterson = counted(petersonMaster, petersonSlave, gSync.petersonCycles);

    memset(claimed(0), 0, 2 * TASKS);
    *uncached(&gNext) = 0;
    both(claimMaster, claimSlave);
    cachePurge();
    for (int32_t i = 0; i < TASKS; i++)
    {
        gSync.claimMaster += claimed(0)[i];
        gSync.claimSlave += claimed(1)[i];
        gSync.claim += claimed(0)[i] + claimed(1)[i] != 1;
    }

    both(consume, produce);
    gSync.order = gOrderErrors;

    both(gbrMaster, gbrSlave);
    gSync.gbr = uncached(gGbrErrors)[0] + uncached(gGbrErrors)[1];
}

bool debugSyncKept()
{
    extern CpuState gMasterState;
    CpuState* g;
    asm volatile("stc gbr, %0" : "=r"(g));
    return g == &gMasterState;
}

#endif
