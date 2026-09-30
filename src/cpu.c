#include <stdio.h>
#include <stdlib.h>
#include "cpu.h"
#include "registers.h"
#include "memory.h"
#include "timing.h"

#define FLAG_Z 0x80
#define FLAG_N 0x40
#define FLAG_H 0x20
#define FLAG_C 0x10

struct registers registers;
static int ime = 0;
static int halted = 0;
static int haltBug = 0;

static void push(uint16_t v) {
    registers.sp -= 2;
    writeByte(registers.sp, v & 0xFF);
    writeByte(registers.sp + 1, v >> 8);
}
static uint16_t pop(void) {
    uint16_t v = readWord(registers.sp);
    registers.sp += 2;
    return v;
}

/* register index: 0=B 1=C 2=D 3=E 4=H 5=L 6=(HL) 7=A */
static uint8_t getR(int i) {
    switch (i) {
        case 0: return registers.b; case 1: return registers.c;
        case 2: return registers.d; case 3: return registers.e;
        case 4: return registers.h; case 5: return registers.l;
        case 6: return readByte(registers.hl);
        default: return registers.a;
    }
}
static void setR(int i, uint8_t v) {
    switch (i) {
        case 0: registers.b = v; break; case 1: registers.c = v; break;
        case 2: registers.d = v; break; case 3: registers.e = v; break;
        case 4: registers.h = v; break; case 5: registers.l = v; break;
        case 6: writeByte(registers.hl, v); break;
        default: registers.a = v; break;
    }
}
/* 16-bit index: 0=BC 1=DE 2=HL 3=SP */
static uint16_t getRR(int i) {
    switch (i) { case 0: return registers.bc; case 1: return registers.de;
                 case 2: return registers.hl; default: return registers.sp; }
}
static void setRR(int i, uint16_t v) {
    switch (i) { case 0: registers.bc = v; break; case 1: registers.de = v; break;
                 case 2: registers.hl = v; break; default: registers.sp = v; break; }
}

/* 0=ADD 1=ADC 2=SUB 3=SBC 4=AND 5=XOR 6=OR 7=CP */
static void alu(int k, uint8_t v) {
    uint8_t a = registers.a;
    int c = (registers.f & FLAG_C) ? 1 : 0;
    int r;
    switch (k) {
    case 0: case 1: {
        int cy = (k == 1) ? c : 0;
        r = a + v + cy;
        registers.f = ((r & 0xFF) == 0 ? FLAG_Z : 0)
                    | (((a & 0xF) + (v & 0xF) + cy) > 0xF ? FLAG_H : 0)
                    | (r > 0xFF ? FLAG_C : 0);
        registers.a = (uint8_t)r;
        break; }
    case 2: case 3: case 7: {
        int cy = (k == 3) ? c : 0;
        r = a - v - cy;
        registers.f = FLAG_N
                    | ((r & 0xFF) == 0 ? FLAG_Z : 0)
                    | (((a & 0xF) - (v & 0xF) - cy) < 0 ? FLAG_H : 0)
                    | (r < 0 ? FLAG_C : 0);
        if (k != 7) registers.a = (uint8_t)r;
        break; }
    case 4: registers.a = a & v; registers.f = FLAG_H | (registers.a ? 0 : FLAG_Z); break;
    case 5: registers.a = a ^ v; registers.f = registers.a ? 0 : FLAG_Z; break;
    case 6: registers.a = a | v; registers.f = registers.a ? 0 : FLAG_Z; break;
    }
}

static uint8_t inc8(uint8_t v) {
    uint8_t r = v + 1;
    registers.f = (registers.f & FLAG_C) | (r == 0 ? FLAG_Z : 0) | ((v & 0xF) == 0xF ? FLAG_H : 0);
    return r;
}
static uint8_t dec8(uint8_t v) {
    uint8_t r = v - 1;
    registers.f = (registers.f & FLAG_C) | FLAG_N | (r == 0 ? FLAG_Z : 0) | ((v & 0xF) == 0 ? FLAG_H : 0);
    return r;
}

/* Handles all opcodes that follow a bit pattern. Returns 1 if handled. */
static int decodeRegular(uint8_t op) {
    if (op >= 0x40 && op <= 0x7F && op != 0x76) { setR((op >> 3) & 7, getR(op & 7)); return 1; }
    if (op >= 0x80 && op <= 0xBF) { alu((op >> 3) & 7, getR(op & 7)); return 1; }
    if (op < 0x40 && (op & 7) == 4) { int r = (op >> 3) & 7; setR(r, inc8(getR(r))); return 1; }
    if (op < 0x40 && (op & 7) == 5) { int r = (op >> 3) & 7; setR(r, dec8(getR(r))); return 1; }
    if ((op & 0xCF) == 0x03) { int r = (op >> 4) & 3; setRR(r, getRR(r) + 1); return 1; }
    if ((op & 0xCF) == 0x0B) { int r = (op >> 4) & 3; setRR(r, getRR(r) - 1); return 1; }
    if ((op & 0xCF) == 0x09) {                      /* ADD HL,rr */
        uint32_t hl = registers.hl, v = getRR((op >> 4) & 3), r = hl + v;
        registers.f = (registers.f & FLAG_Z)
                    | (((hl & 0xFFF) + (v & 0xFFF)) > 0xFFF ? FLAG_H : 0)
                    | (r > 0xFFFF ? FLAG_C : 0);
        registers.hl = (uint16_t)r;
        return 1;
    }
    if ((op & 0xCF) == 0xC1) {                      /* POP */
        int r = (op >> 4) & 3;
        if (r == 3) registers.af = pop() & 0xFFF0; else setRR(r, pop());
        return 1;
    }
    if ((op & 0xCF) == 0xC5) {                      /* PUSH */
        int r = (op >> 4) & 3;
        push(r == 3 ? registers.af : getRR(r));
        return 1;
    }
    return 0;
}

static int condZ(void)  { return registers.f & FLAG_Z; }
static int condC(void)  { return registers.f & FLAG_C; }

static void nop(void) {}

static void daa(void) {
    int a = registers.a;
    if (!(registers.f & FLAG_N)) {
        if ((registers.f & FLAG_C) || a > 0x99) { a += 0x60; registers.f |= FLAG_C; }
        if ((registers.f & FLAG_H) || (a & 0x0F) > 9) a += 6;
    } else {
        if (registers.f & FLAG_C) a -= 0x60;
        if (registers.f & FLAG_H) a -= 6;
    }
    registers.a = a & 0xFF;
    registers.f = (registers.f & (FLAG_N | FLAG_C)) | (registers.a == 0 ? FLAG_Z : 0);
}
static void rlca(void) { int c = registers.a >> 7; registers.a = (registers.a << 1) | c; registers.f = c ? FLAG_C : 0; }
static void rrca(void) { int c = registers.a & 1;  registers.a = (registers.a >> 1) | (c << 7); registers.f = c ? FLAG_C : 0; }
static void rla(void)  { int oc = (registers.f & FLAG_C) ? 1 : 0, c = registers.a >> 7; registers.a = (registers.a << 1) | oc; registers.f = c ? FLAG_C : 0; }
static void rra(void)  { int oc = (registers.f & FLAG_C) ? 1 : 0, c = registers.a & 1;  registers.a = (registers.a >> 1) | (oc << 7); registers.f = c ? FLAG_C : 0; }
static void scf(void)  { registers.f = (registers.f & FLAG_Z) | FLAG_C; }
static void ccf(void)  { registers.f = (registers.f & FLAG_Z) | ((registers.f & FLAG_C) ? 0 : FLAG_C); }
static void stop_op(uint8_t n) { (void)n; }
static void ld_nn_sp(uint16_t nn) { writeByte(nn, registers.sp & 0xFF); writeByte(nn + 1, registers.sp >> 8); }
static uint16_t spOffset(uint8_t n) {
    uint16_t sp = registers.sp;
    registers.f = (((sp & 0xF) + (n & 0xF)) > 0xF ? FLAG_H : 0) | (((sp & 0xFF) + n) > 0xFF ? FLAG_C : 0);
    return (uint16_t)(sp + (int8_t)n);
}
static void add_sp_n(uint8_t n)  { registers.sp = spOffset(n); }
static void ld_hl_sp_n(uint8_t n) { registers.hl = spOffset(n); }
#define RST(n) static void rst_##n(void) { push(registers.pc); registers.pc = 0x##n; }
RST(00) RST(08) RST(10) RST(18) RST(20) RST(28) RST(30) RST(38)

/* CB-prefixed ops: bits 7-6 = group, 5-3 = op/bit, 2-0 = register */
static void cb_op(uint8_t op) {
    int r = op & 7, b = (op >> 3) & 7;
    uint8_t v = getR(r), c = 0;
    switch (op >> 6) {
    case 0: {
        int oc = (registers.f & FLAG_C) ? 1 : 0;
        switch (b) {
        case 0: c = v >> 7; v = (v << 1) | c; break;          /* RLC */
        case 1: c = v & 1;  v = (v >> 1) | (c << 7); break;   /* RRC */
        case 2: c = v >> 7; v = (v << 1) | oc; break;         /* RL  */
        case 3: c = v & 1;  v = (v >> 1) | (oc << 7); break;  /* RR  */
        case 4: c = v >> 7; v = v << 1; break;                /* SLA */
        case 5: c = v & 1;  v = (v >> 1) | (v & 0x80); break; /* SRA */
        case 6: c = 0;      v = (v << 4) | (v >> 4); break;   /* SWAP */
        default: c = v & 1; v = v >> 1; break;                /* SRL */
        }
        registers.f = (v == 0 ? FLAG_Z : 0) | (c ? FLAG_C : 0);
        setR(r, v);
        break; }
    case 1:  /* BIT */
        registers.f = (registers.f & FLAG_C) | FLAG_H | ((v & (1 << b)) ? 0 : FLAG_Z);
        break;
    case 2: setR(r, v & ~(1 << b)); break;  /* RES */
    default: setR(r, v | (1 << b)); break;  /* SET */
    }
}
static void halt_op(void) {
    uint8_t p = readByte(0xFF0F) & readByte(0xFFFF) & 0x1F;
    if (!ime && p) haltBug = 1; else halted = 1;
}
static void di(void) { ime = 0; }
static void ei(void) { ime = 1; }
static void jp_nn(uint16_t nn) { registers.pc = nn; }
static void jp_hl(void) { registers.pc = registers.hl; }
static void call_nn(uint16_t nn) { push(registers.pc); registers.pc = nn; }
static void ret(void) { registers.pc = pop(); }
static void reti(void) { registers.pc = pop(); ime = 1; }

static void jr(uint8_t n)    { registers.pc += (int8_t)n; }
static void jr_nz(uint8_t n) { if (!condZ()) registers.pc += (int8_t)n; }
static void jr_z(uint8_t n)  { if (condZ())  registers.pc += (int8_t)n; }
static void jr_nc(uint8_t n) { if (!condC()) registers.pc += (int8_t)n; }
static void jr_c(uint8_t n)  { if (condC())  registers.pc += (int8_t)n; }

static void jp_nz(uint16_t nn) { if (!condZ()) registers.pc = nn; }
static void jp_z(uint16_t nn)  { if (condZ())  registers.pc = nn; }
static void jp_nc(uint16_t nn) { if (!condC()) registers.pc = nn; }
static void jp_c(uint16_t nn)  { if (condC())  registers.pc = nn; }

static void call_nz(uint16_t nn) { if (!condZ()) call_nn(nn); }
static void call_z(uint16_t nn)  { if (condZ())  call_nn(nn); }
static void call_nc(uint16_t nn) { if (!condC()) call_nn(nn); }
static void call_c(uint16_t nn)  { if (condC())  call_nn(nn); }

static void ret_nz(void) { if (!condZ()) ret(); }
static void ret_z(void)  { if (condZ())  ret(); }
static void ret_nc(void) { if (!condC()) ret(); }
static void ret_c(void)  { if (condC())  ret(); }

static void ld_bc_nn(uint16_t nn) { registers.bc = nn; }
static void ld_de_nn(uint16_t nn) { registers.de = nn; }
static void ld_hl_nn(uint16_t nn) { registers.hl = nn; }
static void ld_sp_nn(uint16_t nn) { registers.sp = nn; }
static void ld_sp_hl(void) { registers.sp = registers.hl; }

static void ld_b_n(uint8_t n) { registers.b = n; }
static void ld_c_n(uint8_t n) { registers.c = n; }
static void ld_d_n(uint8_t n) { registers.d = n; }
static void ld_e_n(uint8_t n) { registers.e = n; }
static void ld_h_n(uint8_t n) { registers.h = n; }
static void ld_l_n(uint8_t n) { registers.l = n; }
static void ld_a_n(uint8_t n) { registers.a = n; }
static void ld_hlp_n(uint8_t n) { writeByte(registers.hl, n); }

static void ld_bcp_a(void) { writeByte(registers.bc, registers.a); }
static void ld_dep_a(void) { writeByte(registers.de, registers.a); }
static void ld_a_bcp(void) { registers.a = readByte(registers.bc); }
static void ld_a_dep(void) { registers.a = readByte(registers.de); }
static void ldi_hl_a(void) { writeByte(registers.hl++, registers.a); }
static void ldi_a_hl(void) { registers.a = readByte(registers.hl++); }
static void ldd_hl_a(void) { writeByte(registers.hl--, registers.a); }
static void ldd_a_hl(void) { registers.a = readByte(registers.hl--); }

static void ld_nn_a(uint16_t nn) { writeByte(nn, registers.a); }
static void ld_a_nn(uint16_t nn) { registers.a = readByte(nn); }
static void ldh_n_a(uint8_t n) { writeByte(0xFF00 + n, registers.a); }
static void ldh_a_n(uint8_t n) { registers.a = readByte(0xFF00 + n); }
static void ldh_c_a(void) { writeByte(0xFF00 + registers.c, registers.a); }
static void ldh_a_c(void) { registers.a = readByte(0xFF00 + registers.c); }

static void cpl(void) { registers.a = ~registers.a; registers.f |= FLAG_N | FLAG_H; }

static void add_n(uint8_t n) { alu(0, n); }
static void adc_n(uint8_t n) { alu(1, n); }
static void sub_n(uint8_t n) { alu(2, n); }
static void sbc_n(uint8_t n) { alu(3, n); }
static void and_n(uint8_t n) { alu(4, n); }
static void xor_n(uint8_t n) { alu(5, n); }
static void or_n(uint8_t n)  { alu(6, n); }
static void cp_n(uint8_t n)  { alu(7, n); }

static const struct instruction instructions[256] = {
    [0x00] = { "NOP",             0, nop     },
    [0x07] = { "RLCA",            0, rlca    },
    [0x08] = { "LD (0x%04X), SP", 2, ld_nn_sp },
    [0x0F] = { "RRCA",            0, rrca    },
    [0x10] = { "STOP",            1, stop_op },
    [0x17] = { "RLA",             0, rla     },
    [0x1F] = { "RRA",             0, rra     },
    [0x27] = { "DAA",             0, daa     },
    [0x37] = { "SCF",             0, scf     },
    [0x3F] = { "CCF",             0, ccf     },
    [0xC7] = { "RST 00",          0, rst_00  },
    [0xCF] = { "RST 08",          0, rst_08  },
    [0xD7] = { "RST 10",          0, rst_10  },
    [0xDF] = { "RST 18",          0, rst_18  },
    [0xE7] = { "RST 20",          0, rst_20  },
    [0xE8] = { "ADD SP, %d",      1, add_sp_n },
    [0xEF] = { "RST 28",          0, rst_28  },
    [0xF7] = { "RST 30",          0, rst_30  },
    [0xF8] = { "LD HL, SP+%d",    1, ld_hl_sp_n },
    [0xFF] = { "RST 38",          0, rst_38  },
    [0xCB] = { "CB %02X",         1, cb_op   },
    [0x76] = { "HALT",            0, halt_op },
    [0x01] = { "LD BC, 0x%04X",   2, ld_bc_nn },
    [0x02] = { "LD (BC), A",      0, ld_bcp_a },
    [0x06] = { "LD B, 0x%02X",    1, ld_b_n  },
    [0x0A] = { "LD A, (BC)",      0, ld_a_bcp },
    [0x0E] = { "LD C, 0x%02X",    1, ld_c_n  },
    [0x11] = { "LD DE, 0x%04X",   2, ld_de_nn },
    [0x12] = { "LD (DE), A",      0, ld_dep_a },
    [0x16] = { "LD D, 0x%02X",    1, ld_d_n  },
    [0x18] = { "JR %d",           1, jr      },
    [0x1A] = { "LD A, (DE)",      0, ld_a_dep },
    [0x1E] = { "LD E, 0x%02X",    1, ld_e_n  },
    [0x20] = { "JR NZ, %d",       1, jr_nz   },
    [0x21] = { "LD HL, 0x%04X",   2, ld_hl_nn },
    [0x22] = { "LDI (HL), A",     0, ldi_hl_a },
    [0x26] = { "LD H, 0x%02X",    1, ld_h_n  },
    [0x28] = { "JR Z, %d",        1, jr_z    },
    [0x2A] = { "LDI A, (HL)",     0, ldi_a_hl },
    [0x2E] = { "LD L, 0x%02X",    1, ld_l_n  },
    [0x2F] = { "CPL",             0, cpl     },
    [0x30] = { "JR NC, %d",       1, jr_nc   },
    [0x31] = { "LD SP, 0x%04X",   2, ld_sp_nn },
    [0x32] = { "LDD (HL), A",     0, ldd_hl_a },
    [0x36] = { "LD (HL), 0x%02X", 1, ld_hlp_n },
    [0x38] = { "JR C, %d",        1, jr_c    },
    [0x3A] = { "LDD A, (HL)",     0, ldd_a_hl },
    [0x3E] = { "LD A, 0x%02X",    1, ld_a_n  },
    [0xC0] = { "RET NZ",          0, ret_nz  },
    [0xC2] = { "JP NZ, 0x%04X",   2, jp_nz   },
    [0xC3] = { "JP 0x%04X",       2, jp_nn   },
    [0xC4] = { "CALL NZ, 0x%04X", 2, call_nz },
    [0xC6] = { "ADD A, 0x%02X",   1, add_n   },
    [0xC8] = { "RET Z",           0, ret_z   },
    [0xC9] = { "RET",             0, ret     },
    [0xCA] = { "JP Z, 0x%04X",    2, jp_z    },
    [0xCC] = { "CALL Z, 0x%04X",  2, call_z  },
    [0xCD] = { "CALL 0x%04X",     2, call_nn },
    [0xCE] = { "ADC A, 0x%02X",   1, adc_n   },
    [0xD0] = { "RET NC",          0, ret_nc  },
    [0xD2] = { "JP NC, 0x%04X",   2, jp_nc   },
    [0xD4] = { "CALL NC, 0x%04X", 2, call_nc },
    [0xD6] = { "SUB 0x%02X",      1, sub_n   },
    [0xD8] = { "RET C",           0, ret_c   },
    [0xD9] = { "RETI",            0, reti    },
    [0xDA] = { "JP C, 0x%04X",    2, jp_c    },
    [0xDC] = { "CALL C, 0x%04X",  2, call_c  },
    [0xDE] = { "SBC A, 0x%02X",   1, sbc_n   },
    [0xE0] = { "LDH (0x%02X), A", 1, ldh_n_a },
    [0xE2] = { "LD (C), A",       0, ldh_c_a },
    [0xE6] = { "AND 0x%02X",      1, and_n   },
    [0xE9] = { "JP (HL)",         0, jp_hl   },
    [0xEA] = { "LD (0x%04X), A",  2, ld_nn_a },
    [0xEE] = { "XOR 0x%02X",      1, xor_n   },
    [0xF0] = { "LDH A, (0x%02X)", 1, ldh_a_n },
    [0xF2] = { "LD A, (C)",       0, ldh_a_c },
    [0xF3] = { "DI",              0, di      },
    [0xF6] = { "OR 0x%02X",       1, or_n    },
    [0xF9] = { "LD SP, HL",       0, ld_sp_hl },
    [0xFA] = { "LD A, (0x%04X)",  2, ld_a_nn },
    [0xFB] = { "EI",              0, ei      },
    [0xFE] = { "CP 0x%02X",       1, cp_n    },
};

void cpuInit(void) {
    ime = 0; halted = 0; haltBug = 0;
    registers.af = 0x01B0; registers.bc = 0x0013;
    registers.de = 0x00D8; registers.hl = 0x014D;
    registers.sp = 0xFFFE; registers.pc = 0x0100;
}

static void cpuExec(void) {
    uint16_t pc = registers.pc;
    uint8_t op = readByte(pc);
    const struct instruction *in = &instructions[op];

    if (getenv("GBTRACE"))
        fprintf(stderr, "PC=%04X OP=%02X AF=%04X BC=%04X DE=%04X HL=%04X SP=%04X\n",
                pc, op, registers.af, registers.bc, registers.de, registers.hl, registers.sp);

    registers.pc += 1;
    if (decodeRegular(op)) return;
    registers.pc -= 1;

    if (!in->execute) {
        fprintf(stderr, "Unimplemented opcode 0x%02X at 0x%04X\n", op, pc);
        exit(1);
    }
    uint16_t operand = 0;
    if (in->operandLength == 1) operand = readByte(pc + 1);
    if (in->operandLength == 2) operand = readWord(pc + 1);

    registers.pc += 1 + in->operandLength;

    switch (in->operandLength) {
        case 0: ((void (*)(void))in->execute)(); break;
        case 1: ((void (*)(uint8_t))in->execute)((uint8_t)operand); break;
        case 2: ((void (*)(uint16_t))in->execute)(operand); break;
    }
}

static int takenCond(uint8_t op, uint8_t f) {
    switch ((op >> 3) & 3) {
        case 0: return !(f & FLAG_Z);
        case 1: return (f & FLAG_Z) != 0;
        case 2: return !(f & FLAG_C);
        default: return (f & FLAG_C) != 0;
    }
}

static const uint8_t cycTable[256] = {
    1,3,2,2,1,1,2,1, 5,2,2,2,1,1,2,1,
    1,3,2,2,1,1,2,1, 3,2,2,2,1,1,2,1,
    2,3,2,2,1,1,2,1, 2,2,2,2,1,1,2,1,
    2,3,2,2,3,3,3,1, 2,2,2,2,1,1,2,1,
    1,1,1,1,1,1,2,1, 1,1,1,1,1,1,2,1,
    1,1,1,1,1,1,2,1, 1,1,1,1,1,1,2,1,
    1,1,1,1,1,1,2,1, 1,1,1,1,1,1,2,1,
    2,2,2,2,2,2,1,2, 1,1,1,1,1,1,2,1,
    1,1,1,1,1,1,2,1, 1,1,1,1,1,1,2,1,
    1,1,1,1,1,1,2,1, 1,1,1,1,1,1,2,1,
    1,1,1,1,1,1,2,1, 1,1,1,1,1,1,2,1,
    1,1,1,1,1,1,2,1, 1,1,1,1,1,1,2,1,
    2,3,3,4,3,4,2,4, 2,4,3,1,3,6,2,4,
    2,3,3,0,3,4,2,4, 2,4,3,0,3,0,2,4,
    3,3,2,0,0,4,2,4, 4,1,4,0,0,0,2,4,
    3,3,2,1,0,4,2,4, 3,2,4,1,0,0,2,4,
};

void cpuStep(void) {
    int cycles = 0;
    uint8_t pending = readByte(0xFF0F) & readByte(0xFFFF) & 0x1F;
    if (pending) {
        halted = 0;
        if (ime) {
            int bit = 0;
            while (!(pending & (1 << bit))) bit++;
            writeByte(0xFF0F, readByte(0xFF0F) & ~(1 << bit));
            ime = 0;
            push(registers.pc);
            registers.pc = 0x40 + bit * 8;
            cycles = 5;
        }
    }
    if (!cycles) {
        if (halted) {
            cycles = 1;
        } else {
            uint16_t pc0 = registers.pc; uint8_t f0 = registers.f;
            uint8_t op = readByte(pc0);
            int hb = haltBug; haltBug = 0;
            cpuExec();
            if (hb && registers.pc == (uint16_t)(pc0 + 1 + instructions[op].operandLength)) registers.pc--;
            cycles = cycTable[op];
            if (op == 0xCB) {
                uint8_t cb = readByte(pc0 + 1);
                cycles = ((cb & 7) == 6) ? (((cb >> 6) == 1) ? 3 : 4) : 2;
            } else switch (op) {
                case 0x20: case 0x28: case 0x30: case 0x38:
                    if (takenCond(op, f0)) cycles += 1; break;
                case 0xC2: case 0xCA: case 0xD2: case 0xDA:
                    if (takenCond(op, f0)) cycles += 1; break;
                case 0xC4: case 0xCC: case 0xD4: case 0xDC:
                    if (takenCond(op, f0)) cycles += 3; break;
                case 0xC0: case 0xC8: case 0xD0: case 0xD8:
                    if (takenCond(op, f0)) cycles += 3; break;
            }
        }
    }
    timingTick(cycles * 4);
}
