EPS Filesystem Tool - Reference Documentation
=============================================

This directory contains technical documentation for the Ensoniq EPS filesystem
and file formats, gathered from various sources on the internet.

================================================================================
FILES IN THIS DIRECTORY
================================================================================

ensoniq_disk_formats.txt
  - Disk layout (blocks, FAT, directory structure)
  - Based on: https://www.youngmonkey.ca/nose/audio_tech/synth/Ensoniq-DiskFormats.html

epslin_format_specs.txt
  - Technical specs extracted from EpsLinNeo source code
  - Block definitions, directory entry structure, FAT format
  - Based on: https://github.com/handsthatstrike/EpsLinNeo

epslin_source_constants.txt
  - All #define constants from EpsLin v1.58 source
  - File type codes, image sizes, signatures
  - Based on: https://github.com/handsthatstrike/EpsLinNeo/blob/master/EpsLin_v1.58.c

ensoniqfs_info.txt
  - EnsoniqFS project information (Total Commander plugin)
  - Bank file structure, multi-disk handling
  - Based on: https://github.com/thoralt/ensoniqfs

eps_instrument_structure.txt
  - Instrument hierarchy (layers, wavesamples)
  - Sample rates, loop modes, memory layout
  - Compiled from various sources

available_technical_docs.txt
  - List of known technical documents available online
  - Download locations for detailed specifications

================================================================================
KEY ONLINE RESOURCES
================================================================================

EpsLinNeo (Source code for EPS file handling):
  https://github.com/handsthatstrike/EpsLinNeo

EnsoniqFS (Windows filesystem plugin with source):
  https://github.com/thoralt/ensoniqfs

R-Massive Ensoniq Archive (Tools and documentation):
  https://zine.r-massive.com/ensoniq-asr-eps-archive/
  https://zine.r-massive.com/ensoniq-technical-documents-and-schematics/

Chicken Systems (Commercial tools, knowledge base):
  http://www.chickensys.com/support/software/disktools/documentation/glossary.html

Young Monkey (Disk format reference):
  https://www.youngmonkey.ca/nose/audio_tech/synth/Ensoniq-DiskFormats.html

================================================================================
DOCUMENTS TO OBTAIN FOR FULL SPECIFICATIONS
================================================================================

For complete byte-level specifications, these documents should be obtained
from the R-Massive archive or other sources:

1. "Ensoniq EFE File Format" (Steve Quartly & Gary Giebler)
   - Critical for instrument file parsing
   - Contains wavesample structure details

2. "The Ensoniq EPS/EPS16+/ASR-10 Bank Format v0.7" (Thoralt Franz)
   - Available at: http://www.thoralt.de/download/The_Ensoniq_Bank_Format.pdf
   - Bank file reference structure

3. "EPS16+ External Command Specification v1.30"
   - MIDI SysEx details
   - Parameter specifications

4. "Ensoniq EPS & ASR Sequence Structure" (Terje Finstad)
   - Sequence file format

5. "Ensoniq Floppy Diskette Format" (Gary Giebler)
   - Original disk format documentation

================================================================================
SUMMARY OF KEY FORMAT DETAILS
================================================================================

BLOCK SIZE: 512 bytes

DIRECTORY ENTRY: 26 bytes
  - Offset 1: File type (0=empty, 1=OS, 2=dir, 3=inst, 4=bank, etc.)
  - Offset 2-13: Filename (12 chars, space-padded)
  - Offset 14-15: Size in blocks (big-endian)
  - Offset 16-17: Contiguous blocks (big-endian)
  - Offset 18-21: Start block (big-endian, 24-bit)

FAT ENTRY: 3 bytes (big-endian)
  - 0x000000 = Free
  - 0x000001 = End of file
  - 0x000002 = Bad block
  - Other = Next block in chain

SIGNATURES:
  - "ID" at block 1, offset 38-39
  - "OS" at block 2, offset 28-29
  - "DR" at end of directory blocks
  - "FB" at end of FAT blocks

DISK SIZES:
  - EPS floppy: 819,200 bytes (1,600 blocks)
  - ASR floppy: 1,638,400 bytes (3,200 blocks)
  - Hard disk: Variable (up to 8GB with 24-bit FAT)

INSTRUMENT LIMITS:
  - Up to 8 layers per instrument
  - Up to 127 wavesamples per instrument
  - 39 files per directory

SAMPLE RATES: 6.25 kHz to 52 kHz
SAMPLE DEPTH: 13-bit (EPS) or 16-bit (EPS-16+/ASR)
