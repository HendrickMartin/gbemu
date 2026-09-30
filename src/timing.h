#pragma once
void timingTick(int tcycles);
int  frameReady(void);   /* returns 1 once per frame, then clears */
void timingReset(void);
