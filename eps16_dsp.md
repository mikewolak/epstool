# EPS-16 PLUS DSP Effects Architecture

## Overview

The EPS-16 PLUS contains a dedicated Digital Signal Processor (ESP - Ensoniq Signal Processor)
for real-time audio effects processing. The ESP is separate from the main 68000 CPU and runs
its own microcode.

Third-party developers like Waveboy Industries exploited this architecture to create custom
effects that could be loaded from floppy disk.

## Effect Structure (from External Command Specification v1.30)

Effects are stored as part of instrument data and have the following structure:

```
Offset  Size    Field                   Description
------  ----    -----                   -----------
0       4       effect_block_size       Size is variable
4       2       effect_ptr_offset       Set to inst_effect_ptr
6       2       effect_ptr_more         Used when effect is its own block
8       2       effect_version_num      Set to 2 after 9/7/90
10      12      effect_name             ASCII name
22      4       effect_size             Total size in bytes, incl. ucode

26      2       effect_ucode            Microcode (downloadable)
28      2       effect_table            ESP-RAM table, or zero
30      2       effect_init_code        Called before ESP runs code
32      2       effect_update_task      Gets installed as a task
34      2       effect_var_1            Blocks of FX control parameters
36      2       effect_var_2
38      2       effect_var_3
40      2       effect_var_4
42      2       effect_param_list_start Page of parameter descriptions
44      2       effect_param_list_last
46      2       effect_param_list_current
48      13      effect_fx1_name         First effect algorithm name
61      13      effect_fx2_name         Second effect algorithm name
74      13      effect_fx3_name         Third effect algorithm name
87      1       effect_current_var      (even)
88      1       effect_output_gpr_L     Left output GPR
89      1       effect_output_gpr_R     Right output GPR
90      1       effect_num_voices       Polyphony: 0=20, 1=13, 2=7
91      2       effect_keydown_routine
```

## Key Fields

### effect_ucode
The actual ESP microcode blob. This is the downloadable code that runs on the DSP.
Third-party effects like Waveboy's custom algorithms are stored here.

### effect_table
ESP-RAM table for delay lines and buffers. Effects that use delay (reverb, delay, chorus)
allocate memory here.

### effect_num_voices
Complex effects steal polyphony from the synth engine:
- 0 = 20 voices (simple effects)
- 1 = 13 voices (moderate complexity)
- 2 = 7 voices (complex effects like convolution reverb)

### effect_init_code / effect_update_task
68000 code hooks that interface with the ESP:
- init_code: Called before ESP starts running
- update_task: Installed as a background task for parameter updates

## Built-in Effects (ROM)

The EPS-16+ includes 13 ROM effect algorithms:

| ROM | Name                  | Variations |
|-----|-----------------------|------------|
| 01  | HALL REVERB           | 1-4        |
| 02  | 44kHz REVERB          | 1-4        |
| 03  | ROOM REVERB           | 1-4        |
| 04  | DUAL DELAYS           | 1-4        |
| 05  | 44kHz DELAYS          | 1-4        |
| 06  | CHORUS+REVERB         | 1-4        |
| 07  | PHASER+REVERB         | 1-4        |
| 08  | FLANGER+REVERB        | 1-4        |
| 09  | ROTARY SPEAKER+REVERB | 1-4        |
| 10  | CHOR+REV+DDL          | 1-4        |
| 11  | CMP+DIST+REV          | 1-4        |
| 12  | DIST+CHO+REV          | 1-4        |
| 13  | WAH+DIST+REV          | 1-4        |

## Waveboy Effects

Waveboy Industries created numerous third-party effects for the EPS-16+:

Known Waveboy effects (from disk images):
- 44K-COMPRS+X - 44kHz Compressor with additional processing
- 4 REVERBS - Four reverb algorithms
- DUAL DELAY+X - Enhanced dual delay
- RESON FILTER 2/3 - Resonant filter effects
- ROOM RVERB+X - Room reverb with extras
- Parallax - (famous spatial effect)
- Hyperprism - Multi-effect processor

## Extracted Waveboy Effect Files

Successfully extracted from HFE disk images (in factory_sounds/WAVEBOY_EFE/):

| File | Size | Description |
|------|------|-------------|
| 44K-COMPRS+.efe | 6656 | 44kHz Compressor |
| 4 REVERBS.efe | 6656 | Four reverb algorithms (A, B, C, D) |
| DUAL DELAY+.efe | 2048 | Enhanced dual delay with X-Input |
| RESON FILTER 2.efe | 6144 | Resonant filter with chorus/reverb |
| RESON FILTER 3.efe | 6144 | Resonant filter variant |
| ROOM RVERB+.efe | 2560 | Room reverb with X-Input |

### Effect File Structure (after 512-byte Giebler header)

```
Offset  Size  Field                 Example (4 REVERBS)
------  ----  -----                 -------------------
0x00    4     effect_block_flags    80 00 00 10
0x04    2     effect_ptr_offset     c8 c0
0x08    2     effect_version        00 02
0x0A    12    effect_name           "4   R E V E R B S" (spaced)
0x16    4     effect_size           17 dd (6109 bytes)
0x1A    2     effect_ucode_offset   14 42
0x1C    2     effect_table_offset   00 00
...
0x68    2     effect_flags          73 74
...
```

### Reverb Algorithm Names (4 REVERBS)

The 4 REVERBS effect contains four selectable algorithms:
- A"REV - Algorithm A reverb
- B"REV - Algorithm B reverb
- C"REV - Algorithm C reverb
- D"REV - Algorithm D reverb

Parameters include: DRY VOL, REV VOL, PAN, DECAY TIME, PREDELAY,
HF DAMPING, DIFFUSION, ROOM SIZE, NODE DELAYS, TAP POSITIONS

## ESP Architecture Notes

The ESP (Ensoniq Signal Processor) appears to be a custom DSP with:
- Dedicated microcode memory
- General Purpose Registers (GPRs) for audio I/O
- RAM for delay lines and buffers
- Interface to main 68000 for parameter control

### ESP2 Microcode Patterns (observed)

From analysis of Waveboy effect files, the DSP microcode uses:
- Instruction opcodes: `ff90`, `f393`, `f394`, `f392`
- GPR references: `40XX`, `48XX`, `75f4`
- Common patterns suggesting multiply-accumulate operations
- 16-bit instruction words

The microcode format and instruction set are not publicly documented, but could potentially
be reverse-engineered from effect dumps.

## SysEx Transfer

Effects are transferred as part of instrument data via:
1. GET INSTRUMENT (0x03) - Downloads instrument including effect
2. PUT INSTRUMENT (0x0C) - Uploads instrument including effect

The `inst_effect_ptr` (word 317 in instrument parameter block) points to the effect data
within the instrument structure.

## Extracting Effect Microcode

To dump an effect's microcode:
1. Load instrument with desired effect on EPS-16+
2. Send GET INSTRUMENT SysEx command
3. Parse instrument data to find effect block
4. Extract effect_ucode field

## References

- Ensoniq EPS-16 PLUS External Command Specification v1.30.00
- EPS-16 PLUS Musician's Manual (effect parameter descriptions)
- Waveboy Industries documentation (if available)
