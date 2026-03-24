/*
 * efe_raw.c - Raw EPS Instrument File Format Implementation
 *
 * Reverse-engineered from factory sound analysis.
 * Supports parsing instrument structure and WAV export.
 */

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <errno.h>
#include "efe_raw.h"

/* WAV file header structure */
#pragma pack(push, 1)
typedef struct {
    char     riff_id[4];      /* "RIFF" */
    uint32_t file_size;       /* File size - 8 */
    char     wave_id[4];      /* "WAVE" */
    char     fmt_id[4];       /* "fmt " */
    uint32_t fmt_size;        /* 16 for PCM */
    uint16_t audio_format;    /* 1 = PCM */
    uint16_t num_channels;    /* 1 = mono */
    uint32_t sample_rate;     /* Sample rate */
    uint32_t byte_rate;       /* sample_rate * num_channels * bits/8 */
    uint16_t block_align;     /* num_channels * bits/8 */
    uint16_t bits_per_sample; /* 16 */
    char     data_id[4];      /* "data" */
    uint32_t data_size;       /* Sample data size */
} wav_header_t;
#pragma pack(pop)

/* Helper to read 16-bit little-endian */
static inline uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

/* Helper to read 32-bit little-endian */
static inline uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

/* Helper to read 16-bit big-endian (EPS sample format) */
static inline int16_t read_be16_signed(const uint8_t *p) {
    return (int16_t)((p[0] << 8) | p[1]);
}

/* Extract UTF-16LE name to ASCII
 * Note: EPS sometimes stores parameter bytes in the high byte of the first
 * few characters. We extract just the low byte if it's a printable ASCII char.
 */
static void extract_name_utf16le(const uint8_t *src, char *dest, int max_chars) {
    int i;
    for (i = 0; i < max_chars; i++) {
        uint8_t low_byte = src[i * 2];
        uint8_t high_byte = src[i * 2 + 1];

        /* If both bytes are 0, end of string */
        if (low_byte == 0 && high_byte == 0) break;

        /* If low byte is printable ASCII (0x20-0x7E), use it regardless of high byte
         * This handles EPS format where high byte may contain parameters */
        if (low_byte >= 0x20 && low_byte <= 0x7E) {
            dest[i] = (char)low_byte;
        } else if (low_byte == 0 && high_byte >= 0x20 && high_byte <= 0x7E) {
            /* Handle potential big-endian encoding */
            dest[i] = (char)high_byte;
        } else {
            /* Non-printable character, stop */
            break;
        }
        dest[i + 1] = '\0';
    }
    /* Trim trailing spaces */
    int len = strlen(dest);
    while (len > 0 && dest[len - 1] == ' ') {
        dest[--len] = '\0';
    }
}

/* Convert MIDI note to name */
const char *efe_midi_note_name(uint8_t note, char *buf, size_t bufsize) {
    if (bufsize < 5) return "???";
    int octave = (note / 12) - 1;  /* MIDI note 60 = C4 */
    int semitone = note % 12;
    snprintf(buf, bufsize, "%s%d", MIDI_NOTE_NAMES[semitone], octave);
    return buf;
}

/* Get sample rate from code */
uint32_t efe_get_sample_rate(uint8_t code) {
    if (code < EPS_NUM_SAMPLE_RATES) {
        return EPS_SAMPLE_RATES[code];
    }
    return 26000; /* Default to 26kHz if unknown */
}

/* Open raw EFE file */
efe_raw_t *efe_raw_open(const char *filename) {
    efe_raw_t *efe = calloc(1, sizeof(efe_raw_t));
    if (!efe) return NULL;

    efe->fp = fopen(filename, "rb");
    if (!efe->fp) {
        free(efe);
        return NULL;
    }

    efe->filename = strdup(filename);

    /* Get file size */
    fseek(efe->fp, 0, SEEK_END);
    efe->data_size = ftell(efe->fp);
    fseek(efe->fp, 0, SEEK_SET);

    /* Read entire file into memory */
    efe->data = malloc(efe->data_size);
    if (!efe->data) {
        fclose(efe->fp);
        free(efe->filename);
        free(efe);
        return NULL;
    }

    if (fread(efe->data, 1, efe->data_size, efe->fp) != efe->data_size) {
        free(efe->data);
        fclose(efe->fp);
        free(efe->filename);
        free(efe);
        return NULL;
    }

    /* Parse instrument structure */
    efe_raw_parse(efe);

    return efe;
}

/* Open from memory buffer */
efe_raw_t *efe_raw_open_mem(const uint8_t *data, size_t size) {
    efe_raw_t *efe = calloc(1, sizeof(efe_raw_t));
    if (!efe) return NULL;

    efe->fp = NULL;
    efe->filename = strdup("<memory>");
    efe->data_size = size;

    efe->data = malloc(size);
    if (!efe->data) {
        free(efe->filename);
        free(efe);
        return NULL;
    }

    memcpy(efe->data, data, size);

    /* Parse instrument structure */
    efe_raw_parse(efe);

    return efe;
}

void efe_raw_close(efe_raw_t *efe) {
    if (!efe) return;

    if (efe->fp) fclose(efe->fp);
    free(efe->data);
    free(efe->filename);
    free(efe);
}

/*
 * Parse EPS instrument structure
 *
 * Based on reverse-engineering of factory sounds:
 *
 * Structure layout (raw data, after Giebler 512-byte header if present):
 *
 * 0x000-0x07F: Instrument header
 *   - 0x008-0x01B: Instrument name (UTF-16LE style)
 *   - 0x024-0x027: Flags/parameters
 *   - 0x028:       Number of wavesamples or related count
 *
 * 0x066-0x085: Layer 1 allocation table (if present)
 * 0x086-0x0FF: Wavesample allocation table
 *   - Each entry: start_block (16-bit LE), block_count (16-bit LE)
 *   - Blocks are 512 bytes, relative to start of raw data
 *
 * 0x280: Layer 1 header
 *   - 0x298: Number of wavesamples in layer (16-bit LE)
 *   - 0x29A-0x31F: Wavesample name (UTF-16LE)
 *   - Key mapping table at 0x2C0
 *
 * 0x370: Wavesample parameter block
 *   - First byte: sample rate code?
 *   - 0x37A-0x39A: Wavesample name (UTF-16LE "UNNAMED WS")
 *
 * Sample data: At block locations from allocation table
 *   - 16-bit signed big-endian audio
 */
int efe_raw_parse(efe_raw_t *efe) {
    if (!efe || !efe->data || efe->data_size < 0x100) {
        return -1;
    }

    memset(&efe->info, 0, sizeof(efe->info));
    memset(efe->layers, 0, sizeof(efe->layers));
    memset(efe->wavesamples, 0, sizeof(efe->wavesamples));

    /* Extract instrument name from UTF-16LE at offset 0x0A */
    extract_name_utf16le(&efe->data[0x0A], efe->info.name, EFE_NAME_LEN);

    efe->info.size_bytes = efe->data_size;
    efe->info.size_blocks = (efe->data_size + 511) / 512;

    /* File type - default to instrument */
    efe->info.file_type = EFE_TYPE_INSTRUMENT;

    /* Number of layers - look at byte 0x24 or similar area */
    efe->info.num_layers = 1; /* Most instruments have 1 layer */

    /* Initialize layer */
    efe->layers[0].header_offset = 0x280;
    efe->layers[0].key_low = 0;
    efe->layers[0].key_high = 127;
    efe->layers[0].velocity_low = 0;
    efe->layers[0].velocity_high = 127;

    /* Get wavesample count from layer header at raw offset 0x298 */
    int expected_ws_count = 1;
    if (efe->data_size > 0x29A) {
        expected_ws_count = read_le16(&efe->data[0x298]);
        if (expected_ws_count < 1 || expected_ws_count > EFE_MAX_WAVESAMPLES) {
            expected_ws_count = 1;
        }
    }

    /* Try to get layer name at offset 0x29A */
    if (efe->data_size > 0x2B0) {
        extract_name_utf16le(&efe->data[0x29A], efe->layers[0].name, EFE_NAME_LEN);
    }

    /*
     * Parse wavesample allocation table at 0x08A (raw offset)
     *
     * Structure around 0x066:
     *   0x066: Marker byte (often 0x29)
     *   0x068-0x085: Layer 1 table (may be zeros)
     *   0x086-0x0BF: Wavesample allocation table
     *
     * Wavesample allocation format:
     *   First 4 bytes may be zeros (WS index 0)
     *   Then pairs of (start_block, block_count) as 16-bit LE values
     *   Blocks are 512 bytes, relative to start of raw data
     */
    size_t alloc_table_offset = 0x08A;  /* Skip to start of actual entries */
    int ws_count = 0;

    /* Check if we have a valid allocation table */
    if (efe->data_size > alloc_table_offset + 40 && expected_ws_count > 1) {
        /*
         * Multi-wavesample instrument: parse allocation table
         * Parse up to 10 wavesample entries (indices 1-10 are commonly used)
         */
        for (int i = 0; i < 10 && ws_count < EFE_MAX_WAVESAMPLES; i++) {
            size_t entry_offset = alloc_table_offset + (i * 4);
            if (entry_offset + 4 > efe->data_size) break;

            uint16_t start_block = read_le16(&efe->data[entry_offset]);
            uint16_t block_count = read_le16(&efe->data[entry_offset + 2]);

            /* Skip empty entries (block 0, count 0) or entries with count 0 */
            if (block_count == 0) continue;

            /* Calculate data offset and size */
            size_t data_offset = start_block * 512;
            size_t data_size = block_count * 512;

            /* Validate: data must be within file */
            if (data_offset + data_size > efe->data_size) continue;

            /* Validate: check if this looks like audio data */
            bool looks_like_audio = false;
            if (data_size >= 64 && data_offset + 32 <= efe->data_size) {
                int audio_count = 0;
                for (int j = 0; j < 16; j++) {
                    int16_t sample = read_be16_signed(&efe->data[data_offset + j * 2]);
                    if (abs(sample) > 0x80) audio_count++;
                }
                looks_like_audio = (audio_count >= 8);
            }

            if (!looks_like_audio) continue;

            /* Valid wavesample found */
            efe_raw_wavesample_t *ws = &efe->wavesamples[ws_count];

            ws->data_offset = data_offset;
            ws->data_size = data_size;
            ws->num_samples = data_size / 2;
            ws->sample_start = 0;
            ws->sample_end = ws->num_samples;

            /* Set name to wavesample index */
            snprintf(ws->name, EFE_NAME_LEN + 1, "WS_%d", i + 1);

            /* Default parameters */
            ws->root_note = 60;  /* Middle C */
            ws->fine_tune = 0;
            ws->loop_mode = 0;
            ws->loop_start = 0;
            ws->loop_end = ws->num_samples;

            /* Default sample rate - 26kHz is common */
            ws->sample_rate_code = 3;
            ws->sample_rate = 26000;

            /* Store allocation info for debugging */
            ws->header_offset = entry_offset;

            ws_count++;
        }
    }

    /*
     * If allocation table parsing didn't find any wavesamples,
     * or if this is a single-wavesample instrument, scan for sample data
     */
    if (ws_count == 0 || (ws_count == 0 && expected_ws_count == 1)) {
        /* Find where audio data starts by scanning from wavesample header area */
        size_t sample_data_start = 0;

        /* Start scanning from 0x490 (typical wavesample data start) */
        for (size_t offset = 0x490; offset < efe->data_size - 32; offset += 2) {
            int audio_count = 0;
            for (int i = 0; i < 16; i++) {
                if (offset + i*2 + 1 < efe->data_size) {
                    int16_t sample = read_be16_signed(&efe->data[offset + i * 2]);
                    /* For audio, look for non-zero values in typical audio range */
                    if (abs(sample) > 0x20 && abs(sample) < 0x7000) audio_count++;
                }
            }
            /* Audio should have mostly non-zero, varying samples */
            if (audio_count >= 10) {
                sample_data_start = offset;
                break;
            }
        }

        if (sample_data_start == 0 && efe->data_size > 0x600) {
            /* Fallback: assume sample data starts at 0x490 */
            sample_data_start = 0x490;
        }

        if (sample_data_start > 0 && sample_data_start < efe->data_size) {
            efe_raw_wavesample_t *ws = &efe->wavesamples[0];

            /* Try to get wavesample name from header area at 0x37A */
            if (efe->data_size > 0x39A) {
                extract_name_utf16le(&efe->data[0x37A], ws->name, EFE_NAME_LEN);
            }
            /* Validate name or fall back to instrument name */
            if (ws->name[0] == '\0' || ws->name[0] < 0x20 || ws->name[0] > 0x7E) {
                if (efe->info.name[0] != '\0') {
                    strncpy(ws->name, efe->info.name, EFE_NAME_LEN);
                } else {
                    strncpy(ws->name, "SAMPLE", EFE_NAME_LEN);
                }
                ws->name[EFE_NAME_LEN] = '\0';
            }

            ws->data_offset = sample_data_start;

            /*
             * Find sample data end by scanning for next layer/wavesample header.
             * Layer headers start with pattern: 0e 00 00 00 06 XX YY 00 YY 00
             * where YY is the layer number and XX varies.
             *
             * If no header found, sample data extends to end of file.
             */
            size_t sample_data_end = efe->data_size;
            for (size_t offset = sample_data_start; offset + 10 < efe->data_size; offset++) {
                /* Look for layer header signature: 0e 00 00 00 06 */
                if (efe->data[offset] == 0x0e &&
                    efe->data[offset + 1] == 0x00 &&
                    efe->data[offset + 2] == 0x00 &&
                    efe->data[offset + 3] == 0x00 &&
                    efe->data[offset + 4] == 0x06) {
                    /* Verify it looks like a layer header by checking for
                     * matching layer numbers at offsets +6 and +8 */
                    uint8_t layer_num1 = efe->data[offset + 6];
                    uint8_t layer_num2 = efe->data[offset + 8];
                    if (layer_num1 == layer_num2 && layer_num1 > 0 && layer_num1 <= 8) {
                        sample_data_end = offset;
                        break;
                    }
                }
            }

            ws->data_size = sample_data_end - sample_data_start;
            ws->num_samples = ws->data_size / 2;
            ws->sample_start = 0;
            ws->sample_end = ws->num_samples;
            ws->root_note = 60;
            ws->sample_rate_code = 3;
            ws->sample_rate = 26000;
            ws->loop_start = 0;
            ws->loop_end = ws->num_samples;
            ws->loop_mode = 0;
            ws->header_offset = 0x370;

            ws_count = 1;
        }
    }

    efe->info.num_wavesamples = ws_count;

    /* Set overall sample data info */
    if (ws_count > 0) {
        /* Find earliest and latest sample offsets */
        size_t min_offset = efe->data_size;
        size_t max_end = 0;

        for (int i = 0; i < ws_count; i++) {
            if (efe->wavesamples[i].data_offset < min_offset) {
                min_offset = efe->wavesamples[i].data_offset;
            }
            size_t end = efe->wavesamples[i].data_offset + efe->wavesamples[i].data_size;
            if (end > max_end) {
                max_end = end;
            }
        }

        efe->info.sample_data_offset = min_offset;
        efe->info.sample_data_size = max_end - min_offset;
    }

    efe->parsed = true;
    return 0;
}

/* Get instrument info */
int efe_raw_get_info(efe_raw_t *efe, efe_raw_info_t *info) {
    if (!efe || !efe->data || efe->data_size < 64) return -1;

    if (info) {
        *info = efe->info;
    }

    return 0;
}

/* Get layer info */
int efe_raw_get_layer(efe_raw_t *efe, int layer_num, efe_raw_layer_t *layer) {
    if (!efe || !efe->data || layer_num < 0 || layer_num >= EFE_MAX_LAYERS) {
        return -1;
    }

    if (layer_num >= efe->info.num_layers) {
        return -1;
    }

    if (layer) {
        *layer = efe->layers[layer_num];
    }

    return 0;
}

/* Get wavesample info */
int efe_raw_get_wavesample(efe_raw_t *efe, int ws_num, efe_raw_wavesample_t *ws) {
    if (!efe || !efe->data || ws_num < 0 || ws_num >= EFE_MAX_WAVESAMPLES) {
        return -1;
    }

    if (ws_num >= efe->info.num_wavesamples) {
        return -1;
    }

    if (ws) {
        *ws = efe->wavesamples[ws_num];
    }

    return 0;
}

/* Print basic info to stdout */
void efe_raw_print_info(efe_raw_t *efe) {
    if (!efe) return;

    printf("EPS Instrument File\n");
    printf("===================\n");
    printf("File:        %s\n", efe->filename);
    printf("Name:        %s\n", efe->info.name);
    printf("Size:        %u bytes (%u blocks)\n",
           (unsigned)efe->info.size_bytes, efe->info.size_blocks);
    printf("Layers:      %u\n", efe->info.num_layers);
    printf("Wavesamples: %u\n", efe->info.num_wavesamples);

    /* Print layer names if available */
    for (int i = 0; i < efe->info.num_layers && i < EFE_MAX_LAYERS; i++) {
        if (efe->layers[i].name[0]) {
            printf("  Layer %d:   %s\n", i + 1, efe->layers[i].name);
        }
    }
}

/* Print detailed report */
void efe_raw_print_report(efe_raw_t *efe) {
    if (!efe) return;

    char note_buf[8];

    printf("================================================================================\n");
    printf("EPS INSTRUMENT REPORT\n");
    printf("================================================================================\n\n");

    printf("File:            %s\n", efe->filename);
    printf("Instrument Name: %s\n", efe->info.name);
    printf("File Size:       %u bytes (%u blocks)\n",
           (unsigned)efe->info.size_bytes, efe->info.size_blocks);
    printf("Sample Data:     %u bytes at offset 0x%X\n",
           (unsigned)efe->info.sample_data_size,
           (unsigned)efe->info.sample_data_offset);
    printf("\n");

    printf("LAYERS (%u):\n", efe->info.num_layers);
    printf("--------------------------------------------------------------------------------\n");
    for (int i = 0; i < efe->info.num_layers && i < EFE_MAX_LAYERS; i++) {
        printf("  Layer %d: %-12s  Keys: %3d-%3d  Velocity: %3d-%3d\n",
               i + 1,
               efe->layers[i].name[0] ? efe->layers[i].name : "(unnamed)",
               efe->layers[i].key_low, efe->layers[i].key_high,
               efe->layers[i].velocity_low, efe->layers[i].velocity_high);
    }
    printf("\n");

    printf("WAVESAMPLES (%u):\n", efe->info.num_wavesamples);
    printf("--------------------------------------------------------------------------------\n");
    for (int i = 0; i < efe->info.num_wavesamples && i < EFE_MAX_WAVESAMPLES; i++) {
        efe_raw_wavesample_t *ws = &efe->wavesamples[i];

        printf("  Wavesample %d: %s\n", i + 1, ws->name);
        printf("    Root Note:    %s (MIDI %d)\n",
               efe_midi_note_name(ws->root_note, note_buf, sizeof(note_buf)),
               ws->root_note);
        printf("    Sample Rate:  %u Hz\n", ws->sample_rate);
        printf("    Samples:      %u (%.2f seconds)\n",
               ws->num_samples,
               (double)ws->num_samples / ws->sample_rate);
        printf("    Data Size:    %u bytes\n", ws->data_size);
        printf("    Loop:         %u - %u (%s)\n",
               ws->loop_start, ws->loop_end,
               ws->loop_mode == 0 ? "off" :
               ws->loop_mode == 1 ? "forward" : "bidirectional");
        printf("\n");
    }

    printf("================================================================================\n");
}

/*
 * Extract wavesample to WAV file
 *
 * Converts from EPS format (16-bit signed big-endian) to
 * standard WAV format (16-bit signed little-endian)
 */
int efe_raw_extract_wav(efe_raw_t *efe, int ws_num, const char *output_path) {
    if (!efe || !efe->data || ws_num < 0 || ws_num >= efe->info.num_wavesamples) {
        return -1;
    }

    efe_raw_wavesample_t *ws = &efe->wavesamples[ws_num];

    if (ws->data_offset + ws->data_size > efe->data_size) {
        fprintf(stderr, "Error: Wavesample data extends beyond file\n");
        return -1;
    }

    FILE *fp = fopen(output_path, "wb");
    if (!fp) {
        fprintf(stderr, "Error: Cannot create output file: %s\n", output_path);
        return -1;
    }

    /* Write WAV header */
    wav_header_t header;
    memcpy(header.riff_id, "RIFF", 4);
    header.file_size = ws->data_size + sizeof(wav_header_t) - 8;
    memcpy(header.wave_id, "WAVE", 4);
    memcpy(header.fmt_id, "fmt ", 4);
    header.fmt_size = 16;
    header.audio_format = 1;  /* PCM */
    header.num_channels = 1;  /* Mono */
    header.sample_rate = ws->sample_rate;
    header.bits_per_sample = 16;
    header.block_align = header.num_channels * (header.bits_per_sample / 8);
    header.byte_rate = header.sample_rate * header.block_align;
    memcpy(header.data_id, "data", 4);
    header.data_size = ws->data_size;

    fwrite(&header, sizeof(header), 1, fp);

    /* Convert and write sample data (BE to LE) */
    const uint8_t *src = &efe->data[ws->data_offset];
    for (uint32_t i = 0; i < ws->num_samples; i++) {
        int16_t sample = read_be16_signed(&src[i * 2]);
        /* Write as little-endian */
        uint8_t le[2];
        le[0] = sample & 0xFF;
        le[1] = (sample >> 8) & 0xFF;
        fwrite(le, 2, 1, fp);
    }

    fclose(fp);
    return 0;
}

/* Extract all wavesamples to directory */
int efe_raw_extract_all_wav(efe_raw_t *efe, const char *output_dir) {
    if (!efe) return -1;

    /* Create output directory if needed */
    mkdir(output_dir, 0755);

    char note_buf[8];
    int extracted = 0;

    for (int i = 0; i < efe->info.num_wavesamples && i < EFE_MAX_WAVESAMPLES; i++) {
        efe_raw_wavesample_t *ws = &efe->wavesamples[i];

        /* Build output filename with root note */
        char filename[512];
        const char *root = efe_midi_note_name(ws->root_note, note_buf, sizeof(note_buf));

        /* Clean up wavesample name for filename */
        char clean_name[EFE_NAME_LEN + 1];
        strncpy(clean_name, ws->name, EFE_NAME_LEN);
        clean_name[EFE_NAME_LEN] = '\0';
        for (int j = 0; clean_name[j]; j++) {
            if (!isalnum((unsigned char)clean_name[j]) && clean_name[j] != '-' && clean_name[j] != '_') {
                clean_name[j] = '_';
            }
        }
        /* Trim trailing underscores */
        int len = strlen(clean_name);
        while (len > 0 && clean_name[len-1] == '_') {
            clean_name[--len] = '\0';
        }

        snprintf(filename, sizeof(filename), "%s/%02d_%s_%s.wav",
                 output_dir, i + 1, clean_name, root);

        printf("Extracting: %s\n", filename);

        if (efe_raw_extract_wav(efe, i, filename) == 0) {
            extracted++;
        } else {
            fprintf(stderr, "  Failed to extract wavesample %d\n", i + 1);
        }
    }

    printf("Extracted %d wavesample(s)\n", extracted);
    return extracted;
}

/* Get raw data pointer */
const uint8_t *efe_raw_get_data(efe_raw_t *efe, size_t *size) {
    if (!efe) {
        if (size) *size = 0;
        return NULL;
    }
    if (size) *size = efe->data_size;
    return efe->data;
}

/* Write raw EFE to file */
int efe_raw_write(efe_raw_t *efe, const char *output_path) {
    if (!efe || !efe->data) return -1;

    FILE *fp = fopen(output_path, "wb");
    if (!fp) return -1;

    size_t written = fwrite(efe->data, 1, efe->data_size, fp);
    fclose(fp);

    return (written == efe->data_size) ? 0 : -1;
}

/* Create from WAV - placeholder */
int efe_raw_create_from_wav(const char *wav_path, const char *output_path,
                            const char *inst_name) {
    (void)wav_path;
    (void)output_path;
    (void)inst_name;
    /* TODO: Implement WAV import */
    fprintf(stderr, "WAV to EFE conversion not yet implemented\n");
    return -1;
}
