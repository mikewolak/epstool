/*
 * ES5510 Standalone Emulator
 *
 * Based on MAME es5510.cpp by Christian Brunschen
 * License: BSD-3-Clause
 *
 * Extracted and adapted for standalone WAV file processing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es5510_standalone.h"

/* Operand select table - from MAME */
static const op_select_t OPERAND_SELECT[16] = {
    { SRC_DST_REG, SRC_DST_REG, SRC_DST_REG, SRC_DST_REG },
    { SRC_DST_REG, SRC_DST_REG, SRC_DST_REG, SRC_DST_DELAY },
    { SRC_DST_REG, SRC_DST_REG, SRC_DST_REG, SRC_DST_BOTH },
    { SRC_DST_REG, SRC_DST_REG, SRC_DST_DELAY, SRC_DST_REG },
    { SRC_DST_REG, SRC_DST_REG, SRC_DST_DELAY, SRC_DST_BOTH },
    { SRC_DST_REG, SRC_DST_DELAY, SRC_DST_REG, SRC_DST_REG },
    { SRC_DST_REG, SRC_DST_DELAY, SRC_DST_DELAY, SRC_DST_REG },
    { SRC_DST_REG, SRC_DST_BOTH, SRC_DST_REG, SRC_DST_REG },
    { SRC_DST_REG, SRC_DST_BOTH, SRC_DST_DELAY, SRC_DST_REG },
    { SRC_DST_DELAY, SRC_DST_REG, SRC_DST_REG, SRC_DST_REG },
    { SRC_DST_DELAY, SRC_DST_REG, SRC_DST_REG, SRC_DST_DELAY },
    { SRC_DST_DELAY, SRC_DST_REG, SRC_DST_REG, SRC_DST_BOTH },
    { SRC_DST_DELAY, SRC_DST_REG, SRC_DST_DELAY, SRC_DST_REG },
    { SRC_DST_DELAY, SRC_DST_REG, SRC_DST_DELAY, SRC_DST_BOTH },
    { SRC_DST_DELAY, SRC_DST_BOTH, SRC_DST_REG, SRC_DST_REG },
    { SRC_DST_DELAY, SRC_DST_BOTH, SRC_DST_DELAY, SRC_DST_REG },
};

/* RAM control table - from MAME */
static const ram_control_t RAM_CONTROL[8] = {
    { RAM_CYCLE_READ,      RAM_CONTROL_DELAY },
    { RAM_CYCLE_WRITE,     RAM_CONTROL_DELAY },
    { RAM_CYCLE_READ,      RAM_CONTROL_TABLE_A },
    { RAM_CYCLE_WRITE,     RAM_CONTROL_TABLE_A },
    { RAM_CYCLE_READ,      RAM_CONTROL_TABLE_B },
    { RAM_CYCLE_DUMP_FIFO, RAM_CONTROL_DELAY },
    { RAM_CYCLE_READ,      RAM_CONTROL_IO },
    { RAM_CYCLE_WRITE,     RAM_CONTROL_IO },
};

/* Sign-extend 24-bit to 32-bit */
static inline int32_t sext24(int32_t value) {
    if (value & 0x800000) {
        return value | 0xFF000000;
    }
    return value & 0x00FFFFFF;
}

/* Flag manipulation helpers - from MAME */
static inline uint8_t setFlagTo(uint8_t ccr, uint8_t flag, bool set) {
    return set ? (ccr | flag) : (ccr & ~flag);
}

static inline bool isFlagSet(uint8_t ccr, uint8_t flag) {
    return (ccr & flag) != 0;
}

/* Negate 24-bit value (one's complement + 1) */
static inline int32_t negate(int32_t value) {
    return ((value ^ 0x00ffffff) + 1) & 0x00ffffff;
}

/* Add with flags - from MAME */
static int32_t add_with_flags(int32_t a, int32_t b, uint8_t *flags) {
    int32_t aSign = a & 0x00800000;
    int32_t bSign = b & 0x00800000;
    int32_t result = a + b;
    int32_t resultSign = result & 0x00800000;
    bool overflow = (aSign == bSign) && (aSign != resultSign);
    bool carry = result & 0x01000000;
    bool negative = resultSign != 0;
    bool lessThan = (overflow && !negative) || (!overflow && negative);
    *flags = setFlagTo(*flags, FLAG_C, carry);
    *flags = setFlagTo(*flags, FLAG_N, negative);
    *flags = setFlagTo(*flags, FLAG_Z, (result & 0x00ffffff) == 0);
    *flags = setFlagTo(*flags, FLAG_V, overflow);
    *flags = setFlagTo(*flags, FLAG_LT, lessThan);
    return result & 0x00ffffff;
}

/* Saturate on overflow - from MAME */
static int32_t saturate(int32_t value, uint8_t *flags, bool negative) {
    if (isFlagSet(*flags, FLAG_V)) {
        *flags = setFlagTo(*flags, FLAG_N, negative);
        *flags = setFlagTo(*flags, FLAG_Z, false);
        return negative ? 0x00800000 : 0x007fffff;
    }
    return value;
}

/* Arithmetic shift left - from MAME */
static int32_t asl(int32_t value, int shift, uint8_t *flags) {
    int32_t signBefore = value & 0x00800000;
    int32_t result = value << shift;
    int32_t signAfter = result & 0x00800000;
    bool overflow = signBefore != signAfter;
    *flags = setFlagTo(*flags, FLAG_Z, (result & 0x00ffffff) == 0);
    *flags = setFlagTo(*flags, FLAG_V, overflow);
    return saturate(result, flags, signBefore != 0);
}

/* Count low ones (for MEMSIZ) */
static int8_t countLowOnes(int32_t x) {
    int8_t n = 0;
    while ((x & 1) == 1) {
        ++n;
        x >>= 1;
    }
    return n;
}

/* Initialize ES5510 */
void es5510_init(es5510_t *dsp) {
    memset(dsp, 0, sizeof(es5510_t));
    dsp->dram = (int16_t *)calloc(ES5510_DRAM_SIZE, sizeof(int16_t));
    dsp->memsiz = 0x00ffffff;
    dsp->memincrement = 0x01000000;
    dsp->memshift = 24;
    dsp->sigreg = 1;
    dsp->mulshift = 1;
    es5510_reset(dsp);
}

/* Free ES5510 resources */
void es5510_free(es5510_t *dsp) {
    if (dsp->dram) {
        free(dsp->dram);
        dsp->dram = NULL;
    }
}

/* Reset ES5510 */
void es5510_reset(es5510_t *dsp) {
    dsp->pc = 0;
    dsp->running = true;
    memset(dsp->gpr, 0, sizeof(dsp->gpr));
    dsp->ser0l = dsp->ser0r = 0;
    dsp->ser1l = dsp->ser1r = 0;
    dsp->ser2l = dsp->ser2r = 0;
    dsp->ser3l = dsp->ser3r = 0;
    dsp->machl = 0;
    dsp->mac_overflow = false;
    dsp->dil = 0;
    dsp->dlength = 0;
    dsp->abase = 0;
    dsp->bbase = 0;
    dsp->dbase = 0;
    dsp->ccr = 0;
    dsp->cmr = 0;
    dsp->dol[0] = dsp->dol[1] = 0;
    dsp->dol_count = 0;
    memset(&dsp->alu, 0, sizeof(alu_t));
    memset(&dsp->mulacc, 0, sizeof(mulacc_t));
    memset(&dsp->ram, 0, sizeof(ram_t));
    memset(&dsp->ram_p, 0, sizeof(ram_t));
    memset(&dsp->ram_pp, 0, sizeof(ram_t));
}

/* Read register - from MAME */
static int32_t read_reg(es5510_t *dsp, uint8_t reg) {
    if (reg < 0xc0) {
        return dsp->gpr[reg];
    }
    switch (reg) {
        /* Serial registers: 16-bit signed, shifted left 8 to 24-bit, masked */
        case 234: { int16_t v = dsp->ser0r; return ((int32_t)v << 8) & 0x00ffffff; }
        case 235: { int16_t v = dsp->ser0l; return ((int32_t)v << 8) & 0x00ffffff; }
        case 236: { int16_t v = dsp->ser1r; return ((int32_t)v << 8) & 0x00ffffff; }
        case 237: { int16_t v = dsp->ser1l; return ((int32_t)v << 8) & 0x00ffffff; }
        case 238: { int16_t v = dsp->ser2r; return ((int32_t)v << 8) & 0x00ffffff; }
        case 239: { int16_t v = dsp->ser2l; return ((int32_t)v << 8) & 0x00ffffff; }
        case 240: { int16_t v = dsp->ser3r; return ((int32_t)v << 8) & 0x00ffffff; }
        case 241: { int16_t v = dsp->ser3l; return ((int32_t)v << 8) & 0x00ffffff; }
        case 242: /* MACL */
            return dsp->mac_overflow ? (dsp->machl < 0 ? 0x00000000 : 0x00ffffff)
                                     : (dsp->machl >> 0) & 0x00ffffff;
        case 243: /* MACH */
            return dsp->mac_overflow ? (dsp->machl < 0 ? 0x00800000 : 0x007fffff)
                                     : (dsp->machl >> 24) & 0x00ffffff;
        case 244: return dsp->dil;  /* DIL when reading */
        case 245: return dsp->dlength;
        case 246: return dsp->abase;
        case 247: return dsp->bbase;
        case 248: return dsp->dbase;
        case 249: return dsp->sigreg;
        case 250: return dsp->ccr;
        case 251: return dsp->cmr;
        case 252: return 0x00ffffff;  /* -1 */
        case 253: return 0x00800000;  /* MIN */
        case 254: return 0x007fffff;  /* MAX */
        case 255: return 0;           /* ZERO */
        default: return 0;
    }
}

/* Debug flag for R144 tracing */
int debug_r144 = 0;

/* Write register - from MAME */
static void write_reg(es5510_t *dsp, uint8_t reg, int32_t value) {
    value &= 0x00ffffff;
    if (reg < 0xc0) {
        if (debug_r144 && reg == 144 && value != dsp->gpr[144]) {
            printf("  [PC=%d] R144: %d -> %d\n", dsp->pc, dsp->gpr[144], value);
        }
        dsp->gpr[reg] = value;
        return;
    }
    switch (reg) {
        case 234: dsp->ser0r = (value >> 8) & 0xffff; break;
        case 235: dsp->ser0l = (value >> 8) & 0xffff; break;
        case 236: dsp->ser1r = (value >> 8) & 0xffff; break;
        case 237: dsp->ser1l = (value >> 8) & 0xffff; break;
        case 238: dsp->ser2r = (value >> 8) & 0xffff; break;
        case 239: dsp->ser2l = (value >> 8) & 0xffff; break;
        case 240: dsp->ser3r = (value >> 8) & 0xffff; break;
        case 241: dsp->ser3l = (value >> 8) & 0xffff; break;
        case 242: { /* MACL */
            int64_t masked = dsp->machl & ((int64_t)0x00ffffff << 24);
            int64_t shifted = (int64_t)(value & 0x00ffffff) << 0;
            dsp->machl = masked | shifted;
            if (dsp->machl & ((int64_t)1 << 47)) {
                dsp->machl |= (int64_t)0xFFFF << 48;
            }
            break;
        }
        case 243: { /* MACH */
            int64_t masked = dsp->machl & ((int64_t)0x00ffffff << 0);
            int64_t shifted = (int64_t)(value & 0x00ffffff) << 24;
            dsp->machl = masked | shifted;
            if (dsp->machl & ((int64_t)1 << 47)) {
                dsp->machl |= (int64_t)0xFFFF << 48;
            }
            dsp->mac_overflow = false;
            break;
        }
        case 244: /* MEMSIZ when writing */
            /* NOTE: Many effects use MUL x,DIL >MEMSIZ as a "discard result" pattern.
             * On real hardware, MEMSIZ writes may be ignored during normal operation
             * after initial configuration. For now, ignore writes that would corrupt
             * the memory configuration (memshift < 4). */
            {
                int8_t new_shift = countLowOnes(value);
                if (new_shift >= 4) {  /* Only accept reasonable configurations */
                    dsp->memshift = new_shift;
                    dsp->memsiz = 0x00ffffff >> (24 - dsp->memshift);
                    dsp->memmask = 0x00ffffff & ~dsp->memsiz;
                    dsp->memincrement = 1 << dsp->memshift;
                }
            }
            break;
        case 245: dsp->dlength = value; break;
        case 246: dsp->abase = value; break;
        case 247: dsp->bbase = value; break;
        case 248: dsp->dbase = value; break;
        case 249: dsp->sigreg = (value != 0); break;
        case 250: dsp->ccr = (value >> 16) & FLAG_MASK; break;
        case 251: dsp->cmr = (value >> 16) & (FLAG_MASK | FLAG_NOT); break;
        /* 252-255 are read-only constants */
        default: break;
    }
}

/* Write to DOL FIFO - from MAME */
static void write_to_dol(es5510_t *dsp, int32_t value) {
    if (dsp->dol_count >= 2) {
        dsp->dol[0] = dsp->dol[1];
        dsp->dol[1] = value;
    } else {
        dsp->dol[dsp->dol_count++] = value;
    }
}

/* ALU operation - from MAME */
static int32_t alu_operation(es5510_t *dsp, uint8_t op, int32_t a, int32_t b, uint8_t *flags) {
    int32_t tmp;
    switch (op) {
        case 0x0: /* ADD */
            tmp = add_with_flags(a, b, flags);
            return saturate(tmp, flags, (a & 0x00800000) != 0);
        case 0x1: /* SUB */
            tmp = add_with_flags(a, negate(b), flags);
            return saturate(tmp, flags, (a & 0x00800000) != 0);
        case 0x2: /* ADDU */
            return add_with_flags(a, b, flags);
        case 0x3: /* SUBU */
            return add_with_flags(a, negate(b), flags);
        case 0x4: /* CMP */
            add_with_flags(a, negate(b), flags);
            return a;
        case 0x5: /* AND */
            a &= b;
            *flags = setFlagTo(*flags, FLAG_N, (a & 0x00800000) != 0);
            *flags = setFlagTo(*flags, FLAG_Z, a == 0);
            return a;
        case 0x6: /* OR */
            a |= b;
            *flags = setFlagTo(*flags, FLAG_N, (a & 0x00800000) != 0);
            *flags = setFlagTo(*flags, FLAG_Z, a == 0);
            return a;
        case 0x7: /* XOR */
            a ^= b;
            *flags = setFlagTo(*flags, FLAG_N, (a & 0x00800000) != 0);
            *flags = setFlagTo(*flags, FLAG_Z, a == 0);
            return a;
        case 0x8: /* ABS */ {
            *flags &= ~FLAG_N;
            bool isNegative = (a & 0x00800000) != 0;
            *flags = setFlagTo(*flags, FLAG_C, isNegative);
            return isNegative ? (0x00ffffff ^ a) : a;
        }
        case 0x9: /* MOV */
            return b;
        case 0xa: /* ASL2 */
            return asl(b, 2, flags);
        case 0xb: /* ASL8 */
            return asl(b, 8, flags);
        case 0xc: /* LS15 */
            *flags &= ~FLAG_N;
            *flags = setFlagTo(*flags, FLAG_C, (b & 0x00800000) != 0);
            return (b << 15) & 0x007fffff;
        case 0xd: /* DIFF */
            return add_with_flags(0x007fffff, negate(b), flags);
        case 0xe: /* ASR */
            *flags = setFlagTo(*flags, FLAG_N, (b & 0x00800000) != 0);
            *flags = setFlagTo(*flags, FLAG_C, (b & 1) != 0);
            return (b >> 1) | (b & 0x00800000);
        case 0xf: /* END */
        default:
            return 0;
    }
}

/* Load microcode from raw binary (6 bytes per instruction) */
void es5510_load_microcode(es5510_t *dsp, const uint8_t *data, int len) {
    int n_instr = len / 6;
    if (n_instr > 160) n_instr = 160;

    for (int i = 0; i < n_instr; i++) {
        const uint8_t *p = data + i * 6;
        /* ES5510 microcode: bytes are D, C, B, A, ctrl_hi, ctrl_lo */
        uint64_t instr = 0;
        instr |= (uint64_t)p[0] << 40;  /* D reg */
        instr |= (uint64_t)p[1] << 32;  /* C reg */
        instr |= (uint64_t)p[2] << 24;  /* B reg */
        instr |= (uint64_t)p[3] << 16;  /* A reg */
        instr |= (uint64_t)p[4] << 8;   /* ctrl hi */
        instr |= (uint64_t)p[5] << 0;   /* ctrl lo */
        dsp->instr[i] = instr;
    }

    /* Fill remaining with END instructions */
    for (int i = n_instr; i < 160; i++) {
        dsp->instr[i] = 0x000000000F00ULL;  /* END opcode */
    }
}

/* Set GPR value (for parameter control) */
void es5510_set_gpr(es5510_t *dsp, uint8_t reg, int32_t value) {
    if (reg < 0xc0) {
        dsp->gpr[reg] = value & 0x00ffffff;
    }
}

/* Get GPR value */
int32_t es5510_get_gpr(es5510_t *dsp, uint8_t reg) {
    if (reg < 0xc0) {
        return dsp->gpr[reg];
    }
    return read_reg(dsp, reg);
}

/* Set serial input (before processing sample) */
void es5510_set_serial_input(es5510_t *dsp, int16_t left, int16_t right) {
    dsp->ser0l = left;
    dsp->ser0r = right;
    /* Other serial ports might be effects returns or gain controls
     * Setting them to passthrough (1.0 = 32767 in 16-bit) seems reasonable
     * since some effects multiply SER1R * SER0L for scaling */
    dsp->ser1l = 32767;  /* Unity gain */
    dsp->ser1r = 32767;
    dsp->ser2l = 0;
    dsp->ser2r = 0;
    dsp->ser3l = 0;
    dsp->ser3r = 0;
}

/* Get serial output (after processing sample)
 * Note: Effects typically output to ser3 (mixed with ser2 for stereo)
 * The EPS-16+ routes: ser0=ADC input, ser2/ser3=effect output to DAC
 */
void es5510_get_serial_output(es5510_t *dsp, int16_t *left, int16_t *right) {
    /* 4 REVERBS writes output to ser3l/ser3r and ser2l/ser2r */
    *left = dsp->ser3l;
    *right = dsp->ser3r;
}

/* Run one sample through the DSP - main emulation loop from MAME */
void es5510_run_sample(es5510_t *dsp) {
    dsp->pc = 0;
    dsp->running = true;

    while (dsp->running && dsp->pc < 160) {
        /* Pipeline state */
        dsp->ram_pp = dsp->ram_p;
        dsp->ram_p = dsp->ram;

        /* T0, clock low: Read instruction N */
        uint64_t instr = dsp->instr[dsp->pc];

        /* RAM cycle N-2: read data into DIL */
        if (dsp->ram_pp.cycle != RAM_CYCLE_WRITE) {
            if (!dsp->ram_pp.io) {
                dsp->dil = dsp->dram[dsp->ram_pp.address & ES5510_DRAM_MASK] << 8;
            }
        }

        /* Start of RAM cycle N */
        ram_control_t ramControl = RAM_CONTROL[(instr >> 3) & 0x07];
        dsp->ram.cycle = ramControl.cycle;
        dsp->ram.io = (ramControl.access == RAM_CONTROL_IO);

        /* RAM cycle N: read offset */
        int32_t offset = dsp->gpr[dsp->pc];
        switch (ramControl.access) {
            case RAM_CONTROL_DELAY:
                /* Delay line: circular buffer using dbase, dlength */
                /* Note: modulo by dlength (not dlength + memincrement) to match TABLE_A addressing */
                if (dsp->dlength > 0) {
                    dsp->ram.address = (((dsp->dbase + offset) % dsp->dlength) & dsp->memmask) >> dsp->memshift;
                } else {
                    dsp->ram.address = ((dsp->dbase + offset) & dsp->memmask) >> dsp->memshift;
                }
                break;
            case RAM_CONTROL_TABLE_A:
                /* Table A: uses abase directly (no circular wrapping) */
                dsp->ram.address = ((dsp->abase + offset) & dsp->memmask) >> dsp->memshift;
                break;
            case RAM_CONTROL_TABLE_B:
                /* Table B: uses bbase directly (no circular wrapping) */
                dsp->ram.address = ((dsp->bbase + offset) & dsp->memmask) >> dsp->memshift;
                break;
            case RAM_CONTROL_IO:
                dsp->ram.address = offset & 0x00fffff0;
                break;
        }

        /* T1, clock high: Decode instruction N */
        uint8_t operandSelect = (instr >> 8) & 0x0f;
        const op_select_t *opSelect = &OPERAND_SELECT[operandSelect];
        bool skippable = (instr & (0x01 << 7)) != 0;
        bool skip = false;

        if (skippable) {
            bool skipConditionSatisfied = (dsp->ccr & dsp->cmr & FLAG_MASK) != 0;
            if (isFlagSet(dsp->cmr, FLAG_NOT)) {
                skipConditionSatisfied = !skipConditionSatisfied;
            }
            skip = skipConditionSatisfied;
        }

        /* Write Multiplier result N-1 */
        if (dsp->mulacc.write_result) {
            dsp->mulacc.product = (int64_t)sext24(dsp->mulacc.cValue) *
                                  (int64_t)sext24(dsp->mulacc.dValue);
            dsp->mulacc.product <<= dsp->mulshift;

            if (dsp->mulacc.accumulate) {
                dsp->mulacc.result = dsp->mulacc.product + dsp->machl;
            } else {
                dsp->mulacc.result = dsp->mulacc.product;
            }

            if (dsp->mulacc.result < -((int64_t)1 << 47) ||
                dsp->mulacc.result >= ((int64_t)1 << 47)) {
                dsp->mac_overflow = true;
            } else {
                dsp->mac_overflow = false;
            }

            dsp->machl = dsp->mulacc.result;
            int32_t tmp = dsp->mac_overflow ?
                (dsp->machl < 0 ? 0x00800000 : 0x007fffff) :
                (dsp->mulacc.result & 0x0000ffffff000000ULL) >> 24;

            if (dsp->mulacc.dst & SRC_DST_REG) {
                write_reg(dsp, dsp->mulacc.cReg, tmp);
            }
            if (dsp->mulacc.dst & SRC_DST_DELAY) {
                write_to_dol(dsp, tmp);
            }
        }

        /* T1, clock low: Start of multiplier cycle N */
        dsp->mulacc.cReg = (instr >> 32) & 0xff;
        dsp->mulacc.dReg = (instr >> 40) & 0xff;
        dsp->mulacc.src = opSelect->mac_src;
        dsp->mulacc.dst = opSelect->mac_dst;
        dsp->mulacc.accumulate = ((instr >> 6) & 0x01) != 0;
        dsp->mulacc.write_result = !skip;

        /* Read Multiplier Operands N */
        if (dsp->mulacc.src == SRC_DST_REG) {
            dsp->mulacc.cValue = read_reg(dsp, dsp->mulacc.cReg);
        } else {
            dsp->mulacc.cValue = dsp->dil;
        }
        dsp->mulacc.dValue = read_reg(dsp, dsp->mulacc.dReg);

        /* T2, clock high: Write ALU Result N-1 */
        if (dsp->alu.write_result) {
            uint8_t flags = dsp->ccr;
            dsp->alu.result = alu_operation(dsp, dsp->alu.op,
                                            dsp->alu.aValue, dsp->alu.bValue, &flags);

            if (dsp->alu.dst & SRC_DST_REG) {
                write_reg(dsp, dsp->alu.aReg, dsp->alu.result);
            }
            if (dsp->alu.dst & SRC_DST_DELAY) {
                write_to_dol(dsp, dsp->alu.result);
            }
            if (dsp->alu.update_ccr) {
                dsp->ccr = flags;
            }
        }

        /* T2, clock low: Start of ALU cycle N */
        dsp->alu.aReg = (instr >> 16) & 0xff;
        dsp->alu.bReg = (instr >> 24) & 0xff;
        dsp->alu.op = (instr >> 12) & 0x0f;
        dsp->alu.src = opSelect->alu_src;
        dsp->alu.dst = opSelect->alu_dst;
        dsp->alu.write_result = !skip;
        dsp->alu.update_ccr = !skippable || (dsp->alu.op == 4); /* CMP always updates */

        if (dsp->alu.op == 0xf) {
            /* END instruction */
            dsp->dbase -= dsp->memincrement;
            if (dsp->dbase < 0) {
                dsp->dbase = dsp->dlength;
            }
            dsp->running = false;
        } else {
            /* Read ALU Operands N */
            int n_ops = (dsp->alu.op <= 0x7) ? 2 : 1;  /* ops 0-7 are 2-operand */
            if (n_ops == 1) {
                if (dsp->alu.src == SRC_DST_REG) {
                    dsp->alu.bValue = read_reg(dsp, dsp->alu.bReg);
                } else {
                    dsp->alu.bValue = dsp->dil;
                }
            } else {
                if (dsp->alu.src == SRC_DST_REG) {
                    dsp->alu.aValue = read_reg(dsp, dsp->alu.aReg);
                } else {
                    dsp->alu.aValue = dsp->dil;
                }
                dsp->alu.bValue = read_reg(dsp, dsp->alu.bReg);
            }
        }

        /* RAM cycle N-1: write to DRAM or eject DOL */
        if (dsp->ram_p.cycle != RAM_CYCLE_READ) {
            if (dsp->ram_p.cycle == RAM_CYCLE_WRITE) {
                if (!dsp->ram_p.io) {
                    dsp->dram[dsp->ram_p.address & ES5510_DRAM_MASK] = dsp->dol[0] >> 8;
                }
            }
            /* Eject from DOL FIFO */
            dsp->dol[0] = dsp->dol[1];
            if (dsp->dol_count > 0) {
                --dsp->dol_count;
            }
        }

        ++dsp->pc;
    }
}
