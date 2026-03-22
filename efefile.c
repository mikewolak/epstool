/*
 * efefile.c - EFE File Tool
 *
 * Standalone tool for working with EFE instrument files.
 * Supports both raw EPS format and Giebler format.
 *
 * Usage:
 *   efefile info <file.efe>           - Show file info
 *   efefile convert <in> <out>        - Convert between formats
 *   efefile strip <giebler> <raw>     - Strip Giebler header
 *   efefile wrap <raw> <giebler>      - Add Giebler header
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "efe_raw.h"
#include "efe_giebler.h"

static void usage(const char *prog) {
    fprintf(stderr, "EFE File Tool - Ensoniq EPS Instrument Handler\n");
    fprintf(stderr, "===============================================\n\n");
    fprintf(stderr, "Usage: %s <command> [args...]\n\n", prog);
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, "  info <file>              Show file information\n");
    fprintf(stderr, "  strip <giebler> <raw>    Convert Giebler to raw format\n");
    fprintf(stderr, "  wrap <raw> <giebler> [name] [type]  Convert raw to Giebler\n");
    fprintf(stderr, "\nFile types for wrap:\n");
    fprintf(stderr, "  inst (default), bank, seq, song, sysex, macro, effect\n");
}

static int cmd_info(const char *filename) {
    /* Try Giebler first */
    if (efe_giebler_is_giebler(filename)) {
        efe_giebler_t *efe = efe_giebler_open(filename);
        if (!efe) {
            fprintf(stderr, "Failed to open Giebler file: %s\n", filename);
            return 1;
        }
        efe_giebler_print_info(efe);
        efe_giebler_close(efe);
        return 0;
    }

    /* Try raw format */
    efe_raw_t *efe = efe_raw_open(filename);
    if (!efe) {
        fprintf(stderr, "Failed to open file: %s\n", filename);
        return 1;
    }
    efe_raw_print_info(efe);
    efe_raw_close(efe);
    return 0;
}

static int cmd_strip(const char *giebler_path, const char *raw_path) {
    if (!efe_giebler_is_giebler(giebler_path)) {
        fprintf(stderr, "Not a Giebler format file: %s\n", giebler_path);
        return 1;
    }

    if (efe_giebler_to_raw(giebler_path, raw_path) != 0) {
        fprintf(stderr, "Failed to convert: %s\n", giebler_path);
        return 1;
    }

    printf("Converted %s -> %s (stripped Giebler header)\n",
           giebler_path, raw_path);
    return 0;
}

static uint8_t parse_type(const char *type_str) {
    if (!type_str || strcmp(type_str, "inst") == 0) return 0x03;
    if (strcmp(type_str, "bank") == 0) return 0x04;
    if (strcmp(type_str, "seq") == 0) return 0x05;
    if (strcmp(type_str, "song") == 0) return 0x06;
    if (strcmp(type_str, "sysex") == 0) return 0x07;
    if (strcmp(type_str, "macro") == 0) return 0x09;
    if (strcmp(type_str, "effect") == 0) return 0x17;
    return 0x03; /* default to instrument */
}

static int cmd_wrap(const char *raw_path, const char *giebler_path,
                    const char *name, const char *type_str) {
    /* Get filename from path if not provided */
    char default_name[13];
    if (!name || name[0] == '\0') {
        const char *base = strrchr(raw_path, '/');
        base = base ? base + 1 : raw_path;
        strncpy(default_name, base, 12);
        default_name[12] = '\0';
        /* Remove extension */
        char *dot = strrchr(default_name, '.');
        if (dot) *dot = '\0';
        name = default_name;
    }

    uint8_t file_type = parse_type(type_str);

    if (efe_raw_to_giebler(raw_path, giebler_path, name, file_type) != 0) {
        fprintf(stderr, "Failed to convert: %s\n", raw_path);
        return 1;
    }

    printf("Converted %s -> %s (added Giebler header, name='%s')\n",
           raw_path, giebler_path, name);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const char *command = argv[1];

    if (strcmp(command, "info") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s info <file>\n", argv[0]);
            return 1;
        }
        return cmd_info(argv[2]);
    }
    else if (strcmp(command, "strip") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s strip <giebler> <raw>\n", argv[0]);
            return 1;
        }
        return cmd_strip(argv[2], argv[3]);
    }
    else if (strcmp(command, "wrap") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s wrap <raw> <giebler> [name] [type]\n", argv[0]);
            return 1;
        }
        const char *name = (argc > 4) ? argv[4] : NULL;
        const char *type = (argc > 5) ? argv[5] : NULL;
        return cmd_wrap(argv[2], argv[3], name, type);
    }
    else {
        fprintf(stderr, "Unknown command: %s\n", command);
        usage(argv[0]);
        return 1;
    }

    return 0;
}
