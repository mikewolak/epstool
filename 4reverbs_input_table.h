/*
 * 4 REVERBS Effect - Input Variable Table
 * 
 * This defines the GPR registers that need external initialization
 * based on reverse engineering of the Waveboy 4 REVERBS effect.
 * 
 * File: factory_sounds/WAVEBOY_EFE/4 REVERBS.efe
 * Microcode offset: 5698 (0x1642), 960 bytes
 */

#ifndef REVERBS_INPUT_TABLE_H
#define REVERBS_INPUT_TABLE_H

#include <stdint.h>

/*
 * DELAY TAP ADDRESSES
 * From file offset 0x276, indices 2-8 only (0-1 are data registers)
 * Values are 16-bit sample counts, multiply by memincrement (0x100) for GPR value
 */
typedef struct {
    uint16_t samples;    // Delay in samples
    uint8_t  gpr_index;  // Which GPR this goes to
    const char* purpose; // Description
} delay_tap_t;

static const delay_tap_t DELAY_WRITE_TAPS[] = {
    { 230, 2, "Delay write tap 1" },
    { 250, 3, "Delay write tap 2" },
    { 334, 4, "Delay write tap 3" },
    { 348, 5, "Delay write tap 4" },
};
#define NUM_DELAY_WRITE_TAPS 4

static const delay_tap_t DELAY_READ_TAPS[] = {
    /* Delay read GPRs - at PC=N, GPR[N] provides the delay RAM offset */
    /* IMPORTANT: GPRs that are "computed" by MAC still need INITIAL values */
    /* because the MAC uses the current GPR value as input (R = R * coef) */

    /* Short reflections - these GPRs are multiplied by mode coefficients */
    { 268,  90, "Early reflection 1" },  /* MAC at PC 4: R90 = R90 * R86 */
    { 300,  92, "Early reflection 2" },  /* MAC at PC 30: R92 = ... */
    { 288,  93, "Early reflection 3" },  /* MAC at PC 25: R93 = ... */
    { 192, 102, "Late reflection 1" },   /* Not computed, preset only */

    /* Feedback taps - computed at earlier PCs but need initial values */
    { 212, 131, "Late reflection 2" },   /* MAC at PC 22 */
    { 230, 132, "Feedback tap 1" },      /* MAC at PC 20 */
    { 250, 133, "Feedback tap 2" },      /* MAC at PC 42 */
    { 334, 134, "Feedback tap 3" },      /* MAC at PC 40 */
    { 348, 135, "Diffusion tap 1" },     /* MAC at PC 62 */
    { 268, 136, "Diffusion tap 2" },     /* MAC at PC 60 */
    { 300, 137, "Feedback loop 1" },     /* MAC at PC 82 */
    { 288, 138, "Feedback loop 2" },     /* MAC at PC 80 */

    /* Long delays - not computed, need preset (from TABLE_A) */
    { 1076, 139, "Long delay 1" },
    { 1096, 140, "Long delay 2" },
    { 1114, 141, "Long delay 3" },
    { 1134, 142, "Long delay 4" },
    { 1218, 143, "Long delay 5" },
    { 1232, 144, "Long delay 6" },
    { 1152, 145, "Long delay 7" },
    { 192, 146, "Final reflection" },
};
#define NUM_DELAY_READ_TAPS 20

/*
 * COEFFICIENT REGISTERS
 * These control the reverb characteristics
 * Values are Q23 format (-1.0 to +1.0 maps to -0x800000 to +0x7FFFFF)
 */
typedef struct {
    uint8_t  gpr_index;
    int32_t  default_value;  // Q23 format
    float    default_float;  // Same as float for reference
    const char* name;
    const char* parameter;   // User-editable parameter that affects this
} coefficient_t;

static const coefficient_t COEFFICIENTS[] = {
    /* Primary coefficients (from microcode analysis) */
    /* NOTE: GPR indices must NOT conflict with delay read PCs! */
    /* PC 90, 92, 93, 102, 131-146 are delay reads and use GPR[PC] as offset */

    { 69,  0x200000,  0.25f,  "input_gain",    "REV VOL" },
    { 117, 0x600000,  0.75f,  "decay",         "REV DECAY TIME" },
    { 121, 0x600000,  0.75f,  "decay_r",       "REV DECAY TIME" },  /* Right channel decay */
    { 95,  0x400000,  0.50f,  "allpass_1",     "REV DIFFUSION" },
    { 103, 0x300000,  0.375f, "diffusion_1",   "REV DIFFUSION" },
    { 109, 0x400000,  0.50f,  "allpass_3",     "REV DIFFUSION" },
    { 110, 0x300000,  0.375f, "diffusion_2",   "REV DIFFUSION" },

    /* MAC operand coefficients (from microcode: R113, R181, R182 are used as multipliers) */
    { 113, 0x400000,  0.50f,  "reflection_coef_1", "REV DIFFUSION" },  /* Used at PC 90 */
    { 181, 0x400000,  0.50f,  "reflection_coef_2", "REV DIFFUSION" },  /* Used at PC 102 */
    { 182, 0x400000,  0.50f,  "reflection_coef_3", "REV DIFFUSION" },  /* Used at PC 92 */

    /* Feedback coefficients - at non-conflicting GPRs */
    { 83, (int16_t)0x93ff << 8, -0.8438f, "feedback_1", "REV DECAY TIME" },

    /* HF damping (one-pole lowpass) */
    { 116, 0x200000,  0.25f,  "hf_damp_coef",  "REV HF DAMPING" },

    /* Output mix - CRITICAL: these are never written by microcode! */
    /* They must be set externally to get any output */
    { 123, 0x740000,  0.906f, "wet_mix_left",  "REV VOL" },
    { 187, 0x740000,  0.906f, "wet_mix_right", "REV VOL" },
};
#define NUM_COEFFICIENTS 14

/*
 * MODE CONTROL REGISTERS
 */
typedef struct {
    uint8_t  gpr_index;
    int32_t  value;
    const char* name;
} mode_reg_t;

static const mode_reg_t MODE_REGISTERS[] = {
    { 144, 0x000000, "mode_select" },   // Compared against R85,R86,R87 for mode A/B/C/D
    { 145, 0x000000, "mode_increment" },
    { 146, 0x000000, "mode_control" },
    { 152, 0x000000, "accumulator_init" },
};
#define NUM_MODE_REGISTERS 4

/*
 * SPECIAL REGISTERS (read-only or ES5510 controlled)
 */
#define GPR_SER0L  235  // ADC input left
#define GPR_SER0R  236  // ADC input right  
#define GPR_SER3R  240  // DAC output right
#define GPR_SER3L  241  // DAC output left
#define GPR_MACL   242  // MAC accumulator low
#define GPR_MACH   243  // MAC accumulator high
#define GPR_CCR    250  // Condition code register
#define GPR_CMR    251  // Condition mask register
#define GPR_MAX    254  // Constant +1.0 (0x7FFFFF)
#define GPR_ZERO   255  // Constant 0

/*
 * Initialize effect GPRs with default values
 */
static inline void reverbs_init_gprs(int32_t* gpr, uint32_t memincrement) {
    /* Clear all user GPRs (0-191) */
    for (int i = 0; i < 192; i++) gpr[i] = 0;

    /* Load delay write taps - these are WRITE addresses */
    for (int i = 0; i < NUM_DELAY_WRITE_TAPS; i++) {
        gpr[DELAY_WRITE_TAPS[i].gpr_index] =
            DELAY_WRITE_TAPS[i].samples * memincrement;
    }

    /* Load delay read taps - these are READ addresses */
    for (int i = 0; i < NUM_DELAY_READ_TAPS; i++) {
        gpr[DELAY_READ_TAPS[i].gpr_index] =
            DELAY_READ_TAPS[i].samples * memincrement;
    }

    /* Load coefficients */
    for (int i = 0; i < NUM_COEFFICIENTS; i++) {
        gpr[COEFFICIENTS[i].gpr_index] = COEFFICIENTS[i].default_value;
    }

    /* Load mode registers */
    for (int i = 0; i < NUM_MODE_REGISTERS; i++) {
        gpr[MODE_REGISTERS[i].gpr_index] = MODE_REGISTERS[i].value;
    }

    /* Special constants (R252-255) are handled internally by read_reg():
     * R252 = -1, R253 = MIN, R254 = MAX (+1.0), R255 = ZERO
     * Do NOT write to gpr[192+] - that overflows the 192-element array!
     */
}

#endif /* REVERBS_INPUT_TABLE_H */
