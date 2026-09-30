#pragma once
#include <stdint.h>
struct instruction {
    const char *disassembly;
    uint8_t operandLength;
    void *execute;
};
void cpuInit(void);
void cpuStep(void);