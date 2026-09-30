CC = gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wno-misleading-indentation -g
SDL = $(shell sdl2-config --cflags --libs)
.RECIPEPREFIX = >

gbemu.exe: $(wildcard src/*.c) $(wildcard src/*.h)
> $(CC) $(CFLAGS) src/*.c -o $@ $(SDL)

run: gbemu.exe
> ./gbemu.exe $(ROM)

clean:
> rm -f gbemu.exe
