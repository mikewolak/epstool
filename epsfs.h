/*
 * epsfs.h - Ensoniq EPS/EPS-16+ Filesystem Definitions
 *
 * Based on the Ensoniq EPS/EPS-16+/ASR hard disk format specification.
 * Supports both floppy (1600 blocks) and hard disk images.
 */

#ifndef EPSFS_H
#define EPSFS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* Block and sector constants */
#define EPS_BLOCK_SIZE          512
#define EPS_FLOPPY_BLOCKS       1600
#define EPS_DIR_ENTRY_SIZE      26
#define EPS_DIR_ENTRIES_PER_BLOCK (EPS_BLOCK_SIZE / EPS_DIR_ENTRY_SIZE)  /* 19 entries + padding */
#define EPS_FAT_ENTRIES_PER_BLOCK 170   /* 512 / 3 = 170 entries per block */

/* Block layout (common to floppy and hard disk) */
#define EPS_BLOCK_UNUSED        0       /* Block 0 - unused/signature */
#define EPS_BLOCK_DEVICE_ID     1       /* Block 1 - device ID */
#define EPS_BLOCK_OS            2       /* Block 2 - OS info + root directory */
#define EPS_BLOCK_DIR_CONT      3       /* Block 3 - root directory continued */
#define EPS_BLOCK_DIR_CONT2     4       /* Block 4 - root directory continued */
#define EPS_BLOCK_FAT_START     5       /* Block 5+ - FAT blocks */

/* Signatures (big-endian in file) */
#define EPS_SIG_ID              0x4944  /* "ID" */
#define EPS_SIG_OS              0x4F53  /* "OS" */
#define EPS_SIG_DR              0x4452  /* "DR" - directory FAT block */
#define EPS_SIG_FB              0x4642  /* "FB" - FAT block */

/* FAT entry special values */
#define EPS_FAT_FREE            0x000000
#define EPS_FAT_END             0x000001    /* End of file chain */
#define EPS_FAT_BAD             0x000002    /* Bad block */

/* File types */
typedef enum {
    EPS_TYPE_UNUSED         = 0x00,
    EPS_TYPE_OS             = 0x01,
    EPS_TYPE_SUBDIR         = 0x02,
    EPS_TYPE_INSTRUMENT     = 0x03,
    EPS_TYPE_BANK           = 0x04,
    EPS_TYPE_SEQUENCE       = 0x05,
    EPS_TYPE_SONG           = 0x06,
    EPS_TYPE_SYSEX          = 0x07,
    EPS_TYPE_PARENT_DIR     = 0x08,
    EPS_TYPE_MACRO          = 0x09,
    /* VFX-SD/SD-1 types */
    EPS_TYPE_PROGRAM_1      = 0x0A,
    EPS_TYPE_PROGRAM_6      = 0x0B,
    EPS_TYPE_PROGRAM_30     = 0x0C,
    EPS_TYPE_PROGRAM_60     = 0x0D,
    /* More types... */
    EPS_TYPE_PRESET_1       = 0x0E,
    EPS_TYPE_PRESET_6       = 0x0F,
    EPS_TYPE_PRESET_30      = 0x10,
    EPS_TYPE_PRESET_60      = 0x11,
    EPS_TYPE_SEQ_VFX        = 0x12,
    EPS_TYPE_SONG_VFX       = 0x13,
    EPS_TYPE_SYSEX_VFX      = 0x14,
    EPS_TYPE_SEQ_SONG_VFX   = 0x15,
    EPS_TYPE_ALL_VFX        = 0x16,
    /* EPS-16+ specific */
    EPS_TYPE_EFFECT         = 0x17,
    EPS_TYPE_EPS16_INST     = 0x18,
    EPS_TYPE_EPS16_BANK     = 0x19,
    EPS_TYPE_EPS16_EFFECT   = 0x1A,
    EPS_TYPE_EPS16_OS       = 0x1B,
} eps_file_type_t;

/* Directory entry structure (26 bytes) */
typedef struct __attribute__((packed)) {
    uint8_t  type_info;         /* Type-dependent info (bank/preset for VFX-SD) */
    uint8_t  file_type;         /* File type (see eps_file_type_t) */
    char     filename[12];      /* Filename (space-padded) */
    uint16_t size_blocks;       /* File size in blocks (big-endian) */
    uint16_t contiguous;        /* Number of contiguous blocks (big-endian) */
    uint32_t first_block;       /* First block pointer (big-endian, only 3 bytes used) */
    uint8_t  reserved[4];       /* Reserved/additional info */
} eps_dir_entry_t;

/* Device ID block structure (at block 1) */
typedef struct __attribute__((packed)) {
    uint8_t  reserved1[6];
    uint16_t unknown1;
    uint8_t  reserved2[4];
    uint8_t  heads;             /* Number of heads (2 for floppy) */
    uint8_t  reserved3;
    uint16_t sectors_per_track; /* Sectors per track (big-endian) */
    uint8_t  reserved4[2];
    uint32_t total_blocks;      /* Total blocks (big-endian, but stored oddly) */
    uint8_t  reserved5[14];
    uint16_t signature;         /* "ID" = 0x4944 */
    uint8_t  reserved6[4];
    uint32_t free_blocks;       /* Free block count (big-endian) */
    uint8_t  label_flag;        /* 0xFF if label present */
    uint8_t  reserved7[3];
    uint8_t  os_info[2];        /* "OS" signature in OS section */
} eps_device_id_t;

/* Filesystem handle */
typedef struct {
    FILE    *fp;                /* File pointer to disk image */
    char    *filename;          /* Path to disk image */
    uint32_t total_blocks;      /* Total blocks in image */
    uint32_t free_blocks;       /* Free blocks */
    uint32_t fat_blocks;        /* Number of FAT blocks */
    uint32_t fat_start;         /* First FAT block number */
    uint32_t root_dir_block;    /* Root directory block */
    bool     is_hard_disk;      /* true if > 1600 blocks */
    char     disk_label[12];    /* Disk label if present */
    bool     has_label;
} eps_fs_t;

/* Directory handle for iteration */
typedef struct {
    eps_fs_t *fs;
    uint32_t  dir_block;        /* Current directory's first block */
    uint32_t  current_block;    /* Current block being read */
    int       entry_index;      /* Current entry within block */
    int       entries_read;     /* Total entries read */
} eps_dir_t;

/* File handle for reading/writing */
typedef struct {
    eps_fs_t *fs;
    eps_dir_entry_t entry;      /* Copy of directory entry */
    uint32_t current_block;     /* Current block in chain */
    uint32_t block_offset;      /* Offset within current block */
    uint32_t position;          /* Current byte position in file */
    uint32_t size_bytes;        /* Total size in bytes */
} eps_file_t;

/* --- Core Functions --- */

/* Open/close filesystem */
eps_fs_t *eps_open(const char *filename);
void eps_close(eps_fs_t *fs);

/* Block I/O */
int eps_read_block(eps_fs_t *fs, uint32_t block_num, void *buffer);
int eps_write_block(eps_fs_t *fs, uint32_t block_num, const void *buffer);

/* FAT operations */
uint32_t eps_fat_read(eps_fs_t *fs, uint32_t block_num);
int eps_fat_write(eps_fs_t *fs, uint32_t block_num, uint32_t value);
uint32_t eps_fat_alloc(eps_fs_t *fs);
int eps_fat_free_chain(eps_fs_t *fs, uint32_t start_block);

/* Directory operations */
eps_dir_t *eps_opendir(eps_fs_t *fs, uint32_t dir_block);
eps_dir_entry_t *eps_readdir(eps_dir_t *dir);
void eps_closedir(eps_dir_t *dir);
int eps_mkdir(eps_fs_t *fs, uint32_t parent_dir, const char *name);
eps_dir_entry_t *eps_find_entry(eps_fs_t *fs, uint32_t dir_block, const char *name);

/* File operations */
eps_file_t *eps_fopen(eps_fs_t *fs, uint32_t dir_block, const char *name);
size_t eps_fread(void *buffer, size_t size, size_t count, eps_file_t *file);
void eps_fclose(eps_file_t *file);
int eps_extract(eps_fs_t *fs, uint32_t dir_block, const char *name, const char *dest_path);
int eps_import(eps_fs_t *fs, uint32_t dir_block, const char *src_path, const char *name, eps_file_type_t type);

/* Utility functions */
const char *eps_type_name(eps_file_type_t type);
void eps_format_filename(const char *raw, char *formatted);
void eps_unformat_filename(const char *name, char *raw12);
uint32_t eps_get_block_count(eps_fs_t *fs);
uint32_t eps_get_free_blocks(eps_fs_t *fs);
void eps_print_info(eps_fs_t *fs);

/* Image creation */
int eps_mkimage(const char *filename, uint32_t size_mb, bool include_os);

/* Big-endian helpers */
static inline uint16_t eps_read_be16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline uint32_t eps_read_be24(const uint8_t *p) {
    return (uint32_t)((p[0] << 16) | (p[1] << 8) | p[2]);
}

static inline uint32_t eps_read_be32(const uint8_t *p) {
    return (uint32_t)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

static inline void eps_write_be16(uint8_t *p, uint16_t v) {
    p[0] = (v >> 8) & 0xFF;
    p[1] = v & 0xFF;
}

static inline void eps_write_be24(uint8_t *p, uint32_t v) {
    p[0] = (v >> 16) & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = v & 0xFF;
}

static inline void eps_write_be32(uint8_t *p, uint32_t v) {
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

#endif /* EPSFS_H */
