#pragma once
#include <stdint.h>
void renderLine(int y);            /* draw one scanline into the internal buffer */
void gpuFrameStart(void);          /* call when LY wraps to 0 */
void renderFrame(uint32_t *out);   /* convert buffer to 160*144 ARGB */
void dumpFrame(const char *path);
