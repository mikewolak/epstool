/*
 * efe_raw.c - Raw EPS Instrument File Format Implementation
 */

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include "efe_raw.h"

/* Helper to read 16-bit little-endian */
static inline uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

/* Helper to read 32-bit little-endian */
static inline uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

/* Extract UTF-16LE name to ASCII */
static void extract_name_utf16le(const uint8_t *src, char *dest, int max_chars) {
    for (int i = 0; i < max_chars; i++) {
        uint16_t ch = read_le16(&src[i * 2]);
        if (ch == 0) break;
        dest[i] = (ch < 128) ? (char)ch : '?';
        dest[i + 1] = '\0';
    }
    /* Trim trailing spaces */
    int len = strlen(dest);
    while (len > 0 && dest[len - 1] == ' ') {
        dest[--len] = '\0';
    }
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

    /* Parse basic info */
    efe_raw_get_info(efe, &efe->info);

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

    /* Parse basic info */
    efe_raw_get_info(efe, &efe->info);

    return efe;
}

void efe_raw_close(efe_raw_t *efe) {
    if (!efe) return;

    if (efe->fp) fclose(efe->fp);
    free(efe->data);
    free(efe->filename);
    free(efe);
}

/* Get instrument info */
int efe_raw_get_info(efe_raw_t *efe, efe_raw_info_t *info) {
    if (!efe || !efe->data || efe->data_size < 64) return -1;

    memset(info, 0, sizeof(efe_raw_info_t));

    /*
     * Raw EPS instrument header structure (observed):
     * 0x00-0x09: Header/flags
     * 0x0A-0x25: Instrument name (UTF-16LE, 12 chars)
     * 0x26-0x27: Flags
     * 0x28-0x29: Number of layers
     * ...
     */

    /* Extract name from UTF-16LE at offset 0x0A */
    extract_name_utf16le(&efe->data[0x0A], info->name, EFE_NAME_LEN);

    info->size_bytes = efe->data_size;
    info->size_blocks = (efe->data_size + 511) / 512;

    /* Number of layers at offset 0x28 (little-endian 16-bit) */
    if (efe->data_size > 0x2A) {
        info->num_layers = efe->data[0x28];
        if (info->num_layers > EFE_MAX_LAYERS) {
            info->num_layers = EFE_MAX_LAYERS;
        }
    }

    /* File type - default to instrument */
    info->file_type = EFE_TYPE_INSTRUMENT;

    return 0;
}

/* Get layer info */
int efe_raw_get_layer(efe_raw_t *efe, int layer_num, efe_raw_layer_t *layer) {
    if (!efe || !efe->data || layer_num < 0 || layer_num >= EFE_MAX_LAYERS) {
        return -1;
    }

    memset(layer, 0, sizeof(efe_raw_layer_t));

    /*
     * Layer definitions appear to start around offset 0x280
     * Each layer has a name in UTF-16LE format
     * Structure needs more reverse engineering
     */

    /* Layer block starts around 0x280 + (layer_num * layer_size) */
    size_t layer_offset = 0x280 + (layer_num * 0x100);

    if (layer_offset + 0x30 > efe->data_size) {
        return -1;
    }

    /* Layer name at offset +0x0A in layer block */
    extract_name_utf16le(&efe->data[layer_offset + 0x0A], layer->name, EFE_NAME_LEN);

    return 0;
}

/* Get wavesample info */
int efe_raw_get_wavesample(efe_raw_t *efe, int ws_num, efe_raw_wavesample_t *ws) {
    if (!efe || !efe->data || ws_num < 0 || ws_num >= EFE_MAX_WAVESAMPLES) {
        return -1;
    }

    memset(ws, 0, sizeof(efe_raw_wavesample_t));

    /* Wavesample parameters need more reverse engineering */
    /* For now return placeholder */

    return -1; /* Not yet implemented */
}

/* Print info to stdout */
void efe_raw_print_info(efe_raw_t *efe) {
    if (!efe) return;

    printf("Raw EPS Instrument File\n");
    printf("=======================\n");
    printf("File:        %s\n", efe->filename);
    printf("Name:        %s\n", efe->info.name);
    printf("Size:        %u bytes (%u blocks)\n",
           (unsigned)efe->info.size_bytes, efe->info.size_blocks);
    printf("Layers:      %u\n", efe->info.num_layers);

    /* Print layer names if available */
    for (int i = 0; i < efe->info.num_layers && i < EFE_MAX_LAYERS; i++) {
        efe_raw_layer_t layer;
        if (efe_raw_get_layer(efe, i, &layer) == 0 && layer.name[0]) {
            printf("  Layer %d:   %s\n", i + 1, layer.name);
        }
    }
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

/* Extract wavesample to WAV - placeholder */
int efe_raw_extract_wav(efe_raw_t *efe, int ws_num, const char *output_path) {
    (void)efe;
    (void)ws_num;
    (void)output_path;
    /* TODO: Implement wavesample extraction */
    return -1;
}

/* Extract all wavesamples - placeholder */
int efe_raw_extract_all_wav(efe_raw_t *efe, const char *output_dir) {
    (void)efe;
    (void)output_dir;
    /* TODO: Implement bulk extraction */
    return -1;
}

/* Create from WAV - placeholder */
int efe_raw_create_from_wav(const char *wav_path, const char *output_path,
                            const char *inst_name) {
    (void)wav_path;
    (void)output_path;
    (void)inst_name;
    /* TODO: Implement WAV import */
    return -1;
}
