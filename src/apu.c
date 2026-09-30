#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "apu.h"
#include "memory.h"

typedef struct {
    int on, lenCtr, lenEn, vol, envDir, envPeriod, envTimer, envInit;
    int duty, pos, timer, freq;
    int r, s, width, lfsr;
} Ch;

static Ch ch[3];   /* 0 = square 1, 1 = square 2, 2 = noise */
static struct { int on, lenCtr, lenEn, vol, freq, timer, pos, sample; } wv;
static int swPeriod, swDir, swShift, swTimer, swOn, swShadow;
static int seqStep, seqClock;
static long long acc;
static float capL, capR;
static SDL_AudioDeviceID dev;
static int started;
#define BUFSZ 4096
static int16_t buf[BUFSZ * 2];
static int count;

static const uint8_t duty[4][8] = {
    {0,0,0,0,0,0,0,1}, {1,0,0,0,0,0,0,1}, {1,0,0,0,0,1,1,1}, {0,1,1,1,1,1,1,0}
};
static const int wshift[4] = { 4, 0, 1, 2 };
static const int noiseDiv[8] = { 8, 16, 32, 48, 64, 80, 96, 112 };
static const uint8_t regMask[32] = {
    0x80,0x3F,0x00,0xFF,0xBF, 0xFF,0x3F,0x00,0xFF,0xBF, 0x7F,0xFF,0x9F,0xFF,0xBF, 0xFF,
    0xFF,0x00,0x00,0xBF, 0x00,0x00,0x70,0xFF, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
};

void apuInit(void) {
    SDL_AudioSpec want;
    memset(&want, 0, sizeof want);
    want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (!dev) fprintf(stderr, "Audio disabled: %s\n", SDL_GetError());
}

void apuShutdown(void) { if (dev) SDL_CloseAudioDevice(dev); dev = 0; }

void apuReset(void) {
    memset(ch, 0, sizeof ch); memset(&wv, 0, sizeof wv);
    swPeriod = swDir = swShift = swTimer = swOn = swShadow = 0;
    seqStep = seqClock = 0; acc = 0; count = 0; capL = capR = 0;
    *memPtr(0xFF10) = 0x80; *memPtr(0xFF11) = 0xBF; *memPtr(0xFF12) = 0xF3; *memPtr(0xFF14) = 0xBF;
    *memPtr(0xFF16) = 0x3F; *memPtr(0xFF19) = 0xBF; *memPtr(0xFF1A) = 0x7F; *memPtr(0xFF1C) = 0x9F;
    *memPtr(0xFF1E) = 0xBF; *memPtr(0xFF20) = 0xFF; *memPtr(0xFF23) = 0xBF;
    *memPtr(0xFF24) = 0x77; *memPtr(0xFF25) = 0xF3; *memPtr(0xFF26) = 0xF1;
}

static int swCalc(void) {
    int d = swShadow >> swShift;
    return swDir ? swShadow - d : swShadow + d;
}

static void setEnv(Ch *c, uint8_t v) {
    c->envInit = v >> 4; c->envDir = v & 8; c->envPeriod = v & 7;
    if (!(v & 0xF8)) c->on = 0;              /* DAC off */
}

static void trigSq(int i) {
    Ch *c = &ch[i];
    if (*memPtr(i ? 0xFF17 : 0xFF12) & 0xF8) c->on = 1;
    if (!c->lenCtr) c->lenCtr = 64;
    c->timer = (2048 - c->freq) * 4;
    c->envTimer = c->envPeriod ? c->envPeriod : 8;
    c->vol = c->envInit;
    if (i == 0) {
        swShadow = c->freq; swTimer = swPeriod ? swPeriod : 8; swOn = swPeriod || swShift;
        if (swShift && swCalc() > 2047) c->on = 0;
    }
}

static void trigWave(void) {
    if (*memPtr(0xFF1A) & 0x80) wv.on = 1;
    if (!wv.lenCtr) wv.lenCtr = 256;
    wv.timer = (2048 - wv.freq) * 2; wv.pos = 0;
}

static void trigNoise(void) {
    Ch *c = &ch[2];
    if (*memPtr(0xFF21) & 0xF8) c->on = 1;
    if (!c->lenCtr) c->lenCtr = 64;
    c->timer = noiseDiv[c->r] << c->s;
    c->envTimer = c->envPeriod ? c->envPeriod : 8;
    c->vol = c->envInit; c->lfsr = 0x7FFF;
}

static void sqWrite(int i, int reg, uint8_t v) {
    Ch *c = &ch[i];
    switch (reg) {
        case 1: c->duty = v >> 6; c->lenCtr = 64 - (v & 63); break;
        case 2: setEnv(c, v); break;
        case 3: c->freq = (c->freq & 0x700) | v; break;
        case 4: c->freq = (c->freq & 0xFF) | ((v & 7) << 8); c->lenEn = (v & 0x40) != 0;
                if (v & 0x80) trigSq(i); break;
    }
}

void apuWrite(uint16_t a, uint8_t v) {
    uint8_t *m = memPtr(a);
    if (a >= 0xFF30) { *m = v; return; }                      /* wave RAM */
    if (a == 0xFF26) {
        int was = *m & 0x80;
        *m = v & 0x80;
        if (was && !(v & 0x80)) {
            for (int r = 0xFF10; r <= 0xFF25; r++) *memPtr(r) = 0;
            memset(ch, 0, sizeof ch); memset(&wv, 0, sizeof wv);
            swPeriod = swDir = swShift = swOn = 0;
        }
        if (!was && (v & 0x80)) seqStep = 0;
        return;
    }
    if (!(*memPtr(0xFF26) & 0x80)) return;                    /* powered off */
    *m = v;
    if (a >= 0xFF11 && a <= 0xFF14) { sqWrite(0, a - 0xFF10, v); return; }
    if (a >= 0xFF16 && a <= 0xFF19) { sqWrite(1, a - 0xFF15, v); return; }
    switch (a) {
        case 0xFF10: swPeriod = (v >> 4) & 7; swDir = v & 8; swShift = v & 7; break;
        case 0xFF1A: if (!(v & 0x80)) wv.on = 0; break;
        case 0xFF1B: wv.lenCtr = 256 - v; break;
        case 0xFF1C: wv.vol = (v >> 5) & 3; break;
        case 0xFF1D: wv.freq = (wv.freq & 0x700) | v; break;
        case 0xFF1E: wv.freq = (wv.freq & 0xFF) | ((v & 7) << 8); wv.lenEn = (v & 0x40) != 0;
                     if (v & 0x80) trigWave(); break;
        case 0xFF20: ch[2].lenCtr = 64 - (v & 63); break;
        case 0xFF21: setEnv(&ch[2], v); break;
        case 0xFF22: ch[2].r = v & 7; ch[2].s = v >> 4; ch[2].width = v & 8; break;
        case 0xFF23: ch[2].lenEn = (v & 0x40) != 0; if (v & 0x80) trigNoise(); break;
    }
}

uint8_t apuRead(uint16_t a) {
    if (a >= 0xFF30) return *memPtr(a);
    if (a == 0xFF26)
        return (*memPtr(a) & 0x80) | 0x70 | ch[0].on | (ch[1].on << 1) | (wv.on << 2) | (ch[2].on << 3);
    return *memPtr(a) | regMask[a - 0xFF10];
}

static void lenStep(void) {
    for (int i = 0; i < 3; i++)
        if (ch[i].lenEn && ch[i].lenCtr > 0 && --ch[i].lenCtr == 0) ch[i].on = 0;
    if (wv.lenEn && wv.lenCtr > 0 && --wv.lenCtr == 0) wv.on = 0;
}

static void envStep(void) {
    for (int i = 0; i < 3; i++) {
        Ch *c = &ch[i];
        if (!c->on || !c->envPeriod) continue;
        if (--c->envTimer <= 0) {
            c->envTimer = c->envPeriod;
            if (c->envDir) { if (c->vol < 15) c->vol++; }
            else if (c->vol > 0) c->vol--;
        }
    }
}

static void sweepStep(void) {
    if (swTimer > 0) swTimer--;
    if (swTimer == 0) {
        swTimer = swPeriod ? swPeriod : 8;
        if (swOn && swPeriod) {
            int n = swCalc();
            if (n > 2047) ch[0].on = 0;
            else if (swShift) {
                swShadow = ch[0].freq = n;
                *memPtr(0xFF13) = n & 0xFF;
                *memPtr(0xFF14) = (*memPtr(0xFF14) & 0xF8) | (n >> 8);
                if (swCalc() > 2047) ch[0].on = 0;
            }
        }
    }
}

static void emit(void) {
    int o[4];
    o[0] = (ch[0].on && duty[ch[0].duty][ch[0].pos]) ? ch[0].vol : 0;
    o[1] = (ch[1].on && duty[ch[1].duty][ch[1].pos]) ? ch[1].vol : 0;
    o[2] = wv.on ? (wv.sample >> wshift[wv.vol]) : 0;
    o[3] = (ch[2].on && !(ch[2].lfsr & 1)) ? ch[2].vol : 0;

    uint8_t n50 = *memPtr(0xFF24), n51 = *memPtr(0xFF25);
    int L = 0, R = 0;
    for (int i = 0; i < 4; i++) {
        if (n51 & (0x10 << i)) L += o[i];
        if (n51 & (1 << i))    R += o[i];
    }
    L *= ((n50 >> 4) & 7) + 1;
    R *= (n50 & 7) + 1;

    float l = L * 30.0f, r = R * 30.0f;              /* high-pass removes the DC offset */
    float ol = l - capL, orr = r - capR;
    capL = l - ol * 0.996f; capR = r - orr * 0.996f;
    if (count < BUFSZ) { buf[count * 2] = (int16_t)ol; buf[count * 2 + 1] = (int16_t)orr; count++; }
}

void apuTick(int t) {
    if (*memPtr(0xFF26) & 0x80) {
        seqClock += t;
        while (seqClock >= 8192) {                     /* 512 Hz frame sequencer */
            seqClock -= 8192;
            if (!(seqStep & 1)) lenStep();
            if (seqStep == 2 || seqStep == 6) sweepStep();
            if (seqStep == 7) envStep();
            seqStep = (seqStep + 1) & 7;
        }
        for (int i = 0; i < 2; i++) {
            Ch *c = &ch[i];
            if (!c->on) continue;
            c->timer -= t;
            while (c->timer <= 0) { c->timer += (2048 - c->freq) * 4; c->pos = (c->pos + 1) & 7; }
        }
        if (wv.on) {
            wv.timer -= t;
            while (wv.timer <= 0) {
                wv.timer += (2048 - wv.freq) * 2;
                wv.pos = (wv.pos + 1) & 31;
                uint8_t b = *memPtr(0xFF30 + wv.pos / 2);
                wv.sample = (wv.pos & 1) ? (b & 0x0F) : (b >> 4);
            }
        }
        if (ch[2].on) {
            ch[2].timer -= t;
            while (ch[2].timer <= 0) {
                ch[2].timer += noiseDiv[ch[2].r] << ch[2].s;
                int x = (ch[2].lfsr & 1) ^ ((ch[2].lfsr >> 1) & 1);
                ch[2].lfsr = (ch[2].lfsr >> 1) | (x << 14);
                if (ch[2].width) ch[2].lfsr = (ch[2].lfsr & ~0x40) | (x << 6);
            }
        }
    }
    acc += (long long)t * 44100;
    while (acc >= 4194304) { acc -= 4194304; emit(); }
}

void apuFlush(void) {
    if (dev && count) {
        if (SDL_GetQueuedAudioSize(dev) < 44100 * 4 / 3) SDL_QueueAudio(dev, buf, count * 4);
        if (!started && SDL_GetQueuedAudioSize(dev) >= 4096 * 4) { SDL_PauseAudioDevice(dev, 0); started = 1; }
    }
    count = 0;
}
