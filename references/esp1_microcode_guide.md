# Ensoniq ESP1 Microcode Programming Guide (Unofficial)

## What are effects files?

Effects files are basically plug ins to the ASR10 operating system. They are loaded at runtime and then, both the 680x0 as well as the ESP code contained in them is run by each of the two processors. All 68k code is written using PC relative or absolute addressing, there is no relocation process when loading the effect thus the addresses have to be relative or refer to a fixed code position (e.g. in the ROMs) or a preset address register (see below). There is only one active effects file in RAM at any given time.

Any effects file essentially consists of the following sections:
- a) global data, parameter list and parameter structure
- b) 68k (CPU) code routines
- c) ESP (the Ensoniq DSP) code

Please note that it is absolutely necessary to have a portion of ESP code in the effect file (even if it is just a passthrough algorithm) as otherwise the effects download will fail. The 68k code is split into two sections:
1. An init routine that is called once when loading the effects file into RAM
2. A task routine that is called at regularly spaced intervals in time and can be used to calculate controller dependencies or modulator waveforms

An optional keydown routine can be supplied to synchronize the effects modulators to keypresses (global to an instrument).

Each effects parameter is connected to the actual ESP processing by a special field located at the end of each display page description.

### Address Registers

The following address registers have a special meaning when viewed from the plugin's task routine:
- `a1`: start of the effect file in RAM
- `a5`: start of the effects variables (FxVar1) of the first VAR in RAM
- `a2`: used by OS routine PutText(), it points to the text string to display (only relevant when calling this OS routine)

### Effects File IDs

For the effects file, two effects IDs exist:
- One recognized only by the EPS 16-PLUS
- One recognized by both EPS 16-PLUS and ASR10

### The ESP1 Processor

The ESP1 ENSONIQ Signal Processor uses its own ucode comprised of its own instruction set. This instruction set is proprietary to ENSONIQ. The ESP1 instruction set suffers from some serious drawbacks which were improved upon with the ESP2 model.

The GP (general purpose) registers of the ESP can be viewed and modified using the **GPR MONITOR** command on the Command/ENV1 page. The current ESP program code can be viewed and modified with the **INSTRUCTION MONITOR** on the same system page.

**Warning:** It is not recommended to use this function in write mode as this can cause severe damage to the speaker system if improper instructions are provided.

---

## Section A - Introduction and Effects File definitions

### a) General considerations

All following comments and descriptions refer to the particular system operating in EDIT mode, ie. only pages with parameters to edit being displayed. A distinct set of parameters is grouped to be displayed on what we shall call a *display page*.

### b) Parameter attributes

There are different *parameter attribute* values which determine how a particular parameter on a display page will look like. These attribute values are of 1 byte in length:

```asm
NewEqNUnderline     .EQU $00
NewNEqNUnderline    .EQU $60
NewEqUnderline      .EQU $80
SameNEqNUnderline   .EQU $a0
SameEqUnderline     .EQU $c0
SameNEqUnderline    .EQU $e0
```

- `New` = new display page
- `Same` = displayed on the same display page
- `Eq` = equal sign after param name
- `NEq` = no equal sign after param name
- `Underline` = cursor underline
- `NUnderline` = no cursor underline

### c) Parameter type

There is a *parameter type* value of 1 byte in length that determines the data format and appearance:

```asm
ParmNumeric127      .EQU $0
ParmNumeric99       .EQU $2
ParmNumeric99s      .EQU $3
ParmNumericW16      .EQU $17
ParmNumericW32      .EQU $16
ParmNumericHex8     .EQU $19
ParmNumericHex16    .EQU $1A
ParmNumericFix      .EQU $13
ParmNumericFrac     .EQU $15
ParmKeyrange        .EQU $11
ParmString          .EQU $E
ParmAlpha           .EQU $14
ParmPitchTables     .EQU $18

; other definitions
RomStart            .EQU $FFF80000
```

---

## Section B - Global Data, Parameter List and Parameter Structure

### a) Global Data Header

Each effects file begins with a header of global data. It has a fixed size and all values occur at predefined positions.

**Note:** Values described with 'used internally' should not be changed. FxPtrOffset and FxPtrMore are used by the memory management routines to create references to the code and structures within RAM.

#### Effect Header Structure

```asm
    .TEXT
    .ORG 0
    dc.l $60000000          ; FxBlockSize (3 blocks*512 bytes = $600)
    dc.w 0                  ; FxPtrOffset - used by mem manager
    dc.w 0                  ; FxPtrMore - used by mem manager
    dc.w 2                  ; FxVersionNum - used internally
    .ASCII "EFFECT NAME "   ; FxName - effect file name (12 chars)
    dc.l EndOfCode+600      ; FxSize - size of actual effect file data
    dc.w FxUcodeStart+2     ; FxUcode - start of ESP ucode (rel. to a1)
    dc.w 0                  ; FxTable - start of ESP RAM table
    dc.w InitCode           ; FxInitCode - start of 68k init routine
    dc.w TaskCode           ; FxUpdateTask - start of 68k task routine
    dc.w FxVar1             ; FxVar1 - start of variables for 1st VAR
    dc.w FxVar2             ; FxVar2 - start of variables for 2nd VAR
    dc.w FxVar3             ; FxVar3 - start of variables for 3rd VAR
    dc.w FxVar4             ; FxVar4 - start of variables for 4th VAR
FxPListStart:   dc.w FxFirstParm    ; FxParamListStart
FxPListLast:    dc.w FxLastParm     ; FxParamListLast
FxPListCurr:    dc.w FxDefaultParm  ; FxParamListCurr
    .ASCIIZ "BYPASS "       ; FxBus1Name - bus name
    .ASCIIZ "BYPASS "       ; FxBus2Name - bus name
    .ASCIIZ "BYPASS "       ; FxBus3Name - bus name
    dc.b 0                  ; FxCurrVar - used internally
    dc.b 0                  ; FxOutputGprL - needs knowledge of ESP
    dc.b 0                  ; FxOutputGprR - needs knowledge of ESP
    dc.b 0                  ; FxNumVoices - needs knowledge of ESP
    dc.b 0                  ; unused
    dc.w 0                  ; FxKeydownRoutine - points to key down rt.
    ds.b 10                 ; spares
```

### b) Parameter List

The parameter list contains pointers to the structures describing the actual parameters that are displayed. This list follows immediately after the header structure and can be of arbitrary length.

When scrolling through the EDIT/effect page, the parameter list is stepped through and the parameters are displayed using their attribute/type descriptions.

```asm
FxFirstParm:    dc.w InfoPage
                dc.w Hex2Dec1
                dc.w Hex2Dec2
                ; ... more parameters ...
FxLastParm:     dc.w AboutPage
FxDefaultParm   .EQU FxFirstParm
```

### c) Parameter Structure

There exist two generic types of parameters:

#### Textual Parameter

```asm
; textual parameter:
; ds.b FxParmAttrib | FxDirectDial  ; parm attribute OR direct dial#
; ds.b FxParmType                   ; param type (always 'ParmString')
; ds   FxTextParams                 ; points to the 'FxTextParams' structure
; ds   FxParamVar                   ; points to storage space for value
; ds   FxDisplayText                ; name of parameter
; ds.l FxESP                        ; related to the ESP program
;
; FxTextParams:
; ds.l FxTextStrings                ; points to the text strings
; ds.b FxStringLen                  ; length of individual text strings
; ds.b FxNumStrings                 ; number of text strings
```

#### Numeric Parameter

```asm
; numeric parameter:
; ds.b FxParmAttrib | FxDirectDial  ; parm attribute OR direct dial#
; ds.b FxParmType                   ; param type
; ds.b FxLoLimit                    ; the parameter's minimum value
; ds.b FxHiLimit                    ; the parameter's maximum value
; ds   FxParamVar                   ; points to storage space for value
; ds   FxDisplayText                ; name of parameter
; ds.l FxESP                        ; related to the ESP program
```

**Note:** Storage locations for parameter values using word length always need to begin at even addresses in memory or a system error will occur.

`FxParamVar` contains the offset to the memory location for storing the parameter's value. This offset is relative to the start of the VAR variable segment (each VAR has its own storage space).

---

## Section C - 68k code section

### a) 68k init routine

The init routine is called once when loading the effects file into RAM:

```asm
InitCode:   move.w FxPListStart(a1),FxPListCurr(a1)
            rts
```

### b) 68k task routine

This routine is periodically executed by the OS. Make sure it does not take up too much time to execute, as this will considerably slow down the system and cause system errors of type 144 (out of system buffers) eventually, since the MIDI buffer will experience an overflow.

```asm
TaskCode:
    ; Your task code here
    rts
```

---

## Section D - ESP code section

Here starts the ESP ucode section. **It must be compiled and linked by a different program**, thus it's always at the end of each effects file.

Most assemblers don't support labels at the end of the code, so a dummy `$FFFF` longword is used and the FxUcode field in the header points to `FxUcodeStart+2` to account for the dummy:

```asm
    .EVEN
FxUcodeStart:   dc.w $FFFF
    .END
```

---

## Key Offsets in Effect File Header

| Offset | Field | Description |
|--------|-------|-------------|
| 0x00 | FxBlockSize | Size in ENSONIQ block format |
| 0x04 | FxPtrOffset | Used by memory manager |
| 0x06 | FxPtrMore | Used by memory manager |
| 0x08 | FxVersionNum | Version number |
| 0x0A | FxName | 12-character name |
| 0x16 | FxSize | Actual file size |
| 0x1A | FxUcode | ESP microcode offset (rel. to a1) |
| 0x1C | FxTable | ESP RAM table offset |
| 0x1E | FxInitCode | 68k init routine offset |
| 0x20 | FxUpdateTask | 68k task routine offset |
| 0x22 | FxVar1-4 | Variable segment offsets |
| 0x2A | FxParamListStart | First parameter pointer |
| 0x2C | FxParamListLast | Last parameter pointer |
| 0x2E | FxParamListCurr | Current/default parameter |
| 0x30 | FxBus1Name | 13-byte bus name |
| 0x3D | FxBus2Name | 13-byte bus name |
| 0x4A | FxBus3Name | 13-byte bus name |
| 0x57 | FxCurrVar | Current variation |
| 0x58 | FxOutputGprL | Left output GPR |
| 0x59 | FxOutputGprR | Right output GPR |
| 0x5A | FxNumVoices | Number of voices |
| 0x5C | FxKeydownRoutine | Key down routine offset |
