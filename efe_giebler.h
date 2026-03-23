/*
 * efe_giebler.h - Giebler EFE/EFA File Format
 *
 * This format was created by Gary Giebler of Giebler Enterprises.
 * It consists of a 512-byte header followed by raw instrument data.
 *
 * Header structure:
 *   0x00-0x01: CR LF (0x0D 0x0A)
 *   0x02-0x11: "Eps File:       " or "Asr File:       "
 *   0x12-0x1D: Filename (12 bytes, space-padded)
 *   0x1E-0x2D: Type text (e.g., "Instr          ")
 *   0x2F-0x31: CR LF EOF (0x0D 0x0A 0x1A)
 *   0x32:      File type code
 *   0x33:      Reserved (0x00)
 *   0x34-0x35: Size in blocks (big-endian)
 *   0x36-0x37: Contiguous blocks (big-endian)
 *   0x38-0x3A: Start block (from original disk)
 *   0x3B:      Multi-file index
 *   0x3C-0x1FF: Zero padding
 *   0x200+:    Raw instrument data
 */

#ifndef EFE_GIEBLER_H
#define EFE_GIEBLER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "efe_raw.h"

/* Header constants */
#define GIEBLER_HEADER_SIZE     512
#define GIEBLER_MAGIC_EPS       "Eps File:       "
#define GIEBLER_MAGIC_ASR       "Asr File:       "
#define GIEBLER_NAME_OFFSET     0x12
#define GIEBLER_TYPE_TEXT_OFFSET 0x1E
#define GIEBLER_TYPE_CODE_OFFSET 0x32
#define GIEBLER_SIZE_OFFSET     0x34
#define GIEBLER_CONTIG_OFFSET   0x36
#define GIEBLER_START_OFFSET    0x38
#define GIEBLER_MULTI_OFFSET    0x3B

/* File type text strings */
#define GIEBLER_TYPE_TEXT_OS        "EPS-OS         "
#define GIEBLER_TYPE_TEXT_SUBDIR    "Sub-Dir        "
#define GIEBLER_TYPE_TEXT_INSTR     "Instr          "
#define GIEBLER_TYPE_TEXT_BANK      "EPS-Bnk        "
#define GIEBLER_TYPE_TEXT_SEQ       "Seq            "
#define GIEBLER_TYPE_TEXT_SONG      "Song           "
#define GIEBLER_TYPE_TEXT_SYSEX     "Sys-Ex         "
#define GIEBLER_TYPE_TEXT_MACRO     "Macro          "
#define GIEBLER_TYPE_TEXT_EFFECT    "Effect         "

/* Giebler header structure */
typedef struct {
    char     magic[17];                  /* "Eps File:       " or "Asr File:       " + null */
    char     filename[13];               /* Filename (space-padded) + null */
    char     type_text[17];              /* Type as text + null */
    uint8_t  file_type;                  /* File type code */
    uint16_t size_blocks;                /* Size in blocks */
    uint16_t contiguous;                 /* Contiguous blocks */
    uint32_t start_block;                /* Original start block */
    uint8_t  multi_index;                /* Multi-file index */
    bool     is_asr;                     /* true if ASR format */
} efe_giebler_header_t;

/* Giebler file handle */
typedef struct {
    FILE    *fp;                         /* File pointer */
    char    *filename;                   /* Path to file */
    efe_giebler_header_t header;         /* Parsed header */
    efe_raw_t *raw;                      /* Raw instrument data */
} efe_giebler_t;

/* --- Core Functions --- */

/* Open/close Giebler EFE file */
efe_giebler_t *efe_giebler_open(const char *filename);
void efe_giebler_close(efe_giebler_t *efe);

/* Check if file is Giebler format */
bool efe_giebler_is_giebler(const char *filename);

/* Get header info */
int efe_giebler_get_header(efe_giebler_t *efe, efe_giebler_header_t *header);

/* Get underlying raw instrument data */
efe_raw_t *efe_giebler_get_raw(efe_giebler_t *efe);

/* Get raw data pointer and size */
const uint8_t *efe_giebler_get_raw_data(efe_giebler_t *efe, size_t *size);

/* Get file type code */
uint8_t efe_giebler_get_type(efe_giebler_t *efe);

/* Get instrument info (wraps raw) */
int efe_giebler_get_info(efe_giebler_t *efe, efe_raw_info_t *info);

/* Print header and info to stdout */
void efe_giebler_print_info(efe_giebler_t *efe);

/* --- Conversion Functions --- */

/* Convert Giebler to raw EFE (strip header) */
int efe_giebler_to_raw(const char *giebler_path, const char *raw_path);

/* Convert raw EFE to Giebler (add header) */
int efe_raw_to_giebler(const char *raw_path, const char *giebler_path,
                       const char *filename, uint8_t file_type);

/* Create Giebler file from raw data buffer */
int efe_giebler_create(const uint8_t *raw_data, size_t raw_size,
                       const char *output_path, const char *filename,
                       uint8_t file_type, bool is_asr);

/* --- Write Functions --- */

/* Write Giebler file */
int efe_giebler_write(efe_giebler_t *efe, const char *output_path);

/* Build Giebler header */
void efe_giebler_build_header(uint8_t *header, const char *filename,
                              uint8_t file_type, uint16_t size_blocks,
                              bool is_asr);

#endif /* EFE_GIEBLER_H */
