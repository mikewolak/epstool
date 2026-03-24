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

### Exporting Wavesamples to WAV

```bash
# Export all wavesamples from an instrument to WAV files
epstool export-wav instrument.efe ./output_dir/
```

This extracts the audio data from EPS instrument files (`.efe`) to standard 16-bit PCM WAV files. Multi-wavesample instruments will produce multiple WAV files.

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

## EPS Instrument File Format

EPS instrument files (`.efe` in Giebler format) contain sample data and parameters. The internal structure (after the 512-byte Giebler header) is:

### Instrument Structure

| Raw Offset | Size | Description |
|------------|------|-------------|
| 0x000-0x07F | 128 | Instrument header |
| 0x00A | 24 | Instrument name (UTF-16LE, 12 chars) |
| 0x086-0x0FF | 122 | Wavesample allocation table |
| 0x280-0x37F | 256 | Layer 1 header |
| 0x298 | 2 | Wavesample count (uint16 LE) |
| 0x29A | 24 | Layer name (UTF-16LE "UNNAMEDLAYER") |
| 0x2C0 | 256 | Key-to-wavesample mapping (2 bytes/key) |
| 0x370-0x48F | 288 | Wavesample 1 parameter block |
| 0x374 | 1 | **Sample rate index** (0-9) |
| 0x37A | 24 | Wavesample name (UTF-16LE "UNNAMED WS") |
| 0x490+ | varies | Sample data |

### Sample Rate Index

The sample rate is stored at offset 0x374 as an index into this table:

| Index | Rate (Hz) | Notes |
|-------|-----------|-------|
| 0 | 52000 | Highest quality |
| 1 | 39000 | |
| 2 | 31200 | |
| 3 | 26000 | Default |
| 4 | 19500 | |
| 5 | 15600 | |
| 6 | 13000 | |
| 7 | 9750 | |
| 8 | 7800 | Most common in factory sounds |
| 9 | 6250 | Lowest quality, longest sample time |

### Sample Data Format

- **Encoding**: 16-bit signed, big-endian
- **Channels**: Mono (EPS), Stereo possible on EPS-16+/ASR
- **Bit depth**: 13-bit effective on original EPS, 16-bit on EPS-16+/ASR

### Layer Header Pattern

Each layer header starts with the signature: `0e 00 00 00 06 XX YY 00 YY 00`
- XX = flags (varies)
- YY = layer number (1-8)
- Followed by layer name in UTF-16LE

Instruments can have up to 8 layers and 127 wavesamples total.

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
