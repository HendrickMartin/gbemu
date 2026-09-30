#pragma once
#include <stdint.h>
void    apuInit(void);
void    apuShutdown(void);
void    apuReset(void);
void    apuTick(int tcycles);
void    apuFlush(void);
uint8_t apuRead(uint16_t a);
void    apuWrite(uint16_t a, uint8_t v);
