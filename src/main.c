#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>
#include "cpu.h"
#include "memory.h"
#include "registers.h"
#include "gpu.h"
#include "timing.h"
#include "apu.h"

uint8_t joypadState = 0xFF;   /* low bit = pressed: 7 Start,6 Select,5 B,4 A,3 Down,2 Up,1 Left,0 Right */

int main(int argc, char **argv) {
    if (argc < 2 || !loadRom(argv[1])) { fprintf(stderr, "usage: gbemu rom.gb [scale 1-10]\n"); return 1; }
    int scale = argc > 2 ? atoi(argv[2]) : 4;
    if (scale < 1 || scale > 10) scale = 4;

    cpuInit();
    loadSave(argv[1]);
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    apuInit();
    SDL_Window *win = SDL_CreateWindow("gbemu", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 160 * scale, 144 * scale, 0);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, 0);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 160, 144);
    static uint32_t frame[160 * 144];

    Uint32 next = SDL_GetTicks(), fpsT = next;
    int paused = 0, fast = 0, fullscreen = 0, frames = 0;

    for (int running = 1; running;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_DROPFILE) {
                writeSave();
                if (loadRom(e.drop.file)) {
                    cpuInit(); timingReset(); gpuFrameStart(); loadSave(e.drop.file);
                } else fprintf(stderr, "Could not load %s\n", e.drop.file);
                SDL_free(e.drop.file);
            }
            if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                int down = e.type == SDL_KEYDOWN, bit = -1;
                switch (e.key.keysym.sym) {
                    case SDLK_RIGHT: bit = 0; break; case SDLK_LEFT: bit = 1; break;
                    case SDLK_UP: bit = 2; break;    case SDLK_DOWN: bit = 3; break;
                    case SDLK_z: bit = 4; break;     case SDLK_x: bit = 5; break;
                    case SDLK_RSHIFT: bit = 6; break; case SDLK_RETURN: bit = 7; break;
                    case SDLK_ESCAPE: running = 0; break;
                    case SDLK_TAB: fast = down; break;
                    case SDLK_r:
                        if (down && !e.key.repeat) { cpuInit(); memReset(); timingReset(); gpuFrameStart(); }
                        break;
                    case SDLK_p: if (down && !e.key.repeat) paused = !paused; break;
                    case SDLK_F11:
                        if (down && !e.key.repeat) {
                            fullscreen = !fullscreen;
                            SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        }
                        break;
                }
                if (bit >= 0) { if (down) joypadState &= ~(1 << bit); else joypadState |= (1 << bit); }
            }
        }

        if (!paused) while (!frameReady()) cpuStep();
        renderFrame(frame);
        SDL_UpdateTexture(tex, NULL, frame, 160 * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
        apuFlush();

        frames++;
        Uint32 now = SDL_GetTicks();
        if (now - fpsT >= 1000) {
            char title[64];
            snprintf(title, sizeof title, "gbemu - %d fps%s%s", frames, paused ? " [PAUSED]" : "", fast ? " [FAST]" : "");
            SDL_SetWindowTitle(win, title);
            frames = 0; fpsT = now;
        }

        if (fast || getenv("GBFAST")) next = now;
        else {
            next += 17;                               /* ~59.7 fps */
            if (next > now) SDL_Delay(next - now); else next = now;
        }
    }
    writeSave();
    apuShutdown();
    SDL_Quit();
    return 0;
}
