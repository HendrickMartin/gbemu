# gbemu: a Game Boy emulator in C

A Game Boy (DMG) emulator written in C11, following the
[Cinoop](https://cturt.github.io/cinoop.html) article. Windows build via MSYS2 + SDL2.

## Build

Install MSYS2, open the UCRT64 shell, then:

    pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-SDL2
    mingw32-make

## Run

    ./gbemu.exe game.gb [scale 1-10]

| Key | Action |
|---|---|
| Arrows | D-pad |
| Z / X | A / B |
| Enter / Right Shift | Start / Select |
| P | Pause |
| Hold Tab | Fast-forward |
| R | Reset |
| F11 | Fullscreen |
| Esc | Quit (writes battery save) |

Dropping a .gb file onto the window loads it.

## What is implemented

- Full SM83 CPU (all base and CB opcodes) driven by an instruction table, cycle counting
- PPU: background, window, sprites (10 per line limit), per-scanline rendering, STAT/LYC interrupts
- Timers (DIV/TIMA), VBlank/STAT/timer interrupts, HALT (including the HALT bug), OAM DMA
- Cartridge mappers MBC1, MBC3 (no real-time clock), MBC5, with battery saves (.sav)
- Sound: two square channels (with sweep), wave, noise, mixed to SDL audio

## Testing

Blargg's test ROMs (https://github.com/retrio/gb-test-roms):

- cpu_instrs: all 11 pass
- instr_timing: pass
- mem_timing, interrupt_time: fail (timing is per instruction, not per memory access)

Also played Super Mario Land (from a cartridge I own) with working graphics, input and sound.

## Not implemented

Game Boy Color, MBC3 clock, cycle-exact memory timing, serial link.

## Notes

ROMs and test ROMs are not included in this repository.
