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

static void (*volatile gTask)();
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
        void (*task)() = *uncached(&gTask);
        if (!task)
        {
            busPause();
            continue;
        }

        cachePurge();
        task();
        *uncached(&gTask) = nullptr;
    }
}

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
}

void slaveRun(void (*task)())
{
    *uncached(&gTask) = task;
}

void (*slaveTask())()
{
    return *uncached(&gTask);
}

void slaveWait()
{
    while (*uncached(&gTask)) {
        busPause();
    }
}
