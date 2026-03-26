/*
 * efe_raw.h - Raw EPS Instrument File Format (native EPS format)
 *
 * This is the format used directly on EPS disk images.
 * No header - raw instrument data starts at offset 0.
 *
 * Reverse-engineered from factory sound analysis.
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
#define EFE_TYPE_EPS16_INST 0x18
#define EFE_TYPE_EPS16_BANK 0x19

/*
 * Raw EPS instrument layout (offsets from raw data start):
 *
 * 0x000-0x009: Instrument header flags
 * 0x00A-0x021: Instrument name (UTF-16LE, 12 chars)
 * 0x022-0x027: Flags/parameters
 * 0x028:       Number of layers (1-8)
 * 0x029-0x03F: More parameters
 * 0x040-0x05F: Keymap zone 1 (key ranges)
 * 0x060-0x07F: Keymap zone 2
 * ...
 * 0x280:       Layer 1 header (0x100 bytes per layer)
 * 0x380:       Layer 2 header
 * ...
 * 0x370+:      Wavesample parameter blocks (variable location)
 * End-N:       Sample data (16-bit signed big-endian)
 */

/* EPS sample rates (Hz) - indexed by sample rate code */
static const uint32_t EPS_SAMPLE_RATES[] = {
    52000,  /* 0: 52.0 kHz */
    39000,  /* 1: 39.0 kHz */
    31200,  /* 2: 31.2 kHz */
    26000,  /* 3: 26.0 kHz */
    19500,  /* 4: 19.5 kHz */
    15600,  /* 5: 15.6 kHz */
    13000,  /* 6: 13.0 kHz */
    9750,   /* 7: 9.75 kHz */
    7800,   /* 8: 7.8 kHz */
    6250,   /* 9: 6.25 kHz */
};
#define EPS_NUM_SAMPLE_RATES 10

/* MIDI note names for root key display */
static const char *MIDI_NOTE_NAMES[] __attribute__((unused)) = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

/* Instrument info structure */
typedef struct {
    char     name[EFE_NAME_LEN + 1];    /* Instrument name (null-terminated) */
    uint8_t  file_type;                  /* File type code */
    uint32_t size_bytes;                 /* Total file size */
    uint32_t size_blocks;                /* Size in 512-byte blocks */
    uint8_t  num_layers;                 /* Number of layers (1-8) */
    uint8_t  num_wavesamples;            /* Number of wavesamples */
    uint32_t sample_data_offset;         /* Offset to sample data in raw file */
    uint32_t sample_data_size;           /* Size of sample data in bytes */
} efe_raw_info_t;

/* Layer info structure */
typedef struct {
    char     name[EFE_NAME_LEN + 1];    /* Layer name */
    uint8_t  wavesample_count;           /* Wavesamples in this layer */
    uint8_t  key_low;                    /* Lowest key */
    uint8_t  key_high;                   /* Highest key */
    uint8_t  velocity_low;               /* Lowest velocity */
    uint8_t  velocity_high;              /* Highest velocity */
    uint32_t header_offset;              /* Offset of layer header in raw data */
} efe_raw_layer_t;

/* Wavesample info structure */
typedef struct {
    char     name[EFE_NAME_LEN + 1];    /* Wavesample name */
    uint32_t sample_start;               /* Sample start offset (in samples) */
    uint32_t sample_end;                 /* Sample end offset (in samples) */
    uint32_t loop_start;                 /* Loop start point (in samples) */
    uint32_t loop_end;                   /* Loop end point (in samples) */
    uint8_t  root_note;                  /* Root key (MIDI note 0-127) */
    int8_t   fine_tune;                  /* Fine tuning in cents */
    uint8_t  loop_mode;                  /* 0=off, 1=forward, 2=bidirectional */
    uint8_t  sample_rate_code;           /* Index into EPS_SAMPLE_RATES */
    uint32_t sample_rate;                /* Actual sample rate in Hz */
    uint32_t data_offset;                /* Offset of sample data in raw file */
    uint32_t data_size;                  /* Size of sample data in bytes */
    uint32_t num_samples;                /* Number of samples */
    uint32_t header_offset;              /* Offset of wavesample header in raw data */
} efe_raw_wavesample_t;

/* Raw EFE file handle */
typedef struct {
    FILE    *fp;                         /* File pointer */
    char    *filename;                   /* Path to file */
    uint8_t *data;                       /* File data buffer */
    size_t   data_size;                  /* Size of data buffer */
    efe_raw_info_t info;                 /* Parsed info */
    efe_raw_layer_t layers[EFE_MAX_LAYERS];
    efe_raw_wavesample_t wavesamples[EFE_MAX_WAVESAMPLES];
    bool     parsed;                     /* True if fully parsed */
} efe_raw_t;

/* --- Core Functions --- */

/* Open/close raw EFE file */
efe_raw_t *efe_raw_open(const char *filename);
void efe_raw_close(efe_raw_t *efe);

/* Open from memory buffer */
efe_raw_t *efe_raw_open_mem(const uint8_t *data, size_t size);

/* Parse instrument structure (called automatically by open) */
int efe_raw_parse(efe_raw_t *efe);

/* Get instrument info */
int efe_raw_get_info(efe_raw_t *efe, efe_raw_info_t *info);

/* Get layer info */
int efe_raw_get_layer(efe_raw_t *efe, int layer_num, efe_raw_layer_t *layer);

/* Get wavesample info */
int efe_raw_get_wavesample(efe_raw_t *efe, int ws_num, efe_raw_wavesample_t *ws);

/* Extract wavesample to WAV file */
int efe_raw_extract_wav(efe_raw_t *efe, int ws_num, const char *output_path);

/* Extract all wavesamples to directory */
int efe_raw_extract_all_wav(efe_raw_t *efe, const char *output_dir);

/* Print info to stdout */
void efe_raw_print_info(efe_raw_t *efe);

/* Print detailed report */
void efe_raw_print_report(efe_raw_t *efe);

/* --- Utility Functions --- */

/* Convert MIDI note number to name string (e.g., "C4", "F#5") */
const char *efe_midi_note_name(uint8_t note, char *buf, size_t bufsize);

/* Get sample rate from code */
uint32_t efe_get_sample_rate(uint8_t code);

/* --- Write Functions --- */

/* Create new raw EFE from WAV file */
int efe_raw_create_from_wav(const char *wav_path, const char *output_path,
                            const char *inst_name);

/* Write raw EFE to file */
int efe_raw_write(efe_raw_t *efe, const char *output_path);

/* Get raw data pointer and size (for disk import) */
const uint8_t *efe_raw_get_data(efe_raw_t *efe, size_t *size);

#endif /* EFE_RAW_H */
