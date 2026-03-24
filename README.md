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

This produces the `epstool` binary with HFE floppy image support (via bundled libhxcfe).

## Usage

### Creating a New Disk Image

```bash
# Create a 100MB disk image with embedded EPS-16+ OS
epstool mkimage mydisk.hda 100

# Create a blank image without OS
epstool mkimage mydisk.hda 100 --no-os
```

Size can range from 1 to 8192 MB (limited by 24-bit FAT addressing).

### Creating HFE Floppy Images from EFE Files

The `mkhfe` command creates HFE floppy images directly from EFE (Ensoniq File Exchange) files. HFE is the format used by the [HxC Floppy Emulator](http://hxc2001.com/), allowing you to load sounds onto hardware EPS/EPS-16+ samplers via SD card or USB floppy emulator.

```bash
# Create an HFE floppy with one instrument
epstool mkhfe output.hfe "MY BASS.efe"

# Create an HFE floppy with multiple instruments
epstool mkhfe sounds.hfe "PIANO.efe" "STRINGS.efe" "DRUMS.efe"

# Include the EPS-16+ OS on the floppy (makes it bootable)
epstool mkhfe --os bootdisk.hfe "STARTUP.efe"
```

The command:
1. Creates a temporary 800KB EPS floppy image
2. Imports the specified EFE files (Giebler format supported)
3. Converts to HFE format using the ENSONIQ_DD_800KB disk layout
4. Outputs an HFE file ready for the HxC Floppy Emulator

**Notes:**
- EPS floppies hold 800KB (1600 blocks), so total file size is limited
- Files are imported to the root directory
- The HFE output uses double-density MFM encoding matching original EPS floppies
- Requires libhxcfe (bundled in `libhxcfe/` directory)

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

All offsets are relative to the start of raw instrument data (after 512-byte Giebler header).

| Raw Offset | Size | Description |
|------------|------|-------------|
| 0x000-0x07F | 128 | Instrument header |
| 0x00A | 24 | Instrument name (UTF-16LE, 12 chars) |
| 0x086-0x0FF | 122 | Wavesample allocation table |
| 0x280-0x28F | 16 | Pre-layer data (purpose unknown) |
| 0x290-0x2BF | 48 | Layer 1 header (see pattern below) |
| 0x298 | 2 | Wavesample count (uint16 LE) |
| 0x29A | 24 | Layer name (UTF-16LE "UNNAMEDLAYER") |
| 0x2C0-0x36F | 176 | Key-to-wavesample mapping (88 keys × 2 bytes) |
| 0x370-0x48F | 288 | Wavesample 1 parameter block |
| 0x374 | 1 | **Sample rate index** (0-9) |
| 0x37A | 24 | Wavesample name (UTF-16LE "UNNAMED WS") |
| 0x490+ | varies | Sample data (16-bit big-endian audio) |

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

Each layer header starts with the signature: `0e 00 00 00 06 XX YY 00 ZZ 00`
- XX = flags (0x40, 0x80, 0xC0 observed)
- YY = layer number (1-8)
- ZZ = wavesample count in this layer
- Followed by layer name in UTF-16LE (12 chars)

Layer header structure (48 bytes):
| Offset | Size | Description |
|--------|------|-------------|
| 0x00 | 5 | Signature `0e 00 00 00 06` |
| 0x05 | 1 | Flags |
| 0x06 | 1 | Layer number |
| 0x07 | 1 | 0x00 |
| 0x08 | 1 | Wavesample count |
| 0x09 | 1 | 0x00 |
| 0x0A | 24 | Layer name (UTF-16LE) |
| 0x22+ | varies | Additional layer data |

Instruments can have up to 8 layers and 127 wavesamples total.

### Multi-Wavesample Instruments

For instruments with multiple wavesamples:

1. **Wavesample Allocation Table** (0x086-0x0FF): Contains up to 30 entries of 4 bytes each:
   - Bytes 0-1: Start block number (uint16 LE)
   - Bytes 2-3: Block count (uint16 LE)
   - Data offset = start_block × 512 (from raw instrument start)

2. **Key-to-Wavesample Mapping** (0x2C0-0x36F): 88 entries of 2 bytes each (piano range, keys 0-87).
   The high byte contains the wavesample number (1-based index into allocation table).

3. **Sample Data**: Located at block offsets specified in allocation table. Each wavesample's
   audio data may be non-contiguous in the file.

Example from ELEC PIANO (2 displayed wavesamples, 4 unique audio regions):
```
WS 2: block 140, 7 blocks (3584 bytes) - lower keys
WS 4: block 41, 6 blocks (3072 bytes)
WS 7: block 126, 6 blocks (3072 bytes)
WS 10: block 12, 7 blocks (3584 bytes) - upper keys
```

## Wavesample Parameter Block (RAM Format)

From the official Ensoniq MIDI SysEx Specification (EPS-MKB2), the wavesample parameter block in RAM consists of 138 words. Each parameter occupies one 16-bit word with the value in the **high byte**.

### Main Parameters (138 words total)

| Word | Description | Range/Notes |
|------|-------------|-------------|
| 0-11 | Name | 12 ASCII bytes, one per word |
| 12 | Wavesample Copy Number | 0 = original ✓ |
| 13 | Wavesample Copy Layer | 0 = original (layer# if copy) ✓ |
| 14-35 | Pitch Envelope #1 | 22 words (see envelope structure) |
| 36-57 | Filter Envelope #2 | 22 words |
| 58-79 | Amplitude Envelope #3 | 22 words |
| **80** | **Root Key** | MIDI key 0-127 ✓ |
| 81 | Pitch Envelope Amount | 0-127 ✓ |
| 82 | LFO to Pitch Amount | 0-127 ✓ |
| 83 | Random Modulation Amount | 0-127 ✓ |
| 84 | Pitch Wheel Bend Range | Typically 13 (±13 semitones) ✓ |
| 85 | Modulation Source | 3=MW, 10=Aftertouch ✓ |
| 86 | Fine Tune | Signed (-128 to +127 cents) ✓ |
| 87 | Modulation Amount | 0-255 ✓ |
| 88 | Filter Mode | 0=off, 1-3=filter types ✓ |
| 89 | FC#1 Cutoff | 0-127 ✓ |
| 90 | FC#2 Cutoff | 0-127 ✓ |
| 91 | FC#1 Keyboard Amount | 0-127 ✓ |
| 92 | FC#2 Keyboard Amount | 0-127 ✓ |
| 93 | FC#1 Filter Envelope Amount | 0-127 ✓ |
| 94 | FC#2 Filter Envelope Amount | 0-127 ✓ |
| 95 | FC#1 Modulation Source | 0-15 (standard mod sources) ✓ |
| 96 | FC#2 Modulation Source | 0-15 (standard mod sources) ✓ |
| 97 | FC#1 Modulation Amount | 0-255 ✓ |
| 98 | FC#2 Modulation Amount | 0-255 ✓ |
| 99 | Volume | 0-127 ✓ |
| 100 | Amplitude Modulation Source | 13=common ✓ |
| 101 | Amplitude Crossfade Curve A | 0-127 ✓ |
| 102 | Amplitude Crossfade Curve B | 0-127 ✓ |
| 103 | Amplitude Crossfade Curve C | 0-127 ✓ |
| 104 | Amplitude Crossfade Curve D | 0-127 ✓ |
| 105 | Pan Position | 0-7 (0=L, 7=R, center=3-4) ✓ |
| 106 | Amplitude Modulation Amount | 0-127 ✓ |
| 107 | LFO Waveform | 0=tri, 2=square ✓ |
| 108 | LFO Speed | 0-127 ✓ |
| 109 | LFO Depth | 0-127 ✓ |
| 110 | LFO Delay Time | 0-127 ✓ |
| 111 | LFO Modulation Source | 10=Aftertouch, 14-15=common ✓ |
| 112 | LFO Mode | 0=normal, 1=sync, 2=key sync ✓ |
| 113 | Random Modulator Frequency | 0-127 ✓ |
| **114** | **Loop Mode** | 0-4 (fwd/bwd/loop fwd/loop bidi/loop+rel) ✓ |
| **115-118** | **Sample Start Offset** | 32-bit, high bytes only, shift right 9 ✓ |
| **119-122** | **Sample End Offset** | 32-bit, high bytes only, shift right 9 ✓ |
| **123-126** | **Loop Start Offset** | 32-bit, high bytes only, shift right 9 ✓ |
| **127-130** | **Loop End Offset** | 32-bit, high bytes only, shift right 9 ✓ |
| **131** | **Sample Rate** | Period = rate × 1.6 µs |
| 132 | Key Range Lo | MIDI note 0-127 ✓ |
| 133 | Key Range Hi | MIDI note 0-127 ✓ |
| 134 | Start/Loop Modulation Source | 0-15 (default 13=On) ✓ |
| 135 | Start/Loop Modulation Amount | 0-255 (default 0) ✓ |
| 136 | Start/Loop Modulation Range | 0-255 (default 6) ✓ |
| 137 | Modulation Type | 0=none, 1=start, 2=loop, 3=both ✓ |
| 138 | unused | 0 |

### Envelope Structure (22 words each)

Each of the three envelopes (Pitch, Filter, Amplitude) uses 22 words:

| Offset | Description |
|--------|-------------|
| 0 | Envelope Type (preset selection) |
| 1 | Soft Level 0 (initial level) |
| 2 | Hard Level 0 (initial level) |
| 3 | Time 1 (attack time) |
| 4 | Soft Level 1 (peak level) |
| 5 | Hard Level 1 (peak level) |
| 6 | Time 2 (first decay time) |
| 7 | Soft Level 2 |
| 8 | Hard Level 2 |
| 9 | Time 3 (second decay time) |
| 10 | Soft Level 3 |
| 11 | Hard Level 3 |
| 12 | Time 4 (third decay time) |
| 13 | Soft Level 4 (sustain level) |
| 14 | Hard Level 4 (sustain level) |
| 15 | Time 5 (release time) |
| 16 | Velocity Switch (soft level on/off) |
| 17 | Level 5 (release breakpoint, relative +/-) |
| 18 | Time 6 (second release time) |
| 19 | Time 1 velocity sensitivity |
| 20 | Keyboard Time Scaling |
| 21 | Mode (0=normal, 1=cycle, 2=repeat) |

**Note:** This is the RAM format used for MIDI SysEx transfers. The on-disk format differs - see mapping below.

### Disk-to-RAM Mapping (Confirmed)

The disk wavesample parameter block at offset 0x370 (from Giebler header start) contains a 10-byte disk-only header followed by the RAM parameter block. The mapping formula is:

```
Disk offset = 0x370 + 10 + (RAM_word × 2)
```

| Disk Offset | Bytes | RAM Word | Description | Verified |
|-------------|-------|----------|-------------|----------|
| 0x370+0 | 10 | (header) | Disk-only header | ✓ |
| 0x370+4 | 1 | (header) | Sample Rate INDEX (0-9) | ✓ |
| 0x370+10 | 24 | 0-11 | Name (12 chars) | ✓ |
| 0x370+38 | 44 | 14-35 | Pitch Envelope (22 words) | ✓ |
| 0x370+82 | 44 | 36-57 | Filter Envelope (22 words) | ✓ |
| 0x370+126 | 44 | 58-79 | Amplitude Envelope (22 words) | ✓ |
| 0x370+170 | 2 | 80 | Root Key (MIDI 0-127) | ✓ |
| 0x370+182 | 2 | 86 | Fine Tune (signed) | ✓ |
| 0x370+186 | 2 | 88 | Filter Mode | ✓ |
| 0x370+188 | 2 | 89 | FC#1 Cutoff | ✓ |
| 0x370+190 | 2 | 90 | FC#2 Cutoff | ✓ |
| 0x370+208 | 2 | 99 | Volume | ✓ |
| 0x370+220 | 2 | 105 | Pan Position | ✓ |
| 0x370+224 | 2 | 107 | LFO Waveform | ✓ |
| 0x370+226 | 2 | 108 | LFO Speed | ✓ |
| 0x370+228 | 2 | 109 | LFO Depth | ✓ |
| 0x370+238 | 2 | 114 | Loop Mode (0-4) | ✓ |
| 0x370+240 | 8 | 115-118 | Sample Start (4 words) | ✓ |
| 0x370+248 | 8 | 119-122 | Sample End (4 words) | ✓ |
| 0x370+256 | 8 | 123-126 | Loop Start (4 words) | ✓ |
| 0x370+264 | 8 | 127-130 | Loop End (4 words) | ✓ |
| 0x370+274 | 2 | 132 | Key Range Lo | ✓ |
| 0x370+276 | 2 | 133 | Key Range Hi | ✓ |

**10-byte Disk Header** (before RAM block):
```
Byte 0:   Checksum or ID (varies per instrument, e.g., 0x2A, 0xB3, 0x55)
Byte 1:   0x00 (constant)
Byte 2:   0x00 (usually, 0x01 observed in PAN FLOOT)
Byte 3:   Flags byte (bit field, see below)
Byte 4:   Sample Rate Index (0-9) ✓
Byte 5:   0x80 (constant flag, marks valid wavesample) ✓
Bytes 6-7: Unknown (16-bit LE word, varies)
Bytes 8-9: Unknown (16-bit LE word, varies)
```

**Byte 3 Flags** (observed bit patterns):
- 0x00 = no flags
- 0x10 = bit 4 set
- 0x20 = bit 5 set
- 0x30 = bits 4+5 set
- 0x40 = bit 6 set
- 0x80 = bit 7 set
- 0x90 = bits 4+7 set

Common values observed:
- Rate index 8 (7800 Hz) most common in factory sounds
- Rate index 6 (13000 Hz) used for synthesized sounds
- B5 = 0x80 indicates valid wavesample

**Loop Mode Values:**
- 0 = Forward (no loop)
- 1 = Backward (no loop, play in reverse)
- 2 = Loop Forward (most common)
- 3 = Loop Bidirectional (ping-pong)
- 4 = Loop and Release

**Modulation Source Values:**
| Value | Source |
|-------|--------|
| 0 | Off |
| 3 | Mod Wheel |
| 6 | Velocity |
| 9 | Keyboard |
| 10 | Aftertouch |
| 13 | On (constant) |
| 14-15 | Off/On variants |

### Decoding Sample/Loop Offsets

The 4-word sample and loop offset fields (words 115-130) encode 32-bit sample positions:

1. Each field uses 4 consecutive 16-bit words
2. Only the **high byte** of each word contains data (per RAM format)
3. Combine high bytes as big-endian 32-bit value
4. Shift right by 9 bits to get sample count

```c
// Example: decoding Sample End (words 119-122 at disk offset 0x370+248)
uint8_t b0 = data[offset + 0];  // high byte of word 119
uint8_t b1 = data[offset + 2];  // high byte of word 120
uint8_t b2 = data[offset + 4];  // high byte of word 121
uint8_t b3 = data[offset + 6];  // high byte of word 122

uint32_t raw = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
uint32_t sample_count = raw >> 9;
```

**Note:** Multi-wavesample instruments have different data at these offsets for the non-primary wavesamples.

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
| HFE | ~2MB | HxC Floppy Emulator format (output only) |

### HFE Support (libhxcfe)

The `mkhfe` command uses the [HxC Floppy Emulator library](http://hxc2001.com/) (libhxcfe) to generate HFE files compatible with the HxC Floppy Emulator hardware and software. The library is bundled in the `libhxcfe/` directory.

HFE files contain MFM-encoded floppy data that perfectly replicates the magnetic signal patterns of original EPS floppy disks. This allows:
- Loading sounds onto real EPS/EPS-16+ hardware via HxC USB Floppy Emulator
- Using the HxC software floppy emulator with vintage sampler software
- Archiving and distributing EPS sounds in a hardware-compatible format

The conversion uses the predefined `ENSONIQ_DD_800KB` disk layout which specifies:
- 80 tracks, 2 sides
- 10 sectors per track
- 512 bytes per sector
- Double-density MFM encoding
- 250 kbit/s data rate

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
├── efe_giebler.c  # Giebler EFE format parser
├── efe_raw.c      # Raw EFE data handling
├── efefile.c      # Standalone EFE info tool
├── eps_os.h       # Embedded OS binary for mkimage
├── Makefile       # Build system
├── libhxcfe/      # HxC Floppy Emulator library (for HFE output)
│   ├── libhxcfe.h # API header
│   └── libhxcfe.so # Shared library (Linux x86_64)
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
