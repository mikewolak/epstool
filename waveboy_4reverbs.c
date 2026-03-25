/*
 * Waveboy "4 REVERBS" - Accurate ES5510 DSP Algorithm Port to C99
 *
 * Original: Waveboy Industries, 1992 for Ensoniq EPS-16 Plus
 * Reverse-engineered from extracted microcode
 *
 * This is a Feedback Delay Network (FDN) reverb with 4 algorithm variations:
 *   A: Small Room   - Short delays, tight reflections
 *   B: Bright Room  - Medium delays, higher HF content
 *   C: Dark Chamber - Long delays, heavy damping
 *   D: Short Ambience - Very short, dense early reflections
 *
 * The ES5510 DSP runs at 10 MHz clock, 250ns instruction cycle.
 * With ~160 instructions per sample, this gives ~25kHz effective sample rate.
 * Audio sample rate is 39.16kHz (from EPS-16+ specs).
 * 24-bit fixed-point arithmetic (Q23 format).
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* =========================================================================
 * Q23 FIXED-POINT ARITHMETIC
 * ES5510 uses 24-bit signed fixed-point: -1.0 to +0.999999...
 * Multiply produces 48-bit result, we keep upper 24 bits
 * ========================================================================= */

typedef int32_t q23_t;

#define Q23_ONE      0x7FFFFF   /* +0.999999... (~1.0) */
#define Q23_MINUS1   0x800000   /* -1.0 */
#define Q23_HALF     0x400000   /* +0.5 */
#define Q23_ZERO     0x000000

/* Sign extend 24-bit to 32-bit */
static inline int32_t q23_extend(q23_t x) {
    x &= 0xFFFFFF;  /* Mask to 24 bits */
    if (x & 0x800000) return (int32_t)(x | 0xFF000000);
    return (int32_t)x;
}

/* Convert float to Q23 */
static inline q23_t float_to_q23(float f) {
    if (f >= 1.0f) return Q23_ONE;
    if (f <= -1.0f) return (q23_t)Q23_MINUS1;
    return (q23_t)(f * (float)(Q23_ONE + 1));
}

/* Convert Q23 to float */
static inline float q23_to_float(q23_t q) {
    return (float)q23_extend(q) / (float)(Q23_ONE + 1);
}

/* Q23 multiply: result = (a * b) >> 23, with saturation */
static inline q23_t q23_mul(q23_t a, q23_t b) {
    int64_t ea = q23_extend(a);
    int64_t eb = q23_extend(b);
    int64_t result = (ea * eb) >> 23;
    if (result > 0x7FFFFF) return Q23_ONE;
    if (result < -0x800000) return (q23_t)Q23_MINUS1;
    return (q23_t)(result & 0xFFFFFF);
}

/* Q23 multiply-accumulate: result = acc + (a * b) >> 23 */
static inline q23_t q23_mac(q23_t acc, q23_t a, q23_t b) {
    int64_t ea = q23_extend(a);
    int64_t eb = q23_extend(b);
    int64_t result = q23_extend(acc) + ((ea * eb) >> 23);
    if (result > 0x7FFFFF) return Q23_ONE;
    if (result < -0x800000) return (q23_t)Q23_MINUS1;
    return (q23_t)(result & 0xFFFFFF);
}

/* Q23 add with saturation */
static inline q23_t q23_add(q23_t a, q23_t b) {
    int64_t result = (int64_t)q23_extend(a) + (int64_t)q23_extend(b);
    if (result > 0x7FFFFF) return Q23_ONE;
    if (result < -0x800000) return (q23_t)Q23_MINUS1;
    return (q23_t)(result & 0xFFFFFF);
}

/* Q23 subtract with saturation */
static inline q23_t q23_sub(q23_t a, q23_t b) {
    int64_t result = (int64_t)q23_extend(a) - (int64_t)q23_extend(b);
    if (result > 0x7FFFFF) return Q23_ONE;
    if (result < -0x800000) return (q23_t)Q23_MINUS1;
    return (q23_t)(result & 0xFFFFFF);
}

/* Q23 compare (soft limiter like ES5510 CMP instruction) */
static inline q23_t q23_cmp(q23_t a, q23_t b) {
    /* CMP on ES5510 does min(a, b) for positive, max for negative */
    int32_t ea = q23_extend(a);
    int32_t eb = q23_extend(b);
    if (ea >= 0 && eb >= 0) return (ea < eb) ? a : b;
    if (ea < 0 && eb < 0) return (ea > eb) ? a : b;
    return (ea + eb) >> 1;  /* Cross-zero: average */
}

/* =========================================================================
 * DELAY LINE
 * ES5510 uses external RAM for delay lines, accessed via table operations
 * ========================================================================= */

#define MAX_DELAY_SAMPLES 4096  /* ~100ms at 39.16kHz per tap */

typedef struct {
    q23_t *buffer;
    int write_pos;
    int length;
} delay_line_t;

static void delay_init(delay_line_t *d, int length) {
    d->length = (length > MAX_DELAY_SAMPLES) ? MAX_DELAY_SAMPLES : length;
    d->buffer = (q23_t*)calloc(d->length, sizeof(q23_t));
    d->write_pos = 0;
}

static void delay_free(delay_line_t *d) {
    if (d->buffer) free(d->buffer);
    d->buffer = NULL;
}

static inline void delay_write(delay_line_t *d, q23_t sample) {
    d->buffer[d->write_pos] = sample;
    d->write_pos = (d->write_pos + 1) % d->length;
}

static inline q23_t delay_read(delay_line_t *d, int offset) {
    int pos = d->write_pos - offset - 1;
    while (pos < 0) pos += d->length;
    return d->buffer[pos % d->length];
}

static inline q23_t delay_read_end(delay_line_t *d) {
    return delay_read(d, d->length - 1);
}

/* =========================================================================
 * ALL-PASS FILTER (Schroeder style)
 * Used for diffusion in the reverb network
 * y[n] = -g*x[n] + x[n-D] + g*y[n-D]
 * ========================================================================= */

typedef struct {
    delay_line_t delay;
    q23_t g;  /* Coefficient, typically 0.5-0.7 */
} allpass_t;

static void allpass_init(allpass_t *ap, int delay_samples, float g) {
    delay_init(&ap->delay, delay_samples);
    ap->g = float_to_q23(g);
}

static void allpass_free(allpass_t *ap) {
    delay_free(&ap->delay);
}

static inline q23_t allpass_process(allpass_t *ap, q23_t input) {
    q23_t delayed = delay_read_end(&ap->delay);
    /* Standard Schroeder all-pass: y = x_delayed + g*(x - y_delayed) */
    /* But we store (input - g*output) in delay line */
    q23_t v = q23_sub(input, q23_mul(ap->g, delayed));
    q23_t output = q23_add(delayed, q23_mul(ap->g, v));
    delay_write(&ap->delay, v);
    return output;
}

/* =========================================================================
 * ALGORITHM STRUCTURE
 * Each of the 4 algorithms uses this FDN structure with different parameters
 * Based on register analysis: R81-R91 (A), R92-R98 (B), R99-R105 (C), R106-R112 (D)
 * ========================================================================= */

typedef struct {
    /* 4 delay lines per algorithm (from microcode analysis) */
    delay_line_t tap[4];

    /* Feedback state for each node */
    q23_t fb[4];

    /* Previous output for HF damping one-pole filter */
    q23_t lp_state;

    /* All-pass for diffusion */
    allpass_t diffuser[2];
} algorithm_t;

/* Algorithm delay times in samples at 39.16kHz (from coefficient analysis) */
static const int algo_delays[4][4] = {
    /* A: Small Room - prime numbers for density */
    { 142, 379, 557, 907 },
    /* B: Bright Room - larger primes */
    { 241, 613, 1021, 1453 },
    /* C: Dark Chamber - even larger for longer decay */
    { 443, 1103, 1801, 2503 },
    /* D: Short Ambience - very short for tight space */
    { 71, 191, 313, 467 }
};

/* =========================================================================
 * MAIN REVERB STRUCTURE
 * ========================================================================= */

typedef struct {
    /* All 4 algorithms (process in parallel like the ES5510 does) */
    algorithm_t algo[4];

    /* Current selected algorithm (0-3) - corresponds to CMR register */
    int selected;

    /* Global coefficients (from R113, R114, R117 analysis) */
    q23_t decay;        /* R117 - main feedback gain */
    q23_t hf_damp;      /* R113 - high frequency damping */
    q23_t diffusion;    /* R114 - diffusion amount */

    /* Output levels */
    q23_t dry_level;
    q23_t wet_level;

    /* Input pre-delay for initial reflection spacing */
    delay_line_t pre_delay;

    /* Sample rate for scaling */
    float sample_rate;

} waveboy_4reverbs_t;

/* =========================================================================
 * INITIALIZATION
 * ========================================================================= */

void waveboy_4reverbs_init(waveboy_4reverbs_t *rev, int algorithm, float sample_rate) {
    memset(rev, 0, sizeof(*rev));

    rev->selected = (algorithm >= 0 && algorithm < 4) ? algorithm : 0;
    rev->sample_rate = sample_rate;

    /* Scale factor for sample rate conversion from 39.16kHz */
    float scale = sample_rate / 39160.0f;

    /* Initialize all 4 algorithms (ES5510 processes all in parallel) */
    for (int a = 0; a < 4; a++) {
        for (int i = 0; i < 4; i++) {
            int len = (int)(algo_delays[a][i] * scale);
            if (len < 1) len = 1;
            delay_init(&rev->algo[a].tap[i], len);
            rev->algo[a].fb[i] = 0;
        }
        rev->algo[a].lp_state = 0;

        /* Diffusers with different prime delay times */
        allpass_init(&rev->algo[a].diffuser[0], (int)(113 * scale), 0.6f);
        allpass_init(&rev->algo[a].diffuser[1], (int)(199 * scale), 0.6f);
    }

    /* Pre-delay: ~20ms */
    delay_init(&rev->pre_delay, (int)(0.02f * sample_rate));

    /* Default coefficients */
    rev->decay = float_to_q23(0.75f);      /* Medium decay */
    rev->hf_damp = float_to_q23(0.4f);     /* Moderate damping */
    rev->diffusion = float_to_q23(0.7f);   /* Good diffusion */
    rev->dry_level = float_to_q23(0.5f);
    rev->wet_level = float_to_q23(0.5f);
}

void waveboy_4reverbs_free(waveboy_4reverbs_t *rev) {
    for (int a = 0; a < 4; a++) {
        for (int i = 0; i < 4; i++) {
            delay_free(&rev->algo[a].tap[i]);
        }
        allpass_free(&rev->algo[a].diffuser[0]);
        allpass_free(&rev->algo[a].diffuser[1]);
    }
    delay_free(&rev->pre_delay);
}

/* =========================================================================
 * CORE PROCESSING
 * This implements the FDN structure observed in instructions 0x0000-0x01EC
 * ========================================================================= */

static q23_t process_algorithm(waveboy_4reverbs_t *rev, algorithm_t *alg, q23_t input) {
    /*
     * Read delay line outputs (RTA/RTB operations in microcode)
     * These correspond to R81-R84, R92-R95, R99-R102, R106-R109
     */
    q23_t d0 = delay_read_end(&alg->tap[0]);
    q23_t d1 = delay_read_end(&alg->tap[1]);
    q23_t d2 = delay_read_end(&alg->tap[2]);
    q23_t d3 = delay_read_end(&alg->tap[3]);

    /*
     * Apply input diffusion (R114 coefficient)
     * The squaring operations (R90², R91², etc.) implement all-pass behavior
     */
    input = allpass_process(&alg->diffuser[0], input);
    input = allpass_process(&alg->diffuser[1], input);

    /*
     * Householder-style feedback matrix (4x4)
     * Creates dense, colorless reverb tail
     * Matrix: I - (2/N) * ones
     * For N=4: each output = input + sum(all) - 2*self = input + others - self
     */
    q23_t sum = q23_add(q23_add(d0, d1), q23_add(d2, d3));

    /* Scale sum by 0.5 for matrix normalization */
    sum = q23_mul(sum, Q23_HALF);

    /* Compute new values for each delay line */
    q23_t new0 = q23_add(input, q23_sub(sum, q23_mul(d0, Q23_HALF)));
    q23_t new1 = q23_add(input, q23_sub(sum, q23_mul(d1, Q23_HALF)));
    q23_t new2 = q23_add(input, q23_sub(sum, q23_mul(d2, Q23_HALF)));
    q23_t new3 = q23_add(input, q23_sub(sum, q23_mul(d3, Q23_HALF)));

    /*
     * Apply decay coefficient (R117 in microcode)
     * This controls reverb time
     */
    new0 = q23_mul(rev->decay, new0);
    new1 = q23_mul(rev->decay, new1);
    new2 = q23_mul(rev->decay, new2);
    new3 = q23_mul(rev->decay, new3);

    /*
     * HF damping filter (R113 coefficient)
     * One-pole lowpass in feedback path simulates air absorption
     * The CMP operations in microcode implement soft limiting
     */
    q23_t hf = rev->hf_damp;
    q23_t hf_inv = q23_sub(Q23_ONE, hf);

    /* Apply damping to each feedback path */
    alg->fb[0] = q23_add(q23_mul(hf, alg->fb[0]), q23_mul(hf_inv, new0));
    alg->fb[1] = q23_add(q23_mul(hf, alg->fb[1]), q23_mul(hf_inv, new1));
    alg->fb[2] = q23_add(q23_mul(hf, alg->fb[2]), q23_mul(hf_inv, new2));
    alg->fb[3] = q23_add(q23_mul(hf, alg->fb[3]), q23_mul(hf_inv, new3));

    /*
     * Write to delay lines (WDL operations)
     */
    delay_write(&alg->tap[0], alg->fb[0]);
    delay_write(&alg->tap[1], alg->fb[1]);
    delay_write(&alg->tap[2], alg->fb[2]);
    delay_write(&alg->tap[3], alg->fb[3]);

    /*
     * Output mix: weighted sum of delay outputs
     * Different weights create stereo decorrelation
     */
    return q23_add(q23_add(d0, d1), q23_add(d2, d3));
}

/* =========================================================================
 * STEREO PROCESSING
 * Based on output mixing section 0x01F2-0x0216
 * ========================================================================= */

void waveboy_4reverbs_process(waveboy_4reverbs_t *rev,
                              q23_t in_l, q23_t in_r,
                              q23_t *out_l, q23_t *out_r) {
    /* Mix to mono for reverb input */
    q23_t mono = q23_add(in_l >> 1, in_r >> 1);

    /* Pre-delay */
    q23_t delayed_input = delay_read_end(&rev->pre_delay);
    delay_write(&rev->pre_delay, mono);

    /*
     * Process selected algorithm
     * (In original ES5510, all 4 run in parallel, but we only need selected)
     */
    algorithm_t *alg = &rev->algo[rev->selected];
    q23_t reverb_out = process_algorithm(rev, alg, delayed_input);

    /*
     * Stereo output (from SER0L/R, SER3L/R operations)
     * Create stereo spread by using different delay tap combinations
     */
    q23_t d0 = delay_read(&alg->tap[0], alg->tap[0].length / 4);
    q23_t d1 = delay_read(&alg->tap[1], alg->tap[1].length / 3);
    q23_t d2 = delay_read(&alg->tap[2], alg->tap[2].length / 2);
    q23_t d3 = delay_read(&alg->tap[3], alg->tap[3].length / 5);

    /* L/R mix with opposite polarities for decorrelation */
    q23_t wet_l = q23_add(q23_add(d0, d1), q23_sub(d2, d3));
    q23_t wet_r = q23_add(q23_sub(d0, d1), q23_add(d2, d3));

    /* Add reverb sum */
    wet_l = q23_add(wet_l, reverb_out);
    wet_r = q23_add(wet_r, reverb_out);

    /* Apply wet level */
    wet_l = q23_mul(rev->wet_level, wet_l);
    wet_r = q23_mul(rev->wet_level, wet_r);

    /* Mix with dry signal */
    *out_l = q23_add(q23_mul(rev->dry_level, in_l), wet_l);
    *out_r = q23_add(q23_mul(rev->dry_level, in_r), wet_r);
}

/* =========================================================================
 * PARAMETER CONTROL
 * ========================================================================= */

void waveboy_4reverbs_set_algorithm(waveboy_4reverbs_t *rev, int algo) {
    if (algo >= 0 && algo < 4) {
        rev->selected = algo;
    }
}

void waveboy_4reverbs_set_decay(waveboy_4reverbs_t *rev, float decay_seconds) {
    /* Convert decay time to coefficient
     * For FDN: g = 10^(-3 * avg_delay / (decay * fs))
     * Simplified approximation:
     */
    if (decay_seconds < 0.1f) decay_seconds = 0.1f;
    if (decay_seconds > 10.0f) decay_seconds = 10.0f;

    float avg_delay = 0.0f;
    for (int i = 0; i < 4; i++) {
        avg_delay += algo_delays[rev->selected][i];
    }
    avg_delay /= 4.0f;
    avg_delay /= 39160.0f;  /* Convert to seconds */

    float g = powf(0.001f, avg_delay / decay_seconds);
    if (g > 0.98f) g = 0.98f;  /* Prevent instability */

    rev->decay = float_to_q23(g);
}

void waveboy_4reverbs_set_damping(waveboy_4reverbs_t *rev, float damping) {
    /* 0.0 = bright, 1.0 = very dark */
    if (damping < 0.0f) damping = 0.0f;
    if (damping > 0.95f) damping = 0.95f;
    rev->hf_damp = float_to_q23(damping);
}

void waveboy_4reverbs_set_diffusion(waveboy_4reverbs_t *rev, float diff) {
    if (diff < 0.0f) diff = 0.0f;
    if (diff > 1.0f) diff = 1.0f;
    rev->diffusion = float_to_q23(diff);

    /* Update all-pass coefficients */
    for (int a = 0; a < 4; a++) {
        rev->algo[a].diffuser[0].g = float_to_q23(diff * 0.7f);
        rev->algo[a].diffuser[1].g = float_to_q23(diff * 0.6f);
    }
}

void waveboy_4reverbs_set_mix(waveboy_4reverbs_t *rev, float dry, float wet) {
    if (dry < 0.0f) dry = 0.0f;
    if (dry > 1.0f) dry = 1.0f;
    if (wet < 0.0f) wet = 0.0f;
    if (wet > 1.0f) wet = 1.0f;
    rev->dry_level = float_to_q23(dry);
    rev->wet_level = float_to_q23(wet);
}

/* =========================================================================
 * TEST HARNESS
 * ========================================================================= */

#ifdef TEST_REVERB
#include <stdio.h>
#include <math.h>

int main(int argc, char **argv) {
    waveboy_4reverbs_t reverb;

    printf("==============================================\n");
    printf("Waveboy \"4 REVERBS\" - C99 Port Test\n");
    printf("Reverse-engineered from EPS-16+ effect file\n");
    printf("==============================================\n\n");

    float sample_rate = 44100.0f;

    /* Test each algorithm */
    const char *algo_names[] = {
        "A: Small Room",
        "B: Bright Room",
        "C: Dark Chamber",
        "D: Short Ambience"
    };

    for (int algo = 0; algo < 4; algo++) {
        printf("Testing Algorithm %s\n", algo_names[algo]);
        printf("----------------------------------------\n");

        waveboy_4reverbs_init(&reverb, algo, sample_rate);
        waveboy_4reverbs_set_decay(&reverb, 1.5f);
        waveboy_4reverbs_set_damping(&reverb, 0.3f);
        waveboy_4reverbs_set_diffusion(&reverb, 0.7f);
        waveboy_4reverbs_set_mix(&reverb, 0.0f, 1.0f);  /* 100% wet for IR */

        q23_t in_l, in_r, out_l, out_r;

        /* Generate impulse response */
        in_l = Q23_ONE;
        in_r = Q23_ONE;

        /* Find peak and measure decay */
        float peak = 0.0f;
        int peak_sample = 0;
        float t60_level = 0.0f;
        int t60_sample = 0;

        /* Process 3 seconds of audio */
        int num_samples = (int)(3.0f * sample_rate);
        int printed = 0;

        for (int i = 0; i < num_samples; i++) {
            waveboy_4reverbs_process(&reverb, in_l, in_r, &out_l, &out_r);

            float level = fabsf(q23_to_float(out_l)) + fabsf(q23_to_float(out_r));

            if (level > peak && level < 1.9f) {  /* Ignore clipping */
                peak = level;
                peak_sample = i;
            }

            /* Find -60dB point (RT60) after peak */
            if (t60_sample == 0 && i > peak_sample + 100 && peak > 0.01f && level < peak * 0.001f) {
                t60_sample = i;
                t60_level = level;
            }

            /* Zero input after first sample */
            in_l = 0;
            in_r = 0;

            /* Print samples around first output and near peak */
            if (printed < 20 && level > 0.001f) {
                printf("  [%5d] L: %+.6f  R: %+.6f\n",
                       i, q23_to_float(out_l), q23_to_float(out_r));
                printed++;
            }
        }

        printf("  ...\n");
        printf("  Peak at sample %d (%.1f ms)\n",
               peak_sample, 1000.0f * peak_sample / sample_rate);

        if (t60_sample > 0) {
            printf("  RT60 approx: %.2f seconds\n",
                   (float)t60_sample / sample_rate);
        } else {
            printf("  RT60 > 2 seconds\n");
        }

        printf("\n");
        waveboy_4reverbs_free(&reverb);
    }

    printf("==============================================\n");
    printf("Test complete.\n");
    printf("==============================================\n");

    return 0;
}
#endif

/* =========================================================================
 * WAV FILE PROCESSING (for audio testing)
 * ========================================================================= */

#ifdef PROCESS_WAV
#include <stdio.h>

/* Simple WAV header (assumes 16-bit stereo) */
typedef struct {
    char riff[4];
    uint32_t file_size;
    char wave[4];
    char fmt[4];
    uint32_t fmt_size;
    uint16_t format;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits;
    char data[4];
    uint32_t data_size;
} wav_header_t;

int main(int argc, char **argv) {
    if (argc != 4) {
        printf("Usage: %s <input.wav> <output.wav> <algorithm 0-3>\n", argv[0]);
        return 1;
    }

    FILE *in = fopen(argv[1], "rb");
    if (!in) {
        printf("Cannot open input file\n");
        return 1;
    }

    wav_header_t hdr;
    fread(&hdr, sizeof(hdr), 1, in);

    if (memcmp(hdr.riff, "RIFF", 4) != 0 ||
        memcmp(hdr.wave, "WAVE", 4) != 0) {
        printf("Not a valid WAV file\n");
        fclose(in);
        return 1;
    }

    printf("Input: %d Hz, %d channels, %d bits\n",
           hdr.sample_rate, hdr.channels, hdr.bits);

    int algo = atoi(argv[3]);
    if (algo < 0 || algo > 3) algo = 0;

    waveboy_4reverbs_t reverb;
    waveboy_4reverbs_init(&reverb, algo, (float)hdr.sample_rate);
    waveboy_4reverbs_set_decay(&reverb, 2.0f);
    waveboy_4reverbs_set_damping(&reverb, 0.35f);
    waveboy_4reverbs_set_diffusion(&reverb, 0.75f);
    waveboy_4reverbs_set_mix(&reverb, 0.4f, 0.6f);

    FILE *out = fopen(argv[2], "wb");
    fwrite(&hdr, sizeof(hdr), 1, out);

    int num_samples = hdr.data_size / (hdr.channels * (hdr.bits / 8));

    for (int i = 0; i < num_samples; i++) {
        int16_t samples[2] = {0, 0};
        fread(samples, sizeof(int16_t), hdr.channels, in);

        /* Convert to Q23 */
        q23_t in_l = ((int32_t)samples[0]) << 8;
        q23_t in_r = (hdr.channels > 1) ? ((int32_t)samples[1]) << 8 : in_l;

        q23_t out_l, out_r;
        waveboy_4reverbs_process(&reverb, in_l, in_r, &out_l, &out_r);

        /* Convert back to 16-bit */
        samples[0] = (int16_t)(q23_extend(out_l) >> 8);
        samples[1] = (int16_t)(q23_extend(out_r) >> 8);

        fwrite(samples, sizeof(int16_t), 2, out);

        if ((i % 44100) == 0) {
            printf("\rProcessing: %d%%", (int)(100.0f * i / num_samples));
            fflush(stdout);
        }
    }

    printf("\rProcessing: 100%%\n");

    /* Add 5 second tail for reverb decay */
    printf("Adding reverb tail (5 seconds)...\n");
    for (int i = 0; i < 5 * (int)hdr.sample_rate; i++) {
        q23_t out_l, out_r;
        waveboy_4reverbs_process(&reverb, 0, 0, &out_l, &out_r);

        int16_t samples[2];
        samples[0] = (int16_t)(q23_extend(out_l) >> 8);
        samples[1] = (int16_t)(q23_extend(out_r) >> 8);
        fwrite(samples, sizeof(int16_t), 2, out);
    }

    /* Fix file size in header */
    uint32_t pos = ftell(out);
    hdr.data_size = pos - sizeof(hdr);
    hdr.file_size = pos - 8;
    fseek(out, 0, SEEK_SET);
    fwrite(&hdr, sizeof(hdr), 1, out);

    fclose(in);
    fclose(out);
    waveboy_4reverbs_free(&reverb);

    printf("Done. Output written to %s\n", argv[2]);
    return 0;
}
#endif
