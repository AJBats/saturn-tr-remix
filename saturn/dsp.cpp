// SCU DSP control (see dsp.h).

#include "dsp.h"

#define SCU_PPAF    (*(volatile uint32_t*)0x25FE0080)   // program control
#define SCU_PPD     (*(volatile uint32_t*)0x25FE0084)   // program RAM data
#define SCU_PDA     (*(volatile uint32_t*)0x25FE0088)   // data RAM address
#define SCU_PDD     (*(volatile uint32_t*)0x25FE008C)   // data RAM data

#define PPAF_EX     0x00010000  // executing
#define PPAF_LE     0x00008000  // load the program counter

void dspLoad(const uint32_t* program, int32_t count)
{
    SCU_PPAF = 0;
    SCU_PPAF = PPAF_LE;
    for (int32_t i = 0; i < count; i++) {
        SCU_PPD = program[i];
    }
}

void dspWrite(uint32_t bank, uint32_t index, const uint32_t* data, int32_t count)
{
    SCU_PDA = (bank << 6) | index;
    for (int32_t i = 0; i < count; i++) {
        SCU_PDD = data[i];
    }
}

void dspRead(uint32_t bank, uint32_t index, uint32_t* data, int32_t count)
{
    SCU_PDA = (bank << 6) | index;
    for (int32_t i = 0; i < count; i++) {
        data[i] = SCU_PDD;
    }
}

void dspStart(uint32_t pc)
{
    SCU_PPAF = PPAF_EX | PPAF_LE | pc;
}

bool dspBusy()
{
    return SCU_PPAF & PPAF_EX;
}
