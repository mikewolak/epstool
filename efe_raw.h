/*
 * efe_raw.h - Raw EPS Instrument File Format (native EPS format)
 *
 * This is the format used directly on EPS disk images.
 * No header - raw instrument data starts at offset 0.
 */

#ifndef EFE_RAW_H
#define EFE_RAW_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* Instrument structure limits */
#define EFE_MAX_LAYERS      8
#define EFE_MAX_WAVESAMPLES 127
#define EFE_NAME_LEN        12

/* File type codes (from directory entry) */
#define EFE_TYPE_INSTRUMENT 0x03
#define EFE_TYPE_BANK       0x04
#define EFE_TYPE_SEQUENCE   0x05
#define EFE_TYPE_SONG       0x06
#define EFE_TYPE_SYSEX      0x07
#define EFE_TYPE_MACRO      0x09
#define EFE_TYPE_EFFECT     0x17

/* Instrument info structure */
typedef struct {
    char     name[EFE_NAME_LEN + 1];    /* Instrument name (null-terminated) */
    uint8_t  file_type;                  /* File type code */
    uint32_t size_bytes;                 /* Total file size */
    uint32_t size_blocks;                /* Size in 512-byte blocks */
    uint8_t  num_layers;                 /* Number of layers (1-8) */
    uint8_t  num_wavesamples;            /* Number of wavesamples */
} efe_raw_info_t;

/* Layer info structure */
typedef struct {
    char     name[EFE_NAME_LEN + 1];    /* Layer name */
    uint8_t  wavesample_count;           /* Wavesamples in this layer */
    uint8_t  key_low;                    /* Lowest key */
    uint8_t  key_high;                   /* Highest key */
} efe_raw_layer_t;

/* Wavesample info structure */
typedef struct {
    uint32_t sample_start;               /* Sample start offset */
    uint32_t sample_end;                 /* Sample end offset */
    uint32_t loop_start;                 /* Loop start point */
    uint32_t loop_end;                   /* Loop end point */
    uint8_t  root_note;                  /* Root key (MIDI note) */
    uint8_t  loop_mode;                  /* 0=off, 1=forward, 2=bidirectional */
    uint16_t sample_rate;                /* Sample rate in Hz */
} efe_raw_wavesample_t;

/* Raw EFE file handle */
typedef struct {
    FILE    *fp;                         /* File pointer */
    char    *filename;                   /* Path to file */
    uint8_t *data;                       /* File data buffer */
    size_t   data_size;                  /* Size of data buffer */
    efe_raw_info_t info;                 /* Parsed info */
} efe_raw_t;

/* --- Core Functions --- */

/* Open/close raw EFE file */
efe_raw_t *efe_raw_open(const char *filename);
void efe_raw_close(efe_raw_t *efe);

/* Open from memory buffer */
efe_raw_t *efe_raw_open_mem(const uint8_t *data, size_t size);

/* Get instrument info */
int efe_raw_get_info(efe_raw_t *efe, efe_raw_info_t *info);

/* Get layer info */
int efe_raw_get_layer(efe_raw_t *efe, int layer_num, efe_raw_layer_t *layer);

/* Get wavesample info */
int efe_raw_get_wavesample(efe_raw_t *efe, int ws_num, efe_raw_wavesample_t *ws);

/* Extract wavesample to WAV file */
int efe_raw_extract_wav(efe_raw_t *efe, int ws_num, const char *output_path);

/* Extract all wavesamples */
int efe_raw_extract_all_wav(efe_raw_t *efe, const char *output_dir);

/* Print info to stdout */
void efe_raw_print_info(efe_raw_t *efe);

/* --- Write Functions --- */

/* Create new raw EFE from WAV file */
int efe_raw_create_from_wav(const char *wav_path, const char *output_path,
                            const char *inst_name);

/* Write raw EFE to file */
int efe_raw_write(efe_raw_t *efe, const char *output_path);

/* Get raw data pointer and size (for disk import) */
const uint8_t *efe_raw_get_data(efe_raw_t *efe, size_t *size);

#endif /* EFE_RAW_H */
