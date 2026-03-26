/*
 * 4 REVERBS Effect - Input Variable Table
 *
 * This defines the GPR registers that need external initialization
 * based on the Waveboy 4 REVERBS effect file (Mode A).
 *
 * File: factory_sounds/WAVEBOY_EFE/4 REVERBS.efe
 * Delay tap table extracted from offset 0x270
 */

#ifndef REVERBS_INPUT_TABLE_H
#define REVERBS_INPUT_TABLE_H

#include <stdint.h>

/*
 * DELAY TAP ADDRESSES (from effect file offset 0x270)
 *
 * GPR[PC] provides the delay offset for RAM operations at that PC.
 * Values are sample counts - multiply by memincrement (0x100) for GPR value.
 *
 * The disassembly shows:
 *   PC 2-5: WDL (Write Delay Line) - writes input to delay buffer
 *   PC 6-27: RTA/RTB (Read Table A/B) - reads delayed samples
 *   PC 55+: RDL (Read Delay Line) - reads from circular buffer
 */
static const uint16_t DELAY_TAPS_MODE_A[] = {
    /* GPR 0-31: Delay tap values from effect file */
       0,     /* GPR 0: unused */
       0,     /* GPR 1: unused */
    3728,     /* GPR 2: WDL - main delay write (95ms) */
     192,     /* GPR 3: WDL - early reflection write 1 (5ms) */
     212,     /* GPR 4: WDL - early reflection write 2 (5.4ms) */
     230,     /* GPR 5: WDL - early reflection write 3 (5.9ms) */
     250,     /* GPR 6: RTA - read tap 1 (6.4ms) */
     334,     /* GPR 7: RTA - read tap 2 (8.5ms) */
     348,     /* GPR 8: RTA - read tap 3 (8.9ms) */
     268,     /* GPR 9: RTA - read tap 4 (6.8ms) */
     300,     /* GPR 10: RTA - read tap 5 (7.7ms) */
     288,     /* GPR 11: RTA - read tap 6 (7.4ms) */
    1076,     /* GPR 12: RTA - medium delay 1 (27.5ms) */
    1096,     /* GPR 13: RTA - medium delay 2 (28ms) */
    1114,     /* GPR 14: RTA - medium delay 3 (28.4ms) */
    1134,     /* GPR 15: RTA - medium delay 4 (29ms) */
    1218,     /* GPR 16: RTA - medium delay 5 (31.1ms) */
    1232,     /* GPR 17: RTA - medium delay 6 (31.5ms) */
    1152,     /* GPR 18: RTA - medium delay 7 (29.4ms) */
    1184,     /* GPR 19: RTA - medium delay 8 (30.2ms) */
    1172,     /* GPR 20: RTA - medium delay 9 (29.9ms) */
    1960,     /* GPR 21: RTA - late reflection 1 (50.1ms) */
    1980,     /* GPR 22: RTA - late reflection 2 (50.6ms) */
    1998,     /* GPR 23: RTA - late reflection 3 (51ms) */
    2018,     /* GPR 24: RTA - late reflection 4 (51.5ms) */
    2102,     /* GPR 25: RTA - late reflection 5 (53.7ms) */
    2116,     /* GPR 26: RTA - late reflection 6 (54ms) */
    2036,     /* GPR 27: RTA - late reflection 7 (52ms) */
    2068,     /* GPR 28: RTB - late reflection 8 (52.8ms) */
    2056,     /* GPR 29: RTB - late reflection 9 (52.5ms) */
    2844,     /* GPR 30: RTB - long delay 1 (72.6ms) */
    2864,     /* GPR 31: RTB - long delay 2 (73.1ms) */
};
#define NUM_DELAY_TAPS 32

/*
 * COEFFICIENT REGISTERS
 * These control the reverb characteristics
 * Values are Q23 format (-1.0 to +1.0 maps to -0x800000 to +0x7FFFFF)
 */
typedef struct {
    uint8_t  gpr_index;
    int32_t  default_value;  /* Q23 format */
    const char* name;
} coefficient_t;

static const coefficient_t COEFFICIENTS[] = {
    /* Input/output gain */
    { 69,  0x200000, "input_gain" },       /* 0.25 */

    /* Decay coefficients - R117 is main decay, used heavily */
    { 117, 0x600000, "decay_main" },       /* 0.75 */
    { 121, 0x600000, "decay_secondary" },  /* 0.75 */

    /* Feedback/damping */
    { 113, 0x400000, "damping_1" },        /* 0.5 */
    { 114, 0x400000, "damping_2" },        /* 0.5 */
    { 115, 0x200000, "hf_damp" },          /* 0.25 */
    { 116, 0x200000, "hf_damp_coef" },     /* 0.25 */

    /* Wet/dry mix - CRITICAL for output! */
    { 123, 0x600000, "wet_mix_l" },        /* 0.75 wet mix left */
    { 187, 0x600000, "wet_mix_r" },        /* 0.75 wet mix right */

    /* Mode control coefficients (used in CMP operations) */
    { 81,  0x200000, "mode_coef_1" },
    { 82,  0x200000, "mode_coef_2" },
    { 83,  0x200000, "mode_coef_3" },
    { 84,  0x200000, "mode_coef_4" },

    /* Allpass/diffusion */
    { 85,  0x400000, "allpass_1" },
    { 86,  0x400000, "allpass_2" },
    { 87,  0x400000, "allpass_3" },
    { 88,  0x400000, "allpass_4" },
    { 89,  0x400000, "allpass_5" },

    /* Internal mixing coefficients */
    { 90,  0x300000, "mix_1" },
    { 91,  0x300000, "mix_2" },
    { 92,  0x300000, "mix_3" },
    { 93,  0x300000, "mix_4" },
    { 94,  0x300000, "mix_5" },

    /* Feedback network */
    { 95,  0x400000, "feedback_1" },
    { 96,  0x400000, "feedback_2" },
    { 97,  0x400000, "feedback_3" },
    { 98,  0x400000, "feedback_4" },
    { 99,  0x400000, "feedback_5" },
    { 100, 0x400000, "feedback_6" },
    { 101, 0x400000, "feedback_7" },
    { 102, 0x400000, "feedback_8" },
    { 103, 0x400000, "feedback_9" },
    { 104, 0x400000, "feedback_10" },
    { 105, 0x400000, "feedback_11" },
    { 106, 0x400000, "feedback_12" },
    { 107, 0x400000, "feedback_13" },
    { 108, 0x400000, "feedback_14" },
    { 109, 0x400000, "late_mix_1" },
    { 110, 0x400000, "late_mix_2" },
    { 111, 0x400000, "late_mix_3" },
    { 112, 0x400000, "late_mix_4" },

    /* Additional mixing for feedback loops */
    { 131, 0x300000, "loop_mix_1" },
    { 132, 0x300000, "loop_mix_2" },
    { 133, 0x300000, "loop_mix_3" },
    { 134, 0x300000, "loop_mix_4" },
    { 135, 0x300000, "loop_mix_5" },
    { 136, 0x300000, "loop_mix_6" },
    { 137, 0x300000, "loop_mix_7" },
    { 138, 0x300000, "loop_mix_8" },

    /* Long delay mixing */
    { 139, 0x200000, "long_mix_1" },
    { 140, 0x200000, "long_mix_2" },
    { 141, 0x200000, "long_mix_3" },
    { 142, 0x200000, "long_mix_4" },

    /* Control/mode registers */
    { 144, 0x000000, "mode_select" },
    { 145, 0x000000, "mode_accumulator" },
    { 146, 0x000000, "mode_temp_1" },
    { 147, 0x000000, "mode_temp_2" },
    { 148, 0x000000, "mode_temp_3" },
    { 149, 0x000000, "accumulator" },
    { 152, 0x000000, "input_accumulator" },
};
#define NUM_COEFFICIENTS (sizeof(COEFFICIENTS) / sizeof(COEFFICIENTS[0]))

/*
 * SPECIAL REGISTERS (read-only or ES5510 controlled)
 */
#define GPR_SER0L  235  /* ADC input left */
#define GPR_SER0R  236  /* ADC input right */
#define GPR_SER3R  240  /* DAC output right */
#define GPR_SER3L  241  /* DAC output left */
#define GPR_MACL   242  /* MAC accumulator low */
#define GPR_MACH   243  /* MAC accumulator high */
#define GPR_CCR    250  /* Condition code register */
#define GPR_CMR    251  /* Condition mask register */
#define GPR_MAX    254  /* Constant +1.0 (0x7FFFFF) */
#define GPR_ZERO   255  /* Constant 0 */

/*
 * Initialize effect GPRs with default values
 */
static inline void reverbs_init_gprs(int32_t* gpr, uint32_t memincrement) {
    /* Clear all user GPRs (0-191) */
    for (int i = 0; i < 192; i++) gpr[i] = 0;

    /* Load delay taps from effect file (GPRs 0-31) */
    /* These are used by RTA/RDL/WDL operations: address = base + GPR[PC] */
    for (int i = 0; i < NUM_DELAY_TAPS; i++) {
        gpr[i] = DELAY_TAPS_MODE_A[i] * memincrement;
    }

    /* Load coefficients */
    for (size_t i = 0; i < NUM_COEFFICIENTS; i++) {
        gpr[COEFFICIENTS[i].gpr_index] = COEFFICIENTS[i].default_value;
    }
}

#endif /* REVERBS_INPUT_TABLE_H */
