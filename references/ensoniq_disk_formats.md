# Ensoniq EPS, EPS16, SD-1, VFX-SD Disk Format Specifications

Source: https://www.youngmonkey.ca/nose/audio_tech/synth/Ensoniq-DiskFormats.html

## Overview

These four Ensoniq synthesizer models share the same fundamental disk architecture:
- 80 tracks (0-79) per side
- 10 sectors (0-9) per track
- 512-byte sectors
- Data fills both sides before advancing to the next track
- 1,600 total blocks per diskette

## Block Calculation

The conversion formula between linear block numbers and physical locations is:

```
Block = ((Track x 2) + Head) x 10) + Sector
```

Reverse calculation:
```
Track = Block ÷ 20 (integer)
Head = (Block mod 20) ÷ 10
Sector = Block mod 10
```

## Sector Layout

### EPS & EPS-16 Plus

| Block | Track | Head | Sector | Description |
|-------|-------|------|--------|-------------|
| 0 | 0 | 0 | 0 | Unused - Repeating 2 byte pattern of 6D B6 (hex) |
| 1 | 0 | 0 | 1 | Device ID Block (similar to VFX-SD) |
| 2 | 0 | 0 | 2 | Operating System Block |
| 3 | 0 | 0 | 3 | Main Directory (1st sector) |
| 4 | 0 | 0 | 4 | Main Directory (2nd sector) |
| 5-14 | - | - | - | File Allocation Blocks |
| 15-1599 | - | - | - | Unused - Repeating 2 byte pattern of 6D B6 (hex) |

### SD-1 & VFX-SD

| Block | Track | Head | Sector | Description |
|-------|-------|------|--------|-------------|
| 0 | 0 | 0 | 0 | Unused - Repeating 2 byte pattern of 6D B6 (hex) |
| 1 | 0 | 0 | 1 | Device ID Block (similar to EPS) |
| 2 | 0 | 0 | 2 | Operating System Block |
| 3 | 0 | 0 | 3 | Main Directory (1st sector) - Points to Sub-Directories 1-4 |
| 4 | 0 | 0 | 4 | Main Directory (2nd sector) |
| 5-14 | - | - | - | File Allocation Blocks |
| 15 | 0 | 1 | 5 | Sub-Directory 1 (1st sector) |
| 16 | 0 | 1 | 6 | Sub-Directory 1 (2nd sector) |
| 17 | 0 | 1 | 7 | Sub-Directory 2 (1st sector) |
| 18 | 0 | 1 | 8 | Sub-Directory 2 (2nd sector) |
| 19 | 0 | 1 | 9 | Sub-Directory 3 (1st sector) |
| 20 | 1 | 0 | 0 | Sub-Directory 3 (2nd sector) |
| 21 | 1 | 0 | 1 | Sub-Directory 4 (1st sector) |
| 22 | 1 | 0 | 2 | Sub-Directory 4 (2nd sector) |
| 23-1599 | - | - | - | Unused - Repeating 2 byte pattern of 6D B6 (hex) |

## Device ID Block (Block 1)

The Device ID Block contains the following 40-byte pattern (repeated to fill the entire block on a newly formatted disk). The keyboards only read the first occurrence of the pattern. They overwrite the rest of the block with unused data when storing files.

### Standard Pattern (Hex)
```
00 80 01 00 00 0A 00 02 00 50 00 00 02 00 00 00 06 40 1E 02
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 49 44
```

### Byte Descriptions

| Byte | Description |
|------|-------------|
| 1 | Peripheral Device Type |
| 2 | Removable Media Device Type |
| 3 | Various Standards Version # |
| 4 | Reserved for SCSI |
| 5-6 | Number of Sectors per Track (10 Sectors) |
| 7-8 | Number of Read/Write Heads (2 Heads) |
| 9-10 | Number of Cylinders (80 Tracks) |
| 11-14 | Number of Bytes per Block (512 Bytes) |
| 15-18 | Number of Blocks on Diskette (1600 Blocks) |
| 19 | SCSI Medium Type |
| 20 | SCSI Density Code |
| 21-30 | Reserved for later use |
| 31-38 | EPS-16 Disk Label (preceded by FF) |
| 39-40 | Device ID Signature = "ID" |

### EPS-16 Plus Disk Label Example

For a disk labeled 'DISK000':
```
00 80 01 00 00 0A 00 02 00 50 00 00 02 00 00 00 06 40 1E 02
00 00 00 00 00 00 00 00 00 00 FF 44 49 53 4B 30 30 30 49 44
                               (  D  I  S  K  0  0  0 )
```

## Operating System Block (Block 2)

The Operating System Block contains a 30-byte pattern (repeated to fill the block). Only the first occurrence is read.

### Standard Pattern (Hex)
```
00 14 36 14 02 31 01 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 4F 53
```

### EPS with OS Pattern (Hex)
```
00 14 36 14 02 31 01 00 00 00 00 00 00 00 00 00
01 08 02 31 00 00 00 00 00 00 00 00 4F 53
```

### Byte Descriptions

| Byte | Description |
|------|-------------|
| 1-4 | Remaining blocks available (free blocks) |
| 5-8 | EPS OS Revision (if OS stored on disk) |
| 9-10 | Keyboard Type: 00 00 = EPS/EPS-16, 00 01 = VFX-SD |
| 11-28 | Reserved/unused |
| 29-30 | OS Signature = "OS" |

## Directory Entry Structure (26 bytes)

| Byte | Description |
|------|-------------|
| 1 | Type-dependent information (bank/preset number for VFX-SD) |
| 2 | File type code (see File Types below) |
| 3-14 | File name (12 characters for EPS, 11 + null for VFX-SD) |
| 15-16 | Number of blocks in file (big-endian) |
| 17-18 | Number of contiguous blocks (big-endian) |
| 19-22 | First block pointer (big-endian, 24-bit used) |
| 23 | File number 0-59 (VFX-SD only) |
| 24-26 | File byte count (24-bit, VFX-SD only) |

### Directory Block Structure

- Each directory spans 2 blocks
- Block 1: Up to 19 entries (26 bytes each) + 18 bytes padding
- Block 2: Up to 19 entries + ends with "DR" signature at bytes 511-512
- Empty directories have all zeros in sector 1

## File Type Codes

| Code | Type | Description |
|------|------|-------------|
| 0x00 | Unused | Empty entry |
| 0x01 | EPS-OS | EPS Operating System |
| 0x02 | Sub-Dir | Subdirectory |
| 0x03 | Instr | Instrument |
| 0x04 | EPS-Bnk | Bank |
| 0x05 | Seq | Sequence |
| 0x06 | Song | Song |
| 0x07 | Sys-Ex | System Exclusive |
| 0x08 | ParentDir | Parent directory pointer |
| 0x09 | Macro | Macro file |
| 0x0A | Prog-1 | VFX-SD 1-voice Program |
| 0x0B | Prog-6 | VFX-SD 6-voice Program |
| 0x0C | Prog-30 | VFX-SD 30-voice Program |
| 0x0D | Prog-60 | VFX-SD 60-voice Program |
| 0x0E | Preset-1 | VFX-SD 1-voice Preset |
| 0x0F | Preset-6 | VFX-SD 6-voice Preset |
| 0x10 | Preset-30 | VFX-SD 30-voice Preset |
| 0x11 | Preset-60 | VFX-SD 60-voice Preset |
| 0x12 | Seq-VFX | VFX-SD Sequence |
| 0x13 | Song-VFX | VFX-SD Song |
| 0x14 | Sys-Ex-VFX | VFX-SD System Exclusive |
| 0x15 | Seq-Song | VFX-SD Sequence-Song combo |
| 0x16 | All-VFX | VFX-SD All data |
| 0x17 | Effect | EPS-16+ Effect |
| 0x18 | E16-Inst | EPS-16+ Instrument |
| 0x19 | E16-Bnk | EPS-16+ Bank |
| 0x1A | E16-Effect | EPS-16+ Effect (alternate) |
| 0x1B | E16-OS | EPS-16+ Operating System |

## File Allocation Table (FAT)

- Located in blocks 5-14 (10 blocks for floppy)
- Each entry is 3 bytes (24-bit)
- 170 entries per block (510 bytes used, 2 bytes padding)

### FAT Entry Values

| Value | Meaning |
|-------|---------|
| 0x000000 | Unused/free block |
| 0x000001 | End of file |
| 0x000002 | Bad sector |
| Other | Pointer to next block in chain |

### Notes

- Files are stored contiguously when possible
- FAT enables non-contiguous storage when disk is fragmented
- Contiguous block count in directory entry allows optimized reading
