/*
 * epsfs.c - Ensoniq EPS/EPS-16+ Filesystem Implementation
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include "epsfs.h"
#include "efe_giebler.h"

/* Directory constants - a directory is always a 2-block pair (1024 bytes) */
#define EPS_DIR_BLOCKS          2
#define EPS_DIR_PAIR_SIZE       (EPS_DIR_BLOCKS * EPS_BLOCK_SIZE)  /* 1024 bytes */
#define EPS_MAX_DIR_ENTRIES     39   /* Max entries per directory (39 * 26 = 1014 bytes) */

/* File type names */
static const char *type_names[] = {
    [EPS_TYPE_UNUSED]       = "Unused",
    [EPS_TYPE_OS]           = "OS",
    [EPS_TYPE_SUBDIR]       = "Directory",
    [EPS_TYPE_INSTRUMENT]   = "Instrument",
    [EPS_TYPE_BANK]         = "Bank",
    [EPS_TYPE_SEQUENCE]     = "Sequence",
    [EPS_TYPE_SONG]         = "Song",
    [EPS_TYPE_SYSEX]        = "SysEx",
    [EPS_TYPE_PARENT_DIR]   = "Parent Dir",
    [EPS_TYPE_MACRO]        = "Macro",
    [EPS_TYPE_PROGRAM_1]    = "Program(1)",
    [EPS_TYPE_PROGRAM_6]    = "Program(6)",
    [EPS_TYPE_PROGRAM_30]   = "Program(30)",
    [EPS_TYPE_PROGRAM_60]   = "Program(60)",
    [EPS_TYPE_PRESET_1]     = "Preset(1)",
    [EPS_TYPE_PRESET_6]     = "Preset(6)",
    [EPS_TYPE_PRESET_30]    = "Preset(30)",
    [EPS_TYPE_PRESET_60]    = "Preset(60)",
    [EPS_TYPE_SEQ_VFX]      = "VFX Seq",
    [EPS_TYPE_SONG_VFX]     = "VFX Song",
    [EPS_TYPE_SYSEX_VFX]    = "VFX SysEx",
    [EPS_TYPE_SEQ_SONG_VFX] = "VFX Seq+Song",
    [EPS_TYPE_ALL_VFX]      = "VFX All",
    [EPS_TYPE_EFFECT]       = "Effect",
    [EPS_TYPE_EPS16_INST]   = "EPS16 Inst",
    [EPS_TYPE_EPS16_BANK]   = "EPS16 Bank",
    [EPS_TYPE_EPS16_EFFECT] = "EPS16 Effect",
    [EPS_TYPE_EPS16_OS]     = "EPS16 OS",
};

const char *eps_type_name(eps_file_type_t type)
{
    if (type < sizeof(type_names) / sizeof(type_names[0]) && type_names[type]) {
        return type_names[type];
    }
    return "Unknown";
}

/* Format a raw 12-byte filename to a printable string (trim trailing spaces) */
void eps_format_filename(const char *raw, char *formatted)
{
    int i;
    memcpy(formatted, raw, 12);
    formatted[12] = '\0';

    /* Trim trailing spaces */
    for (i = 11; i >= 0 && formatted[i] == ' '; i--) {
        formatted[i] = '\0';
    }
}

/* Convert a name to raw 12-byte space-padded format */
void eps_unformat_filename(const char *name, char *raw12)
{
    size_t len = strlen(name);
    if (len > 12) len = 12;

    memset(raw12, ' ', 12);
    memcpy(raw12, name, len);

    /* Convert to uppercase */
    for (int i = 0; i < 12; i++) {
        raw12[i] = toupper((unsigned char)raw12[i]);
    }
}

/* Open a disk image */
eps_fs_t *eps_open(const char *filename)
{
    eps_fs_t *fs = calloc(1, sizeof(eps_fs_t));
    if (!fs) return NULL;

    fs->fp = fopen(filename, "r+b");
    if (!fs->fp) {
        /* Try read-only */
        fs->fp = fopen(filename, "rb");
        if (!fs->fp) {
            free(fs);
            return NULL;
        }
    }

    fs->filename = strdup(filename);

    /* Get file size to determine total blocks */
    fseek(fs->fp, 0, SEEK_END);
    long file_size = ftell(fs->fp);
    fs->total_blocks = file_size / EPS_BLOCK_SIZE;
    fseek(fs->fp, 0, SEEK_SET);

    fs->is_hard_disk = (fs->total_blocks > EPS_FLOPPY_BLOCKS);
    fs->root_dir_block = EPS_BLOCK_ROOT_DIR;  /* Root directory entries are in blocks 3-4 */
    fs->fat_start = EPS_BLOCK_FAT_START;

    /* Calculate FAT blocks needed: ceil(total_blocks / 170) */
    /* Each FAT block holds 170 3-byte entries */
    fs->fat_blocks = (fs->total_blocks + EPS_FAT_ENTRIES_PER_BLOCK - 1) / EPS_FAT_ENTRIES_PER_BLOCK;

    /* Read device ID block to get free blocks and label */
    uint8_t block[EPS_BLOCK_SIZE];
    if (eps_read_block(fs, EPS_BLOCK_DEVICE_ID, block) == 0) {
        /* Check for "ID" signature at offset 0x26 */
        if (block[0x26] == 'I' && block[0x27] == 'D') {
            /* Total blocks stored at offset 0x0E as 24-bit value */
            /* Formatted size seems to be 0x144fff for a 650MB disk = 1331199 */
        }
    }

    /* Read block 2 (OS block) for free block count and disk ID */
    if (eps_read_block(fs, EPS_BLOCK_OS, block) == 0) {
        /* Free blocks stored at offset 0x00-0x02 as 24-bit big-endian */
        /* Combined with byte at 0x02-0x03 */
        /* Format appears to be: [high 8 bits at 0x01] [low 16 bits at 0x02-0x03] */
        /* Or simply 24-bit at offset 0x01 */
        fs->free_blocks = eps_read_be24(&block[0x01]);

        /* Disk ID is at offset 0x04-0x07 in OS block */
        memcpy(fs->disk_id, &block[0x04], 4);
    }

    /*
     * Load entire FAT into memory for fast access.
     * Each FAT entry is 3 bytes, so total FAT size = total_blocks * 3 bytes.
     */
    size_t fat_size = fs->total_blocks * 3;
    fs->fat_cache = malloc(fat_size);
    if (fs->fat_cache) {
        /* Read all FAT blocks into the cache */
        uint8_t fat_block[EPS_BLOCK_SIZE];
        for (uint32_t fb = 0; fb < fs->fat_blocks; fb++) {
            if (eps_read_block(fs, fs->fat_start + fb, fat_block) == 0) {
                /* Copy FAT entries from this block to cache */
                for (int i = 0; i < EPS_FAT_ENTRIES_PER_BLOCK; i++) {
                    uint32_t entry_idx = fb * EPS_FAT_ENTRIES_PER_BLOCK + i;
                    if (entry_idx < fs->total_blocks) {
                        memcpy(&fs->fat_cache[entry_idx * 3], &fat_block[i * 3], 3);
                    }
                }
            }
        }
        fs->fat_dirty = false;
        /* Initialize free block hint to first data block */
        fs->next_free_hint = fs->fat_start + fs->fat_blocks;
    }

    return fs;
}

/* Sync free block count to disk */
static int eps_sync_free_blocks(eps_fs_t *fs)
{
    if (!fs || !fs->fp) return -1;

    uint8_t block[EPS_BLOCK_SIZE];
    if (eps_read_block(fs, EPS_BLOCK_OS, block) != 0) {
        return -1;
    }

    /* Write free block count at offset 0x01 as 24-bit big-endian */
    eps_write_be24(&block[0x01], fs->free_blocks);

    return eps_write_block(fs, EPS_BLOCK_OS, block);
}

/* Flush dirty FAT cache to disk */
static int eps_fat_flush(eps_fs_t *fs)
{
    if (!fs || !fs->fat_cache || !fs->fat_dirty) return 0;

    uint8_t fat_block[EPS_BLOCK_SIZE];
    for (uint32_t fb = 0; fb < fs->fat_blocks; fb++) {
        memset(fat_block, 0, EPS_BLOCK_SIZE);

        /* Copy FAT entries from cache to block */
        for (int i = 0; i < EPS_FAT_ENTRIES_PER_BLOCK; i++) {
            uint32_t entry_idx = fb * EPS_FAT_ENTRIES_PER_BLOCK + i;
            if (entry_idx < fs->total_blocks) {
                memcpy(&fat_block[i * 3], &fs->fat_cache[entry_idx * 3], 3);
            }
        }

        /* FAT block signature at end */
        fat_block[510] = 'F';
        fat_block[511] = 'B';

        if (eps_write_block(fs, fs->fat_start + fb, fat_block) != 0) {
            return -1;
        }
    }

    fs->fat_dirty = false;
    return 0;
}

void eps_close(eps_fs_t *fs)
{
    if (!fs) return;

    /* Flush FAT cache if dirty */
    eps_fat_flush(fs);

    /* Sync free block count before closing */
    eps_sync_free_blocks(fs);

    if (fs->fp) fclose(fs->fp);
    free(fs->filename);
    free(fs->fat_cache);
    free(fs);
}

/* Read a block from the disk image */
int eps_read_block(eps_fs_t *fs, uint32_t block_num, void *buffer)
{
    if (!fs || !fs->fp || block_num >= fs->total_blocks) {
        return -1;
    }

    if (fseek(fs->fp, (long)block_num * EPS_BLOCK_SIZE, SEEK_SET) != 0) {
        return -1;
    }

    if (fread(buffer, EPS_BLOCK_SIZE, 1, fs->fp) != 1) {
        return -1;
    }

    return 0;
}

/* Write a block to the disk image */
int eps_write_block(eps_fs_t *fs, uint32_t block_num, const void *buffer)
{
    if (!fs || !fs->fp || block_num >= fs->total_blocks) {
        return -1;
    }

    if (fseek(fs->fp, (long)block_num * EPS_BLOCK_SIZE, SEEK_SET) != 0) {
        return -1;
    }

    if (fwrite(buffer, EPS_BLOCK_SIZE, 1, fs->fp) != 1) {
        return -1;
    }

    return 0;
}

/* Read a FAT entry - uses cache for fast access */
uint32_t eps_fat_read(eps_fs_t *fs, uint32_t block_num)
{
    if (!fs || block_num >= fs->total_blocks) {
        return EPS_FAT_BAD;
    }

    /* Use cache if available */
    if (fs->fat_cache) {
        return eps_read_be24(&fs->fat_cache[block_num * 3]);
    }

    /* Fallback to disk read (shouldn't happen normally) */
    uint32_t fat_index = block_num;
    uint32_t fat_block = fs->fat_start + (fat_index / EPS_FAT_ENTRIES_PER_BLOCK);
    uint32_t entry_offset = (fat_index % EPS_FAT_ENTRIES_PER_BLOCK) * 3;

    uint8_t block[EPS_BLOCK_SIZE];
    if (eps_read_block(fs, fat_block, block) != 0) {
        return EPS_FAT_BAD;
    }

    /* Read 3-byte big-endian entry */
    return eps_read_be24(&block[entry_offset]);
}

/* Write a FAT entry - uses cache for fast access */
int eps_fat_write(eps_fs_t *fs, uint32_t block_num, uint32_t value)
{
    if (!fs || block_num >= fs->total_blocks) {
        return -1;
    }

    /* Use cache if available */
    if (fs->fat_cache) {
        eps_write_be24(&fs->fat_cache[block_num * 3], value);
        fs->fat_dirty = true;
        return 0;
    }

    /* Fallback to disk write (shouldn't happen normally) */
    uint32_t fat_index = block_num;
    uint32_t fat_block = fs->fat_start + (fat_index / EPS_FAT_ENTRIES_PER_BLOCK);
    uint32_t entry_offset = (fat_index % EPS_FAT_ENTRIES_PER_BLOCK) * 3;

    uint8_t block[EPS_BLOCK_SIZE];
    if (eps_read_block(fs, fat_block, block) != 0) {
        return -1;
    }

    /* Write 3-byte big-endian entry */
    eps_write_be24(&block[entry_offset], value);

    return eps_write_block(fs, fat_block, block);
}

/* Allocate a free block from FAT - uses hint for fast sequential allocation */
uint32_t eps_fat_alloc(eps_fs_t *fs)
{
    if (!fs) return 0;

    uint32_t data_start = fs->fat_start + fs->fat_blocks;

    /* Use hint if available and valid */
    uint32_t start = fs->next_free_hint;
    if (start < data_start) {
        start = data_start;
    }

    /* First, search from hint to end */
    for (uint32_t i = start; i < fs->total_blocks; i++) {
        if (eps_fat_read(fs, i) == EPS_FAT_FREE) {
            /* Mark as end-of-chain for now */
            eps_fat_write(fs, i, EPS_FAT_END);
            if (fs->free_blocks > 0) fs->free_blocks--;
            /* Update hint to next block for sequential allocation */
            fs->next_free_hint = i + 1;
            return i;
        }
    }

    /* Wrap around: search from data_start to hint */
    for (uint32_t i = data_start; i < start; i++) {
        if (eps_fat_read(fs, i) == EPS_FAT_FREE) {
            eps_fat_write(fs, i, EPS_FAT_END);
            if (fs->free_blocks > 0) fs->free_blocks--;
            fs->next_free_hint = i + 1;
            return i;
        }
    }

    return 0; /* No free blocks */
}

/* Free a chain of blocks */
int eps_fat_free_chain(eps_fs_t *fs, uint32_t start_block)
{
    if (!fs || start_block == 0) return -1;

    uint32_t block = start_block;
    while (block != EPS_FAT_END && block != EPS_FAT_FREE && block != EPS_FAT_BAD) {
        uint32_t next = eps_fat_read(fs, block);
        eps_fat_write(fs, block, EPS_FAT_FREE);
        fs->free_blocks++;
        block = next;
    }

    return 0;
}

/* Forward declaration */
static uint32_t read_dir_pair(eps_fs_t *fs, uint32_t first_block, uint8_t *dir_buf);

/* Open a directory for reading */
eps_dir_t *eps_opendir(eps_fs_t *fs, uint32_t dir_block)
{
    if (!fs) return NULL;

    eps_dir_t *dir = calloc(1, sizeof(eps_dir_t));
    if (!dir) return NULL;

    dir->fs = fs;
    dir->dir_block = dir_block;
    dir->entry_index = 0;
    dir->entries_read = 0;
    dir->loaded = false;

    /* Load the full directory pair (2 blocks, 1024 bytes) */
    uint32_t second = read_dir_pair(fs, dir_block, dir->dir_buf);
    if (!second) {
        free(dir);
        return NULL;
    }
    dir->second_block = second;
    dir->loaded = true;

    return dir;
}

/*
 * Read a directory pair (2 blocks, 1024 bytes) into a buffer.
 * Entries span continuously across both blocks (39 entries max).
 * For root directory (block 3), block 4 is the implicit second block.
 * For subdirectories, the second block is found via FAT lookup.
 * Returns the second block number, or 0 on error.
 */
static uint32_t read_dir_pair(eps_fs_t *fs, uint32_t first_block, uint8_t *dir_buf)
{
    /* Read first block */
    if (eps_read_block(fs, first_block, dir_buf) != 0) {
        return 0;
    }

    uint32_t second_block;

    /* Root directory (block 3) has block 4 as its implicit second block */
    if (first_block == EPS_BLOCK_ROOT_DIR) {
        second_block = EPS_BLOCK_ROOT_DIR2;  /* Block 4 */
    } else {
        /* Get second block from FAT */
        second_block = eps_fat_read(fs, first_block);
        if (second_block == EPS_FAT_END || second_block == EPS_FAT_FREE || second_block == EPS_FAT_BAD) {
            return 0;
        }
    }

    /* Read second block into second half of buffer */
    if (eps_read_block(fs, second_block, dir_buf + EPS_BLOCK_SIZE) != 0) {
        return 0;
    }

    return second_block;
}

/*
 * Write a directory pair (2 blocks, 1024 bytes) from a buffer.
 */
static int write_dir_pair(eps_fs_t *fs, uint32_t first_block, uint32_t second_block,
                          const uint8_t *dir_buf)
{
    if (eps_write_block(fs, first_block, dir_buf) != 0) {
        return -1;
    }
    if (eps_write_block(fs, second_block, dir_buf + EPS_BLOCK_SIZE) != 0) {
        return -1;
    }
    return 0;
}

/*
 * Find a free slot in a directory pair buffer.
 * Returns slot index (0-38) or -1 if directory is full.
 * All directories (root or subdirectory) have entries starting at offset 0.
 */
static int find_free_slot_in_dir(const uint8_t *dir_buf, uint32_t start_block)
{
    (void)start_block;  /* All directories have same layout now */

    for (int i = 0; i < EPS_MAX_DIR_ENTRIES; i++) {
        const uint8_t *entry_ptr = dir_buf + (i * EPS_DIR_ENTRY_SIZE);
        if (entry_ptr[1] == EPS_TYPE_UNUSED) {
            return i;
        }
    }
    return -1;
}

/*
 * Write an entry to a directory pair buffer at the specified slot.
 * All directories (root or subdirectory) have entries starting at offset 0.
 */
static void write_entry_to_dir(uint8_t *dir_buf, uint32_t start_block, int slot,
                               uint8_t type_info, uint8_t file_type, const char *name,
                               uint16_t size_blocks, uint16_t contiguous, uint32_t first_block_ptr)
{
    (void)start_block;  /* All directories have same layout now */
    uint8_t *entry = dir_buf + (slot * EPS_DIR_ENTRY_SIZE);

    entry[0] = type_info;
    entry[1] = file_type;
    eps_unformat_filename(name, (char *)&entry[2]);
    eps_write_be16(&entry[14], size_blocks);
    eps_write_be16(&entry[16], contiguous);
    eps_write_be32(&entry[18], first_block_ptr);
}

/* Read next directory entry */
eps_dir_entry_t *eps_readdir(eps_dir_t *dir)
{
    static eps_dir_entry_t entry;

    if (!dir || !dir->fs || !dir->loaded) return NULL;

    /* Iterate through all 39 possible entries in the directory pair */
    while (dir->entry_index < EPS_MAX_DIR_ENTRIES) {
        /* Entries are laid out continuously across both blocks */
        uint8_t *entry_ptr = dir->dir_buf + (dir->entry_index * EPS_DIR_ENTRY_SIZE);

        dir->entry_index++;
        dir->entries_read++;

        /* Skip unused entries */
        if (entry_ptr[1] == EPS_TYPE_UNUSED) {
            continue;
        }

        /* Copy entry data - format is: type_info(1), file_type(1), name(12), size(2), contig(2), block(4), reserved(2) */
        entry.type_info = entry_ptr[0];
        entry.file_type = entry_ptr[1];
        memcpy(entry.filename, &entry_ptr[2], 12);
        entry.size_blocks = eps_read_be16(&entry_ptr[14]);
        entry.contiguous = eps_read_be16(&entry_ptr[16]);
        entry.first_block = eps_read_be32(&entry_ptr[18]);

        return &entry;
    }

    return NULL;  /* No more entries */
}

void eps_closedir(eps_dir_t *dir)
{
    free(dir);
}

/* Find an entry by name in a directory */
eps_dir_entry_t *eps_find_entry(eps_fs_t *fs, uint32_t dir_block, const char *name)
{
    eps_dir_t *dir = eps_opendir(fs, dir_block);
    if (!dir) return NULL;

    char raw_name[12];
    eps_unformat_filename(name, raw_name);

    eps_dir_entry_t *entry;
    static eps_dir_entry_t found;

    while ((entry = eps_readdir(dir)) != NULL) {
        if (memcmp(entry->filename, raw_name, 12) == 0) {
            memcpy(&found, entry, sizeof(found));
            eps_closedir(dir);
            return &found;
        }
    }

    eps_closedir(dir);
    return NULL;
}

/* Open a file for reading */
eps_file_t *eps_fopen(eps_fs_t *fs, uint32_t dir_block, const char *name)
{
    eps_dir_entry_t *entry = eps_find_entry(fs, dir_block, name);
    if (!entry) return NULL;

    eps_file_t *file = calloc(1, sizeof(eps_file_t));
    if (!file) return NULL;

    file->fs = fs;
    memcpy(&file->entry, entry, sizeof(eps_dir_entry_t));
    file->current_block = entry->first_block;
    file->block_offset = 0;
    file->position = 0;
    file->size_bytes = entry->size_blocks * EPS_BLOCK_SIZE;

    return file;
}

/* Read from a file */
size_t eps_fread(void *buffer, size_t size, size_t count, eps_file_t *file)
{
    if (!file || !buffer || !file->fs) return 0;

    size_t total_bytes = size * count;
    size_t bytes_read = 0;
    uint8_t *out = (uint8_t *)buffer;
    uint8_t block[EPS_BLOCK_SIZE];

    while (bytes_read < total_bytes && file->position < file->size_bytes) {
        /* Read current block */
        if (eps_read_block(file->fs, file->current_block, block) != 0) {
            break;
        }

        /* Calculate how much to read from this block */
        size_t available = EPS_BLOCK_SIZE - file->block_offset;
        size_t remaining = total_bytes - bytes_read;
        size_t file_remaining = file->size_bytes - file->position;

        size_t to_read = available;
        if (to_read > remaining) to_read = remaining;
        if (to_read > file_remaining) to_read = file_remaining;

        memcpy(out + bytes_read, block + file->block_offset, to_read);
        bytes_read += to_read;
        file->position += to_read;
        file->block_offset += to_read;

        /* Move to next block if needed */
        if (file->block_offset >= EPS_BLOCK_SIZE) {
            /* Check if we should follow contiguous blocks or FAT chain */
            uint32_t next = eps_fat_read(file->fs, file->current_block);
            if (next == EPS_FAT_END || next == EPS_FAT_FREE) {
                break;
            }
            file->current_block = next;
            file->block_offset = 0;
        }
    }

    return bytes_read / size;
}

void eps_fclose(eps_file_t *file)
{
    free(file);
}

/* Extract a file to the host filesystem */
int eps_extract(eps_fs_t *fs, uint32_t dir_block, const char *name, const char *dest_path)
{
    eps_file_t *file = eps_fopen(fs, dir_block, name);
    if (!file) return -1;

    FILE *out = fopen(dest_path, "wb");
    if (!out) {
        eps_fclose(file);
        return -1;
    }

    uint8_t buffer[EPS_BLOCK_SIZE];
    size_t read;

    while ((read = eps_fread(buffer, 1, EPS_BLOCK_SIZE, file)) > 0) {
        if (fwrite(buffer, 1, read, out) != read) {
            fclose(out);
            eps_fclose(file);
            return -1;
        }
    }

    fclose(out);
    eps_fclose(file);
    return 0;
}

/* Get filesystem statistics */
uint32_t eps_get_block_count(eps_fs_t *fs)
{
    return fs ? fs->total_blocks : 0;
}

uint32_t eps_get_free_blocks(eps_fs_t *fs)
{
    return fs ? fs->free_blocks : 0;
}

/* Print filesystem information */
void eps_print_info(eps_fs_t *fs)
{
    if (!fs) return;

    printf("Ensoniq EPS Filesystem\n");
    printf("======================\n");
    printf("Image file:     %s\n", fs->filename);
    printf("Disk ID:        %02X %02X %02X %02X\n",
           fs->disk_id[0], fs->disk_id[1], fs->disk_id[2], fs->disk_id[3]);
    printf("Total blocks:   %u (%u KB / %u MB)\n", fs->total_blocks,
           (fs->total_blocks * EPS_BLOCK_SIZE) / 1024,
           (fs->total_blocks * EPS_BLOCK_SIZE) / (1024 * 1024));
    printf("Free blocks:    %u (%u KB / %u MB)\n", fs->free_blocks,
           (fs->free_blocks * EPS_BLOCK_SIZE) / 1024,
           (fs->free_blocks * EPS_BLOCK_SIZE) / (1024 * 1024));
    printf("Type:           %s\n", fs->is_hard_disk ? "Hard Disk" : "Floppy");
    printf("FAT blocks:     %u (starting at block %u)\n", fs->fat_blocks, fs->fat_start);
    printf("Root dir block: %u\n", fs->root_dir_block);
    if (fs->has_label) {
        printf("Disk label:     %s\n", fs->disk_label);
    }
}

/* Create a subdirectory */
int eps_mkdir(eps_fs_t *fs, uint32_t parent_dir, const char *name)
{
    if (!fs || !name) return -1;

    /* Check if name already exists */
    if (eps_find_entry(fs, parent_dir, name)) {
        return -1; /* Already exists */
    }

    /* Allocate blocks for new directory (2 blocks typical) */
    uint32_t dir_block1 = eps_fat_alloc(fs);
    uint32_t dir_block2 = eps_fat_alloc(fs);

    if (!dir_block1 || !dir_block2) {
        if (dir_block1) eps_fat_write(fs, dir_block1, EPS_FAT_FREE);
        return -1;
    }

    /* Link the two directory blocks */
    eps_fat_write(fs, dir_block1, dir_block2);
    eps_fat_write(fs, dir_block2, EPS_FAT_END);

    /* Initialize directory blocks - block 2 gets "DR" signature */
    uint8_t empty_block[EPS_BLOCK_SIZE] = {0};
    eps_write_block(fs, dir_block1, empty_block);
    /* Second directory block has "DR" signature at bytes 510-511 */
    empty_block[510] = 'D';
    empty_block[511] = 'R';
    eps_write_block(fs, dir_block2, empty_block);

    /* Create parent directory entry in new directory */
    uint8_t block[EPS_BLOCK_SIZE] = {0};

    /* First entry: parent directory pointer */
    block[0] = 0;  /* type_info */
    block[1] = EPS_TYPE_PARENT_DIR;
    /* Parent name - for root level this would be empty/spaces, for subdirs it's parent name */
    memset(&block[2], ' ', 12);
    eps_write_be16(&block[14], 0);  /* size_blocks = 0 for parent entry */
    eps_write_be16(&block[16], 2);  /* contiguous */
    /* For root-level directories, parent points to block 3, not block 2 */
    uint32_t parent_block = (parent_dir == EPS_BLOCK_OS) ? 3 : parent_dir;
    eps_write_be32(&block[18], parent_block);

    eps_write_block(fs, dir_block1, block);

    /*
     * Add entry to parent directory.
     * Directories are stored as 2-block pairs with entries spanning continuously.
     */
    uint8_t dir_buf[EPS_DIR_PAIR_SIZE];
    uint32_t second_block = read_dir_pair(fs, parent_dir, dir_buf);

    if (!second_block) {
        eps_fat_free_chain(fs, dir_block1);
        return -1;
    }

    int slot = find_free_slot_in_dir(dir_buf, parent_dir);
    if (slot < 0) {
        eps_fat_free_chain(fs, dir_block1);
        return -1;
    }

    /* Write directory entry to the pair buffer */
    write_entry_to_dir(dir_buf, parent_dir, slot, 0, EPS_TYPE_SUBDIR, name,
                       2, 2, dir_block1);

    /* Write both directory blocks back */
    if (write_dir_pair(fs, parent_dir, second_block, dir_buf) != 0) {
        eps_fat_free_chain(fs, dir_block1);
        return -1;
    }

    /*
     * For root directory (parent_dir == EPS_BLOCK_OS), the entry is written to both
     * block 2 (at offset 0x1E) and block 3 (at offset 0) as part of the pair.
     * This is handled automatically by write_entry_to_dir and write_dir_pair.
     */

    return 0;
}

/* Import a file from host filesystem */
int eps_import(eps_fs_t *fs, uint32_t dir_block, const char *src_path,
               const char *name, eps_file_type_t type)
{
    if (!fs || !src_path || !name) return -1;

    /* Check if name already exists */
    if (eps_find_entry(fs, dir_block, name)) {
        return -1;
    }

    FILE *in = fopen(src_path, "rb");
    if (!in) return -1;

    /* Get file size */
    fseek(in, 0, SEEK_END);
    long file_size = ftell(in);
    fseek(in, 0, SEEK_SET);

    /* Calculate blocks needed */
    uint32_t blocks_needed = (file_size + EPS_BLOCK_SIZE - 1) / EPS_BLOCK_SIZE;
    if (blocks_needed == 0) blocks_needed = 1;

    /* Check free space */
    if (blocks_needed > fs->free_blocks) {
        fclose(in);
        return -1;
    }

    /* Allocate blocks */
    uint32_t *block_list = malloc(blocks_needed * sizeof(uint32_t));
    if (!block_list) {
        fclose(in);
        return -1;
    }

    for (uint32_t i = 0; i < blocks_needed; i++) {
        block_list[i] = eps_fat_alloc(fs);
        if (!block_list[i]) {
            /* Free already allocated blocks */
            for (uint32_t j = 0; j < i; j++) {
                eps_fat_write(fs, block_list[j], EPS_FAT_FREE);
            }
            free(block_list);
            fclose(in);
            return -1;
        }
    }

    /* Link blocks in FAT */
    for (uint32_t i = 0; i < blocks_needed - 1; i++) {
        eps_fat_write(fs, block_list[i], block_list[i + 1]);
    }
    eps_fat_write(fs, block_list[blocks_needed - 1], EPS_FAT_END);

    /* Write file data */
    uint8_t buffer[EPS_BLOCK_SIZE];
    for (uint32_t i = 0; i < blocks_needed; i++) {
        memset(buffer, 0, EPS_BLOCK_SIZE);
        size_t read = fread(buffer, 1, EPS_BLOCK_SIZE, in);
        (void)read; /* May be less than block size for last block */
        eps_write_block(fs, block_list[i], buffer);
    }

    fclose(in);

    /* Calculate contiguous blocks */
    uint32_t contiguous = 1;
    for (uint32_t i = 1; i < blocks_needed; i++) {
        if (block_list[i] == block_list[i-1] + 1) {
            contiguous++;
        } else {
            break;
        }
    }

    /*
     * Find free slot in directory.
     * Directories are stored as 2-block pairs (1024 bytes) with entries
     * spanning continuously across both blocks (39 entries max).
     */
    uint8_t dir_buf[EPS_DIR_PAIR_SIZE];
    uint32_t second_block = read_dir_pair(fs, dir_block, dir_buf);

    if (!second_block) {
        eps_fat_free_chain(fs, block_list[0]);
        free(block_list);
        return -1;
    }

    int slot = find_free_slot_in_dir(dir_buf, dir_block);
    if (slot < 0) {
        eps_fat_free_chain(fs, block_list[0]);
        free(block_list);
        return -1;
    }

    /* Write directory entry to the pair buffer */
    write_entry_to_dir(dir_buf, dir_block, slot, 0, type, name,
                       blocks_needed, contiguous, block_list[0]);

    /* Write both directory blocks back */
    if (write_dir_pair(fs, dir_block, second_block, dir_buf) != 0) {
        eps_fat_free_chain(fs, block_list[0]);
        free(block_list);
        return -1;
    }

    /*
     * For root directory (block 2), the entry is written to both:
     * - Block 2 at offset 0x1E (for epstool compatibility)
     * - Block 3 at offset 0 (for epslin compatibility)
     * The write_entry_to_dir handles block 2 offset, and block 3 is
     * already written as part of the pair (it's the second block).
     */

    free(block_list);
    return 0;
}

/*
 * Import a file with automatic Giebler format detection.
 * If the file has a Giebler header, strip it and use the embedded metadata.
 * Returns the file type detected (or -1 on error).
 */
int eps_import_efe(eps_fs_t *fs, uint32_t dir_block, const char *src_path,
                   char *name_out, eps_file_type_t *type_out)
{
    if (!fs || !src_path) return -1;

    /* Check if it's a Giebler format file */
    if (efe_giebler_is_giebler(src_path)) {
        efe_giebler_t *efe = efe_giebler_open(src_path);
        if (!efe) return -1;

        /* Get metadata from Giebler header */
        char name[13];
        strncpy(name, efe->header.filename, 12);
        name[12] = '\0';

        eps_file_type_t type = (eps_file_type_t)efe->header.file_type;

        /* Get raw data (without Giebler header) */
        size_t raw_size;
        const uint8_t *raw_data = efe_raw_get_data(efe->raw, &raw_size);
        if (!raw_data) {
            efe_giebler_close(efe);
            return -1;
        }

        /* Check if name already exists */
        if (eps_find_entry(fs, dir_block, name)) {
            efe_giebler_close(efe);
            return -1;  /* Duplicate name */
        }

        /* Calculate blocks needed */
        uint32_t blocks_needed = (raw_size + EPS_BLOCK_SIZE - 1) / EPS_BLOCK_SIZE;
        if (blocks_needed == 0) blocks_needed = 1;

        /* Check free space */
        if (blocks_needed > fs->free_blocks) {
            efe_giebler_close(efe);
            return -1;
        }

        /* Allocate blocks */
        uint32_t *block_list = malloc(blocks_needed * sizeof(uint32_t));
        if (!block_list) {
            efe_giebler_close(efe);
            return -1;
        }

        for (uint32_t i = 0; i < blocks_needed; i++) {
            block_list[i] = eps_fat_alloc(fs);
            if (!block_list[i]) {
                for (uint32_t j = 0; j < i; j++) {
                    eps_fat_write(fs, block_list[j], EPS_FAT_FREE);
                }
                free(block_list);
                efe_giebler_close(efe);
                return -1;
            }
        }

        /* Link blocks in FAT */
        for (uint32_t i = 0; i < blocks_needed - 1; i++) {
            eps_fat_write(fs, block_list[i], block_list[i + 1]);
        }
        eps_fat_write(fs, block_list[blocks_needed - 1], EPS_FAT_END);

        /* Write raw data to disk blocks */
        uint8_t buffer[EPS_BLOCK_SIZE];
        for (uint32_t i = 0; i < blocks_needed; i++) {
            memset(buffer, 0, EPS_BLOCK_SIZE);
            size_t offset = i * EPS_BLOCK_SIZE;
            size_t to_copy = raw_size - offset;
            if (to_copy > EPS_BLOCK_SIZE) to_copy = EPS_BLOCK_SIZE;
            memcpy(buffer, raw_data + offset, to_copy);

            /* For bank files, patch the disk ID in the first block */
            if (i == 0 && (type == EPS_TYPE_BANK || type == EPS_TYPE_EPS16_BANK)) {
                /* Bank format: bytes 4-7 contain source disk ID */
                uint8_t old_id[4];
                memcpy(old_id, &buffer[4], 4);

                /* Report disk ID change - zero it out to mean "any disk" */
                if (old_id[0] != 0 || old_id[1] != 0 || old_id[2] != 0 || old_id[3] != 0) {
                    fprintf(stderr, "  Bank disk ID: %02X%02X%02X%02X -> 00000000 (cleared)\n",
                            old_id[0], old_id[1], old_id[2], old_id[3]);
                }

                /* Clear disk ID - zeros may mean "any disk" */
                buffer[4] = 0x00;
                buffer[5] = 0x00;
                buffer[6] = 0x00;
                buffer[7] = 0x00;
            }

            eps_write_block(fs, block_list[i], buffer);
        }

        /* Calculate contiguous blocks */
        uint32_t contiguous = 1;
        for (uint32_t i = 1; i < blocks_needed; i++) {
            if (block_list[i] == block_list[i-1] + 1) {
                contiguous++;
            } else {
                break;
            }
        }

        /* Find free slot in directory */
        uint8_t dir_buf[EPS_DIR_PAIR_SIZE];
        uint32_t second_block = read_dir_pair(fs, dir_block, dir_buf);

        if (!second_block) {
            eps_fat_free_chain(fs, block_list[0]);
            free(block_list);
            efe_giebler_close(efe);
            return -1;
        }

        int slot = find_free_slot_in_dir(dir_buf, dir_block);
        if (slot < 0) {
            eps_fat_free_chain(fs, block_list[0]);
            free(block_list);
            efe_giebler_close(efe);
            return -1;
        }

        /* Write directory entry */
        write_entry_to_dir(dir_buf, dir_block, slot, 0, type, name,
                           blocks_needed, contiguous, block_list[0]);

        /* Write both directory blocks back */
        if (write_dir_pair(fs, dir_block, second_block, dir_buf) != 0) {
            eps_fat_free_chain(fs, block_list[0]);
            free(block_list);
            efe_giebler_close(efe);
            return -1;
        }

        /* Return metadata to caller */
        if (name_out) strncpy(name_out, name, 13);
        if (type_out) *type_out = type;

        free(block_list);
        efe_giebler_close(efe);
        return 0;
    }

    /* Not a Giebler file - cannot auto-import without type info */
    return -1;
}

/*
 * Extract slot number from Giebler filename like "[01][Instr] NAME.efe"
 * Returns slot number (1-39) or 99 if no slot prefix found.
 */
static int extract_slot_number(const char *filename)
{
    if (filename[0] == '[' && filename[3] == ']') {
        int slot = (filename[1] - '0') * 10 + (filename[2] - '0');
        if (slot >= 1 && slot <= 39) {
            return slot;
        }
    }
    return 99;  /* No valid slot - sort to end */
}

/*
 * Comparison function for scandir() - sorts by [NN] slot prefix.
 * Files with [NN] prefix come first in slot order.
 * Files without prefix come last in alphabetical order.
 */
static int slot_compare(const struct dirent **a, const struct dirent **b)
{
    int slot_a = extract_slot_number((*a)->d_name);
    int slot_b = extract_slot_number((*b)->d_name);

    if (slot_a != slot_b) {
        return slot_a - slot_b;
    }
    /* Same slot or both no slot - sort alphabetically */
    return strcmp((*a)->d_name, (*b)->d_name);
}

/*
 * Recursively import a directory of EFE files.
 * Creates subdirectories as needed.
 * Files are sorted by [NN] slot prefix to preserve BANK file references.
 * Returns count of files imported, or -1 on error.
 */
int eps_import_dir(eps_fs_t *fs, uint32_t parent_dir, const char *src_dir,
                   const char *dest_name, int verbose)
{
    if (!fs || !src_dir || !dest_name) return -1;

    /* Create subdirectory on disk */
    if (eps_mkdir(fs, parent_dir, dest_name) != 0) {
        /* Directory might already exist - try to find it */
        eps_dir_entry_t *existing = eps_find_entry(fs, parent_dir, dest_name);
        if (!existing || existing->file_type != EPS_TYPE_SUBDIR) {
            return -1;
        }
    }

    /* Get the new directory's block */
    eps_dir_entry_t *dir_entry = eps_find_entry(fs, parent_dir, dest_name);
    if (!dir_entry || dir_entry->file_type != EPS_TYPE_SUBDIR) {
        return -1;
    }
    uint32_t new_dir_block = dir_entry->first_block;

    /* Scan and sort directory entries by slot number */
    struct dirent **namelist;
    int n = scandir(src_dir, &namelist, NULL, slot_compare);
    if (n < 0) return -1;

    int imported = 0;

    for (int i = 0; i < n; i++) {
        struct dirent *entry = namelist[i];

        /* Skip . and .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            free(entry);
            continue;
        }

        /* Build full path */
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", src_dir, entry->d_name);

        struct stat st;
        if (stat(path, &st) != 0) {
            free(entry);
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            /* Recursively import subdirectory */
            /* Extract dir name (max 12 chars) */
            char subdir_name[13];
            strncpy(subdir_name, entry->d_name, 12);
            subdir_name[12] = '\0';

            int sub_count = eps_import_dir(fs, new_dir_block, path, subdir_name, verbose);
            if (sub_count >= 0) {
                imported += sub_count;
            }
        } else if (S_ISREG(st.st_mode)) {
            /* Check if it's an EFE file (by extension or magic) */
            const char *ext = strrchr(entry->d_name, '.');
            if (ext && (strcasecmp(ext, ".efe") == 0 || strcasecmp(ext, ".efa") == 0)) {
                char name[13];
                eps_file_type_t type;

                if (eps_import_efe(fs, new_dir_block, path, name, &type) == 0) {
                    imported++;
                    if (verbose) {
                        printf("  Imported: %s (%s)\n", name, eps_type_name(type));
                        fflush(stdout);
                    }
                } else if (verbose) {
                    printf("  Failed: %s\n", entry->d_name);
                    fflush(stdout);
                }
            }
        }
        free(entry);
    }

    free(namelist);
    return imported;
}

/* Include embedded OS data */
#include "eps_os.h"

/*
 * Create a new blank EPS hard disk image
 *
 * Layout:
 *   Block 0: Unused (zeros)
 *   Block 1: Device ID block with "ID" and "OS" signatures
 *   Block 2: OS block + root directory start
 *   Block 3: Root directory continuation
 *   Block 4: Root directory continuation with "DR" signature
 *   Blocks 5-(5+fat_blocks-1): FAT with "FB" signatures
 *   Remaining: OS file data, subdirectories, then free space
 */
int eps_mkimage(const char *filename, uint32_t size_mb, bool include_os)
{
    uint32_t total_blocks = (size_mb * 1024 * 1024) / EPS_BLOCK_SIZE;
    uint32_t fat_blocks = (total_blocks + EPS_FAT_ENTRIES_PER_BLOCK - 1) / EPS_FAT_ENTRIES_PER_BLOCK;
    uint32_t os_start_block = EPS_BLOCK_FAT_START + fat_blocks;
    uint32_t os_blocks = include_os ? EPS_OS_BLOCKS : 0;
    uint32_t data_start = os_start_block + os_blocks;
    /* total_blocks - 1 is stored in ID block, so free = (total-1) - (used - 1)
     * used blocks = 5 system + fat_blocks + os_blocks = data_start
     * But block 0 is counted as used in FAT yet excluded from total in ID block
     * So: free = total_blocks - data_start - 1 */
    uint32_t free_blocks = total_blocks - data_start - 1;

    /* Create the file */
    FILE *fp = fopen(filename, "wb");
    if (!fp) return -1;

    uint8_t block[EPS_BLOCK_SIZE];
    uint8_t block3[EPS_BLOCK_SIZE];  /* For directory continuation */
    memset(block, 0, EPS_BLOCK_SIZE);
    memset(block3, 0, EPS_BLOCK_SIZE);

    /* Block 0: Unused - just zeros */
    if (fwrite(block, EPS_BLOCK_SIZE, 1, fp) != 1) goto error;

    /* Block 1: Device ID block */
    memset(block, 0, EPS_BLOCK_SIZE);
    /*
     * Block 1 format (based on reference analysis):
     * 0x00-0x03: 00 00 02 00 (format indicator)
     * 0x0C: heads (0x02)
     * 0x0E-0x11: total_blocks - 1, big-endian 32-bit
     * 0x26-0x27: "ID" signature
     * 0x28-0x2B: 00 00 + cylinders (16-bit BE)
     * 0x2C-0x2F: 02 31 01 00 (format version flags)
     * 0x44-0x45: "OS" signature
     * 0x46-0x49: 00 14 + high 16 bits of total_blocks - 1
     */
    uint32_t total_minus_1 = total_blocks - 1;

    block[0x00] = 0x00;
    block[0x01] = 0x00;
    block[0x02] = 0x02;
    block[0x03] = 0x00;
    /* Offset 0x0C: heads = 2 */
    block[0x0C] = 0x02;
    /* Offset 0x0E-0x11: total_blocks - 1, big-endian 32-bit */
    eps_write_be32(&block[0x0E], total_minus_1);
    /* "ID" signature at offset 0x26 */
    block[0x26] = 'I';
    block[0x27] = 'D';
    /* Offset 0x28-0x2B: 00 00 + cylinders */
    block[0x28] = 0x00;
    block[0x29] = 0x00;
    uint16_t cylinders = total_blocks / (2 * 20);  /* heads=2, sectors=20 */
    block[0x2A] = (cylinders >> 8) & 0xFF;
    block[0x2B] = cylinders & 0xFF;
    /* Byte 0x2C-0x2F: format version flags */
    block[0x2C] = 0x02;
    block[0x2D] = 0x31;
    block[0x2E] = 0x01;
    block[0x2F] = 0x00;

    /* Offset 0x44: "OS" signature with disk geometry */
    block[0x44] = 'O';
    block[0x45] = 'S';
    /* Offset 0x46-0x49: 00 14 + high 16 bits of total_blocks - 1 */
    block[0x46] = 0x00;
    block[0x47] = 0x14;
    block[0x48] = (total_minus_1 >> 8) & 0xFF;
    block[0x49] = total_minus_1 & 0xFF;

    if (fwrite(block, EPS_BLOCK_SIZE, 1, fp) != 1) goto error;

    /* Block 2: OS info + root directory */
    memset(block, 0, EPS_BLOCK_SIZE);
    /* Free blocks count at offset 0x01-0x03 (24-bit big-endian) */
    /* Actually looking at reference: 00 14 30 A7 at bytes 0-3 */
    /* This seems to be: byte0=0, bytes 1-3 = free block count */
    block[0x00] = 0x00;
    block[0x01] = (free_blocks >> 16) & 0xFF;
    block[0x02] = (free_blocks >> 8) & 0xFF;
    block[0x03] = free_blocks & 0xFF;
    /* Disk ID at 0x04-0x07 - format version/geometry flags */
    /* Original value from reference disk images */
    block[0x04] = 0x02;
    block[0x05] = 0x31;
    block[0x06] = 0x01;
    block[0x07] = 0x00;
    /* "OS" signature at offset 0x1C */
    block[0x1C] = 'O';
    block[0x1D] = 'S';

    /* Root directory entries start at offset 0x1E in block 2 */
    uint8_t *entry = &block[0x1E];
    /* Also maintain pointer for block 3 (continuation) */
    uint8_t *entry3 = block3;

    if (include_os) {
        /* OS file entry - in block 2 */
        entry[0] = 0x00;                    /* type_info */
        entry[1] = EPS_TYPE_OS;             /* file_type = OS */
        memcpy(&entry[2], "EPS-1 O.S.  ", 12);  /* filename */
        eps_write_be16(&entry[14], EPS_OS_BLOCKS);  /* size */
        eps_write_be16(&entry[16], EPS_OS_BLOCKS);  /* contiguous */
        eps_write_be32(&entry[18], os_start_block); /* first block */
        entry += EPS_DIR_ENTRY_SIZE;

        /* Also write to block 3 (mirrored) */
        entry3[0] = 0x00;
        entry3[1] = EPS_TYPE_OS;
        memcpy(&entry3[2], "EPS-1 O.S.  ", 12);
        eps_write_be16(&entry3[14], EPS_OS_BLOCKS);
        eps_write_be16(&entry3[16], EPS_OS_BLOCKS);
        eps_write_be32(&entry3[18], os_start_block);
        entry3 += EPS_DIR_ENTRY_SIZE;
    }

    if (fwrite(block, EPS_BLOCK_SIZE, 1, fp) != 1) goto error;

    /* Block 3: Directory continuation (mirror of entries from block 2) */
    if (fwrite(block3, EPS_BLOCK_SIZE, 1, fp) != 1) goto error;

    /* Block 4: Directory continuation with "DR" signature */
    memset(block, 0, EPS_BLOCK_SIZE);
    block[510] = 'D';
    block[511] = 'R';
    if (fwrite(block, EPS_BLOCK_SIZE, 1, fp) != 1) goto error;

    /* FAT blocks */
    for (uint32_t fb = 0; fb < fat_blocks; fb++) {
        memset(block, 0, EPS_BLOCK_SIZE);

        /* Fill FAT entries for this block */
        for (int i = 0; i < EPS_FAT_ENTRIES_PER_BLOCK; i++) {
            uint32_t block_num = fb * EPS_FAT_ENTRIES_PER_BLOCK + i;
            uint32_t fat_value;

            if (block_num >= total_blocks) {
                fat_value = EPS_FAT_FREE;  /* Beyond disk */
            } else if (block_num < EPS_BLOCK_FAT_START) {
                fat_value = EPS_FAT_END;   /* System blocks 0-4 */
            } else if (block_num < os_start_block) {
                fat_value = EPS_FAT_END;   /* FAT blocks themselves */
            } else if (include_os && block_num >= os_start_block && block_num < os_start_block + os_blocks) {
                /* OS file chain */
                if (block_num == os_start_block + os_blocks - 1) {
                    fat_value = EPS_FAT_END;
                } else {
                    fat_value = block_num + 1;
                }
            } else {
                fat_value = EPS_FAT_FREE;  /* Free space */
            }

            /* Write 3-byte big-endian FAT entry */
            int offset = i * 3;
            block[offset + 0] = (fat_value >> 16) & 0xFF;
            block[offset + 1] = (fat_value >> 8) & 0xFF;
            block[offset + 2] = fat_value & 0xFF;
        }

        /* FAT block signature at end */
        block[510] = 'F';
        block[511] = 'B';

        if (fwrite(block, EPS_BLOCK_SIZE, 1, fp) != 1) goto error;
    }

    /* Write OS data if included */
    if (include_os) {
        if (fwrite(eps_os_data, EPS_OS_SIZE, 1, fp) != 1) goto error;
    }

    /* Fill remaining space with zeros */
    memset(block, 0, EPS_BLOCK_SIZE);
    uint32_t current_block = data_start;
    while (current_block < total_blocks) {
        if (fwrite(block, EPS_BLOCK_SIZE, 1, fp) != 1) goto error;
        current_block++;
    }

    fclose(fp);
    printf("Created EPS disk image: %s\n", filename);
    printf("  Size: %u MB (%u blocks)\n", size_mb, total_blocks);
    printf("  FAT blocks: %u\n", fat_blocks);
    printf("  OS included: %s\n", include_os ? "yes" : "no");
    printf("  Free blocks: %u (%u KB)\n", free_blocks,
           (free_blocks * EPS_BLOCK_SIZE) / 1024);
    return 0;

error:
    fclose(fp);
    return -1;
}
