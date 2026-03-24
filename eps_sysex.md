# Ensoniq EPS MIDI SysEx Protocol

Reference documentation for building applications to transfer instruments and samples to/from the Ensoniq EPS and EPS-16+ samplers via MIDI System Exclusive.

Based on: *Ensoniq Performance Sampler External Command Specification* (June 12, 1989, MKB2)

---

## Table of Contents

1. [Protocol Overview](#protocol-overview)
2. [Message Format](#message-format)
3. [Communication Rules](#communication-rules)
4. [Command Reference](#command-reference)
5. [Parameter Blocks](#parameter-blocks)
6. [Status Codes](#status-codes)
7. [Parameter Numbers](#parameter-numbers)
8. [Implementation Notes](#implementation-notes)

---

## Protocol Overview

The EPS SysEx protocol enables bidirectional communication between the EPS sampler and an external computer (EXT) for:

- Reading/writing instrument, layer, and wavesample parameters
- Transferring sample wavedata
- Creating, copying, and deleting instruments/layers/wavesamples
- Digital sample processing (crossfade loops, normalize, etc.)
- Remote control via virtual button presses

### Key Concepts

- **Instrument**: Top-level container (slots 0-7, displayed as 1-8)
- **Layer**: Velocity layer within an instrument (0-7)
- **Wavesample**: Individual sample within a layer (1-127)
- **Pitch Table**: Custom tuning table (0-7 per instrument)
- **Patch**: Layer combination for performance (0-3)

---

## Message Format

### SysEx Packet Structure

Every message is wrapped in a SysEx frame:

```
[SysEx Head] [Message] [SysEx Tail]
```

### SysEx Header (4 bytes)

| Byte | Hex  | Description |
|------|------|-------------|
| 0    | F0   | SysEx status byte |
| 1    | 0F   | Ensoniq manufacturer ID |
| 2    | 03   | EPS product ID |
| 3    | 0n   | Base MIDI channel (n = 0-15) |

### SysEx Tail (1 byte)

| Byte | Hex  | Description |
|------|------|-------------|
| 0    | F7   | End of SysEx |

### Data Word Formats

The protocol uses two word formats to minimize bandwidth:

#### 12-Bit Word Format (2 bytes)

Used for most parameters. Transmits the lower 12 bits of a 16-bit value:

```
Byte 0: 00xxxxxx  (bits 11-6)
Byte 1: 00yyyyyy  (bits 5-0)

16-bit value: 0000xxxxxxyyyyyy
```

**Example**: Value 0x0ABC transmitted as bytes 0x2A, 0x3C

#### 16-Bit Word Format (3 bytes)

Used for parameter blocks and wavedata. Transmits all 16 bits:

```
Byte 0: 0000HHHH  (bits 15-12)
Byte 1: 00hhhhhh  (bits 11-6)
Byte 2: 00llllll  (bits 5-0)

16-bit value: HHHHhhhhhhllllll
```

**Example**: Value 0xABCD transmitted as bytes 0x0A, 0x2F, 0x0D

### Wavedata Offset Format

20-bit offsets transmitted as two 12-bit words (4 bytes):

```
Offset 0x00200 → bytes: 00, 00, 08, 00
Offset 0xAAAAA → bytes: 00, 0A, 2A, 2A
```

### Scale Factor Format

16-bit fixed-point (8.7 format: integer.fraction):

```
High byte: integer part (0-255)
Low byte:  fractional part (0-127, representing 0.0-0.99)

Factor 1.0   = 0x0100 → bytes: 00, 00, 04, 00
Factor 255.99 = 0xFF7F → bytes: 00, 0F, 3D, 3F
```

---

## Communication Rules

### Normal Mode

Standard request-response protocol:

1. **Transmitter** sends command, starts 2-second timeout
2. **Receiver** processes command, sends RESPONSE with status code
3. **Transmitter** waits for response before sending next command

For PUT commands with data blocks:
1. First message: command + parameters
2. Wait for ACK
3. Second message: data block
4. Wait for final ACK

### Wait Mode

For long operations, receiver can request more time:

1. Receiver sends RESPONSE with WAIT status (0x01)
2. Transmitter resets timeout to 30 seconds, sends ACK
3. Receiver can send additional WAIT responses if needed
4. Transmitter can send CANCEL to abort

### Open Loop Mode

Single MIDI cable (no response path):
- Transmitter waits full 2 seconds between commands
- No error recovery possible

---

## Command Reference

### Command Byte Values

| Code | Name | Direction | Description |
|------|------|-----------|-------------|
| 00 | - | - | Unused |
| 01 | RESPONSE | Both | Status response |
| 02 | CANCEL | EXT→EPS | Abort in WAIT mode |
| 03 | GET INSTRUMENT | EXT→EPS | Request instrument params |
| 04 | GET LAYER | EXT→EPS | Request layer params |
| 05 | GET WAVESAMPLE PARAMETERS | EXT→EPS | Request wavesample params |
| 06 | GET WAVESAMPLE DATA | EXT→EPS | Request wavedata |
| 07 | GET PITCH TABLE | EXT→EPS | Request pitch table |
| 08 | GET PARAMETER | EXT→EPS | Request single parameter |
| 0A | GET WAVESAMPLE OVERVIEW | EXT→EPS | Request waveform thumbnail |
| 0C | PUT INSTRUMENT | Both | Send instrument params |
| 0D | PUT LAYER | Both | Send layer params |
| 0E | PUT WAVESAMPLE PARAMETERS | Both | Send wavesample params |
| 0F | PUT WAVESAMPLE DATA | Both | Send wavedata |
| 10 | PUT PITCH TABLE | Both | Send pitch table |
| 11 | PUT PARAMETER | EXT→EPS | Set single parameter (no response) |
| 13 | PUT WAVESAMPLE OVERVIEW | EPS→EXT | Send waveform thumbnail |
| 15 | CREATE INSTRUMENT | EXT→EPS | Create empty instrument |
| 16 | CREATE LAYER | EXT→EPS | Create layer with 1 wavesample |
| 17 | DELETE LAYER | EXT→EPS | Delete layer and all wavesamples |
| 18 | COPY LAYER | EXT→EPS | Copy layer to destination |
| 19 | CREATE WAVESAMPLE | EXT→EPS | Create wavesample (square wave) |
| 1A | DELETE WAVESAMPLE | EXT→EPS | Delete wavesample |
| 1B | COPY WAVESAMPLE | EXT→EPS | Copy wavesample |
| 1C | DELETE INSTRUMENT | EXT→EPS | Delete instrument |
| 1D | AUDITION WAVESAMPLES | EXT→EPS | A/B compare wavesamples |
| 1E | TRUNCATE WAVESAMPLE | EXT→EPS | Remove unused data |
| 1F | CLEAR WAVEDATA | EXT→EPS | Zero wavedata range |
| 20 | COPY WAVEDATA | EXT→EPS | Copy wavedata |
| 21 | ADD WAVEDATA | EXT→EPS | Mix wavedata |
| 22 | SCALE WAVEDATA | EXT→EPS | Gain adjustment |
| 23 | INVERT WAVEDATA | EXT→EPS | Phase invert |
| 24 | REVERSE WAVEDATA | EXT→EPS | Reverse sample |
| 25 | REPLICATE WAVEDATA | EXT→EPS | Tile/repeat sample |
| 26 | CROSS FADE LOOP | EXT→EPS | Create crossfade loop |
| 27 | FADE IN WAVEDATA | EXT→EPS | Fade in |
| 28 | FADE OUT WAVEDATA | EXT→EPS | Fade out |
| 29 | REVERSE CROSS FADE LOOP | EXT→EPS | Reverse + crossfade |
| 2A | ENSEMBLE CROSS FADE LOOP | EXT→EPS | Zone = loop length |
| 2B | BOWTIE CROSS FADE LOOP | EXT→EPS | Symmetric crossfade |
| 2C | LENGTHEN LOOP | EXT→EPS | Extend loop |
| 2D | MIX WAVEDATA | EXT→EPS | Mix with balance |
| 2E | MERGE/SPLICE WAVEDATA | EXT→EPS | Merge with crossfade |
| 2F | VOLUME SMOOTHING | EXT→EPS | Dynamic compression |
| 40 | VIRTUAL BUTTON PRESS | EXT→EPS | Simulate button |
| 41 | NORMALIZE GAIN | EXT→EPS | Normalize to full scale |
| 42 | SYNTHESIZED LOOP | EXT→EPS | AI loop synthesis |
| 43 | BIDIRECTIONAL CROSS FADE | EXT→EPS | Bidi loop crossfade |
| 44 | CREATE PRESET | EXT→EPS | Save preset |

---

### Common Parameter Format

Most commands include the edit context (6 bytes in 12-bit format):

```
Byte 0-1: Instrument number (0-7)
Byte 2-3: Layer number (0-7)
Byte 4-5: Wavesample number (1-127, or 0 for "all")
```

### GET Commands

#### GET INSTRUMENT (0x03)

Request instrument parameter block.

```
F0 0F 03 0n 03 [inst] [layer] [ws] F7
```

Response: PUT INSTRUMENT

#### GET LAYER (0x04)

Request layer parameter block.

```
F0 0F 03 0n 04 [inst] [layer] [ws] F7
```

Response: PUT LAYER

#### GET WAVESAMPLE PARAMETERS (0x05)

Request wavesample parameter block.

```
F0 0F 03 0n 05 [inst] [layer] [ws] F7
```

Response: PUT WAVESAMPLE PARAMETERS

#### GET WAVESAMPLE DATA (0x06)

Request wavedata in specified range.

```
F0 0F 03 0n 06 [inst] [layer] [ws] [start_offset] [end_offset] F7
```

- `start_offset`: 20-bit offset from wavesample start (4 bytes)
- `end_offset`: 20-bit offset from wavesample start (4 bytes)

Response: PUT WAVESAMPLE DATA

#### GET PITCH TABLE (0x07)

Request pitch table for layer.

```
F0 0F 03 0n 07 [inst] [layer] [ws] F7
```

Response: PUT PITCH TABLE

#### GET PARAMETER (0x08)

Request single parameter value.

```
F0 0F 03 0n 08 [inst] [layer] [ws] [param_num] F7
```

- `param_num`: Parameter number (2 bytes, see Parameter Numbers)

Response: PUT PARAMETER

#### GET WAVESAMPLE OVERVIEW (0x0A)

Request waveform thumbnail (512 peak values).

```
F0 0F 03 0n 0A [inst] [layer] [ws] [start_offset] [end_offset] F7
```

Response: PUT WAVESAMPLE OVERVIEW

### PUT Commands

#### PUT PARAMETER (0x11)

Set single parameter. **No response required** (only sends response on error).

```
F0 0F 03 0n 11 [inst] [layer] [ws] [param_num] [value_hi] [value_lo] F7
```

- `param_num`: 2 bytes (12-bit format)
- `value_hi`, `value_lo`: 4 bytes total (two 12-bit words for 24-bit value)

### Instrument/Layer/Wavesample Management

#### CREATE INSTRUMENT (0x15)

Create empty instrument (no layers/wavesamples).

```
F0 0F 03 0n 15 [inst] [layer] [ws] F7
```

#### DELETE INSTRUMENT (0x1C)

Delete instrument and free memory.

```
F0 0F 03 0n 1C [inst] [layer] [ws] F7
```

#### CREATE LAYER (0x16)

Create layer with one default wavesample.

```
F0 0F 03 0n 16 [inst] [layer] [ws] F7
```

#### DELETE LAYER (0x17)

Delete layer and all its wavesamples.

```
F0 0F 03 0n 17 [inst] [layer] [ws] F7
```

#### COPY LAYER (0x18)

Copy layer to destination.

```
F0 0F 03 0n 18 [src_inst] [src_layer] [src_ws] [dst_inst] [dst_layer] [dummy_ws] [copy_data] F7
```

- `copy_data`: 0=params only, 1=params + wavedata

#### CREATE WAVESAMPLE (0x19)

Create wavesample with single-cycle square wave.

```
F0 0F 03 0n 19 [inst] [layer] [ws] F7
```

#### DELETE WAVESAMPLE (0x1A)

Delete wavesample and free memory.

```
F0 0F 03 0n 1A [inst] [layer] [ws] F7
```

#### COPY WAVESAMPLE (0x1B)

Copy wavesample to destination.

```
F0 0F 03 0n 1B [src_inst] [src_layer] [src_ws] [dst_inst] [dst_layer] [dst_ws] [copy_data] F7
```

---

## Parameter Blocks

### Instrument Parameter Block (PUT INSTRUMENT data)

323 words, transmitted in 16-bit format.

| Word | Description |
|------|-------------|
| 0-11 | Name (12 ASCII characters, 1 per word) |
| 12 | MIDI Channel (outbound) |
| 13 | MIDI Program Number (outbound) |
| 14 | MIDI Pressure Mode (outbound) |
| 15 | Total Size in blocks (1 block = 256 words) |
| 16 | Key Destination (0=Both, 1=Local, 2=MIDI) |
| 17 | Patch 0 (layer bitmap: bit0=L1, bit1=L2...) |
| 18 | Patch 1 |
| 19 | Patch 2 |
| 20 | Patch 3 |
| 21 | Key Down Layers (bitmap) |
| 22 | Key Up Layers (bitmap) |
| 23-25 | Unused |
| 26 | Key Range Lo |
| 27 | Key Range Hi |
| 28 | Transposition (signed semitones) |
| 29-44 | Pitch Table Offsets (relative to inst base) |
| 45-60 | Layer Offsets (relative to inst base) |
| 61-316 | Wavesample Offsets (256 entries) |
| 317-322 | Unused |

### Layer Parameter Block (PUT LAYER data)

107 words, transmitted in 16-bit format.

| Word | Description |
|------|-------------|
| 0-11 | Name (12 ASCII characters) |
| 12 | Glide Mode (0=Off, 1=Mono, 2=Pedal) |
| 13 | Glide Time (0-127) |
| 14 | Legato Layer Number (0-7) |
| 15 | Velocity Lo (0-127) |
| 16 | Velocity Hi (0-127) |
| 17 | Pitch Table Number (0-7) |
| 18 | Unused |
| 19-106 | Layer Map (88 keys, wavesample # in hi byte) |

### Wavesample Parameter Block (PUT WAVESAMPLE data)

139 words, transmitted in 16-bit format.

| Word | Description |
|------|-------------|
| 0-11 | Name (12 ASCII characters) |
| 12 | Copy Number (source WS# if this is a copy) |
| 13 | Copy Layer (source layer# if this is a copy) |
| 14-35 | Pitch Envelope (ENV1) - see Envelope Structure |
| 36-57 | Filter Envelope (ENV2) |
| 58-79 | Amplitude Envelope (ENV3) |
| 80 | Root Key (MIDI note) |
| 81 | Pitch Envelope Amount |
| 82 | LFO Amount |
| 83 | Random Modulation Amount |
| 84 | Pitch Wheel Bend Range |
| 85 | Pitch Modulation Source |
| 86 | Fine Tune (signed, hi byte = cents) |
| 87 | Pitch Modulation Amount |
| 88 | Filter Mode |
| 89 | FC#1 Cutoff |
| 90 | FC#2 Cutoff |
| 91 | FC#1 Keyboard Amount |
| 92 | FC#2 Keyboard Amount |
| 93 | FC#1 Envelope Amount |
| 94 | FC#2 Envelope Amount |
| 95 | FC#1 Modulation Source |
| 96 | FC#2 Modulation Source |
| 97 | FC#1 Modulation Amount |
| 98 | FC#2 Modulation Amount |
| 99 | Volume (0-127) |
| 100 | Amp Modulation Source |
| 101-104 | Amp Crossfade Points A/B/C/D |
| 105 | Pan Position (includes aux out assignment) |
| 106 | Amp Modulation Amount |
| 107 | LFO Waveform |
| 108 | LFO Speed |
| 109 | LFO Depth |
| 110 | LFO Delay Time |
| 111 | LFO Modulation Source |
| 112 | LFO Mode |
| 113 | Random Modulator Frequency |
| 114 | Loop Mode |
| 115-118 | Sample Start (32-bit, left justified, >>9 for word offset) |
| 119-122 | Sample End |
| 123-126 | Loop Start |
| 127-130 | Loop End (>>5 for 4-bit fraction) |
| 131 | Sample Rate (period = rate × 1.6µs) |
| 132 | Key Range Lo |
| 133 | Key Range Hi |
| 134 | Start/Loop Mod Source |
| 135 | Start/Loop Mod Amount |
| 136 | Start/Loop Mod Range |
| 137 | Modulation Type (0=None, 1=Loop, 2=Start, 3=Both) |
| 138 | Unused |

### Envelope Structure (22 words each)

| Offset | Description |
|--------|-------------|
| 0 | Envelope Type (preset) |
| 1 | Soft Level 0 (initial) |
| 2 | Hard Level 0 (initial) |
| 3 | Time 1 (attack) |
| 4 | Soft Level 1 (peak) |
| 5 | Hard Level 1 (peak) |
| 6 | Time 2 (decay 1) |
| 7 | Soft Level 2 |
| 8 | Hard Level 2 |
| 9 | Time 3 (decay 2) |
| 10 | Soft Level 3 |
| 11 | Hard Level 3 |
| 12 | Time 4 (decay 3) |
| 13 | Soft Level 4 (sustain) |
| 14 | Hard Level 4 (sustain) |
| 15 | Time 5 (release 1) |
| 16 | Velocity Switch (soft level on/off) |
| 17 | Level 5 (release breakpoint, signed) |
| 18 | Time 6 (release 2) |
| 19 | Time 1 Velocity Sensitivity |
| 20 | Keyboard Time Scaling |
| 21 | Mode (0=Normal, 1=Cycle, 2=Repeat) |

### Pitch Table Block (PUT PITCH TABLE data)

107 words, transmitted in 16-bit format.

| Word | Description |
|------|-------------|
| 0-11 | Name (12 ASCII characters) |
| 12-99 | Key Map (88 entries, bits 15:9=semitone, 8:3=fraction) |
| 100-106 | Unused |

---

## Status Codes

Response format: `F0 0F 03 0n 01 00 [status] F7`

| Code | Name | Description |
|------|------|-------------|
| 00 | ACK | Success |
| 01 | WAIT | Need more time (30s timeout) |
| 02 | INSERT SYSTEM DISK | Disk required |
| 03 | INVALID PARAMETER NUMBER | Unknown parameter |
| 04 | INVALID PARAMETER VALUE | Out of range |
| 05 | INVALID INSTRUMENT | Instrument doesn't exist |
| 06 | INVALID LAYER | Layer doesn't exist |
| 07 | LAYER IN USE | Destination layer exists |
| 08 | INVALID WAVESAMPLE | Wavesample doesn't exist |
| 09 | WAVESAMPLE IN USE | Destination WS exists |
| 0A | INVALID WAVEDATA RANGE | Range exceeds sample |
| 0B | FILE NOT FOUND | Disk file not found |
| 0C | MEMORY FULL | Out of RAM |
| 0D | INSTRUMENT IN USE | Can't use for command |
| 0E | NO MORE LAYERS | All 8 layers used |
| 0F | NO MORE WAVESAMPLES | All 127 used |
| 10 | (reserved) | Internal use |
| 11 | WAVESAMPLE IS A COPY | Must edit original |
| 12 | ZONE TOO BIG | Crossfade zone too large |
| 13 | SEQUENCER MUST BE STOPPED | Stop sequencer first |
| 14 | DISK ACCESS IN PROGRESS | Busy with disk |
| 15 | DISK FULL | No space on disk |
| 16 | LOOP IS TOO LONG | For synthesized loop |
| 17 | NAK | Data transfer error |
| 18 | NO LAYER EDIT | Can't edit all WS at once |
| 19 | NO MORE PITCH TABLES | All 8 used |
| 1A | CROSSFADE LENGTH IS ZERO | Invalid crossfade |
| 1B | CROSSFADE > 50% | Zone > half loop length |
| 1C | LOOP START TOO CLOSE | Not enough pre-loop data |
| 1D | LOOP END TOO CLOSE | Not enough post-loop data |
| 1E | QUIET LAYER | Layer not in patch |

---

## Parameter Numbers

Parameter number = (High byte × 256) + Low byte, transmitted as 12-bit word.

### System Parameters (High byte: 0x0D)

| Low | Name | Range |
|-----|------|-------|
| 0 | Free System Blocks | 0-10000 (read-only) |
| 1 | Free Disk Blocks | 0-10000 (read-only) |
| 2 | Master Tune | -127 to +127 |
| 3 | Global Bend Range | 0-12 |
| 4 | Touch Sensitivity | 0-15 |
| 5 | Mod Pedal Mode | 0=Volume, 1=Modulator |
| 6 | Sustain Pedal Mode | 0=Sustain, 1=Left Patch |
| 7 | Aux Pedal Mode | 0=Start/Stop, 1=Right Patch |
| 8 | Autoloop Switch | 0=Off, 1=On |

### MIDI Parameters (High byte: 0x0C)

| Low | Name | Range |
|-----|------|-------|
| 0 | Base Channel | 0-15 |
| 1 | Transmit Mode | 0=Base, 1=Instrument |
| 2 | Base Channel Pressure | 0=Off, 1=Key, 2=Channel |
| 3 | MIDI In Mode | 0=Omni, 1=Poly, 2=Multi, 3=Mono A, 4=Mono B |
| 4 | Controllers Enable | 0=Off, 1=On |
| 5 | SysEx Enable | 0=Off, 1=On |
| 6 | Program Change Enable | 0=Off, 1=On |
| 7 | Song Position Enable | 0=Off, 1=On |
| 8 | XCTRL Value | 0-127 |

### Instrument Parameters (High byte: 0x0A)

| Low | Name | Range |
|-----|------|-------|
| 0 | Patch | 0-255 (layer bitmap) |
| 1 | Key Down Layers | 0-255 |
| 2 | Key Up Layers | 0-255 |
| 3 | MIDI Channel | 0-15 |
| 4 | MIDI Program | 1-127 |
| 5 | Pressure Mode | 0-2 |
| 6 | Send Keys To | 0=Both, 1=Local, 2=MIDI |
| 7 | Instrument Size | 0-10000 (read-only) |
| 10 | Range Low Key | 0-127 |
| 11 | Range High Key | 0-127 |
| 12 | Transpose Octave | 0-5 |
| 13 | Transpose Semitone | 0-12 |

### Layer Parameters (High byte: 0x09)

| Low | Name | Range |
|-----|------|-------|
| 0 | Glide Mode | 0=Off, 1=Mono, 2=Pedal |
| 1 | Glide Time | 0-127 |
| 2 | Legato Layer | 0-7 |
| 3 | Velocity Low | 0-127 |
| 4 | Pitch Table | 0-7 |
| 10 | Velocity High | 0-127 |

### Wavesample Parameters (High byte: 0x08)

| Low | Name | Range |
|-----|------|-------|
| 0 | Loop Mode | 0-4 (see Loop Modes) |
| 6 | Loop Mod Type | 0=Off, 1=Loop, 2=Start, 3=Both |
| 7 | Loop Mod Source | 0-17 (see Mod Sources) |
| 8 | Loop Mod Amount 1 | 0-127 |
| 9 | Loop Mod Amount 2 | 0-21 (see Mod Ranges) |
| 10 | Loop End Fractional | 0-127 |
| 11 | Key Range Low | 0-127 |
| 12 | Key Range High | 0-127 |
| 13 | Sample Rate | 0-127 |
| 21 | Wavedata Start | 0-0xFFFFFF |
| 22 | Wavedata End | 0-0xFFFFFF |
| 23 | Loop Start | 0-0xFFFFFF |
| 24 | Loop End | 0-0xFFFFFF |
| 25 | Loop Position | 0-0xFFFFFF |

### Envelope Parameters (High byte: 0x01=ENV1, 0x02=ENV2, 0x03=ENV3)

| Low | Name | Range |
|-----|------|-------|
| 0 | Envelope Type | 0-15 (preset) |
| 1 | Level 0 Hard | 0-127 |
| 2 | Level 0 Soft | 0-127 |
| 3 | Time 1 | 0-99 |
| 4 | 2nd Release Time | 0-127 |
| 5 | Attack Time Velocity | 0-99 |
| 6 | Keyboard Scaling | 0-127 |
| 7 | Soft Velocity On/Off | 0-1 |
| 8 | Envelope Mode | 0=Normal, 1=Cycle, 2=Repeat |
| 11 | Level 1 Hard | 0-127 |
| 12 | Level 2 Hard | 0-127 |
| 13 | Level 3 Hard | 0-127 |
| 14 | Level 4 Hard | 0-127 |
| 15 | Level 1 Soft | 0-127 |
| 16 | Level 2 Soft | 0-127 |
| 17 | Level 3 Soft | 0-127 |
| 18 | Level 4 Soft | 0-127 |
| 19 | Time 2 | 0-99 |
| 20 | Time 3 | 0-99 |
| 21 | Time 4 | 0-99 |
| 22 | Time 5 | 0-99 |
| 23 | 2nd Release Level | -127 to +127 |

### Pitch Parameters (High byte: 0x04)

| Low | Name | Range |
|-----|------|-------|
| 1 | Root Key | 0-127 |
| 2 | LFO Amount | 0-127 |
| 3 | Envelope 1 Amount | -127 to +127 |
| 5 | Random Frequency | 0-127 |
| 6 | Bend Range | 0-13 (13=global) |
| 7 | Modulation Source | 0-18 |
| 10 | Fine Tune | -127 to +127 |
| 11 | Modulation Amount | -127 to +127 |
| 12 | Random Amount | -127 to +127 |

### Filter Parameters (High byte: 0x05)

| Low | Name | Range |
|-----|------|-------|
| 0 | Filter Mode | 0-3 (see Filter Modes) |
| 1 | Filter 1 Cutoff | 0-127 |
| 2 | Filter 1 Env Amount | -127 to +127 |
| 3 | Filter 1 Keyboard | -127 to +127 |
| 7 | Filter 1 Mod Source | 0-18 |
| 8 | Filter 2 Mod Source | 0-18 |
| 11 | Filter 2 Cutoff | 0-127 |
| 12 | Filter 2 Env Amount | -127 to +127 |
| 13 | Filter 2 Keyboard | -127 to +127 |
| 14 | Filter 1 Mod Amount | -127 to +127 |
| 15 | Filter 2 Mod Amount | -127 to +127 |

### Volume Parameters (High byte: 0x06)

| Low | Name | Range |
|-----|------|-------|
| 1 | Wavesample Volume | 0-127 |
| 2 | Pan | 0-17 (see Pan/Output) |
| 3 | Modulation A | 0-127 |
| 4 | Modulation C | 0-127 |
| 7 | Modulation Source | 0-18 |
| 10 | Modulation Amount | 0-127 |
| 11 | Modulation B | 0-127 |
| 12 | Modulation D | 0-127 |

### LFO Parameters (High byte: 0x07)

| Low | Name | Range |
|-----|------|-------|
| 1 | LFO Wave | 0-6 (see LFO Waves) |
| 2 | LFO Speed | 0-99 |
| 3 | LFO Depth | 0-127 |
| 4 | LFO Delay | 0-99 |
| 5 | LFO Mode | 0=Reset Off, 1=Reset On, 2=Humanize |
| 7 | LFO Mod Source | 0-18 |

### Edit Parameters (High byte: 0x0E)

| Low | Name | Range |
|-----|------|-------|
| 0 | Edit Instrument | 0-7 |
| 1 | Edit Layer | 0-7 |
| 2 | Edit Wavesample | 0-128 (0=ALL) |

---

## Value Tables

### Modulation Sources (0-17)

| Value | Source |
|-------|--------|
| 0 | LFO |
| 1 | Random |
| 2 | ENV1 |
| 3 | ENV2 |
| 4 | Pressure+Velocity |
| 5 | Velocity |
| 6 | Velocity 1 |
| 7 | Velocity 2 |
| 8 | Keyboard |
| 9 | Pitch |
| 10 | Wheel |
| 11 | Pedal |
| 12 | XCTRL |
| 13 | Pressure |
| 14 | Wheel+Pressure |
| 15 | Off |

### Filter Modes (0-3)

| Value | Mode |
|-------|------|
| 0 | F1=2-pole LP, F2=2-pole HP |
| 1 | F1=3-pole LP, F2=1-pole HP |
| 2 | F1=2-pole LP, F2=2-pole LP |
| 3 | F1=3-pole LP, F2=1-pole LP |

### Loop Modes (0-4)

| Value | Mode |
|-------|------|
| 0 | Forward, No Loop |
| 1 | Backward, No Loop |
| 2 | Loop Forward |
| 3 | Loop Bidirectional |
| 4 | Loop and Release |

### LFO Waveforms (0-6)

| Value | Wave |
|-------|------|
| 0 | Triangle |
| 1 | Sin/Triangle |
| 2 | Sine |
| 3 | Positive Triangle |
| 4 | Positive Sine |
| 5 | Sawtooth |
| 6 | Square |

### Pan/Output Positions (0-17)

| Value | Output |
|-------|--------|
| 0 | Wavesample (use WS setting) |
| 1-8 | Stereo position (*-------) through (-------*) |
| 9-16 | Solo Out 1-8 |
| 17 | Random Pan |
| 18 | Keyboard |

### Envelope Presets (0-15)

| Value | Preset |
|-------|--------|
| 0 | Current Value |
| 1 | Full On |
| 2 | All Zeros |
| 3 | Full Vel Range |
| 4 | Slow String |
| 5 | Piano Decay |
| 6 | Percussion |
| 7 | Ramp Up |
| 8 | Ramp Down |
| 9 | Short Blip |
| 10 | Brass Filter |
| 11 | Repeat Triangle |
| 12 | Repeat Ramp |
| 13 | Wind Driven |
| 14 | Reverb |
| 15 | Saved |

### Button Numbers (for VIRTUAL BUTTON PRESS)

| # | Button | # | Button |
|---|--------|---|--------|
| 0-7 | Instrument 1-8 | 32 | Up Arrow |
| 16 | Sample | 33 | Down Arrow |
| 17 | Command Mode | 34 | Left Arrow |
| 18 | Key Range | 35 | Cancel/No |
| 19 | Edit Mode | 36 | Right Arrow |
| 20 | Load Mode | 37 | Enter/Yes |
| 21 | MIDI | 48-57 | 0-9 (Track, Env1-3, Pitch, Filter, Amp, LFO, Wave, Layer) |
| 22 | Seq/Song | | |
| 23 | Instrument | | |
| 24 | System | | |

---

## Implementation Notes

### Wavedata Transfer Example

To receive wavedata from EPS:

1. **EXT→EPS**: GET WAVESAMPLE DATA (specify inst/layer/ws/range)
2. **EPS→EXT**: RESPONSE with ACK (or error)
3. **EPS→EXT**: PUT WAVESAMPLE DATA (first part - params)
4. **EXT→EPS**: RESPONSE with ACK
5. **EPS→EXT**: PUT WAVESAMPLE DATA (second part - 16-bit samples)
6. **EXT→EPS**: RESPONSE with ACK

### Sample Data Format

- 16-bit signed samples
- Transmitted in 16-bit word format (3 MIDI bytes per sample)
- Sample rate stored as period: `rate_hz = 1000000 / (period × 1.6)`
- Standard rates: period 20 = 31.25kHz, period 40 = 15.625kHz

### Loop Point Encoding

Loop points are stored as 32-bit left-justified values:

```c
// Reading loop points from parameter block:
sample_offset = (word[0] << 24 | word[1] << 16 | word[2] << 8 | word[3]) >> 9;

// Loop end has 4-bit fractional part:
loop_end_offset = (word[0] << 24 | word[1] << 16 | word[2] << 8 | word[3]) >> 5;
loop_end_fraction = loop_end_offset & 0x0F;
loop_end_samples = loop_end_offset >> 4;
```

### Crossfade Zone Constraints

For crossfade loop commands, the fade zone must satisfy:
- `zone <= (loop_end - loop_start)` (not larger than loop)
- `zone <= (loop_start - sample_start)` (room before loop)
- `zone <= (sample_end - loop_end)` (room after loop)

### Memory Management

- 8 instrument slots (0-7)
- 8 layers per instrument (0-7)
- 127 wavesamples per instrument (1-127)
- 8 pitch tables per instrument (0-7)
- RAM measured in blocks (1 block = 512 bytes = 256 words)

### Timing

- Normal command timeout: 2 seconds
- WAIT mode timeout: 30 seconds
- Minimum delay between commands (open loop): 2 seconds
