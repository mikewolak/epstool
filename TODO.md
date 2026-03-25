# ES5510 DSP Emulator - Current Status

## Project Overview

Standalone ES5510 DSP emulator for running Ensoniq EPS-16+ Waveboy DSP effects outside of MAME. The goal is to process audio through effects like "4 REVERBS" using extracted microcode.

## Current Status

### Working
- Microcode loading from extracted binary files
- Basic instruction execution (ALU, MAC operations)
- Memory addressing modes (DELAY, TABLE_A, TABLE_B)
- GPR (General Purpose Register) access
- Serial I/O for audio input/output
- DOL/DIL FIFO operations for delay line access

### Fixed Issues (Committed)

1. **op_select_t struct field order mismatch** (commit 80d0386)
   - Struct had `{alu_src, alu_dst, mac_src, mac_dst}`
   - MAME initializers use `{mac_src, mac_dst, alu_src, alu_dst}`
   - This caused `alu_dst` to be misread, preventing DOL writes

2. **DOL eject during I/O operations** (commit 80d0386)
   - DOL FIFO was ejected for I/O cycles (ram_p.io=1)
   - This caused data loss before actual DRAM writes
   - Fix: Only eject DOL for non-I/O operations

### Remaining Issue: Reverb Feedback Loop

The "4 REVERBS" effect produces output but with incorrect behavior:
- **CMR=0x00**: All instructions execute → runaway feedback → clipping at 32767
- **CMR=0x0c** (from effect file): 110 instructions skip → minimal output, no reverb tail
- **CMR=0x20+**: Only 50 instructions execute → output stage only, no reverb

## Technical Findings

### Skip/Conditional Execution Logic

The ES5510 uses CMR (Condition Mask Register) and CCR (Condition Code Register) for conditional instruction execution:

```c
if (instruction.skip_flag == 1) {
    int condition = ((ccr & cmr & 0x1f) != 0);
    if (cmr & 0x20)  // bit 5 inverts condition
        condition = !condition;
    if (condition)
        skip_this_instruction();
}
```

### 4 REVERBS Effect Structure

The effect has 160 instructions organized as:
- **PC 0-21**: Reverb A processing (ramCtl=1,2,7 - Write A, Read A, Write B)
- **PC 22-43**: Reverb B processing (ramCtl=3,4)
- **PC 44-65**: Reverb C processing (ramCtl=5,6)
- **PC 66-89**: Reverb D processing (ramCtl=7)
- **PC 90-130**: Output mixing stage
- **PC 131-159**: Serial I/O

Almost ALL instructions in PC 0-89 have `skip=1`, making them conditional.

### CCR Dynamic Updates

The code modifies CCR during execution:
- CCR toggles between 0x00, 0x08, and 0x90
- This allows different reverb paths to execute on different samples
- With incorrect CMR, the wrong instructions execute/skip

### Root Cause Hypothesis

The "4 REVERBS" effect appears designed to run **one reverb mode at a time**, selected by CMR/CCR combination. The EPS-16+ hardware likely:
1. Sets CMR based on user-selected reverb mode (A/B/C/D)
2. The code uses CCR updates to sequence operations within that mode

When CMR=0x00 (all execute), all 4 reverb paths run simultaneously, causing excessive feedback and clipping.

## How to Run Tests

### Build
```bash
cd /home/mwolak/EPS16
make
```

### Test Files

The microcode binary is at `/tmp/4reverbs_ucode.bin`. Extract it from the EFE file:
```bash
# Extract first ~640 bytes (160 instructions × 4 bytes)
dd if="factory_sounds/WAVEBOY_EFE/4 REVERBS.efe" of=/tmp/4reverbs_ucode.bin bs=1 skip=2580 count=640
```

### Basic Reverb Test
```bash
cat > /tmp/test_reverb.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "es5510_standalone.h"

int main(void) {
    es5510_t dsp;
    es5510_init(&dsp);

    FILE *f = fopen("/tmp/4reverbs_ucode.bin", "rb");
    fseek(f, 0, SEEK_END); int len = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *ucode = malloc(len); fread(ucode, 1, len, f); fclose(f);
    es5510_load_microcode(&dsp, ucode, len); free(ucode);

    // Memory config from effect file
    dsp.memshift = 8; dsp.memsiz = 0xff;
    dsp.memmask = 0xffff00; dsp.memincrement = 0x100;
    dsp.dlength = 0x100000;

    // Delay tap addresses (GPR 0-79)
    static const uint16_t taps[] = {
        0x0e90, 0x00c0, 0x00d4, 0x00e6, 0x00fa,
        0x014e, 0x015c, 0x010c, 0x012c, 0x0120,
        0x0434, 0x0448, 0x045a, 0x046e, 0x04c2,
        0x04d0, 0x0480, 0x04a0, 0x0494, 0x07a8,
    };
    for (int i = 0; i < 80; i++) {
        es5510_set_gpr(&dsp, i, taps[i % 20] * dsp.memincrement);
    }

    // Coefficients
    es5510_set_gpr(&dsp, 69, 0x7FFFFF);   // Unity gain
    es5510_set_gpr(&dsp, 113, 0x400000);  // Feedback ~0.5
    es5510_set_gpr(&dsp, 114, 0x400000);
    es5510_set_gpr(&dsp, 117, 0x600000);  // Feedback ~0.75
    es5510_set_gpr(&dsp, 121, 0x600000);

    dsp.cmr = 0x00;  // Try different values: 0x00, 0x20, 0x0c, etc.
    dsp.ccr = 0x00;

    for (int s = 0; s < 5000; s++) {
        dsp.abase = dsp.dbase;
        dsp.bbase = dsp.dbase;

        int16_t in = (s == 0) ? 10000 : 0;
        es5510_set_serial_input(&dsp, in, in);
        es5510_run_sample(&dsp);

        if (dsp.ser0l != 0 || dsp.ser0r != 0) {
            printf("Sample %4d: L=%6d R=%6d\n", s, dsp.ser0l, dsp.ser0r);
        }
    }

    es5510_free(&dsp);
    return 0;
}
EOF
gcc -I/home/mwolak/EPS16 -o /tmp/test_reverb /tmp/test_reverb.c /home/mwolak/EPS16/es5510_standalone.c -lm
/tmp/test_reverb
```

### Debug Flags

Enable in code or set externally:
```c
extern int debug_r144;  // R144 tracing
extern int debug_skip;  // Skip decision tracing
extern int debug_alu;   // ALU->DOL writes
extern int debug_ram;   // RAM write operations
extern int debug_ccr;   // CCR changes
```

## Suggested Further Tests

### 1. Analyze EPS-16+ Firmware for CMR Initialization
The firmware at `eps16_firmware/EPS16-OS-V110.bin` should show how CMR is set when loading effects:
```bash
# Search for ES5510 register writes
xxd eps16_firmware/EPS16-OS-V110.bin | grep -i "pattern_for_dsp_writes"
```

### 2. Test Single Reverb Mode Selection
Try running only one reverb mode by manipulating skip flags:
```c
// Force-skip all instructions except mode D (PC 66-89)
for (int pc = 0; pc < 66; pc++) dsp.instr[pc] |= (1 << 7);   // Set skip
for (int pc = 90; pc < 160; pc++) dsp.instr[pc] |= (1 << 7); // Set skip
```

### 3. Trace MAME ES5510 Behavior
Compare instruction execution sequence between our emulator and MAME's ensoniq.cpp:
- Load same microcode in MAME
- Add tracing to MAME's es5510.cpp
- Compare PC-by-PC execution

### 4. Reduce Feedback Coefficients
Test with lower feedback to prevent clipping:
```c
es5510_set_gpr(&dsp, 113, 0x200000);  // 0.25 instead of 0.5
es5510_set_gpr(&dsp, 117, 0x300000);  // 0.375 instead of 0.75
```

### 5. Verify GPR Initialization Order
The effect file may have GPR init data in a specific order. Cross-reference with Giebler EFE format documentation.

### 6. Check for Missing Host Communication
Real hardware may update CMR/CCR through host interface during effect execution. Check if effect expects runtime CMR changes.

## File Locations

- Emulator source: `es5510_standalone.c`, `es5510_standalone.h`
- Effect files: `factory_sounds/WAVEBOY_EFE/`
- Firmware: `eps16_firmware/EPS16-OS-V110.bin`
- Test files: `/tmp/test_*.c`
- Microcode binary: `/tmp/4reverbs_ucode.bin`

## References

- MAME ES5510 source: `src/devices/sound/es5510.cpp`
- ES5510 datasheet (limited public info available)
- Giebler EFE format: `references/giebler_efe_format.txt`
