#pragma once
#include <stdint.h>

struct registers {
    struct { union { struct { uint8_t f, a; }; uint16_t af; }; };
    struct { union { struct { uint8_t c, b; }; uint16_t bc; }; };
    struct { union { struct { uint8_t e, d; }; uint16_t de; }; };
    struct { union { struct { uint8_t l, h; }; uint16_t hl; }; };
    uint16_t sp, pc;
};
extern struct registers registers;