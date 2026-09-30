#include "timing.h"
#include "memory.h"
#include "gpu.h"
#include "apu.h"

static int lineClock = 0, divClock = 0, timaClock = 0, frame = 0, frameClock = 0, statLine = 0;

static void updateStat(void) {
    uint8_t *stat = memPtr(0xFF41), *IF = memPtr(0xFF0F);
    uint8_t ly = *memPtr(0xFF44);
    int mode;
    if (ly >= 144) mode = 1;
    else if (lineClock < 80) mode = 2;
    else if (lineClock < 252) mode = 3;
    else mode = 0;
    int coin = (ly == *memPtr(0xFF45));
    *stat = (*stat & 0x78) | 0x80 | (coin << 2) | mode;
    int line = (coin && (*stat & 0x40)) || (mode == 0 && (*stat & 0x08))
            || (mode == 1 && (*stat & 0x10)) || (mode == 2 && (*stat & 0x20));
    if (line && !statLine) *IF |= 0x02;
    statLine = line;
}

void timingTick(int t) {
    apuTick(t);
    uint8_t *IF = memPtr(0xFF0F);

    divClock += t;
    while (divClock >= 256) { divClock -= 256; (*memPtr(0xFF04))++; }

    uint8_t tac = *memPtr(0xFF07);
    if (tac & 4) {
        static const int period[4] = { 1024, 16, 64, 256 };
        timaClock += t;
        while (timaClock >= period[tac & 3]) {
            timaClock -= period[tac & 3];
            uint8_t *tima = memPtr(0xFF05);
            if (++(*tima) == 0) { *tima = *memPtr(0xFF06); *IF |= 0x04; }
        }
    }

    if (!(*memPtr(0xFF40) & 0x80)) {
        lineClock = 0; statLine = 0; *memPtr(0xFF44) = 0;
        *memPtr(0xFF41) &= ~3;
        gpuFrameStart();
        frameClock += t;
        if (frameClock >= 70224) { frameClock -= 70224; frame = 1; }
        return;
    }
    frameClock = 0;

    uint8_t *ly = memPtr(0xFF44);
    int prev = lineClock;
    lineClock += t;
    if (*ly < 144 && prev < 252 && lineClock >= 252) renderLine(*ly);

    if (lineClock >= 456) {
        lineClock -= 456;
        *ly = (*ly + 1) % 154;
        if (*ly == 144) { *IF |= 0x01; frame = 1; }
        if (*ly == 0) gpuFrameStart();
    }
    updateStat();
}

int frameReady(void) { int f = frame; frame = 0; return f; }

void timingReset(void) { lineClock = divClock = timaClock = frame = frameClock = statLine = 0; }
