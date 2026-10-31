// Slave SH-2 start-up and task hand-off (after libyaul's polling mode).

#include "slave.h"
#include <stddef.h>
#include "common.h"
#include "crash.h"
#include "system.h"

#define BIOS_SET_UINT       (*(void (**)(uint32_t, void (*)()))0x06000310)  // CPU interrupt vector
#define BIOS_SLAVE_ENTRY    (*(volatile uint32_t*)0x06000250)   // where the slave starts after SSHON
#define BIOS_SLAVE_STACK    (*(volatile uint32_t*)0x060002AC)   // its stack, if not 0

// The slave's blanking interrupts (the BIOS jumps to the entry with them
// enabled; the entry masks them at once)
#define VECTOR_SLAVE_HBLANK_IN  (0x100 + 0x41)
#define VECTOR_SLAVE_VBLANK_IN  (0x100 + 0x43)

#define SMPC_SSHON          0x02
#define SMPC_SSHOFF         0x03

#define CCR                 (*(volatile uint8_t*)0xFFFFFE92)
#define CCR_CE              0x01    // cache enable
#define CCR_CP              0x10    // purge all lines

static void (*volatile gTask)(void*);
static void* volatile gTaskArg;
static volatile bool gStarted;

extern "C" uint8_t gSlaveStack[4096] __attribute__((aligned(16)));
uint8_t gSlaveStack[4096];

extern "C" void slaveEntry();     // irq.s: masks interrupts, jumps to slaveMain
extern "C" void slaveIgnoreIrq(); // irq.s: rte

// Must run from the cache-through mirror: the cache is off part of the time.
static void __attribute__((noinline)) cachePurgeBody()
{
    uint8_t ccr = CCR & ~CCR_CE;
    CCR = ccr;
    CCR = ccr | CCR_CP;
    CCR = ccr | CCR_CE;
}

void cachePurge()
{
    uncached(cachePurgeBody)();
}

#define FRT_TCR     (*(volatile uint8_t*)0xFFFFFE16)

extern "C" CpuState gMasterState, gSlaveState;   // crt0.s and irq.s point GBR at them
CpuState gMasterState, gSlaveState;
static_assert(offsetof(CpuState, matrixPtr) == 36, "meshxform.s repeats the layout");

void cpuInit(bool slave)
{
    CpuState &s = slave ? gSlaveState : gMasterState;
    s.slave = slave;
    s.matrixPtr = s.matrixStack;
}

extern "C" void slaveMain()
{
    asm volatile("ldc %0, sr" : : "r"(0xF0)); // no interrupts on the slave
    cpuInit(true);
    cachePurge();
    FRT_TCR = 2; // its own free-running timer, at the master's rate (timerTicks)
    *uncached(&gStarted) = true;

    while (1)
    {
        void (*task)(void*) = *uncached(&gTask);
        if (!task)
        {
            busPause();
            continue;
        }

        cachePurge();
        task(*uncached(&gTaskArg));
        *uncached(&gTask) = nullptr;
    }
}

static void clockSync();

void slaveInit()
{
    smpcCommand(SMPC_SSHOFF);
    BIOS_SET_UINT(VECTOR_SLAVE_HBLANK_IN, slaveIgnoreIrq);
    BIOS_SET_UINT(VECTOR_SLAVE_VBLANK_IN, slaveIgnoreIrq);
    BIOS_SLAVE_ENTRY = uint32_t(slaveEntry);
    BIOS_SLAVE_STACK = uint32_t(gSlaveStack + sizeof(gSlaveStack));
    smpcCommand(SMPC_SSHON);

    uint32_t wait = 0;
    while (!*uncached(&gStarted) && wait < 0x100000) {
        wait++;
    }
    crashLog("SLAVE", *uncached(&gStarted) ? wait : 0xFFFFFFFF);
    if (*uncached(&gStarted)) {
        clockSync();
    }
}

void slaveRun(void (*task)(void*), void* arg)
{
    *uncached(&gTaskArg) = arg;
    *uncached(&gTask) = task;
}

static void plainTask(void* task)
{
    ((void (*)())task)();
}

void slaveRun(void (*task)())
{
    slaveRun(plainTask, (void*)task);
}

void (*slaveTask())(void*)
{
    return *uncached(&gTask);
}

void slaveWait()
{
    while (*uncached(&gTask)) {
        busPause();
    }
}

bool slaveDone()
{
    return !*uncached(&gTask);
}

// The slave's timer against the master's: the master pings, the slave
// answers with its tick; the answer of the shortest round trip read as
// taken halfway through it.
#define CLOCK_PINGS 64
static volatile uint32_t gPing, gPong, gPongTick;
static uint32_t gSlaveOffset;   // the slave's tick less the master's, 16 bits

static void clockPong(void*)
{
    for (uint32_t k = 1; k <= CLOCK_PINGS; k++)
    {
        while (*uncached(&gPing) != k);
        *uncached(&gPongTick) = ticks();
        *uncached(&gPong) = k;
    }
}

static void clockSync()
{
    slaveRun(clockPong, nullptr);
    uint32_t best = 0x10000;
    for (uint32_t k = 1; k <= CLOCK_PINGS; k++)
    {
        uint32_t t0 = ticks();
        *uncached(&gPing) = k;
        while (*uncached(&gPong) != k);
        uint32_t trip = (ticks() - t0) & 0xFFFF;
        if (trip < best)
        {
            best = trip;
            gSlaveOffset = (*uncached(&gPongTick) - t0 - trip / 2) & 0xFFFF;
        }
    }
    slaveWait();
}

uint32_t slaveTickOnMaster(uint32_t tick)
{
    return (tick - gSlaveOffset) & 0xFFFF;
}

uint32_t masterTickOnSlave(uint32_t tick)
{
    return (tick + gSlaveOffset) & 0xFFFF;
}
