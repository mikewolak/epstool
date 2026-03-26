# ES5510 DSP Emulator - Current Status

---

## LATEST UPDATE: 2025-03-25 - MEMORY MODEL DISCOVERY

### Summary
The MACHL=0 issue is PARTIALLY FIXED. MUL operations now correctly load MACHL (we saw MACHL=0x47ff40000000 at sample 4502). However, the 4 REVERBS algorithm still produces constant output (32767) because of a **complex memory addressing model** that I don't fully understand.

### Key Findings About ES5510 Memory Model

**RDL (Read Delay Line):**
- Address = `(dbase + GPR[PC]) % (dlength + memincrement)`
- `dbase` decrements each sample (circular buffer pointer)
- Used for reading from the delay line relative to current position

**RTA (Read Table A):**
- Address = `(abase + GPR[PC]) & memmask`
- `abase` is FIXED (typically 0)
- NO circular wrapping - absolute address lookup
- Used for coefficient/state tables, NOT delay taps!

**WDL (Write Delay Line):**
- Address = `(dbase + GPR[PC]) % (dlength + memincrement)`
- Writes to circular buffer at dbase-relative position

### What I Tried That Didn't Work

1. **Setting abase = dbase at sample start** - This made RTA behave like RDL, but the algorithm expects RTA for fixed table lookups, not delay reads.

2. **Assuming delay taps are read via RTA** - Wrong! Looking at the active RDL operations (PC 57, 59, 60, 68), they all use GPR offset = 0, which reads the CURRENT position, not delayed data.

### The Mystery

The 4 REVERBS disassembly shows:
- PC 2-5: WDL (Write Delay Line) - writes input to delay buffer
- PC 6-27: RTA (Read Table A) - uses delay tap GPRs (0x141900 = 5145 samples)
- PC 57-68: RDL (Read Delay Line) - uses GPR offset = 0

If RTA is reading from `abase=0 + 0x141900 = address 0x1419`, but WDL wrote to `dbase + small_offset`, these addresses never overlap!

**Possible explanations:**
1. The delay tap GPRs are actually STATE memory addresses, not delay times
2. There's a different mechanism I'm missing for delay reads
3. The algorithm structure is more complex than expected

### Output Analysis

At sample 5145 with impulse at sample 0:
- `R187 = 0x7FFFFF` (output gain L)
- `SER3L = R187 + 0 = 32767` (constant!)
- RTA at PC 7 reads from `abase + 0x141900 = address 0x4001` - but impulse was written to `address 0`

The addresses don't match, so RTA reads 0, and output is just the gain coefficient.

### What Was Fixed in This Session

1. **RIO handling bug** - MAME sets DIL=0 for RIO operations; my emulator left DIL unchanged
   - Fixed by adding `dsp->dil = 0;` branch for I/O reads in es5510_standalone.c

2. **Confirmed OPERAND_SELECT order** - Verified struct field order matches MAME initializers

3. **MUL operations now work** - MACHL shows correct product values:
   ```
   [PC=9 WRITE] cVal=0x5fff00 dVal=0x600000 prod=000047ff40000000 result=000047ff40000000 -> MACHL
   ```

4. **Added debug flags** - `debug_rta` for tracing RTA address calculation

### Current Test Output

```
Sample 5145 with RTA tracing:
  [PC=6] RTA: abase=0x2be800 offset=0x0 addr=0x2be8 DRAM=24575
  [PC=7] RTA: abase=0x2be800 offset=0x141900 addr=0x4001 DRAM=0

After: ser3l=32767 (constant = R187 gain)
```

RTA at PC 7 reads from wrong address (0x4001 instead of where impulse was written).

### Next Steps to Try

1. **Run 4 REVERBS in full MAME EPS-16+** - See actual memory contents during execution
2. **Trace what firmware initializes** - The real EPS-16+ firmware might set up abase/bbase
3. **Investigate RDL operations** - The RDL at PC 57-68 uses GPR=0; maybe those are the real delay reads
4. **Try simpler effect** - RESON FILTER has working 1:1 C ports in waveboy_reson_filter_1to1.c

---

## LETTER TO FUTURE SELF / NEW DEVELOPER

**Date:** 2024-03-25
**Project:** Standalone ES5510 DSP emulator for Ensoniq EPS-16+ Waveboy effects

### What This Project Is

You're building a standalone emulator for the Ensoniq ES5510 DSP chip, extracted from MAME. The goal is to run Waveboy effects (like "4 REVERBS") on audio files without needing the full MAME emulator. The ES5510 is a 24-bit fixed-point DSP with 160 instructions, 192 GPRs, delay line memory, and MAC/MUL operations.

### The Current Problem (READ THIS FIRST)

**The reverb doesn't work because the MAC accumulator (MACHL) is always zero.**

Here's the signal flow that SHOULD happen:
```
Input audio → DRAM (delay buffer) → DIL (delay input latch) → MACHL → MAC operation → SER0L/SER3L → Output
```

Here's what's ACTUALLY happening:
```
Input audio → DRAM (delay buffer) → DIL (delay input latch) → MACHL=0 (STUCK!) → MAC gives 0 → No output
```

The delay line IS working. At sample 4502 (102ms delay), DIL correctly reads 24575 (the stored impulse). But MACHL stays at 0x0000000000000000, so the MAC operation `SER0L = MACHL + SER1R * SER0L` just does `SER0L = 0 + gain * 0 = 0`.

### What You Need To Do Next

**Step 1: Understand the MAC/MUL difference**
- MUL: `dest = op_c * op_d` — CLEARS the accumulator (MACHL = result)
- MAC: `dest = MACHL + (op_c * op_d)` — ADDS to accumulator

**Step 2: Find which instruction should load DIL into MACHL**
Look at the disassembly (`/home/mwolak/EPS16/factory_sounds/WAVEBOY_EFE/4_REVERBS_disasm.txt`) for instructions like:
```
MUL Rxxx,DIL >somewhere    <- This would put DIL*Rxxx into MACHL
MAC Rxxx,DIL >somewhere    <- This would ADD DIL*Rxxx to MACHL
```

**Step 3: Check if those instructions are being SKIPPED**
Most instructions have an `S` flag (skippable). The skip logic uses CCR/CMR:
```c
if (skip_flag && ((ccr & cmr & 0x1f) != 0)) skip_instruction();
```
Add debug output to see which instructions actually execute.

**Step 4: Verify the emulator's MAC implementation**
Open `es5510_standalone.c` and find where MAC operations update `machl`. Verify:
- MUL stores result directly to machl (replaces)
- MAC adds to existing machl value

### Quick Start Commands

```bash
cd /home/mwolak/EPS16

# Compile and run the MAC accumulator test (shows MACHL is always 0):
gcc -o /tmp/test_mac_accum /tmp/test_mac_accum.c es5510_standalone.c -I. -I/tmp -lm
/tmp/test_mac_accum

# Disassemble the microcode:
./es5510-disas /tmp/4reverbs_ucode.bin

# Look for MAC/MUL operations that use DIL:
grep "DIL" factory_sounds/WAVEBOY_EFE/4_REVERBS_disasm.txt
```

### Key Files You'll Need

| File | Purpose |
|------|---------|
| `es5510_standalone.c` | Main emulator - look for `machl` usage |
| `es5510_standalone.h` | Struct definition - `int64_t machl` |
| `/tmp/4reverbs_ucode.bin` | Extracted microcode binary |
| `/tmp/4reverbs_complete_init.h` | GPR initialization values |
| `/tmp/test_mac_accum.c` | Test that proves MACHL is always 0 |
| `factory_sounds/WAVEBOY_EFE/4_REVERBS_disasm.txt` | Full disassembly |

### Reference Implementation

MAME's ES5510 implementation is at `/home/mwolak/mame/src/devices/cpu/es5510/es5510.cpp`. Search for how it handles:
- `machl` updates in MAC vs MUL operations
- The skip/conditional execution logic
- How DIL feeds into MAC operations

The SD-1 VST project at `/tmp/ensoniq_sd1_vst/` wraps MAME and could be useful for comparison, but it's not standalone ES5510 code.

### What's Already Working (Don't Break These)

1. Microcode loads correctly (48-bit instructions parsed)
2. DRAM stores input (13496 non-zero entries after impulse)
3. DIL reads from delay line correctly (0x5fff00 at sample 4502)
4. R145 accumulates (changes each sample - reverb IS being computed internally)
5. Memory config is correct (dlength=0x400000, memshift=8)
6. **RIO handling fixed** - DIL set to 0 for I/O reads (was leaving DIL unchanged)
7. **MUL operations work** - MACHL now shows non-zero products (0x47ff40000000)
8. **dbase decrements correctly** - Circular buffer pointer moves each sample

### The Smoking Gun

Run `/tmp/test_mac_accum` and you'll see:
```
Sample 4502: MACHL=0x0000000000000000 DIL=0x5fff00 ser0l=0
```

DIL has the reverb signal. MACHL is zero. That's the bug. Fix MACHL and the reverb will work.

---

## Project Goal

Standalone ES5510 DSP emulator for running Ensoniq EPS-16+ Waveboy DSP effects (especially "4 REVERBS") outside of MAME. Process audio through extracted microcode and produce correct reverb output.

---

## CURRENT BLOCKER (2025-03-25)

**RTA memory addressing doesn't match WDL write addresses!**

The MACHL=0 issue is now FIXED (MUL operations produce non-zero results). The NEW blocker is:

- WDL writes to `(dbase + offset) % dlength` - circular buffer addresses
- RTA reads from `(abase + offset)` - fixed table addresses (abase=0)
- These address spaces don't overlap!

**Evidence:**
```
Sample 0: WDL writes impulse to dbase=0 (address 0)
Sample 5145: RTA reads from abase+0x141900 = address 0x4001
Result: RTA reads 0 (nothing was written there), output = R187 = 32767 constant
```

**Previous issue (FIXED):**
The MACHL=0 bug was caused by RIO not updating DIL. Now MUL correctly computes:
```
[PC=9 WRITE] cVal=0x5fff00 dVal=0x600000 prod=000047ff40000000 -> MACHL
```

---

## Where We Are At

### WORKING:
1. **Microcode loading** - Correctly parses 48-bit instructions from extracted binary
2. **Memory configuration** - dlength=0x400000 (16384 samples), memshift=8
3. **Delay line reads** - DIL correctly reads stored samples at delay tap times
4. **DRAM writes** - Input signal correctly stored in delay buffer (13496 non-zero entries)
5. **GPR initialization** - Mode A coefficients loaded from `/tmp/4reverbs_complete_init.h`
6. **R145 accumulation** - Reverb signal accumulates correctly in R145 (changes each sample)
7. **Serial port setup** - esqpump-style mixing (ser3l = ser0l + ser1l + ser2l)

### NOT WORKING:
1. **MAC accumulator always 0** - Root cause of no output
2. **SER0L/SER2L decay to 0** - Because MACHL doesn't contain reverb signal
3. **SER3L stuck at 32767** - Just constant gain offset from R187
4. **SER3R always 0** - Right channel completely silent

---

## Where We Are Trying To Get

1. Reverb signal from DIL should load into MACHL
2. MACHL should feed into the output MAC operations (PC 89, 90)
3. SER0L/SER2L should contain processed reverb signal
4. SER3L should contain mixed dry+wet output
5. Produce audible reverb tail lasting ~300ms (15246 samples)

---

## What We Are Doing To Get There

### Current Investigation: Why is MACHL always 0?

The microcode has many MUL and MAC operations:
- **MUL** clears accumulator: `dest = op_a * op_b` (MACHL = 0)
- **MAC** adds to accumulator: `dest = MACHL + (op_a * op_b)`

Looking at PC 82-89:
```
82: MUL R117,DIL >MEMSIZ    <- MUL clears MACHL!
83: MUL R117,R112 >R112
84: MUL R117,DIL >MEMSIZ
85: MUL R117,R138 >R138
86: MUL R117,DIL >MEMSIZ
87: MAC R117,R137 >R137     <- First MAC after MULs
88: MAC R114,R114 >R114
89: MAC SER1R,SER0L >SER0L  <- Output to serial
```

Most instructions before PC 89 are MUL (clear accumulator). The MAC operations at 87-88 might be SKIPPED due to the `S` flag!

### Hypothesis:
The skip logic (CCR/CMR) may be incorrectly skipping the MAC operations that should load MACHL with reverb signal.

---

## Files Being Edited

### Emulator Source (main repo):
```
/home/mwolak/EPS16/es5510_standalone.c   <- Main emulator
/home/mwolak/EPS16/es5510_standalone.h   <- Header with es5510_t struct
/home/mwolak/EPS16/es5510-disas.c        <- Disassembler
```

### GPR Initialization:
```
/tmp/4reverbs_complete_init.h            <- Mode A/B/C/D coefficient tables
```

### Microcode Binary:
```
/tmp/4reverbs_ucode.bin                  <- Extracted from 4 REVERBS.efe
```

### Test Files:
```
/tmp/test_debug_all.c         <- Shows all serial ports per sample
/tmp/test_esqpump_mix.c       <- esqpump-style serial mixing
/tmp/test_delay_taps.c        <- Checks DRAM at delay tap times
/tmp/test_gain_simple.c       <- Tests gain values (SER1R/SER3R)
/tmp/test_mac_accum.c         <- Traces MACHL (reveals it's always 0!)
/tmp/test_eps16_memconfig.c   <- Correct dlength configuration
```

---

## Compilation Commands

```bash
# Compile emulator (from /home/mwolak/EPS16)
gcc -o /tmp/test_X /tmp/test_X.c es5510_standalone.c -I. -I/tmp -lm

# Example for MAC accumulator test:
gcc -o /tmp/test_mac_accum /tmp/test_mac_accum.c es5510_standalone.c -I. -I/tmp -lm
/tmp/test_mac_accum

# Disassemble microcode:
./es5510-disas /tmp/4reverbs_ucode.bin > /tmp/4reverbs_disasm.txt
```

---

## Tools Being Used

1. **MAME ES5510 source** (`/home/mwolak/mame/src/devices/cpu/es5510/es5510.cpp`)
   - Reference implementation for correct behavior
   - esqpump.cpp shows serial port routing

2. **Ensoniq SD-1 VST project** (`/tmp/ensoniq_sd1_vst/`)
   - Cloned from github.com/sojusrecords/Ensoniq-SD-1-32-Voice-VST-emulation
   - Uses same ES5510 chip, wraps MAME emulation

3. **Factory Waveboy effects** (`/home/mwolak/EPS16/factory_sounds/WAVEBOY_EFE/`)
   - 4 REVERBS.efe, RESON FILTER 2.efe, ROOM RVERB+.efe, etc.

4. **Disassembler** (`es5510-disas`)
   - Custom disassembler for ES5510 48-bit instructions

---

## Summary of Test Results

| Test | Result |
|------|--------|
| Microcode loads correctly | PASS |
| DRAM stores input impulse | PASS (28672 stored) |
| DIL reads at delay taps | PASS (0x5fff00 at sample 4502) |
| R145 accumulates signal | PASS (values change each sample) |
| MACHL contains reverb | **FAIL** (always 0x0000000000000000) |
| SER0L has output | FAIL (decays to 0 after 3 samples) |
| SER3L has output | PARTIAL (stuck at 32767 constant) |
| Reverb tail audible | **FAIL** (no echoes at delay tap times) |

---

## Realistic Next Steps

### 0. HIGHEST PRIORITY: Understand the 4 REVERBS memory model

The algorithm uses BOTH RDL and RTA. Current theory:
- **RTA with delay tap GPRs (PC 6-27)**: Reads from STATE memory (filter states, all-pass coefficients)
- **RDL with GPR=0 (PC 57-68)**: Reads from current delay position (but why GPR=0?)

Need to understand:
1. What does the state memory at `abase + delay_tap` contain?
2. Are the delay taps being read differently than expected?
3. Compare with MAME's full EPS-16+ emulation to see actual memory contents

### 1. Debug skip logic for MAC operations
The instructions before PC 89 that should load MACHL may be getting skipped. Need to trace:
- Which instructions execute vs skip
- CCR value when MAC operations run
- Whether MUL operations clear MACHL right before output

```c
// Add to es5510_standalone.c:
if (pc >= 82 && pc <= 89) {
    printf("PC %d: %s skip=%d CCR=0x%02x MACHL=0x%llx\n",
           pc, (instr.mac) ? "MAC" : "MUL", skipped, ccr, machl);
}
```

### 2. Check MAC vs MUL implementation
Verify in es5510_standalone.c:
- MUL should set `machl = op_c * op_d` (clear accumulator)
- MAC should set `machl = machl + (op_c * op_d)` (add to accumulator)

### 3. Compare with MAME execution
Run same microcode in MAME's EPS-16+ emulation:
```bash
cd /home/mwolak/mame
./mamed eps16p -debug
```
Trace MACHL values at PC 82-89.

### 4. Trace the complete reverb path
Find which instruction SHOULD load DIL into MACHL:
- Search microcode for `MAC xxx,DIL` pattern
- Ensure that instruction isn't being skipped

### 5. Consider simpler test case
Try RESON FILTER instead of 4 REVERBS - it has simpler signal path and works differently (uses opsel=13 to copy DIL directly to SER3R).

---

## References

- MAME ES5510: `src/devices/cpu/es5510/es5510.cpp`
- MAME esqpump: `src/devices/sound/esqpump.cpp`
- ES5510 datasheet: `references/es5510searchable.pdf`
- ESP2 chip guide: `references/ESP2.pdf`
- Disassembly: `/home/mwolak/EPS16/factory_sounds/WAVEBOY_EFE/4_REVERBS_disasm.txt`
