#pragma once
#include <stdint.h>
int      loadRom(const char *path);
uint8_t  readByte(uint16_t addr);
uint16_t readWord(uint16_t addr);
void     writeByte(uint16_t addr, uint8_t val);
uint8_t *memPtr(uint16_t addr);
void     memReset(void);
void     loadSave(const char *romPath);
void     writeSave(void);
