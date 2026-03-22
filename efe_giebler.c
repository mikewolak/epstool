/*
 * efe_giebler.c - Giebler EFE/EFA File Format Implementation
 */

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "efe_giebler.h"

/* Helper to read 16-bit big-endian */
static inline uint16_t read_be16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

/* Helper to read 24-bit big-endian */
static inline uint32_t read_be24(const uint8_t *p) {
    return (uint32_t)((p[0] << 16) | (p[1] << 8) | p[2]);
}

/* Helper to write 16-bit big-endian */
static inline void write_be16(uint8_t *p, uint16_t v) {
    p[0] = (v >> 8) & 0xFF;
    p[1] = v & 0xFF;
}

/* Helper to write 24-bit big-endian */
static inline void write_be24(uint8_t *p, uint32_t v) {
    p[0] = (v >> 16) & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = v & 0xFF;
}

/* Check if file is Giebler format */
bool efe_giebler_is_giebler(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return false;

    uint8_t header[32];
    if (fread(header, 1, 32, fp) != 32) {
        fclose(fp);
        return false;
    }
    fclose(fp);

    /* Check for CR LF at start */
    if (header[0] != 0x0D || header[1] != 0x0A) {
        return false;
    }

    /* Check for "Eps File:" or "Asr File:" magic */
    if (memcmp(&header[2], "Eps File:", 9) == 0 ||
        memcmp(&header[2], "Asr File:", 9) == 0) {
        return true;
    }

    return false;
}

/* Parse Giebler header */
static int parse_header(const uint8_t *data, efe_giebler_header_t *header) {
    memset(header, 0, sizeof(efe_giebler_header_t));

    /* Check magic */
    if (data[0] != 0x0D || data[1] != 0x0A) {
        return -1;
    }

    /* Copy magic string */
    memcpy(header->magic, &data[2], 16);
    header->magic[16] = '\0';

    /* Check if EPS or ASR */
    header->is_asr = (memcmp(&data[2], "Asr File:", 9) == 0);

    /* Copy filename (12 bytes at offset 0x12) */
    memcpy(header->filename, &data[0x12], 12);
    header->filename[12] = '\0';

    /* Trim trailing spaces from filename */
    for (int i = 11; i >= 0 && header->filename[i] == ' '; i--) {
        header->filename[i] = '\0';
    }

    /* Copy type text (16 bytes at offset 0x1E) */
    memcpy(header->type_text, &data[0x1E], 16);
    header->type_text[16] = '\0';

    /* File type code at offset 0x32 */
    header->file_type = data[0x32];

    /* Size in blocks at offset 0x34 (big-endian) */
    header->size_blocks = read_be16(&data[0x34]);

    /* Contiguous blocks at offset 0x36 */
    header->contiguous = read_be16(&data[0x36]);

    /* Start block at offset 0x38 (24-bit big-endian) */
    header->start_block = read_be24(&data[0x38]);

    /* Multi-file index at offset 0x3B */
    header->multi_index = data[0x3B];

    return 0;
}

/* Open Giebler EFE file */
efe_giebler_t *efe_giebler_open(const char *filename) {
    if (!efe_giebler_is_giebler(filename)) {
        return NULL;
    }

    efe_giebler_t *efe = calloc(1, sizeof(efe_giebler_t));
    if (!efe) return NULL;

    efe->fp = fopen(filename, "rb");
    if (!efe->fp) {
        free(efe);
        return NULL;
    }

    efe->filename = strdup(filename);

    /* Read header */
    uint8_t header_data[GIEBLER_HEADER_SIZE];
    if (fread(header_data, 1, GIEBLER_HEADER_SIZE, efe->fp) != GIEBLER_HEADER_SIZE) {
        fclose(efe->fp);
        free(efe->filename);
        free(efe);
        return NULL;
    }

    if (parse_header(header_data, &efe->header) != 0) {
        fclose(efe->fp);
        free(efe->filename);
        free(efe);
        return NULL;
    }

    /* Get raw data size */
    fseek(efe->fp, 0, SEEK_END);
    long file_size = ftell(efe->fp);
    size_t raw_size = file_size - GIEBLER_HEADER_SIZE;

    /* Read raw data */
    fseek(efe->fp, GIEBLER_HEADER_SIZE, SEEK_SET);
    uint8_t *raw_data = malloc(raw_size);
    if (!raw_data) {
        fclose(efe->fp);
        free(efe->filename);
        free(efe);
        return NULL;
    }

    if (fread(raw_data, 1, raw_size, efe->fp) != raw_size) {
        free(raw_data);
        fclose(efe->fp);
        free(efe->filename);
        free(efe);
        return NULL;
    }

    /* Create raw EFE handle */
    efe->raw = efe_raw_open_mem(raw_data, raw_size);
    free(raw_data);

    if (!efe->raw) {
        fclose(efe->fp);
        free(efe->filename);
        free(efe);
        return NULL;
    }

    return efe;
}

void efe_giebler_close(efe_giebler_t *efe) {
    if (!efe) return;

    if (efe->raw) efe_raw_close(efe->raw);
    if (efe->fp) fclose(efe->fp);
    free(efe->filename);
    free(efe);
}

/* Get header info */
int efe_giebler_get_header(efe_giebler_t *efe, efe_giebler_header_t *header) {
    if (!efe || !header) return -1;
    memcpy(header, &efe->header, sizeof(efe_giebler_header_t));
    return 0;
}

/* Get underlying raw instrument data */
efe_raw_t *efe_giebler_get_raw(efe_giebler_t *efe) {
    return efe ? efe->raw : NULL;
}

/* Get instrument info */
int efe_giebler_get_info(efe_giebler_t *efe, efe_raw_info_t *info) {
    if (!efe || !efe->raw) return -1;
    return efe_raw_get_info(efe->raw, info);
}

/* Get file type name */
static const char *get_type_name(uint8_t type) {
    switch (type) {
        case 0x01: return "OS";
        case 0x02: return "Directory";
        case 0x03: return "Instrument";
        case 0x04: return "Bank";
        case 0x05: return "Sequence";
        case 0x06: return "Song";
        case 0x07: return "SysEx";
        case 0x09: return "Macro";
        case 0x17: return "Effect";
        case 0x19: return "EPS16 Instrument";
        case 0x1A: return "EPS16 Bank";
        case 0x1B: return "EPS16 OS";
        default:   return "Unknown";
    }
}

/* Print header and info to stdout */
void efe_giebler_print_info(efe_giebler_t *efe) {
    if (!efe) return;

    printf("Giebler EFE File\n");
    printf("================\n");
    printf("File:          %s\n", efe->filename);
    printf("Format:        %s\n", efe->header.is_asr ? "ASR (EFA)" : "EPS (EFE)");
    printf("Name:          %s\n", efe->header.filename);
    printf("Type:          %s (0x%02X)\n",
           get_type_name(efe->header.file_type), efe->header.file_type);
    printf("Size:          %u blocks (%u bytes)\n",
           efe->header.size_blocks, efe->header.size_blocks * 512);
    printf("Contiguous:    %u blocks\n", efe->header.contiguous);
    printf("Orig block:    %u\n", efe->header.start_block);
    if (efe->header.multi_index > 0) {
        printf("Multi-file:    Part %u\n", efe->header.multi_index);
    }

    /* Print raw instrument info if available */
    if (efe->raw) {
        printf("\n");
        efe_raw_print_info(efe->raw);
    }
}

/* Convert Giebler to raw EFE */
int efe_giebler_to_raw(const char *giebler_path, const char *raw_path) {
    efe_giebler_t *efe = efe_giebler_open(giebler_path);
    if (!efe) return -1;

    int result = efe_raw_write(efe->raw, raw_path);
    efe_giebler_close(efe);

    return result;
}

/* Build Giebler header */
void efe_giebler_build_header(uint8_t *header, const char *filename,
                              uint8_t file_type, uint16_t size_blocks,
                              bool is_asr) {
    memset(header, 0, GIEBLER_HEADER_SIZE);

    /* CR LF at start */
    header[0x00] = 0x0D;
    header[0x01] = 0x0A;

    /* Magic string */
    if (is_asr) {
        memcpy(&header[0x02], "Asr File:       ", 16);
    } else {
        memcpy(&header[0x02], "Eps File:       ", 16);
    }

    /* Filename (12 bytes, space-padded) */
    char padded_name[13];
    memset(padded_name, ' ', 12);
    padded_name[12] = '\0';
    size_t name_len = strlen(filename);
    if (name_len > 12) name_len = 12;
    memcpy(padded_name, filename, name_len);
    /* Convert to uppercase */
    for (int i = 0; i < 12; i++) {
        padded_name[i] = toupper((unsigned char)padded_name[i]);
    }
    memcpy(&header[0x12], padded_name, 12);

    /* Type text */
    const char *type_text;
    switch (file_type) {
        case 0x01: type_text = "EPS-OS         "; break;
        case 0x02: type_text = "Sub-Dir        "; break;
        case 0x03: type_text = "Instr          "; break;
        case 0x04: type_text = "EPS-Bnk        "; break;
        case 0x05: type_text = "Seq            "; break;
        case 0x06: type_text = "Song           "; break;
        case 0x07: type_text = "Sys-Ex         "; break;
        case 0x09: type_text = "Macro          "; break;
        case 0x17: type_text = "Effect         "; break;
        default:   type_text = "Unknown        "; break;
    }
    memcpy(&header[0x1E], type_text, 16);

    /* CR LF EOF at end of text header */
    header[0x2E] = 0x0D;
    header[0x2F] = 0x0A;
    header[0x30] = 0x1A;

    /* File type code */
    header[0x32] = file_type;

    /* Size in blocks (big-endian) */
    write_be16(&header[0x34], size_blocks);

    /* Contiguous (same as size for new files) */
    write_be16(&header[0x36], size_blocks);

    /* Start block - 0 for standalone files */
    write_be24(&header[0x38], 0);

    /* Multi-file index - 0 for single files */
    header[0x3B] = 0;
}

/* Convert raw EFE to Giebler */
int efe_raw_to_giebler(const char *raw_path, const char *giebler_path,
                       const char *filename, uint8_t file_type) {
    /* Open raw file */
    FILE *in = fopen(raw_path, "rb");
    if (!in) return -1;

    /* Get size */
    fseek(in, 0, SEEK_END);
    long raw_size = ftell(in);
    fseek(in, 0, SEEK_SET);

    uint16_t size_blocks = (raw_size + 511) / 512;

    /* Build header */
    uint8_t header[GIEBLER_HEADER_SIZE];
    efe_giebler_build_header(header, filename, file_type, size_blocks, false);

    /* Create output file */
    FILE *out = fopen(giebler_path, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }

    /* Write header */
    if (fwrite(header, 1, GIEBLER_HEADER_SIZE, out) != GIEBLER_HEADER_SIZE) {
        fclose(in);
        fclose(out);
        return -1;
    }

    /* Copy raw data */
    uint8_t buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return -1;
        }
    }

    fclose(in);
    fclose(out);
    return 0;
}

/* Create Giebler file from raw data buffer */
int efe_giebler_create(const uint8_t *raw_data, size_t raw_size,
                       const char *output_path, const char *filename,
                       uint8_t file_type, bool is_asr) {
    uint16_t size_blocks = (raw_size + 511) / 512;

    /* Build header */
    uint8_t header[GIEBLER_HEADER_SIZE];
    efe_giebler_build_header(header, filename, file_type, size_blocks, is_asr);

    /* Create output file */
    FILE *out = fopen(output_path, "wb");
    if (!out) return -1;

    /* Write header */
    if (fwrite(header, 1, GIEBLER_HEADER_SIZE, out) != GIEBLER_HEADER_SIZE) {
        fclose(out);
        return -1;
    }

    /* Write raw data */
    if (fwrite(raw_data, 1, raw_size, out) != raw_size) {
        fclose(out);
        return -1;
    }

    fclose(out);
    return 0;
}

/* Write Giebler file */
int efe_giebler_write(efe_giebler_t *efe, const char *output_path) {
    if (!efe || !efe->raw) return -1;

    size_t raw_size;
    const uint8_t *raw_data = efe_raw_get_data(efe->raw, &raw_size);
    if (!raw_data) return -1;

    return efe_giebler_create(raw_data, raw_size, output_path,
                              efe->header.filename, efe->header.file_type,
                              efe->header.is_asr);
}
