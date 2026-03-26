/*
 * 4 REVERBS Effect - GPR Initialization Table
 * Extracted from "4 REVERBS.efe" 68k init code
 *
 * These are the base delay tap addresses for each reverb mode.
 * Values are in ES5510 DRAM address format (24-bit, memincrement=0x100)
 */

#ifndef _4REVERBS_GPR_INIT_H
#define _4REVERBS_GPR_INIT_H

#include <stdint.h>

/* Delay tap GPR initialization values
 * These define the delay line tap positions for each reverb mode.
 * Format: GPR index -> base DRAM offset value
 */
static const struct {
    uint8_t  gpr;
    uint32_t value;
    const char *description;
} reverb_delay_gprs[] = {
    /* Mode A reverb taps (short reverb ~100-350ms) */
    {  5, 0x00119600, "Mode A: early reflection 1 (102ms)" },
    {  7, 0x00141900, "Mode A: early reflection 2 (117ms)" },
    {  9, 0x00185400, "Mode A: early reflection 3 (141ms)" },
    { 13, 0x0023bf00, "Mode A: diffusion 1 (208ms)" },
    { 15, 0x00261a00, "Mode A: diffusion 2 (221ms)" },
    { 17, 0x002b4e00, "Mode A: diffusion 3 (251ms)" },
    {  2, 0x0034b600, "Mode A: late reverb L (306ms)" },
    {  3, 0x0034b600, "Mode A: late reverb R (306ms)" },
    {  1, 0x003b8e00, "Mode A: feedback L (346ms)" },
    { 11, 0x003b8e00, "Mode A: feedback R (346ms)" },

    /* Mode B reverb taps (medium reverb ~470-720ms) */
    { 25, 0x00519500, "Mode B: early reflection 1 (474ms)" },
    { 27, 0x00541800, "Mode B: early reflection 2 (488ms)" },
    { 29, 0x00585300, "Mode B: early reflection 3 (513ms)" },
    { 33, 0x0063be00, "Mode B: diffusion 1 (579ms)" },
    { 35, 0x00661900, "Mode B: diffusion 2 (593ms)" },
    { 37, 0x006b4d00, "Mode B: diffusion 3 (623ms)" },
    { 22, 0x0074b500, "Mode B: late reverb L (677ms)" },
    { 23, 0x0074b500, "Mode B: late reverb R (677ms)" },
    { 21, 0x007b8d00, "Mode B: feedback L (717ms)" },
    { 31, 0x007b8d00, "Mode B: feedback R (717ms)" },

    /* Mode C reverb taps (long reverb ~850-1090ms) */
    { 45, 0x00919400, "Mode C: early reflection 1 (845ms)" },
    { 47, 0x00941700, "Mode C: early reflection 2 (860ms)" },
    { 49, 0x00985200, "Mode C: early reflection 3 (884ms)" },
    { 53, 0x00a3bd00, "Mode C: diffusion 1 (951ms)" },
    { 55, 0x00a61800, "Mode C: diffusion 2 (964ms)" },
    { 57, 0x00ab4c00, "Mode C: diffusion 3 (994ms)" },
    { 42, 0x00b4b400, "Mode C: late reverb L (1049ms)" },
    { 43, 0x00b4b400, "Mode C: late reverb R (1049ms)" },
    { 41, 0x00bb8c00, "Mode C: feedback L (1089ms)" },
    { 51, 0x00bb8c00, "Mode C: feedback R (1089ms)" },

    /* Mode D reverb taps (extra long reverb ~1220-1460ms) */
    { 65, 0x00d19300, "Mode D: early reflection 1 (1217ms)" },
    { 67, 0x00d41600, "Mode D: early reflection 2 (1231ms)" },
    { 69, 0x00d85100, "Mode D: early reflection 3 (1256ms)" },
    { 73, 0x00e3bc00, "Mode D: diffusion 1 (1322ms)" },
    { 75, 0x00e61700, "Mode D: diffusion 2 (1336ms)" },
    { 77, 0x00eb4b00, "Mode D: diffusion 3 (1366ms)" },
    { 62, 0x00f4b300, "Mode D: late reverb L (1420ms)" },
    { 63, 0x00f4b300, "Mode D: late reverb R (1420ms)" },
    { 61, 0x00fb8b00, "Mode D: feedback L (1460ms)" },
    { 71, 0x00fb8b00, "Mode D: feedback R (1460ms)" },
};

#define NUM_DELAY_GPRS (sizeof(reverb_delay_gprs) / sizeof(reverb_delay_gprs[0]))

/* Coefficient GPRs (direct writes, values depend on parameters)
 * These are written with fixed or computed coefficient values.
 * GPR 121, 145-146, 158-159, 171-172, 184-185
 */
static const uint8_t reverb_coeff_gprs[] = {
    121,  /* Possibly dry/wet mix or master gain */
    145,  /* Mode A coefficients */
    146,
    158,  /* Mode B coefficients */
    159,
    171,  /* Mode C coefficients */
    172,
    184,  /* Mode D coefficients */
    185,
};

#define NUM_COEFF_GPRS (sizeof(reverb_coeff_gprs) / sizeof(reverb_coeff_gprs[0]))

/* Initialize delay GPRs with default values (Mode A selected) */
static inline void init_reverb_gprs(int32_t *gpr) {
    for (size_t i = 0; i < NUM_DELAY_GPRS; i++) {
        gpr[reverb_delay_gprs[i].gpr] = reverb_delay_gprs[i].value;
    }

    /* Default coefficient values (need verification from real hardware) */
    gpr[121] = 0x400000;  /* 0.5 gain */

    /* Mode selector coefficients - set all to enable Mode A only */
    gpr[145] = 0x7FFFFF;  /* Mode A enabled (max) */
    gpr[146] = 0x7FFFFF;
    gpr[158] = 0x000000;  /* Mode B disabled */
    gpr[159] = 0x000000;
    gpr[171] = 0x000000;  /* Mode C disabled */
    gpr[172] = 0x000000;
    gpr[184] = 0x000000;  /* Mode D disabled */
    gpr[185] = 0x000000;
}

#endif /* _4REVERBS_GPR_INIT_H */
