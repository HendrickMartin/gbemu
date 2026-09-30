#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory.h"
#include "apu.h"

static uint8_t mem[0x10000];
static uint8_t eram[0x20000];
static uint8_t *rom = NULL;
static size_t romSize = 0;
static int mbc = 0, ramEn = 0, romLo = 1, romHi = 0, ramSel = 0, mode = 0;
static char savePath[512];
static int hasBattery = 0;
extern uint8_t joypadState;

void memReset(void) {                       /* keeps ROM and cartridge RAM */
    memset(mem, 0, sizeof mem);
    ramEn = 0; romLo = 1; romHi = 0; ramSel = 0; mode = 0;
    mem[0xFF40] = 0x91; mem[0xFF47] = 0xFC;
    mem[0xFF48] = 0xFF; mem[0xFF49] = 0xFF;
    mem[0xFF0F] = 0xE1; mem[0xFF26] = 0xF1;
    apuReset();
}

int loadRom(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0x150) { fclose(f); return 0; }
    uint8_t *buf = malloc(n);
    size_t got = fread(buf, 1, n, f);
    fclose(f);
    if (got < 0x150) { free(buf); return 0; }

    free(rom);                               /* old ROM only released once the new one is good */
    rom = buf; romSize = got;
    memset(eram, 0, sizeof eram);

    uint8_t t = rom[0x147];
    mbc = 0;
    if (t >= 0x01 && t <= 0x03) mbc = 1;
    else if (t >= 0x0F && t <= 0x13) mbc = 3;
    else if (t >= 0x19 && t <= 0x1E) mbc = 5;
    else if (t != 0) fprintf(stderr, "Warning: unsupported cartridge type 0x%02X\n", t);

    fprintf(stderr, "ROM: %.15s | type 0x%02X (MBC%d) | %zu KB\n",
            (const char *)&rom[0x134], t, mbc, romSize / 1024);
    memReset();
    return 1;
}

uint8_t *memPtr(uint16_t a) { return &mem[a]; }

static size_t romBank(void) {
    if (mbc == 1) return (size_t)((romHi << 5) | romLo);
    if (mbc == 5) return (size_t)((romHi << 8) | romLo);
    return (size_t)romLo;
}
static int ramIndex(uint16_t a) {
    int bank = 0;
    if (mbc == 1) bank = mode ? ramSel : 0;
    else if (mbc == 3) { if (ramSel > 3) return -1; bank = ramSel; }
    else bank = ramSel & 0x0F;
    return ((bank * 0x2000) + (a - 0xA000)) & 0x1FFFF;
}

uint8_t readByte(uint16_t a) {
    if (a < 0x4000) return rom[a % romSize];
    if (a < 0x8000) return rom[(romBank() * 0x4000 + (a - 0x4000)) % romSize];
    if (a >= 0xA000 && a < 0xC000 && mbc) {
        if (!ramEn) return 0xFF;
        int i = ramIndex(a);
        return i < 0 ? 0xFF : eram[i];
    }
    if (a >= 0xFF10 && a <= 0xFF3F) return apuRead(a);
    if (a == 0xFF00) {
        uint8_t sel = mem[0xFF00], out = 0xCF | (sel & 0x30);
        if (!(sel & 0x20)) out &= 0xF0 | (joypadState >> 4);
        if (!(sel & 0x10)) out &= 0xF0 | (joypadState & 0x0F);
        return out;
    }
    if (a >= 0xE000 && a < 0xFE00) return mem[a - 0x2000];
    return mem[a];
}
uint16_t readWord(uint16_t a) { return readByte(a) | (readByte(a + 1) << 8); }

void writeByte(uint16_t a, uint8_t v) {
    if (a < 0x8000) {
        if (mbc == 0) return;
        if (a < 0x2000) ramEn = (v & 0x0F) == 0x0A;
        else if (a < 0x4000) {
            if (mbc == 1)      { romLo = v & 0x1F; if (!romLo) romLo = 1; }
            else if (mbc == 3) { romLo = v & 0x7F; if (!romLo) romLo = 1; }
            else               { if (a < 0x3000) romLo = v; else romHi = v & 1; }
        }
        else if (a < 0x6000) {
            if (mbc == 1) { romHi = v & 3; ramSel = v & 3; }
            else ramSel = v & 0x0F;
        }
        else if (mbc == 1) mode = v & 1;
        return;
    }
    if (a >= 0xA000 && a < 0xC000 && mbc) {
        if (ramEn) { int i = ramIndex(a); if (i >= 0) eram[i] = v; }
        return;
    }
    if (a >= 0xFF10 && a <= 0xFF3F) { apuWrite(a, v); return; }
    if (a == 0xFF04 || a == 0xFF44) { mem[a] = 0; return; }
    if (a == 0xFF46) {
        for (int i = 0; i < 160; i++) mem[0xFE00 + i] = readByte((uint16_t)((v << 8) + i));
        mem[a] = v;
        return;
    }
    if (a >= 0xE000 && a < 0xFE00) a -= 0x2000;
    if (a == 0xFF02 && v == 0x81) { putchar(mem[0xFF01]); fflush(stdout); }
    mem[a] = v;
}

void loadSave(const char *romPath) {
    uint8_t t = rom[0x147];
    hasBattery = (t == 0x03 || t == 0x09 || t == 0x0F || t == 0x10 ||
                  t == 0x13 || t == 0x1B || t == 0x1E);
    if (!hasBattery) return;
    snprintf(savePath, sizeof savePath, "%s.sav", romPath);
    FILE *f = fopen(savePath, "rb");
    if (f) { (void)fread(eram, 1, sizeof eram, f); fclose(f); fprintf(stderr, "Loaded %s\n", savePath); }
}

void writeSave(void) {
    if (!hasBattery) return;
    FILE *f = fopen(savePath, "wb");
    if (f) { fwrite(eram, 1, sizeof eram, f); fclose(f); fprintf(stderr, "Saved %s\n", savePath); }
}
