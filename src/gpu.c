#include <stdio.h>
#include <stdint.h>
#include "gpu.h"
#include "memory.h"

static uint8_t px[144][160], bgc[144][160];
static int winLine = 0;
static const uint8_t shade[4] = { 255, 170, 85, 0 };

static int tilePixel(uint16_t tileAddr, int x, int y) {
    uint8_t lo = readByte(tileAddr + y * 2), hi = readByte(tileAddr + y * 2 + 1);
    int bit = 7 - x;
    return (((hi >> bit) & 1) << 1) | ((lo >> bit) & 1);
}

void gpuFrameStart(void) { winLine = 0; }

void renderLine(int y) {
    uint8_t lcdc = readByte(0xFF40), scy = readByte(0xFF42), scx = readByte(0xFF43);
    uint8_t bgp = readByte(0xFF47), wy = readByte(0xFF4A), wx = readByte(0xFF4B);
    int winUsed = 0;

    for (int x = 0; x < 160; x++) {
        int c = 0;
        if (lcdc & 0x01) {
            int useWin = (lcdc & 0x20) && y >= wy && x >= wx - 7;
            uint16_t map; int mx, my;
            if (useWin) { map = (lcdc & 0x40) ? 0x9C00 : 0x9800; mx = x - (wx - 7); my = winLine; winUsed = 1; }
            else { map = (lcdc & 0x08) ? 0x9C00 : 0x9800; mx = (x + scx) & 255; my = (y + scy) & 255; }
            uint8_t t = readByte(map + (my / 8) * 32 + mx / 8);
            uint16_t a = (lcdc & 0x10) ? (uint16_t)(0x8000 + t * 16) : (uint16_t)(0x9000 + (int8_t)t * 16);
            c = tilePixel(a, mx & 7, my & 7);
        }
        bgc[y][x] = c;
        px[y][x] = shade[(bgp >> (c * 2)) & 3];
    }
    if (winUsed) winLine++;

    if (lcdc & 0x02) {
        int h = (lcdc & 0x04) ? 16 : 8, n = 0, sel[10];
        for (int i = 0; i < 40 && n < 10; i++) {
            int sy = readByte(0xFE00 + i * 4) - 16;
            if (y >= sy && y < sy + h) sel[n++] = i;
        }
        for (int x = 0; x < 160; x++) {
            int bestX = 999, bestC = 0, bestFl = 0, bestPal = 0;
            for (int k = 0; k < n; k++) {
                uint16_t o = 0xFE00 + sel[k] * 4;
                int sy = readByte(o) - 16, sx = readByte(o + 1) - 8;
                if (x < sx || x >= sx + 8 || sx >= bestX) continue;
                uint8_t tile = readByte(o + 2), fl = readByte(o + 3);
                if (h == 16) tile &= 0xFE;
                int row = y - sy, col = x - sx;
                if (fl & 0x40) row = h - 1 - row;
                if (fl & 0x20) col = 7 - col;
                int c = tilePixel(0x8000 + tile * 16, col, row);
                if (c == 0) continue;
                bestX = sx; bestC = c; bestFl = fl; bestPal = readByte((fl & 0x10) ? 0xFF49 : 0xFF48);
            }
            if (bestC && !((bestFl & 0x80) && bgc[y][x]))
                px[y][x] = shade[(bestPal >> (bestC * 2)) & 3];
        }
    }
}

void renderFrame(uint32_t *out) {
    uint8_t lcdc = readByte(0xFF40);
    for (int y = 0; y < 144; y++)
        for (int x = 0; x < 160; x++) {
            uint8_t g = (lcdc & 0x80) ? px[y][x] : 255;
            out[y * 160 + x] = 0xFF000000u | (g << 16) | (g << 8) | g;
        }
}

static void put32(FILE *f, uint32_t v) { for (int i = 0; i < 4; i++) fputc((v >> (8 * i)) & 0xFF, f); }

void dumpFrame(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fputc('B', f); fputc('M', f);
    put32(f, 54 + 160 * 144 * 3); put32(f, 0); put32(f, 54);
    put32(f, 40); put32(f, 160); put32(f, 144);
    fputc(1, f); fputc(0, f); fputc(24, f); fputc(0, f);
    put32(f, 0); put32(f, 160 * 144 * 3); put32(f, 2835); put32(f, 2835); put32(f, 0); put32(f, 0);
    for (int y = 143; y >= 0; y--)
        for (int x = 0; x < 160; x++) { fputc(px[y][x], f); fputc(px[y][x], f); fputc(px[y][x], f); }
    fclose(f);
}
