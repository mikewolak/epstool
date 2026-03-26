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
#include "4reverbs_gpr_init.h"

extern int debug_memtrace;
extern int debug_mac;
extern int debug_trace_samples;
extern int debug_rta;
extern int debug_mul;
extern int debug_skip;
extern int debug_alu;

/* Optional: load ESP microcode (and table) directly from a raw stripped .efe */
static int load_effect_blob(const char *path, es5510_t *dsp) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open effect blob: %s\n", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(len);
    if (!buf) { fclose(f); return -1; }
    if (fread(buf, 1, len, f) != (size_t)len) {
        fclose(f);
        free(buf);
        return -1;
    }
    fclose(f);

    /* Hard-coded offsets for Waveboy 4 REVERBS (after Giebler header is stripped) */
    const int ucode_off = 0x1442;
    const int var1_off  = 0x11e8;
    const int var1_len  = 0x82;   /* 130 bytes = 43 packed 24-bit values */
    const int var2_off  = 0x126a;
    const int var3_off  = 0x12ec;
    const int var4_off  = 0x136e;

    if (ucode_off + 6 > len) {
        fprintf(stderr, "Effect blob too small to contain microcode\n");
        free(buf);
        return -1;
    }
    int ucode_len = (int)(len - ucode_off);
    es5510_load_microcode(dsp, buf + ucode_off, ucode_len);

    /* Flatten VAR blocks into a value pool */
    int32_t pool[256];
    int pool_count = 0;
    int var_offs[] = {var1_off, var2_off, var3_off, var4_off};
    int var_lens[] = {var1_len, var1_len, var1_len, (int)(0x1442 - var4_off)};
    for (int b = 0; b < 4; b++) {
        if (var_offs[b] + var_lens[b] <= len) {
            const uint8_t *v = buf + var_offs[b];
            int entries = var_lens[b] / 3;
            for (int i = 0; i < entries && pool_count < (int)(sizeof(pool)/sizeof(pool[0])); i++) {
                int32_t val = (v[i*3] << 16) | (v[i*3+1] << 8) | v[i*3+2];
                pool[pool_count++] = val;
            }
        }
    }

    /* Simplest guess: put all values sequentially at start of DRAM (addr = i) */
    int seq_loaded = 0;
    for (int i = 0; i < pool_count && i < ES5510_DRAM_SIZE; i++) {
        dsp->dram[i] = (int16_t)(pool[i] >> 8);
        seq_loaded++;
    }
    printf("Loaded %d table words sequentially into DRAM\n", seq_loaded);

    free(buf);
    return 0;
}
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
     * Size the delay line to the full masked span so long taps don't wrap.
     */
    int32_t memsiz_value = 0xFF;  /* 8 low ones = memshift 8 */
    dsp->memshift = countLowOnes(memsiz_value);
    dsp->memsiz = 0x00ffffff >> (24 - dsp->memshift);
    dsp->memmask = 0x00ffffff & ~dsp->memsiz;
    dsp->memincrement = 1 << dsp->memshift;
    dsp->dlength = 0x00200000;  /* keep delay memory below table base */
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

/* Load one of the four VAR blocks from the .efe file and map it linearly to GPRs.
 * The VAR blocks in the Waveboy file are 24-bit signed words stored back-to-back;
 * each block corresponds to one algorithm (A/B/C/D). This mirrors what the 68k
 * init code would have done through the ROM ESP loader.
 */
static int load_reverb_var_block(const char *efe_path, int algorithm, int32_t *gpr_out) {
    static const int offsets[] = { 0x11e8, 0x126a, 0x12ec, 0x136e };
    static const int next_offsets[] = { 0x126a, 0x12ec, 0x136e, 0x1442 }; /* ucode start */
    if (algorithm < 0 || algorithm > 3) algorithm = 0;

    FILE *f = fopen(efe_path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open %s\n", efe_path);
        return -1;
    }

    /* Skip 512-byte Giebler header then seek to VAR block */
    if (fseek(f, 512 + offsets[algorithm], SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    int block_len = next_offsets[algorithm] - offsets[algorithm];
    unsigned char *buf = malloc(block_len);
    if (!buf) { fclose(f); return -1; }
    size_t n = fread(buf, 1, block_len, f);
    fclose(f);

    int words = (int)(n / 3); /* 24-bit words */
    for (int i = 0; i < 192; i++) gpr_out[i] = 0;
    for (int i = 0; i < words && i < 192; i++) {
        int32_t v = (buf[3*i] << 16) | (buf[3*i+1] << 8) | buf[3*i+2];
        /* sign-extend 24-bit */
        if (v & 0x00800000) v |= 0xFF000000;
        gpr_out[i] = v;
    }
    free(buf);
    return words;
}

/* Initialize 4 REVERBS using raw VAR-block data from the .efe (closest to real unit). */
static void init_reverb(es5510_t *dsp, float decay, float predelay_ms, int algorithm) {
    (void)predelay_ms;  /* TODO: implement predelay using delay tap offsets */

    init_memory(dsp);

    /* Start from clean GPRs and known tap/coef tables */
    for (int i = 0; i < 192; i++) dsp->gpr[i] = 0;
    init_reverb_gprs(dsp->gpr);
    for (int i = 0; i < NUM_COEFFICIENTS; i++) {
        dsp->gpr[COEFFICIENTS[i].gpr_index] = COEFFICIENTS[i].default_value;
    }

    /* Base addresses from observed hardware traces: ABASE near 0x2be800 */
    dsp->abase = 0x0000c8c0; /* A2 observed from effect init via Musashi */
    dsp->bbase = dsp->abase;
    dsp->dlength = 0x00400000; /* 4,194,304 bytes -> 16,384 words at memshift=8 (matches prior traces) */
    dsp->dbase = dsp->dlength;  /* circular pointer starts at end like ROM code */

    /* Load algorithm-specific table from effect file into DRAM starting at ABASE */
    const char *efe_path = "factory_sounds/WAVEBOY_EFE/4 REVERBS.efe";
    const int offsets[] = { 0x11e8, 0x126a, 0x12ec, 0x136e };
    const int next_offsets[] = { 0x126a, 0x12ec, 0x136e, 0x1442 };
    const int header_offset_table_base = 0x120; /* ROM loader treats this as base for pointer math */
    int off = offsets[(algorithm < 0 || algorithm > 3) ? 0 : algorithm];
    int len = next_offsets[(algorithm < 0 || algorithm > 3) ? 0 : algorithm] - off;
    FILE *f = fopen(efe_path, "rb");
    if (f) {
        fseek(f, 512 + off, SEEK_SET);
        unsigned char *buf = malloc(len);
        if (buf && fread(buf, 1, len, f) == (size_t)len) {
            /* Fill table A from VAR block */
            int words = len / 3;
            for (int i = 0; i < words; i++) {
                int32_t v = (buf[3*i] << 16) | (buf[3*i+1] << 8) | buf[3*i+2];
                if (v & 0x00800000) v |= 0xFF000000;
                uint32_t addr = (dsp->abase >> dsp->memshift) + i;
                dsp->dram[addr & ES5510_DRAM_MASK] = (int16_t)((v >> 8) & 0xffff);
            }

            /*
             * Emulate ROM loader four-word staging:
             * The 68k code stores four 24-bit offsets into ABASE+0xF0/0xF8/0x100/0x108
             * using (ptr - (A2+0x120))<<8 with bit0 cleared. The ptrs are the VAR block
             * offsets listed in the effect header (0x11e8, 0x126a, 0x12ec, 0x136e).
             */
            /* ptrs unused once we inject real staged words */
            int32_t v0 = 0x45FA00;
            int32_t v1 = 0x8E8DC0;
            int32_t v2 = 0x034E09;
            int32_t v3 = 0x60000E;
            uint32_t abase_words = (dsp->abase >> dsp->memshift);
            dsp->dram[(abase_words + 0xF0) & ES5510_DRAM_MASK] = (int16_t)((v0 >> 8) & 0xffff);
            dsp->dram[(abase_words + 0xF8) & ES5510_DRAM_MASK] = (int16_t)((v1 >> 8) & 0xffff);
            dsp->dram[(abase_words + 0x100) & ES5510_DRAM_MASK] = (int16_t)((v2 >> 8) & 0xffff);
            dsp->dram[(abase_words + 0x108) & ES5510_DRAM_MASK] = (int16_t)((v3 >> 8) & 0xffff);
            /* Also inject the init-built table at A2+0x120 (Musashi dump) */
            const uint8_t table120[] = {
                0x01,0x02, 0x00,0x60, 0x00,0x05, 0x03,0x75,
                0x0A,0x95, 0x96,0x00, 0x01,0x03, 0x81,0x7F,
                0x00,0x06, 0x03,0x87, 0x09,0x00, 0x74,0x00,
                0x14,0x16, 0x48,0x42, 0x12,0x3C, 0x00,0x91,
                0x4E,0xB8, 0x80,0x30, 0x44,0x82, 0x12,0x3C,
                0x00,0x92, 0x4E,0xF8, 0x80,0x30, 0x01,0x17,
                0x00,0x96, 0x00,0x08, 0x03,0x65, 0x07,0x04,
                0x00,0x00, 0x00,0x00, 0x01,0x00, 0x10,0x80,
                0x00,0x0A, 0x03,0x98, 0x09,0x00, 0x4D,0xEE,
                0x00,0x02, 0x61,0x00, 0x00,0x5A, 0x4D,0xEE,
                0x00,0x02, 0x61,0x00, 0x00,0x78, 0x4D,0xEE,
                0x00,0x02, 0x61,0x00, 0x00,0x96, 0x4D,0xEE,
                0x00,0x02, 0x61,0x00, 0x00,0xB4, 0x4D,0xEE,
                0x00,0x02, 0x61,0x00, 0x00,0xD2, 0x4D,0xEE,
                0x00,0x02, 0x61,0x00, 0x00,0xF0, 0x4D,0xEE,
                0x00,0x02, 0x61,0x00, 0x01,0x0E, 0x4D,0xEE,
                0x00,0x02, 0x61,0x00, 0x01,0x2C, 0x4D,0xEE,
                0x00,0x02, 0x61,0x00, 0x01,0x4A, 0x4D,0xEE,
                0x00,0x02, 0x61,0x00, 0x01,0x68, 0x4D,0xEE,
                0xFF,0xEC, 0x4E,0x75, 0x01,0x17, 0x02,0x82,
                0x00,0x0C, 0x03,0xC9, 0x09,0x00, 0x34,0x16,
                0x70,0x00, 0x10,0x2E, 0xFF,0xFE, 0xC4,0xC0,
                0xD4,0x82, 0x42,0x02, 0xD4,0xBC, 0x00,0x11,
                0x96,0x00, 0x12,0x3C, 0x00,0x05, 0x4E,0xF8,
                0x80,0x30, 0x01,0x17, 0x04,0x3A, 0x00,0x0E,
                0x03,0xD7, 0x09,0x00, 0x34,0x16, 0x70,0x00,
                0x10,0x2E, 0xFF,0xFC, 0xC4,0xC0, 0xD4,0x82,
                0x42,0x02, 0xD4,0xBC, 0x00,0x14, 0x19,0x00
            };
            for (size_t i = 0; i < sizeof(table120); i+=2) {
                uint16_t w = (table120[i]<<8) | table120[i+1];
                dsp->dram[(abase_words + 0x120 + (i/2)) & ES5510_DRAM_MASK] = w;
            }
        }
        free(buf);
        fclose(f);
    }

    /* Override key user-adjustable coefficients so CLI controls still work */
    int32_t decay_q23 = (int32_t)(decay * 8388607.0f);
    es5510_set_gpr(dsp, 117, decay_q23);   /* main decay */
    es5510_set_gpr(dsp, 121, decay_q23);   /* right decay */
    es5510_set_gpr(dsp, 69, 0x7FFFFF);     /* input gain unity */
    es5510_set_gpr(dsp, 123, 0x740000);    /* wet mix L */
    es5510_set_gpr(dsp, 187, 0x740000);    /* wet mix R */

    printf("REVERB: decay=%.2f, algorithm=%d\n", decay, algorithm);
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
    const char *effect_blob = getenv("EFFECT_RAW");  /* optional raw .efe (Giebler header stripped) */

    /* Optional debug tracing via environment */
    if (getenv("TRACE_MEM")) debug_memtrace = 1;
    if (getenv("TRACE_MAC")) debug_mac = 1;
    if (getenv("TRACE_RTA")) debug_rta = 1;
    if (getenv("TRACE_MUL")) debug_mul = 1;
    if (getenv("TRACE_SKIP")) debug_skip = 1;
    if (getenv("TRACE_ALU")) debug_alu = 1;
    if (getenv("TRACE_SAMPLES")) debug_trace_samples = atoi(getenv("TRACE_SAMPLES"));

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

    /* Load microcode (or full effect blob) */
    if (effect_blob) {
        if (load_effect_blob(effect_blob, &dsp) != 0) {
            es5510_free(&dsp);
            return 1;
        }
        printf("Loaded ESP microcode from effect blob: %s\n", effect_blob);
    } else {
        int microcode_len;
        uint8_t *microcode = load_microcode(microcode_file, &microcode_len);
        if (!microcode) {
            es5510_free(&dsp);
            return 1;
        }
        es5510_load_microcode(&dsp, microcode, microcode_len);
        free(microcode);
    }

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
