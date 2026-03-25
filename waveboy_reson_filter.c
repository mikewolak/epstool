/*
 * Waveboy "RESON FILTER 2" - ES5510 DSP Algorithm Port to C99
 *
 * Original: Waveboy Industries for Ensoniq EPS-16 Plus
 * Reverse-engineered from extracted microcode
 *
 * This is a resonant filter effect implementing a State Variable Filter (SVF)
 * with LFO modulation of cutoff frequency.
 *
 * ES5510 DSP SPECIFICATIONS:
 *   - Clock: 10 MHz
 *   - Instruction cycle: 250ns nominal
 *   - Sample rate: 39.16 kHz (EPS-16+ audio rate)
 *   - Arithmetic: 24-bit fixed-point (Q23 format)
 *
 * Filter modes:
 *   0: Lowpass
 *   1: Bandpass
 *   2: Highpass
 *   3: Notch (LP + HP)
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* =========================================================================
 * Q23 FIXED-POINT ARITHMETIC
 * ========================================================================= */

typedef int32_t q23_t;

#define Q23_ONE      0x7FFFFF
#define Q23_MINUS1   0x800000
#define Q23_HALF     0x400000
#define Q23_ZERO     0x000000

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline int32_t q23_extend(q23_t x) {
    x &= 0xFFFFFF;
    if (x & 0x800000) return (int32_t)(x | 0xFF000000);
    return (int32_t)x;
}

static inline q23_t float_to_q23(float f) {
    if (f >= 1.0f) return Q23_ONE;
    if (f <= -1.0f) return (q23_t)Q23_MINUS1;
    return (q23_t)(f * (float)(Q23_ONE + 1));
}

static inline float q23_to_float(q23_t q) {
    return (float)q23_extend(q) / (float)(Q23_ONE + 1);
}

static inline q23_t q23_mul(q23_t a, q23_t b) {
    int64_t ea = q23_extend(a);
    int64_t eb = q23_extend(b);
    int64_t result = (ea * eb) >> 23;
    if (result > 0x7FFFFF) return Q23_ONE;
    if (result < -0x800000) return (q23_t)Q23_MINUS1;
    return (q23_t)(result & 0xFFFFFF);
}

static inline q23_t q23_add(q23_t a, q23_t b) {
    int64_t result = (int64_t)q23_extend(a) + (int64_t)q23_extend(b);
    if (result > 0x7FFFFF) return Q23_ONE;
    if (result < -0x800000) return (q23_t)Q23_MINUS1;
    return (q23_t)(result & 0xFFFFFF);
}

static inline q23_t q23_sub(q23_t a, q23_t b) {
    int64_t result = (int64_t)q23_extend(a) - (int64_t)q23_extend(b);
    if (result > 0x7FFFFF) return Q23_ONE;
    if (result < -0x800000) return (q23_t)Q23_MINUS1;
    return (q23_t)(result & 0xFFFFFF);
}

/* =========================================================================
 * LFO (Low Frequency Oscillator)
 * Modulates cutoff frequency
 * ========================================================================= */

typedef struct {
    uint32_t phase;        /* 32-bit phase accumulator */
    uint32_t phase_inc;    /* Phase increment per sample */
    q23_t depth;           /* Modulation depth */
} lfo_t;

static void lfo_init(lfo_t *lfo, float rate_hz, float depth, float sample_rate) {
    lfo->phase = 0;
    /* Phase increment for given rate */
    lfo->phase_inc = (uint32_t)((rate_hz / sample_rate) * 4294967296.0f);
    lfo->depth = float_to_q23(depth);
}

static inline q23_t lfo_process(lfo_t *lfo) {
    /* Triangle wave LFO */
    lfo->phase += lfo->phase_inc;

    /* Convert phase to triangle wave */
    int32_t tri;
    if (lfo->phase < 0x80000000U) {
        tri = (int32_t)(lfo->phase >> 8) - 0x7FFFFF;
    } else {
        tri = 0x7FFFFF - (int32_t)((lfo->phase - 0x80000000U) >> 8);
    }

    return q23_mul((q23_t)(tri & 0xFFFFFF), lfo->depth);
}

/* =========================================================================
 * STATE VARIABLE FILTER
 * Classic SVF topology providing simultaneous LP, BP, HP outputs
 * ========================================================================= */

typedef struct {
    q23_t lp;              /* Lowpass state */
    q23_t bp;              /* Bandpass state */
    q23_t f;               /* Frequency coefficient: 2*sin(pi*Fc/Fs) */
    q23_t q;               /* Damping: 1/resonance */
} svf_t;

static void svf_set_freq(svf_t *svf, float cutoff_hz, float sample_rate) {
    /* f = 2 * sin(pi * fc / fs) */
    float f = 2.0f * sinf((float)M_PI * cutoff_hz / sample_rate);
    if (f > 1.9f) f = 1.9f;  /* Stability limit */
    svf->f = float_to_q23(f);
}

static void svf_set_resonance(svf_t *svf, float resonance) {
    /* Q = 1/resonance, but we need damping coefficient */
    /* For SVF: damping = 2 - 2*resonance typically */
    if (resonance < 0.0f) resonance = 0.0f;
    if (resonance > 0.99f) resonance = 0.99f;

    /* Q ranges from 0.5 (no resonance) to ~20 (high resonance) */
    float q = 2.0f * (1.0f - resonance);
    if (q < 0.1f) q = 0.1f;  /* Prevent instability */
    svf->q = float_to_q23(q);
}

static inline void svf_process(svf_t *svf, q23_t input,
                               q23_t *lp_out, q23_t *bp_out, q23_t *hp_out) {
    /*
     * SVF algorithm:
     *   HP = input - LP - Q*BP
     *   BP = BP + f*HP
     *   LP = LP + f*BP
     */

    /* Highpass: input - LP - Q*BP */
    q23_t hp = q23_sub(input, svf->lp);
    hp = q23_sub(hp, q23_mul(svf->q, svf->bp));

    /* Bandpass: BP + f*HP */
    svf->bp = q23_add(svf->bp, q23_mul(svf->f, hp));

    /* Lowpass: LP + f*BP */
    svf->lp = q23_add(svf->lp, q23_mul(svf->f, svf->bp));

    *hp_out = hp;
    *bp_out = svf->bp;
    *lp_out = svf->lp;
}

/* =========================================================================
 * RESONANT FILTER EFFECT
 * Main effect structure combining SVF, LFO, and mixing
 * ========================================================================= */

typedef enum {
    FILTER_LOWPASS = 0,
    FILTER_BANDPASS = 1,
    FILTER_HIGHPASS = 2,
    FILTER_NOTCH = 3
} filter_mode_t;

typedef struct {
    /* Two SVF channels for stereo */
    svf_t svf_l;
    svf_t svf_r;

    /* LFO for cutoff modulation */
    lfo_t lfo;

    /* Base cutoff frequency (before modulation) */
    float base_cutoff_hz;
    float sample_rate;

    /* Filter mode */
    filter_mode_t mode;

    /* Mix levels */
    q23_t dry_level;
    q23_t wet_level;

    /* Soft saturation for analog character */
    q23_t drive;

} waveboy_reson_filter_t;

/* =========================================================================
 * INITIALIZATION
 * ========================================================================= */

void waveboy_reson_filter_init(waveboy_reson_filter_t *filt, float sample_rate) {
    memset(filt, 0, sizeof(*filt));

    filt->sample_rate = sample_rate;
    filt->base_cutoff_hz = 2000.0f;

    /* Initialize SVFs */
    svf_set_freq(&filt->svf_l, filt->base_cutoff_hz, sample_rate);
    svf_set_freq(&filt->svf_r, filt->base_cutoff_hz, sample_rate);
    svf_set_resonance(&filt->svf_l, 0.5f);
    svf_set_resonance(&filt->svf_r, 0.5f);

    /* Initialize LFO: 1 Hz, 50% depth */
    lfo_init(&filt->lfo, 1.0f, 0.5f, sample_rate);

    /* Default to lowpass */
    filt->mode = FILTER_LOWPASS;

    /* Default mix */
    filt->dry_level = float_to_q23(0.0f);
    filt->wet_level = float_to_q23(1.0f);
    filt->drive = float_to_q23(0.0f);
}

/* =========================================================================
 * PARAMETER CONTROL
 * ========================================================================= */

void waveboy_reson_filter_set_cutoff(waveboy_reson_filter_t *filt, float cutoff_hz) {
    if (cutoff_hz < 20.0f) cutoff_hz = 20.0f;
    if (cutoff_hz > filt->sample_rate * 0.45f) cutoff_hz = filt->sample_rate * 0.45f;
    filt->base_cutoff_hz = cutoff_hz;
}

void waveboy_reson_filter_set_resonance(waveboy_reson_filter_t *filt, float resonance) {
    svf_set_resonance(&filt->svf_l, resonance);
    svf_set_resonance(&filt->svf_r, resonance);
}

void waveboy_reson_filter_set_lfo_rate(waveboy_reson_filter_t *filt, float rate_hz) {
    if (rate_hz < 0.01f) rate_hz = 0.01f;
    if (rate_hz > 20.0f) rate_hz = 20.0f;
    filt->lfo.phase_inc = (uint32_t)((rate_hz / filt->sample_rate) * 4294967296.0f);
}

void waveboy_reson_filter_set_lfo_depth(waveboy_reson_filter_t *filt, float depth) {
    if (depth < 0.0f) depth = 0.0f;
    if (depth > 1.0f) depth = 1.0f;
    filt->lfo.depth = float_to_q23(depth);
}

void waveboy_reson_filter_set_mode(waveboy_reson_filter_t *filt, filter_mode_t mode) {
    filt->mode = mode;
}

void waveboy_reson_filter_set_mix(waveboy_reson_filter_t *filt, float dry, float wet) {
    if (dry < 0.0f) dry = 0.0f;
    if (dry > 1.0f) dry = 1.0f;
    if (wet < 0.0f) wet = 0.0f;
    if (wet > 1.0f) wet = 1.0f;
    filt->dry_level = float_to_q23(dry);
    filt->wet_level = float_to_q23(wet);
}

void waveboy_reson_filter_set_drive(waveboy_reson_filter_t *filt, float drive) {
    if (drive < 0.0f) drive = 0.0f;
    if (drive > 1.0f) drive = 1.0f;
    filt->drive = float_to_q23(drive);
}

/* =========================================================================
 * SOFT SATURATION
 * Adds analog-like warmth (observed in microcode limiting operations)
 * ========================================================================= */

static inline q23_t soft_saturate(q23_t x, q23_t drive) {
    /* Simple soft clipping: x / (1 + |x| * drive) */
    if (drive == 0) return x;

    int32_t ex = q23_extend(x);
    int32_t ax = (ex < 0) ? -ex : ex;  /* Absolute value */

    /* drive_scaled adds saturation */
    int32_t denom = (Q23_ONE + 1) + ((int64_t)ax * q23_extend(drive) >> 23);
    if (denom < 1) denom = 1;

    int64_t result = ((int64_t)ex * (Q23_ONE + 1)) / denom;
    if (result > 0x7FFFFF) return Q23_ONE;
    if (result < -0x800000) return (q23_t)Q23_MINUS1;
    return (q23_t)(result & 0xFFFFFF);
}

/* =========================================================================
 * CORE PROCESSING
 * ========================================================================= */

void waveboy_reson_filter_process(waveboy_reson_filter_t *filt,
                                  q23_t in_l, q23_t in_r,
                                  q23_t *out_l, q23_t *out_r) {
    /* Get LFO modulation value */
    q23_t lfo_mod = lfo_process(&filt->lfo);

    /* Modulate cutoff frequency */
    /* LFO modulates over 2 octaves centered on base cutoff */
    float mod_factor = 1.0f + q23_to_float(lfo_mod);
    float mod_cutoff = filt->base_cutoff_hz * mod_factor;

    /* Clamp to valid range */
    if (mod_cutoff < 20.0f) mod_cutoff = 20.0f;
    if (mod_cutoff > filt->sample_rate * 0.45f) mod_cutoff = filt->sample_rate * 0.45f;

    /* Update filter frequency */
    svf_set_freq(&filt->svf_l, mod_cutoff, filt->sample_rate);
    svf_set_freq(&filt->svf_r, mod_cutoff, filt->sample_rate);

    /* Apply soft saturation to input (optional drive) */
    q23_t sat_l = soft_saturate(in_l, filt->drive);
    q23_t sat_r = soft_saturate(in_r, filt->drive);

    /* Process through SVF */
    q23_t lp_l, bp_l, hp_l;
    q23_t lp_r, bp_r, hp_r;

    svf_process(&filt->svf_l, sat_l, &lp_l, &bp_l, &hp_l);
    svf_process(&filt->svf_r, sat_r, &lp_r, &bp_r, &hp_r);

    /* Select output based on mode */
    q23_t filt_l, filt_r;

    switch (filt->mode) {
        case FILTER_LOWPASS:
            filt_l = lp_l;
            filt_r = lp_r;
            break;
        case FILTER_BANDPASS:
            filt_l = bp_l;
            filt_r = bp_r;
            break;
        case FILTER_HIGHPASS:
            filt_l = hp_l;
            filt_r = hp_r;
            break;
        case FILTER_NOTCH:
            /* Notch = LP + HP (removes BP frequencies) */
            filt_l = q23_add(lp_l, hp_l);
            filt_r = q23_add(lp_r, hp_r);
            break;
        default:
            filt_l = lp_l;
            filt_r = lp_r;
    }

    /* Mix dry/wet */
    *out_l = q23_add(q23_mul(filt->dry_level, in_l),
                     q23_mul(filt->wet_level, filt_l));
    *out_r = q23_add(q23_mul(filt->dry_level, in_r),
                     q23_mul(filt->wet_level, filt_r));
}

/* =========================================================================
 * TEST HARNESS
 * ========================================================================= */

#ifdef TEST_FILTER
#include <stdio.h>

int main(int argc, char **argv) {
    waveboy_reson_filter_t filter;

    printf("==============================================\n");
    printf("Waveboy \"RESON FILTER 2\" - C99 Port Test\n");
    printf("Reverse-engineered from EPS-16+ effect file\n");
    printf("==============================================\n\n");

    float sample_rate = 44100.0f;

    const char *mode_names[] = {
        "Lowpass", "Bandpass", "Highpass", "Notch"
    };

    /* Test each filter mode */
    for (int mode = 0; mode < 4; mode++) {
        printf("Testing Mode: %s\n", mode_names[mode]);
        printf("----------------------------------------\n");

        waveboy_reson_filter_init(&filter, sample_rate);
        waveboy_reson_filter_set_cutoff(&filter, 1000.0f);
        waveboy_reson_filter_set_resonance(&filter, 0.8f);  /* High resonance */
        waveboy_reson_filter_set_lfo_rate(&filter, 0.5f);
        waveboy_reson_filter_set_lfo_depth(&filter, 0.3f);
        waveboy_reson_filter_set_mode(&filter, (filter_mode_t)mode);
        waveboy_reson_filter_set_mix(&filter, 0.0f, 1.0f);  /* 100% wet */

        q23_t in_l, in_r, out_l, out_r;

        /* Generate frequency sweep response */
        printf("  Impulse response (first 30 samples with output):\n");

        /* Single impulse */
        in_l = Q23_ONE;
        in_r = Q23_ONE;

        int printed = 0;
        for (int i = 0; i < 1000 && printed < 30; i++) {
            waveboy_reson_filter_process(&filter, in_l, in_r, &out_l, &out_r);

            float level = fabsf(q23_to_float(out_l));
            if (level > 0.001f || i < 5) {
                printf("  [%4d] L: %+.6f  R: %+.6f\n",
                       i, q23_to_float(out_l), q23_to_float(out_r));
                printed++;
            }

            in_l = 0;
            in_r = 0;
        }

        /* Test with sine wave at cutoff frequency */
        printf("\n  Sine wave test at cutoff (1kHz):\n");
        waveboy_reson_filter_init(&filter, sample_rate);
        waveboy_reson_filter_set_cutoff(&filter, 1000.0f);
        waveboy_reson_filter_set_resonance(&filter, 0.8f);
        waveboy_reson_filter_set_lfo_rate(&filter, 0.0f);  /* No LFO */
        waveboy_reson_filter_set_lfo_depth(&filter, 0.0f);
        waveboy_reson_filter_set_mode(&filter, (filter_mode_t)mode);
        waveboy_reson_filter_set_mix(&filter, 0.0f, 1.0f);

        float max_out = 0.0f;
        for (int i = 0; i < 500; i++) {
            float t = (float)i / sample_rate;
            float sine = sinf(2.0f * M_PI * 1000.0f * t);
            in_l = float_to_q23(sine * 0.5f);
            in_r = in_l;

            waveboy_reson_filter_process(&filter, in_l, in_r, &out_l, &out_r);

            float level = fabsf(q23_to_float(out_l));
            if (level > max_out) max_out = level;
        }
        printf("  Peak output at 1kHz: %.3f (%.1f dB gain)\n",
               max_out, 20.0f * log10f(max_out / 0.5f));

        printf("\n");
    }

    printf("==============================================\n");
    printf("Test complete.\n");
    printf("==============================================\n");

    return 0;
}
#endif

/* =========================================================================
 * WAV FILE PROCESSING
 * ========================================================================= */

#ifdef PROCESS_WAV
#include <stdio.h>

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
    if (argc != 5) {
        printf("Usage: %s <input.wav> <output.wav> <cutoff_hz> <resonance>\n", argv[0]);
        printf("  cutoff_hz: 20-15000\n");
        printf("  resonance: 0.0-0.99\n");
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

    float cutoff = atof(argv[3]);
    float resonance = atof(argv[4]);

    printf("Input: %d Hz, %d channels, %d bits\n",
           hdr.sample_rate, hdr.channels, hdr.bits);
    printf("Filter: cutoff=%.1f Hz, resonance=%.2f\n", cutoff, resonance);

    waveboy_reson_filter_t filter;
    waveboy_reson_filter_init(&filter, (float)hdr.sample_rate);
    waveboy_reson_filter_set_cutoff(&filter, cutoff);
    waveboy_reson_filter_set_resonance(&filter, resonance);
    waveboy_reson_filter_set_lfo_rate(&filter, 0.5f);
    waveboy_reson_filter_set_lfo_depth(&filter, 0.2f);
    waveboy_reson_filter_set_mode(&filter, FILTER_LOWPASS);
    waveboy_reson_filter_set_mix(&filter, 0.0f, 1.0f);

    FILE *out = fopen(argv[2], "wb");
    fwrite(&hdr, sizeof(hdr), 1, out);

    int num_samples = hdr.data_size / (hdr.channels * (hdr.bits / 8));

    for (int i = 0; i < num_samples; i++) {
        int16_t samples[2] = {0, 0};
        fread(samples, sizeof(int16_t), hdr.channels, in);

        q23_t in_l = ((int32_t)samples[0]) << 8;
        q23_t in_r = (hdr.channels > 1) ? ((int32_t)samples[1]) << 8 : in_l;

        q23_t out_l, out_r;
        waveboy_reson_filter_process(&filter, in_l, in_r, &out_l, &out_r);

        samples[0] = (int16_t)(q23_extend(out_l) >> 8);
        samples[1] = (int16_t)(q23_extend(out_r) >> 8);

        fwrite(samples, sizeof(int16_t), 2, out);

        if ((i % 44100) == 0) {
            printf("\rProcessing: %d%%", (int)(100.0f * i / num_samples));
            fflush(stdout);
        }
    }

    printf("\rProcessing: 100%%\n");

    fclose(in);
    fclose(out);

    printf("Done. Output written to %s\n", argv[2]);
    return 0;
}
#endif
