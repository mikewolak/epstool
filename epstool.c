/*
 * epstool.c - Ensoniq EPS Filesystem Tool
 *
 * Command-line tool for manipulating Ensoniq EPS/EPS-16+ disk images.
 *
 * Usage:
 *   epstool mkimage <image> <size_mb> [--no-os]  - Create new disk image
 *   epstool <image> info                    - Show disk information
 *   epstool <image> ls [path]               - List directory contents
 *   epstool <image> tree                    - Show directory tree
 *   epstool <image> cat <file>              - Dump file to stdout
 *   epstool <image> extract <file> <dest>   - Extract file to host
 *   epstool <image> import <src> <name>     - Import file from host
 *   epstool <image> mkdir <name>            - Create directory
 *   epstool <image> rm <file>               - Delete file
 *   epstool <image> hexdump <block>         - Dump block in hex
 *   epstool <image> fat [block]             - Show FAT entries
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <libgen.h>
#include "epsfs.h"
#include "efe_giebler.h"
#include "efe_raw.h"

static void usage(const char *prog)
{
    fprintf(stderr, "Ensoniq EPS Filesystem Tool\n");
    fprintf(stderr, "============================\n\n");
    fprintf(stderr, "Usage: %s <command> [args...]\n\n", prog);
    fprintf(stderr, "Image creation:\n");
    fprintf(stderr, "  mkimage <file> <size_mb> [--no-os]  - Create new disk image\n");
    fprintf(stderr, "\nImage operations: %s <image> <command> [args...]\n\n", prog);
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, "  info                       - Show disk information\n");
    fprintf(stderr, "  ls [-v] [path]             - List directory contents\n");
    fprintf(stderr, "  tree                       - Show directory tree\n");
    fprintf(stderr, "  cat <file>                 - Dump file contents to stdout\n");
    fprintf(stderr, "  extract <file> <dest>      - Extract file to host filesystem\n");
    fprintf(stderr, "  import <src> <name> <type> - Import raw file from host\n");
    fprintf(stderr, "  import-efe <src> [path]    - Import EFE file (auto-detect format/type)\n");
    fprintf(stderr, "  import-dir <src> <name>    - Recursively import directory of EFE files\n");
    fprintf(stderr, "  mkdir <name>               - Create directory\n");
    fprintf(stderr, "  rm <file>                  - Delete file\n");
    fprintf(stderr, "  hexdump <block>            - Dump block in hex\n");
    fprintf(stderr, "  fat [start] [count]        - Show FAT entries\n");
    fprintf(stderr, "  validate-bank <path>       - Validate bank file references\n");
    fprintf(stderr, "  validate-banks [path]      - Validate all banks in directory\n");
    fprintf(stderr, "  inst-info <file.efe>       - Show instrument structure (Giebler format)\n");
    fprintf(stderr, "  export-wav <file.efe> <outdir> - Export wavesamples to WAV files\n");
    fprintf(stderr, "\nFile types for import:\n");
    fprintf(stderr, "  inst, bank, seq, song, sysex, macro, effect\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  %s disk.hda import-efe sound.efe SOUNDS/     - Import to SOUNDS dir\n", prog);
    fprintf(stderr, "  %s disk.hda import-dir ./factory FACTORY     - Import factory sounds\n", prog);
}

/* Forward declaration */
static uint32_t navigate_path(eps_fs_t *fs, const char *path);

static void print_entry(eps_dir_entry_t *entry, int verbose)
{
    char name[13];
    eps_format_filename(entry->filename, name);

    if (verbose) {
        printf("%-12s  %6u blks  %6u contig  @%-8u  %s\n",
               name,
               entry->size_blocks,
               entry->contiguous,
               entry->first_block,
               eps_type_name(entry->file_type));
    } else {
        const char *type_char;
        switch (entry->file_type) {
            case EPS_TYPE_SUBDIR:     type_char = "d"; break;
            case EPS_TYPE_PARENT_DIR: type_char = "^"; break;
            case EPS_TYPE_OS:         type_char = "o"; break;
            default:                  type_char = "-"; break;
        }
        printf("%s %-12s %6u KB  %s\n",
               type_char, name,
               (entry->size_blocks * EPS_BLOCK_SIZE) / 1024,
               eps_type_name(entry->file_type));
    }
}

static int cmd_info(eps_fs_t *fs)
{
    eps_print_info(fs);
    return 0;
}

static int cmd_ls(eps_fs_t *fs, uint32_t dir_block, int verbose)
{
    eps_dir_t *dir = eps_opendir(fs, dir_block);
    if (!dir) {
        fprintf(stderr, "Failed to open directory\n");
        return 1;
    }

    eps_dir_entry_t *entry;
    int count = 0;
    uint32_t total_blocks = 0;

    while ((entry = eps_readdir(dir)) != NULL) {
        print_entry(entry, verbose);
        count++;
        total_blocks += entry->size_blocks;
    }

    printf("\n%d entries, %u blocks (%u KB)\n", count, total_blocks,
           (total_blocks * EPS_BLOCK_SIZE) / 1024);

    eps_closedir(dir);
    return 0;
}

static void print_tree_recursive(eps_fs_t *fs, uint32_t dir_block, int depth)
{
    eps_dir_t *dir = eps_opendir(fs, dir_block);
    if (!dir) return;

    eps_dir_entry_t *entry;

    while ((entry = eps_readdir(dir)) != NULL) {
        /* Skip parent dir entries */
        if (entry->file_type == EPS_TYPE_PARENT_DIR) continue;

        /* Print indentation */
        for (int i = 0; i < depth; i++) {
            printf("    ");
        }

        char name[13];
        eps_format_filename(entry->filename, name);

        if (entry->file_type == EPS_TYPE_SUBDIR) {
            printf("[%s]/\n", name);
            print_tree_recursive(fs, entry->first_block, depth + 1);
        } else {
            printf("%s (%s, %u KB)\n", name,
                   eps_type_name(entry->file_type),
                   (entry->size_blocks * EPS_BLOCK_SIZE) / 1024);
        }
    }

    eps_closedir(dir);
}

static int cmd_tree(eps_fs_t *fs)
{
    printf("/\n");
    print_tree_recursive(fs, fs->root_dir_block, 1);
    return 0;
}

static int cmd_cat(eps_fs_t *fs, const char *filename)
{
    eps_file_t *file = eps_fopen(fs, fs->root_dir_block, filename);
    if (!file) {
        fprintf(stderr, "File not found: %s\n", filename);
        return 1;
    }

    uint8_t buffer[EPS_BLOCK_SIZE];
    size_t read;

    while ((read = eps_fread(buffer, 1, EPS_BLOCK_SIZE, file)) > 0) {
        fwrite(buffer, 1, read, stdout);
    }

    eps_fclose(file);
    return 0;
}

static int cmd_extract(eps_fs_t *fs, const char *filename, const char *dest)
{
    if (eps_extract(fs, fs->root_dir_block, filename, dest) != 0) {
        fprintf(stderr, "Failed to extract: %s\n", filename);
        return 1;
    }

    printf("Extracted %s to %s\n", filename, dest);
    return 0;
}

static int cmd_import(eps_fs_t *fs, const char *src, const char *dest_path, const char *type_str)
{
    eps_file_type_t type = EPS_TYPE_INSTRUMENT;

    if (strcmp(type_str, "inst") == 0) {
        type = EPS_TYPE_INSTRUMENT;
    } else if (strcmp(type_str, "bank") == 0) {
        type = EPS_TYPE_BANK;
    } else if (strcmp(type_str, "seq") == 0) {
        type = EPS_TYPE_SEQUENCE;
    } else if (strcmp(type_str, "song") == 0) {
        type = EPS_TYPE_SONG;
    } else if (strcmp(type_str, "sysex") == 0) {
        type = EPS_TYPE_SYSEX;
    } else if (strcmp(type_str, "macro") == 0) {
        type = EPS_TYPE_MACRO;
    } else if (strcmp(type_str, "effect") == 0) {
        type = EPS_TYPE_EFFECT;
    } else {
        fprintf(stderr, "Unknown file type: %s\n", type_str);
        return 1;
    }

    /* Split dest_path into parent directory and filename */
    char *path_copy = strdup(dest_path);
    char *last_slash = strrchr(path_copy, '/');

    uint32_t parent_dir;
    char name[13];

    if (last_slash) {
        *last_slash = '\0';
        parent_dir = navigate_path(fs, path_copy);
        strncpy(name, last_slash + 1, 12);
        name[12] = '\0';
    } else {
        parent_dir = fs->root_dir_block;
        strncpy(name, dest_path, 12);
        name[12] = '\0';
    }

    free(path_copy);

    if (parent_dir == 0) {
        fprintf(stderr, "Parent directory not found for: %s\n", dest_path);
        return 1;
    }

    if (eps_import(fs, parent_dir, src, name, type) != 0) {
        fprintf(stderr, "Failed to import: %s\n", src);
        return 1;
    }

    printf("Imported %s as %s (%s)\n", src, dest_path, eps_type_name(type));
    return 0;
}

static int cmd_import_efe(eps_fs_t *fs, const char *src, const char *dest_path)
{
    /* Navigate to parent directory */
    uint32_t parent_dir = fs->root_dir_block;

    if (dest_path && strlen(dest_path) > 0 && strcmp(dest_path, "/") != 0) {
        parent_dir = navigate_path(fs, dest_path);
        if (parent_dir == 0) {
            fprintf(stderr, "Directory not found: %s\n", dest_path);
            return 1;
        }
    }

    char name[13];
    eps_file_type_t type;

    if (eps_import_efe(fs, parent_dir, src, name, &type) != 0) {
        fprintf(stderr, "Failed to import: %s\n", src);
        fprintf(stderr, "(File may not be Giebler format, or name already exists)\n");
        return 1;
    }

    printf("Imported %s as %s (%s)\n", src, name, eps_type_name(type));
    return 0;
}

static int cmd_import_dir(eps_fs_t *fs, const char *src_dir, const char *dest_path)
{
    /* Navigate to parent directory */
    char *path_copy = strdup(dest_path);
    char *last_slash = strrchr(path_copy, '/');

    uint32_t parent_dir;
    char name[13];

    if (last_slash && last_slash != path_copy) {
        *last_slash = '\0';
        parent_dir = navigate_path(fs, path_copy);
        strncpy(name, last_slash + 1, 12);
        name[12] = '\0';
    } else {
        parent_dir = fs->root_dir_block;
        /* Remove leading slash if present */
        const char *n = dest_path;
        if (n[0] == '/') n++;
        strncpy(name, n, 12);
        name[12] = '\0';
    }

    free(path_copy);

    if (parent_dir == 0) {
        fprintf(stderr, "Parent directory not found\n");
        return 1;
    }

    printf("Importing %s to %s...\n", src_dir, dest_path);

    int count = eps_import_dir(fs, parent_dir, src_dir, name, 1);
    if (count < 0) {
        fprintf(stderr, "Failed to import directory\n");
        return 1;
    }

    printf("\nImported %d files to %s\n", count, dest_path);
    return 0;
}

static int cmd_mkdir(eps_fs_t *fs, const char *path)
{
    /* Split path into parent and name */
    char *path_copy = strdup(path);
    char *last_slash = strrchr(path_copy, '/');

    uint32_t parent_dir;
    char name[13];

    if (last_slash) {
        *last_slash = '\0';
        parent_dir = navigate_path(fs, path_copy);
        strncpy(name, last_slash + 1, 12);
        name[12] = '\0';
    } else {
        parent_dir = fs->root_dir_block;
        strncpy(name, path, 12);
        name[12] = '\0';
    }

    free(path_copy);

    if (parent_dir == 0) {
        fprintf(stderr, "Parent directory not found\n");
        return 1;
    }

    if (eps_mkdir(fs, parent_dir, name) != 0) {
        fprintf(stderr, "Failed to create directory: %s\n", path);
        return 1;
    }

    printf("Created directory: %s\n", path);
    return 0;
}

static int cmd_rm(eps_fs_t *fs, const char *filename)
{
    eps_dir_entry_t *entry = eps_find_entry(fs, fs->root_dir_block, filename);
    if (!entry) {
        fprintf(stderr, "File not found: %s\n", filename);
        return 1;
    }

    /* Free the file's block chain */
    if (eps_fat_free_chain(fs, entry->first_block) != 0) {
        fprintf(stderr, "Failed to free blocks for: %s\n", filename);
        return 1;
    }

    /* Clear the directory entry */
    /* This requires finding and modifying the entry in the directory block */
    /* For simplicity, we mark the entry as unused by clearing its type */
    uint8_t block[EPS_BLOCK_SIZE];
    uint32_t current = fs->root_dir_block;
    char raw_name[12];
    eps_unformat_filename(filename, raw_name);

    while (current != EPS_FAT_END && current != EPS_FAT_FREE) {
        if (eps_read_block(fs, current, block) != 0) {
            return 1;
        }

        for (int i = 0; i < 19; i++) {
            uint8_t *e = &block[i * EPS_DIR_ENTRY_SIZE];
            if (memcmp(&e[2], raw_name, 12) == 0 && e[1] != EPS_TYPE_UNUSED) {
                /* Found it - mark as unused */
                e[1] = EPS_TYPE_UNUSED;
                eps_write_block(fs, current, block);
                printf("Deleted: %s\n", filename);
                return 0;
            }
        }

        current = eps_fat_read(fs, current);
    }

    fprintf(stderr, "Failed to update directory entry\n");
    return 1;
}

static int cmd_hexdump(eps_fs_t *fs, uint32_t block_num)
{
    uint8_t block[EPS_BLOCK_SIZE];

    if (eps_read_block(fs, block_num, block) != 0) {
        fprintf(stderr, "Failed to read block %u\n", block_num);
        return 1;
    }

    printf("Block %u (0x%X), offset 0x%X:\n", block_num, block_num,
           block_num * EPS_BLOCK_SIZE);

    for (int i = 0; i < EPS_BLOCK_SIZE; i += 16) {
        printf("%04X: ", i);

        /* Hex */
        for (int j = 0; j < 16; j++) {
            if (i + j < EPS_BLOCK_SIZE) {
                printf("%02X ", block[i + j]);
            } else {
                printf("   ");
            }
            if (j == 7) printf(" ");
        }

        printf(" ");

        /* ASCII */
        for (int j = 0; j < 16 && i + j < EPS_BLOCK_SIZE; j++) {
            uint8_t c = block[i + j];
            printf("%c", (c >= 32 && c < 127) ? c : '.');
        }

        printf("\n");
    }

    return 0;
}

static int cmd_fat(eps_fs_t *fs, uint32_t start, uint32_t count)
{
    printf("FAT entries (3 bytes each, big-endian):\n");
    printf("Block -> Next (0=free, 1=end, 2=bad)\n\n");

    for (uint32_t i = start; i < start + count && i < fs->total_blocks; i++) {
        uint32_t next = eps_fat_read(fs, i);

        const char *status = "";
        if (next == EPS_FAT_FREE) status = " (free)";
        else if (next == EPS_FAT_END) status = " (end)";
        else if (next == EPS_FAT_BAD) status = " (bad)";

        printf("%6u -> %6u%s\n", i, next, status);
    }

    return 0;
}

/*
 * Bank validation - check all instrument references in bank files
 *
 * Raw disk bank format (EPS):
 *   0x00-0x07: Header bytes
 *   0x08-0x1F: Bank name (12 chars, interleaved with spaces/zeros)
 *   0x20: Instrument mask (bit 0=inst 1 valid, bit 7=inst 8 valid)
 *   0x22+: 8 instrument entries (16 bytes each for EPS)
 *
 * Each instrument entry (EPS, 16 bytes):
 *   byte 0: Path depth (0=same dir, 1-5=subdirectory levels, 0x80+=copy of)
 *   byte 2: Device (0=floppy)
 *   byte 6: File index in directory
 */
static int cmd_validate_bank(eps_fs_t *fs, const char *path)
{
    /* Navigate to parent directory and find the bank file */
    char *path_copy = strdup(path);
    char *last_slash = strrchr(path_copy, '/');

    uint32_t parent_dir;
    char bank_name[13];

    if (last_slash && last_slash != path_copy) {
        *last_slash = '\0';
        parent_dir = navigate_path(fs, path_copy);
        strncpy(bank_name, last_slash + 1, 12);
        bank_name[12] = '\0';
    } else {
        parent_dir = fs->root_dir_block;
        const char *n = (path[0] == '/') ? path + 1 : path;
        strncpy(bank_name, n, 12);
        bank_name[12] = '\0';
    }
    free(path_copy);

    if (parent_dir == 0) {
        fprintf(stderr, "Parent directory not found\n");
        return 1;
    }

    /* Find the bank file */
    eps_dir_entry_t *bank_entry = eps_find_entry(fs, parent_dir, bank_name);
    if (!bank_entry) {
        fprintf(stderr, "Bank file not found: %s\n", bank_name);
        return 1;
    }

    if (bank_entry->file_type != EPS_TYPE_BANK) {
        fprintf(stderr, "Not a bank file (type=%d)\n", bank_entry->file_type);
        return 1;
    }

    /* Read the bank file data */
    uint8_t bank_data[1024];
    uint32_t block = bank_entry->first_block;
    int offset = 0;

    while (block != EPS_FAT_END && block != EPS_FAT_FREE && offset < 1024) {
        uint8_t buf[512];
        if (eps_read_block(fs, block, buf) != 0) {
            fprintf(stderr, "Failed to read bank data\n");
            return 1;
        }
        int to_copy = (1024 - offset > 512) ? 512 : (1024 - offset);
        memcpy(bank_data + offset, buf, to_copy);
        offset += to_copy;
        block = eps_fat_read(fs, block);
    }

    printf("Bank: %s\n", bank_name);

    /* Bank name at 0x08-0x1F (interleaved with spaces) */
    printf("Bank Name: ");
    for (int i = 0x08; i < 0x20; i += 2) {
        if (bank_data[i] >= 32 && bank_data[i] < 127)
            putchar(bank_data[i]);
    }
    printf("\n");

    /* Instrument mask at 0x20 */
    uint8_t inst_mask = bank_data[0x20];
    printf("Inst Mask: 0x%02X (", inst_mask);
    for (int i = 7; i >= 0; i--) {
        putchar((inst_mask >> i) & 1 ? '1' : '0');
    }
    printf(")\n\n");

    /* Build directory listing for validation */
    eps_dir_t *dir = eps_opendir(fs, parent_dir);
    if (!dir) {
        fprintf(stderr, "Failed to open directory for validation\n");
        return 1;
    }

    /* Collect directory entries */
    eps_dir_entry_t entries[39];
    int entry_count = 0;
    eps_dir_entry_t *e;
    while ((e = eps_readdir(dir)) != NULL && entry_count < 39) {
        memcpy(&entries[entry_count], e, sizeof(eps_dir_entry_t));
        entry_count++;
    }
    eps_closedir(dir);

    /* Validate each instrument entry (16 bytes each starting at 0x22) */
    int all_valid = 1;

    for (int inst = 0; inst < 8; inst++) {
        printf("Inst %d: ", inst + 1);

        /* Check if this instrument slot is valid */
        if (!((inst_mask >> inst) & 1)) {
            printf("(empty)\n");
            continue;
        }

        uint8_t *inst_data = &bank_data[0x22 + inst * 16];
        uint8_t path_depth = inst_data[0];

        if (path_depth >= 0x80) {
            /* Copy of another instrument */
            int copy_of = (path_depth & 0x0F) + 1;
            printf("Copy of Inst %d\n", copy_of);
            continue;
        }

        if (path_depth == 0x7F) {
            printf("(deleted)\n");
            continue;
        }

        /* File index is at byte 4 of the entry */
        uint8_t file_idx = inst_data[4];

        /* For path_depth > 0, we'd need to follow subdirectories */
        if (path_depth > 0) {
            printf("Path depth %d - subdirectory reference (idx=%d", path_depth, inst_data[4]);
            for (int p = 1; p <= path_depth; p++) {
                printf("/%d", inst_data[4 + 2*p]);
            }
            printf(") - NOT VALIDATED\n");
            continue;
        }

        /* path_depth == 0: file is in same directory as bank */
        printf("Slot %d -> ", file_idx);

        /* Validate: find entry at slot file_idx */
        if (file_idx >= entry_count) {
            printf("INVALID (slot %d doesn't exist, only %d entries)\n",
                   file_idx, entry_count);
            all_valid = 0;
            continue;
        }

        eps_dir_entry_t *target = &entries[file_idx];
        char name[13];
        eps_format_filename(target->filename, name);

        /* Check if it's an instrument */
        if (target->file_type == EPS_TYPE_INSTRUMENT ||
            target->file_type == EPS_TYPE_EPS16_INST) {
            printf("'%s' (%s) - OK\n", name, eps_type_name(target->file_type));
        } else if (target->file_type == EPS_TYPE_PARENT_DIR) {
            printf("INVALID (points to parent dir at slot %d)\n", file_idx);
            all_valid = 0;
        } else {
            printf("'%s' (%s) - WARNING: not an instrument\n",
                   name, eps_type_name(target->file_type));
        }
    }

    /* Validate song entry (entry 9, at offset 0xA2) */
    printf("Song:   ");
    uint8_t *song_data = &bank_data[0xA2];
    uint8_t song_path_depth = song_data[0];

    if (song_path_depth >= 0x80) {
        printf("(none)\n");
    } else if (song_path_depth == 0x7F) {
        printf("(deleted)\n");
    } else {
        uint8_t song_idx = song_data[4];

        if (song_path_depth > 0) {
            printf("Path depth %d - subdirectory reference (idx=%d", song_path_depth, song_data[4]);
            for (int p = 1; p <= song_path_depth; p++) {
                printf("/%d", song_data[4 + 2*p]);
            }
            printf(") - NOT VALIDATED\n");
        } else {
            printf("Slot %d -> ", song_idx);

            if (song_idx >= entry_count) {
                printf("INVALID (slot %d doesn't exist, only %d entries)\n",
                       song_idx, entry_count);
                all_valid = 0;
            } else {
                eps_dir_entry_t *target = &entries[song_idx];
                char name[13];
                eps_format_filename(target->filename, name);

                if (target->file_type == EPS_TYPE_SONG ||
                    target->file_type == EPS_TYPE_SONG_VFX ||
                    target->file_type == EPS_TYPE_SEQUENCE ||
                    target->file_type == EPS_TYPE_SEQ_VFX) {
                    printf("'%s' (%s) - OK\n", name, eps_type_name(target->file_type));
                } else if (target->file_type == EPS_TYPE_PARENT_DIR) {
                    printf("INVALID (points to parent dir)\n");
                    all_valid = 0;
                } else {
                    printf("'%s' (%s) - WARNING: not a song/sequence\n",
                           name, eps_type_name(target->file_type));
                }
            }
        }
    }

    printf("\n");
    if (all_valid) {
        printf("Result: All bank references are VALID\n");
    } else {
        printf("Result: Some bank references are INVALID\n");
    }

    return all_valid ? 0 : 1;
}

/*
 * Validate all bank files in a directory recursively
 */
static int cmd_validate_banks(eps_fs_t *fs, uint32_t dir_block, const char *path_prefix)
{
    eps_dir_t *dir = eps_opendir(fs, dir_block);
    if (!dir) {
        fprintf(stderr, "Failed to open directory\n");
        return 1;
    }

    int total_banks = 0;
    int valid_banks = 0;
    eps_dir_entry_t *entry;

    /* First pass: collect all entries (skip parent dir) */
    eps_dir_entry_t entries[39];
    int entry_count = 0;
    while ((entry = eps_readdir(dir)) != NULL && entry_count < 39) {
        if (entry->file_type != EPS_TYPE_PARENT_DIR) {
            memcpy(&entries[entry_count], entry, sizeof(eps_dir_entry_t));
            entry_count++;
        }
    }
    eps_closedir(dir);

    /* Process entries */
    for (int i = 0; i < entry_count; i++) {
        entry = &entries[i];
        char name[13];
        eps_format_filename(entry->filename, name);

        if (entry->file_type == EPS_TYPE_SUBDIR) {
            /* Recurse into subdirectory */
            char subpath[256];
            snprintf(subpath, sizeof(subpath), "%s/%s", path_prefix, name);
            int sub_result = cmd_validate_banks(fs, entry->first_block, subpath);
            if (sub_result > 0) {
                total_banks += sub_result >> 16;
                valid_banks += sub_result & 0xFFFF;
            }
        } else if (entry->file_type == EPS_TYPE_BANK) {
            /* Validate this bank */
            char bank_path[256];
            snprintf(bank_path, sizeof(bank_path), "%s/%s", path_prefix, name);

            printf("=== Validating %s ===\n", bank_path);
            int result = cmd_validate_bank(fs, bank_path);
            total_banks++;
            if (result == 0) valid_banks++;
            printf("\n");
        }
    }

    /* Return combined count */
    return (total_banks << 16) | valid_banks;
}

/*
 * Show instrument information from a Giebler format EFE file
 */
static int cmd_inst_info(const char *efe_path)
{
    /* Check if it's Giebler format */
    if (!efe_giebler_is_giebler(efe_path)) {
        fprintf(stderr, "Not a Giebler format EFE file: %s\n", efe_path);
        fprintf(stderr, "Try using a .efe file with Giebler header\n");
        return 1;
    }

    /* Open Giebler file to get raw data offset */
    efe_giebler_t *giebler = efe_giebler_open(efe_path);
    if (!giebler) {
        fprintf(stderr, "Failed to open EFE file: %s\n", efe_path);
        return 1;
    }

    /* Get raw data */
    size_t raw_size;
    const uint8_t *raw_data = efe_giebler_get_raw_data(giebler, &raw_size);

    if (!raw_data || raw_size < 512) {
        fprintf(stderr, "File too small or invalid\n");
        efe_giebler_close(giebler);
        return 1;
    }

    /* Check file type from Giebler header */
    uint8_t file_type = efe_giebler_get_type(giebler);
    if (file_type != 0x03 && file_type != 0x18) {
        fprintf(stderr, "Not an instrument file (type 0x%02X)\n", file_type);
        efe_giebler_close(giebler);
        return 1;
    }

    /* Parse as raw instrument */
    efe_raw_t *efe = efe_raw_open_mem(raw_data, raw_size);
    if (!efe) {
        fprintf(stderr, "Failed to parse instrument\n");
        efe_giebler_close(giebler);
        return 1;
    }

    /* Print detailed report */
    efe_raw_print_report(efe);

    efe_raw_close(efe);
    efe_giebler_close(giebler);
    return 0;
}

/*
 * Export wavesamples from instrument to WAV files
 */
static int cmd_export_wav(const char *efe_path, const char *output_dir)
{
    /* Check if it's Giebler format */
    if (!efe_giebler_is_giebler(efe_path)) {
        fprintf(stderr, "Not a Giebler format EFE file: %s\n", efe_path);
        return 1;
    }

    /* Open Giebler file */
    efe_giebler_t *giebler = efe_giebler_open(efe_path);
    if (!giebler) {
        fprintf(stderr, "Failed to open EFE file: %s\n", efe_path);
        return 1;
    }

    /* Get raw data */
    size_t raw_size;
    const uint8_t *raw_data = efe_giebler_get_raw_data(giebler, &raw_size);

    if (!raw_data || raw_size < 512) {
        fprintf(stderr, "File too small or invalid\n");
        efe_giebler_close(giebler);
        return 1;
    }

    /* Check file type */
    uint8_t file_type = efe_giebler_get_type(giebler);
    if (file_type != 0x03 && file_type != 0x18) {
        fprintf(stderr, "Not an instrument file (type 0x%02X)\n", file_type);
        efe_giebler_close(giebler);
        return 1;
    }

    /* Parse as raw instrument */
    efe_raw_t *efe = efe_raw_open_mem(raw_data, raw_size);
    if (!efe) {
        fprintf(stderr, "Failed to parse instrument\n");
        efe_giebler_close(giebler);
        return 1;
    }

    /* Print info */
    printf("Instrument: %s\n", efe->info.name);
    printf("Wavesamples: %d\n\n", efe->info.num_wavesamples);

    /* Extract all wavesamples */
    int count = efe_raw_extract_all_wav(efe, output_dir);

    efe_raw_close(efe);
    efe_giebler_close(giebler);

    return (count > 0) ? 0 : 1;
}

/* Navigate to a path and return the directory block */
static uint32_t navigate_path(eps_fs_t *fs, const char *path)
{
    if (!path || path[0] == '\0' || strcmp(path, "/") == 0) {
        return fs->root_dir_block;
    }

    char *path_copy = strdup(path);
    char *saveptr;
    char *token = strtok_r(path_copy, "/", &saveptr);
    uint32_t current_dir = fs->root_dir_block;

    while (token) {
        eps_dir_entry_t *entry = eps_find_entry(fs, current_dir, token);
        if (!entry) {
            fprintf(stderr, "Not found: %s\n", token);
            free(path_copy);
            return 0;
        }

        if (entry->file_type != EPS_TYPE_SUBDIR &&
            entry->file_type != EPS_TYPE_PARENT_DIR) {
            fprintf(stderr, "Not a directory: %s\n", token);
            free(path_copy);
            return 0;
        }

        current_dir = entry->first_block;
        token = strtok_r(NULL, "/", &saveptr);
    }

    free(path_copy);
    return current_dir;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    /* Handle mkimage separately - it doesn't need an existing image */
    if (strcmp(argv[1], "mkimage") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s mkimage <filename> <size_mb> [--no-os]\n", argv[0]);
            fprintf(stderr, "  size_mb: Disk size in megabytes (e.g., 100, 500, 1000)\n");
            fprintf(stderr, "  --no-os: Create blank image without embedded OS\n");
            return 1;
        }

        const char *filename = argv[2];
        uint32_t size_mb = strtoul(argv[3], NULL, 10);
        bool include_os = true;

        if (argc >= 5 && strcmp(argv[4], "--no-os") == 0) {
            include_os = false;
        }

        if (size_mb < 1 || size_mb > 8192) {
            fprintf(stderr, "Invalid size: %u MB (must be 1-8192)\n", size_mb);
            return 1;
        }

        return eps_mkimage(filename, size_mb, include_os);
    }

    /* Handle standalone EFE file commands - don't need disk image */
    if (strcmp(argv[1], "inst-info") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s inst-info <file.efe>\n", argv[0]);
            return 1;
        }
        return cmd_inst_info(argv[2]);
    }

    if (strcmp(argv[1], "export-wav") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s export-wav <file.efe> <output-dir>\n", argv[0]);
            return 1;
        }
        return cmd_export_wav(argv[2], argv[3]);
    }

    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }

    const char *image_path = argv[1];
    const char *command = argv[2];

    eps_fs_t *fs = eps_open(image_path);
    if (!fs) {
        fprintf(stderr, "Failed to open image: %s\n", image_path);
        return 1;
    }

    int result = 0;

    if (strcmp(command, "info") == 0) {
        result = cmd_info(fs);
    }
    else if (strcmp(command, "ls") == 0) {
        uint32_t dir_block = fs->root_dir_block;
        int verbose = 0;

        /* Check for -v flag or path */
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "-l") == 0) {
                verbose = 1;
            } else {
                dir_block = navigate_path(fs, argv[i]);
                if (dir_block == 0) {
                    result = 1;
                    goto cleanup;
                }
            }
        }

        result = cmd_ls(fs, dir_block, verbose);
    }
    else if (strcmp(command, "tree") == 0) {
        result = cmd_tree(fs);
    }
    else if (strcmp(command, "cat") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s %s cat <filename>\n", argv[0], argv[1]);
            result = 1;
        } else {
            result = cmd_cat(fs, argv[3]);
        }
    }
    else if (strcmp(command, "extract") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Usage: %s %s extract <filename> <dest>\n", argv[0], argv[1]);
            result = 1;
        } else {
            result = cmd_extract(fs, argv[3], argv[4]);
        }
    }
    else if (strcmp(command, "import") == 0) {
        if (argc < 6) {
            fprintf(stderr, "Usage: %s %s import <src> <name> <type>\n", argv[0], argv[1]);
            fprintf(stderr, "Types: inst, bank, seq, song, sysex, macro, effect\n");
            result = 1;
        } else {
            result = cmd_import(fs, argv[3], argv[4], argv[5]);
        }
    }
    else if (strcmp(command, "import-efe") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s %s import-efe <src.efe> [dest-path]\n", argv[0], argv[1]);
            result = 1;
        } else {
            const char *dest = (argc >= 5) ? argv[4] : "/";
            result = cmd_import_efe(fs, argv[3], dest);
        }
    }
    else if (strcmp(command, "import-dir") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Usage: %s %s import-dir <src-dir> <dest-name>\n", argv[0], argv[1]);
            fprintf(stderr, "Example: %s %s import-dir ./factory FACTORY\n", argv[0], argv[1]);
            result = 1;
        } else {
            result = cmd_import_dir(fs, argv[3], argv[4]);
        }
    }
    else if (strcmp(command, "mkdir") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s %s mkdir <name>\n", argv[0], argv[1]);
            result = 1;
        } else {
            result = cmd_mkdir(fs, argv[3]);
        }
    }
    else if (strcmp(command, "rm") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s %s rm <filename>\n", argv[0], argv[1]);
            result = 1;
        } else {
            result = cmd_rm(fs, argv[3]);
        }
    }
    else if (strcmp(command, "hexdump") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s %s hexdump <block>\n", argv[0], argv[1]);
            result = 1;
        } else {
            result = cmd_hexdump(fs, strtoul(argv[3], NULL, 0));
        }
    }
    else if (strcmp(command, "fat") == 0) {
        uint32_t start = 0;
        uint32_t count = 100;

        if (argc >= 4) start = strtoul(argv[3], NULL, 0);
        if (argc >= 5) count = strtoul(argv[4], NULL, 0);

        result = cmd_fat(fs, start, count);
    }
    else if (strcmp(command, "validate-bank") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s %s validate-bank <path/to/bank>\n", argv[0], argv[1]);
            result = 1;
        } else {
            result = cmd_validate_bank(fs, argv[3]);
        }
    }
    else if (strcmp(command, "validate-banks") == 0) {
        const char *path = (argc >= 4) ? argv[3] : "/";
        uint32_t dir_block = navigate_path(fs, path);
        if (dir_block == 0) {
            result = 1;
        } else {
            int counts = cmd_validate_banks(fs, dir_block, path);
            int total = counts >> 16;
            int valid = counts & 0xFFFF;
            printf("=== SUMMARY ===\n");
            printf("Total banks: %d\n", total);
            printf("Valid banks: %d\n", valid);
            printf("Invalid banks: %d\n", total - valid);
            result = (valid == total) ? 0 : 1;
        }
    }
    else if (strcmp(command, "inst-info") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s %s inst-info <file.efe>\n", argv[0], argv[1]);
            result = 1;
        } else {
            /* Note: This command works directly on EFE files, not image contents */
            eps_close(fs);
            fs = NULL;
            result = cmd_inst_info(argv[3]);
            goto done;
        }
    }
    else if (strcmp(command, "export-wav") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Usage: %s %s export-wav <file.efe> <output-dir>\n", argv[0], argv[1]);
            result = 1;
        } else {
            /* Note: This command works directly on EFE files, not image contents */
            eps_close(fs);
            fs = NULL;
            result = cmd_export_wav(argv[3], argv[4]);
            goto done;
        }
    }
    else {
        fprintf(stderr, "Unknown command: %s\n", command);
        usage(argv[0]);
        result = 1;
    }

cleanup:
    eps_close(fs);
done:
    return result;
}
