/*
 * Test Waveboy Effects with ES5510 Emulator
 *
 * Loads extracted microcode and processes WAV files
 * Uses MAME-based ES5510 emulation for cycle-accurate results
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "es5510_standalone.h"
#include "4reverbs_input_table.h"

/* WAV file header */
#pragma pack(push, 1)
typedef struct {
    char riff[4];
    uint32_t file_size;
    char wave[4];
    char fmt[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} wav_header_t;

typedef struct {
    char id[4];
    uint32_t size;
} chunk_header_t;
#pragma pack(pop)

/* Load microcode from file */
static uint8_t *load_microcode(const char *filename, int *len) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open microcode file: %s\n", filename);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *data = malloc(*len);
    if (!data) {
        fclose(f);
        return NULL;
    }

    fread(data, 1, *len, f);
    fclose(f);

    printf("Loaded microcode: %d bytes (%d instructions)\n", *len, *len / 6);
    return data;
}

/* Load WAV file */
static int16_t *load_wav(const char *filename, int *num_samples, int *sample_rate, int *num_channels) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open WAV file: %s\n", filename);
        return NULL;
    }

    wav_header_t header;
    fread(&header, sizeof(header), 1, f);

    if (memcmp(header.riff, "RIFF", 4) != 0 || memcmp(header.wave, "WAVE", 4) != 0) {
        fprintf(stderr, "Error: Not a valid WAV file\n");
        fclose(f);
        return NULL;
    }

    /* Skip extra format bytes if present */
    if (header.fmt_size > 16) {
        fseek(f, header.fmt_size - 16, SEEK_CUR);
    }

    /* Find data chunk */
    chunk_header_t chunk;
    while (1) {
        if (fread(&chunk, sizeof(chunk), 1, f) != 1) {
            fprintf(stderr, "Error: Cannot find data chunk\n");
            fclose(f);
            return NULL;
        }
        if (memcmp(chunk.id, "data", 4) == 0) break;
        fseek(f, chunk.size, SEEK_CUR);
    }

    *sample_rate = header.sample_rate;
    *num_channels = header.num_channels;
    int bytes_per_sample = header.bits_per_sample / 8;
    *num_samples = chunk.size / (bytes_per_sample * header.num_channels);

    printf("WAV: %d Hz, %d-bit, %d ch, %d samples (%.2f sec)\n",
           header.sample_rate, header.bits_per_sample,
           header.num_channels, *num_samples,
           (float)*num_samples / header.sample_rate);

    /* Read sample data */
    int16_t *samples = malloc(chunk.size);
    if (!samples) {
        fclose(f);
        return NULL;
    }

    fread(samples, 1, chunk.size, f);
    fclose(f);
    return samples;
}

/* Save WAV file */
static int save_wav(const char *filename, int16_t *samples, int num_samples,
                    int sample_rate, int num_channels) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Error: Cannot create output file: %s\n", filename);
        return -1;
    }

    int data_size = num_samples * num_channels * sizeof(int16_t);

    wav_header_t header = {
        .riff = {'R', 'I', 'F', 'F'},
        .file_size = 36 + data_size,
        .wave = {'W', 'A', 'V', 'E'},
        .fmt = {'f', 'm', 't', ' '},
        .fmt_size = 16,
        .audio_format = 1,
        .num_channels = num_channels,
        .sample_rate = sample_rate,
        .byte_rate = sample_rate * num_channels * sizeof(int16_t),
        .block_align = num_channels * sizeof(int16_t),
        .bits_per_sample = 16
    };

    chunk_header_t data_chunk = {
        .id = {'d', 'a', 't', 'a'},
        .size = data_size
    };

    fwrite(&header, sizeof(header), 1, f);
    fwrite(&data_chunk, sizeof(data_chunk), 1, f);
    fwrite(samples, 1, data_size, f);
    fclose(f);

    printf("Saved: %s (%d samples)\n", filename, num_samples);
    return 0;
}

/* Helper to count low ones (for MEMSIZ) */
static int8_t countLowOnes(int32_t x) {
    int8_t n = 0;
    while ((x & 1) == 1) {
        ++n;
        x >>= 1;
    }
    return n;
}

/* Configure DSP memory parameters using MEMSIZ simulation */
static void init_memory(es5510_t *dsp) {
    /*
     * Configure delay memory using MEMSIZ register simulation.
     * Use memshift=8 which gives good address resolution:
     *   memsiz = 0xFF (256 sample granularity)
     *   memmask = 0xFFFF00 (keeps bits 8-23)
     *   memincrement = 0x100
     */
    int32_t memsiz_value = 0xFF;  /* 8 low ones = memshift 8 */
    dsp->memshift = countLowOnes(memsiz_value);
    dsp->memsiz = 0x00ffffff >> (24 - dsp->memshift);
    dsp->memmask = 0x00ffffff & ~dsp->memsiz;
    dsp->memincrement = 1 << dsp->memshift;
    dsp->dlength = 0x100000;  /* Delay buffer length */
}

/* Initialize RESON FILTER parameters */
static void init_reson_filter(es5510_t *dsp, float cutoff_hz, float resonance) {
    /*
     * From waveboy_reson_filter_annotated.txt:
     *   Cutoff -> R24, R25, R26, R27 (freq coefficients)
     *   Resonance -> R29, R32 (damping/feedback)
     *
     * Filter coefficient: f = 2 * sin(pi * Fc / Fs)
     * EPS-16+ sample rate is ~39160 Hz
     */
    const float sample_rate = 39160.0f;
    float fc_norm = cutoff_hz / sample_rate;
    if (fc_norm > 0.45f) fc_norm = 0.45f;

    float f_coef = 2.0f * sinf(3.14159265f * fc_norm);
    int32_t f_q23 = (int32_t)(f_coef * 8388607.0f);

    /* Set frequency coefficients */
    es5510_set_gpr(dsp, 24, f_q23);
    es5510_set_gpr(dsp, 25, f_q23);
    es5510_set_gpr(dsp, 26, f_q23);
    es5510_set_gpr(dsp, 27, f_q23);

    /* Q = 2 - 2*resonance (range 0-2) */
    float q_coef = 2.0f - (2.0f * resonance);
    if (q_coef < 0.01f) q_coef = 0.01f;
    int32_t q_q23 = (int32_t)(q_coef * 8388607.0f);

    es5510_set_gpr(dsp, 29, q_q23);
    es5510_set_gpr(dsp, 32, q_q23);

    /* Initialize delay memory */
    init_memory(dsp);

    printf("RESON FILTER: cutoff=%.1f Hz, resonance=%.2f\n", cutoff_hz, resonance);
}

/*
 * Delay tap offsets from 4 REVERBS.efe at offset 0x276
 * These are 16-bit sample offsets for the reverb delay lines
 * 37 values ranging from 192 to 3738 samples (5-95ms at 39160 Hz)
 */
static const uint16_t reverb_delay_taps[] = {
    0x00c0, 0x00d4, 0x00e6, 0x00fa, 0x014e, 0x015c, 0x010c, 0x012c, 0x0120,
    0x0434, 0x0448, 0x045a, 0x046e, 0x04c2, 0x04d0, 0x0480, 0x04a0, 0x0494,
    0x07a8, 0x07bc, 0x07ce, 0x07e2, 0x0836, 0x0844, 0x07f4, 0x0814, 0x0808,
    0x0b1c, 0x0b30, 0x0b42, 0x0b56, 0x0baa, 0x0bb8, 0x0b68, 0x0b88, 0x0b7c,
    0x0e9a
};
#define NUM_DELAY_TAPS (sizeof(reverb_delay_taps) / sizeof(reverb_delay_taps[0]))

/* Note: delay_access_gprs array removed - using sequential GPR ranges instead */

/* Initialize 4 REVERBS parameters using the input variable table */
static void init_reverb(es5510_t *dsp, float decay, float predelay_ms, int algorithm) {
    (void)predelay_ms;  /* TODO: implement predelay using delay tap offsets */
    (void)algorithm;    /* TODO: implement mode selection via CCR/CMR */

    /* Initialize delay memory configuration */
    init_memory(dsp);

    /*
     * Use the input variable table from 4reverbs_input_table.h
     * This loads all delay taps, coefficients, and mode registers
     */
    reverbs_init_gprs(dsp->gpr, dsp->memincrement);

    /* Set table base addresses - all at 0 so delay lines share memory */
    dsp->abase = 0x000000;
    dsp->bbase = 0x000000;
    dsp->dbase = 0x000000;

    /* Override decay coefficient with user-specified value */
    int32_t decay_q23 = (int32_t)(decay * 8388607.0f);
    es5510_set_gpr(dsp, 117, decay_q23);  /* Main decay (used ~45 times) */
    es5510_set_gpr(dsp, 121, decay_q23);  /* Secondary decay */

    /* Override input gain to unity for full input */
    es5510_set_gpr(dsp, 69, 0x7FFFFF);

    /* Ensure wet mix is set for output (from input table defaults: 0x740000 = 0.9) */
    /* R123 feeds ser3l at PC=101-102, R187 adds to ser3l at PC=112 */

    printf("REVERB: decay=%.2f, predelay=%.1f ms, algorithm=%d\n",
           decay, predelay_ms, algorithm);
    printf("  Initialized from 4reverbs_input_table.h\n");
    printf("  Delay write taps: %d, Delay read taps: %d, Coefficients: %d\n",
           NUM_DELAY_WRITE_TAPS, NUM_DELAY_READ_TAPS, NUM_COEFFICIENTS);
    printf("  R123 (wet_mix_left)=0x%06x, R187 (wet_mix_right)=0x%06x\n",
           dsp->gpr[123] & 0xFFFFFF, dsp->gpr[187] & 0xFFFFFF);
}

void print_usage(const char *prog) {
    printf("Usage: %s <effect> <input.wav> <output.wav> [options]\n\n", prog);
    printf("Effects:\n");
    printf("  filter   - RESON FILTER (resonant lowpass/bandpass/highpass)\n");
    printf("  reverb   - 4 REVERBS (algorithmic reverb)\n");
    printf("\nFilter options:\n");
    printf("  -c <hz>     Cutoff frequency (default: 2000)\n");
    printf("  -r <0-1>    Resonance (default: 0.5)\n");
    printf("\nReverb options:\n");
    printf("  -d <0-1>    Decay time (default: 0.7)\n");
    printf("  -p <ms>     Predelay (default: 20)\n");
    printf("  -a <0-3>    Algorithm A/B/C/D (default: 0)\n");
    printf("\nGeneral options:\n");
    printf("  -t <sec>    Tail time in seconds for reverb decay (default: 0)\n");
    printf("\nMicrocode files (defaults):\n");
    printf("  Filter: /tmp/reson_ucode.bin\n");
    printf("  Reverb: /tmp/4reverbs_ucode.bin\n");
}

int main(int argc, char **argv) {
    if (argc < 4) {
        print_usage(argv[0]);
        return 1;
    }

    const char *effect = argv[1];
    const char *input_file = argv[2];
    const char *output_file = argv[3];

    /* Effect parameters */
    float cutoff = 2000.0f;
    float resonance = 0.5f;
    float decay = 0.7f;
    float predelay = 20.0f;
    int algorithm = 0;
    float tail_seconds = 0.0f;
    const char *microcode_file = NULL;

    /* Parse options */
    for (int i = 4; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i+1 < argc) cutoff = atof(argv[++i]);
        else if (strcmp(argv[i], "-r") == 0 && i+1 < argc) resonance = atof(argv[++i]);
        else if (strcmp(argv[i], "-d") == 0 && i+1 < argc) decay = atof(argv[++i]);
        else if (strcmp(argv[i], "-p") == 0 && i+1 < argc) predelay = atof(argv[++i]);
        else if (strcmp(argv[i], "-a") == 0 && i+1 < argc) algorithm = atoi(argv[++i]);
        else if (strcmp(argv[i], "-t") == 0 && i+1 < argc) tail_seconds = atof(argv[++i]);
        else if (strcmp(argv[i], "-u") == 0 && i+1 < argc) microcode_file = argv[++i];
    }

    /* Select microcode file */
    if (!microcode_file) {
        if (strcmp(effect, "filter") == 0) {
            microcode_file = "/tmp/reson_ucode.bin";
        } else if (strcmp(effect, "reverb") == 0) {
            microcode_file = "/tmp/4reverbs_ucode.bin";
        } else {
            fprintf(stderr, "Unknown effect: %s\n", effect);
            return 1;
        }
    }

    /* Initialize ES5510 emulator */
    es5510_t dsp;
    es5510_init(&dsp);

    /* Load microcode */
    int microcode_len;
    uint8_t *microcode = load_microcode(microcode_file, &microcode_len);
    if (!microcode) {
        es5510_free(&dsp);
        return 1;
    }
    es5510_load_microcode(&dsp, microcode, microcode_len);
    free(microcode);

    /* Initialize effect parameters */
    if (strcmp(effect, "filter") == 0) {
        init_reson_filter(&dsp, cutoff, resonance);
    } else if (strcmp(effect, "reverb") == 0) {
        init_reverb(&dsp, decay, predelay, algorithm);
    }

    /* Load input WAV */
    int num_samples, sample_rate, num_channels;
    int16_t *input = load_wav(input_file, &num_samples, &sample_rate, &num_channels);
    if (!input) {
        es5510_free(&dsp);
        return 1;
    }

    /* Calculate tail samples (for reverb decay) */
    int tail_samples = (int)(tail_seconds * sample_rate);
    int total_output_samples = num_samples + tail_samples;

    /* Allocate output buffer (with tail) */
    int16_t *output = malloc(total_output_samples * num_channels * sizeof(int16_t));
    if (!output) {
        free(input);
        es5510_free(&dsp);
        return 1;
    }

    /* Process audio through ES5510 emulator */
    printf("Processing %d samples + %d tail samples through ES5510...\n",
           num_samples, tail_samples);

    for (int i = 0; i < total_output_samples; i++) {
        int16_t in_left, in_right;

        /* Get input sample (or silence for tail) */
        if (i < num_samples) {
            if (num_channels == 2) {
                in_left = input[i * 2];
                in_right = input[i * 2 + 1];
            } else {
                in_left = in_right = input[i];
            }
        } else {
            /* Tail - feed silence to let reverb decay */
            in_left = in_right = 0;
        }

        /* Feed to DSP */
        es5510_set_serial_input(&dsp, in_left, in_right);

        /* Run one sample period */
        es5510_run_sample(&dsp);

        /* Get output */
        int16_t out_left, out_right;
        es5510_get_serial_output(&dsp, &out_left, &out_right);

        /* Store output */
        if (num_channels == 2) {
            output[i * 2] = out_left;
            output[i * 2 + 1] = out_right;
        } else {
            output[i] = out_left;
        }

        /* Progress */
        if (i % 10000 == 0) {
            printf("\r  %d / %d (%.1f%%)", i, total_output_samples,
                   100.0f * i / total_output_samples);
            fflush(stdout);
        }
    }
    printf("\r  %d / %d (100.0%%)\n", total_output_samples, total_output_samples);

    /* Save output */
    save_wav(output_file, output, total_output_samples, sample_rate, num_channels);

    /* Cleanup */
    free(input);
    free(output);
    es5510_free(&dsp);

    printf("Done!\n");
    return 0;
}
