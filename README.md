# epstool

A command-line tool for manipulating Ensoniq EPS, EPS-16+, and ASR-10/88 filesystem images.

## Overview

`epstool` allows you to read, write, and manipulate disk images from Ensoniq EPS-series samplers. These classic 1980s/90s samplers used a proprietary filesystem format on floppy disks and SCSI hard drives. This tool enables:

- Listing and extracting files from disk images
- Importing files into disk images
- Creating new blank disk images (with optional embedded OS)
- Low-level inspection of disk structures (FAT, blocks)

## Building

Requires a C99-compatible compiler (GCC, Clang).

```bash
make
```

This produces the `epstool` binary.

## Usage

### Creating a New Disk Image

```bash
# Create a 100MB disk image with embedded EPS-16+ OS
epstool mkimage mydisk.hda 100

# Create a blank image without OS
epstool mkimage mydisk.hda 100 --no-os
```

Size can range from 1 to 8192 MB (limited by 24-bit FAT addressing).

### Viewing Disk Information

```bash
epstool mydisk.hda info
```

Output includes disk label, total/free blocks, and filesystem geometry.

### Listing Files

```bash
# List root directory
epstool mydisk.hda ls

# Verbose listing with block details
epstool mydisk.hda ls -v

# List a subdirectory
epstool mydisk.hda ls DRUMS
```

### Directory Tree

```bash
epstool mydisk.hda tree
```

### Extracting Files

```bash
# Extract a file to the host filesystem
epstool mydisk.hda extract "BASS SYNTH" bass_synth.efe
```

### Importing Files

```bash
# Import an instrument file
epstool mydisk.hda import myinstrument.efe "MY INST" inst

# Import types: inst, bank, seq, song, sysex, macro, effect
```

### Creating Directories

```bash
epstool mydisk.hda mkdir "NEW FOLDER"
```

### Deleting Files

```bash
epstool mydisk.hda rm "OLD FILE"
```

### Low-Level Inspection

```bash
# Hex dump a specific block
epstool mydisk.hda hexdump 2

# View FAT entries (block allocation table)
epstool mydisk.hda fat 0 50
```

## Supported File Types

| Type | Code | Description |
|------|------|-------------|
| OS | 0x01 | Operating System |
| Subdir | 0x02 | Subdirectory |
| Instrument | 0x03 | EPS Instrument |
| Bank | 0x04 | Bank of Sounds |
| Sequence | 0x05 | Sequence |
| Song | 0x06 | Song |
| Sys-Ex | 0x07 | MIDI System Exclusive |
| Macro | 0x09 | Macro File |
| EPS-16+ Instrument | 0x19 | EPS-16+ Instrument |
| Effect | 0x1A | EPS-16+ Effect |

## Filesystem Structure

The Ensoniq EPS filesystem uses 512-byte blocks with the following layout:

- **Block 0**: Unused/signature block
- **Block 1**: Device ID block ("ID" signature)
- **Block 2**: OS block + root directory start ("OS" signature)
- **Blocks 3-4**: Root directory continuation
- **Block 5+**: FAT (File Allocation Table)
- **Remaining**: Data blocks

Key characteristics:
- 26-byte directory entries
- 3-byte FAT entries (24-bit block pointers)
- Big-endian byte ordering (Motorola 68000 architecture)
- Up to 39 files per directory
- Up to 8GB addressable with 24-bit FAT

## Supported Image Formats

| Format | Size | Description |
|--------|------|-------------|
| Raw HDA | Variable | Raw SCSI hard disk dump |
| EPS Floppy | 800KB | 1600 blocks (DD) |
| ASR Floppy | 1.6MB | 3200 blocks (HD) |

## Reference Documentation

The `references/` directory contains technical documentation on the EPS filesystem format, gathered from various sources including:

- EpsLinNeo source code
- EnsoniqFS project
- Young Monkey technical archives
- R-Massive Ensoniq archive

## Project Structure

```
├── epstool.c      # CLI tool implementation
├── epsfs.c        # Filesystem library
├── epsfs.h        # API header
├── eps_os.h       # Embedded OS binary for mkimage
├── Makefile       # Build system
└── references/    # Technical documentation
```

## Compatibility

Tested with:
- EPS-16+ hard disk images
- Raw SCSI disk dumps

Should also work with EPS and ASR-10/88 images (same basic filesystem structure).

## License

This project is provided for educational and archival purposes. The Ensoniq EPS filesystem format is a product of Ensoniq Corporation (later E-MU/Creative Labs).

## See Also

- [EpsLinNeo](https://github.com/handsthatstrike/EpsLinNeo) - EPS disk utility
- [EnsoniqFS](https://github.com/thoralt/ensoniqfs) - Total Commander plugin
- [R-Massive Ensoniq Archive](https://zine.r-massive.com/ensoniq-asr-eps-archive/) - Tools and documentation
