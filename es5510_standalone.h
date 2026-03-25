/*
 * ES5510 Standalone Emulator
 *
 * Based on MAME es5510.cpp by Christian Brunschen
 * License: BSD-3-Clause
 *
 * Extracted and adapted for standalone WAV file processing
 * to test Waveboy DSP microcode (RESON FILTER, 4 REVERBS, etc.)
 */

#ifndef ES5510_STANDALONE_H
#define ES5510_STANDALONE_H

#include <stdint.h>
#include <stdbool.h>

#define ES5510_DRAM_SIZE (1 << 20)  /* 1MB DRAM */
#define ES5510_DRAM_MASK (ES5510_DRAM_SIZE - 1)

/* Flags */
#define FLAG_N   (1 << 7)
#define FLAG_C   (1 << 6)
#define FLAG_V   (1 << 5)
#define FLAG_LT  (1 << 4)
#define FLAG_Z   (1 << 3)
#define FLAG_NOT (1 << 2)
#define FLAG_MASK (FLAG_N | FLAG_C | FLAG_V | FLAG_LT | FLAG_Z)

/* Source/destination types for operands */
typedef enum {
    SRC_DST_REG   = 1 << 0,
    SRC_DST_DELAY = 1 << 1,
    SRC_DST_BOTH  = (1 << 0) | (1 << 1)
} op_src_dst_t;

/* Operand select structure - order matches MAME initializers */
typedef struct {
    op_src_dst_t mac_src;
    op_src_dst_t mac_dst;
    op_src_dst_t alu_src;
    op_src_dst_t alu_dst;
} op_select_t;

/* RAM access types */
typedef enum {
    RAM_CONTROL_DELAY = 0,
    RAM_CONTROL_TABLE_A,
    RAM_CONTROL_TABLE_B,
    RAM_CONTROL_IO
} ram_control_access_t;

/* RAM cycle types */
typedef enum {
    RAM_CYCLE_READ = 0,
    RAM_CYCLE_WRITE = 1,
    RAM_CYCLE_DUMP_FIFO = 2
} ram_cycle_t;

/* RAM control structure */
typedef struct {
    ram_cycle_t cycle;
    ram_control_access_t access;
} ram_control_t;

/* ALU state */
typedef struct {
    uint8_t aReg;
    uint8_t bReg;
    op_src_dst_t src;
    op_src_dst_t dst;
    uint8_t op;
    int32_t aValue;
    int32_t bValue;
    int32_t result;
    bool update_ccr;
    bool write_result;
} alu_t;

/* Multiply-accumulate state */
typedef struct {
    uint8_t cReg;
    uint8_t dReg;
    op_src_dst_t src;
    op_src_dst_t dst;
    bool accumulate;
    int32_t cValue;
    int32_t dValue;
    int64_t product;
    int64_t result;
    bool write_result;
} mulacc_t;

/* RAM operation state */
typedef struct {
    int32_t address;
    bool io;
    ram_cycle_t cycle;
} ram_t;

/* ES5510 device state */
typedef struct {
    /* Program counter */
    uint8_t pc;
    bool running;

    /* General purpose registers (192 + special) */
    int32_t gpr[0xc0];

    /* Serial I/O registers */
    int16_t ser0r, ser0l;
    int16_t ser1r, ser1l;
    int16_t ser2r, ser2l;
    int16_t ser3r, ser3l;

    /* MAC accumulator (48-bit) */
    int64_t machl;
    bool mac_overflow;

    /* DIL - Data Input Latch */
    int32_t dil;

    /* Memory size and configuration */
    int32_t memsiz;
    int32_t memmask;
    int32_t memincrement;
    int8_t memshift;

    /* Delay line parameters */
    int32_t dlength;
    int32_t abase;
    int32_t bbase;
    int32_t dbase;

    /* Signal and control registers */
    int32_t sigreg;
    int mulshift;
    int8_t ccr;
    int8_t cmr;

    /* DOL - Data Output Latch (FIFO with 2 entries) */
    int32_t dol[2];
    int dol_count;

    /* Microcode program (160 x 48-bit instructions) */
    uint64_t instr[160];

    /* Delay RAM */
    int16_t *dram;

    /* Pipeline state */
    alu_t alu;
    mulacc_t mulacc;
    ram_t ram, ram_p, ram_pp;

} es5510_t;

/* Debug flag for R144 tracing */
extern int debug_r144;
extern int debug_skip;
extern int debug_alu;
extern int debug_ram;
extern int debug_ccr;

/* Function prototypes */
void es5510_init(es5510_t *dsp);
void es5510_free(es5510_t *dsp);
void es5510_reset(es5510_t *dsp);
void es5510_load_microcode(es5510_t *dsp, const uint8_t *data, int len);
void es5510_set_gpr(es5510_t *dsp, uint8_t reg, int32_t value);
int32_t es5510_get_gpr(es5510_t *dsp, uint8_t reg);
void es5510_set_serial_input(es5510_t *dsp, int16_t left, int16_t right);
void es5510_get_serial_output(es5510_t *dsp, int16_t *left, int16_t *right);
void es5510_run_sample(es5510_t *dsp);

#endif /* ES5510_STANDALONE_H */
