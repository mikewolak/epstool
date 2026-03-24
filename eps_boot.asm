; Ensoniq EPS v2.4 ROM (hi/lo interleaved, base = 0xC00000)
; Focus: reset/boot, floppy + SCSI (AM33C93A), VFD + buttons (0x2400xx)
; Derived from eps_os_24_hi.bin/eps_os_24_lo.bin disassembly.

; -------------------------
; Vector table (offset 0)
; -------------------------
        ORG     $C00000
V_RESET_SP      DC.L    $00001548
V_RESET_PC      DC.L    RESET
V_BUS_ERROR     DC.L    $C0A476
V_ADDRESS_ERR   DC.L    $C0A876
V_ILLEGAL       DC.L    $C0AC76
V_ZERODIV       DC.L    $C0B076
V_CHK           DC.L    $C0B476
V_TRAPV         DC.L    $C0B876
V_PRIV          DC.L    $C0BC76
V_TRACE         DC.L    $C0C076
V_LINE1010      DC.L    $C0C476
V_LINE1111      DC.L    $C0CC76
; ... all remaining vectors point into $C0D476 region (default handler)

; ----------------------------------------------------
; Memory‑mapped I/O (observed in ROM)
; ----------------------------------------------------
; Floppy FDC (byte, odd addresses):
FDC_STAT        EQU     $280001   ; status (read) / command (write)
FDC_DATA        EQU     $280003   ; data port
FDC_DOR         EQU     $280005   ; motor / drive select / rate
FDC_FIFO        EQU     $280007   ; data fifo (alt)
FDC_RATE        EQU     $280009   ; data rate
FDC_TC          EQU     $28000B   ; terminal count / int ack
; misc flags: 0x280013/15/17/19/1B/1D/1F touched during seek/calibration

; SCSI AM33C93A (WD33C93A‑compatible, byte)
SCSI_STAT       EQU     $2C0001   ; Addr/Control (ICS* on 16‑bit bus, odd)
SCSI_DATA       EQU     $2C0003   ; Data register
SCSI_CMD        EQU     $2C0007   ; Command/Data FIFO (selected by addr reg)

; Front‑panel / VFD / buttons (observed)
IO_BTN0         EQU     $240000   ; reads into 0x2C9, also written FF during init
IO_BTN1         EQU     $240001   ; reads into 0x2CA
IO_LCD_CMD      EQU     $240007   ; written 0x10 during init (cursor/display setup)
IO_LCD_DATA0    EQU     $240040   ; read back into 0x2C9
IO_LCD_DATA1    EQU     $240041   ; read back into 0x2CA
IO_LCD_CTRL1    EQU     $240044   ; written word 0x8182 (LCD bias/contrast?)
IO_LCD_CTRL2    EQU     $240046   ; written 0/4 toggled (RS/E bits)
IO_LCD_CTRL3    EQU     $240047   ; written 0x10 / 0x88 (enable, blink?)
IO_LCD_PTR      EQU     $24004C   ; long pointer (frame buffer base)
IO_LCD_LEN      EQU     $24004A   ; transfer length
; (heuristic: these writes precede/ follow bursts to SCSI; likely VFD or keypad scan)

; RAM scratch used by loader
RAM_BUF         EQU     $0228     ; sector load ptr (long)
RAM_SECT        EQU     $022C     ; current addr pointer into buffer
RAM_FLAGS       EQU     $02C8     ; global error code (0 = OK)
RAM_SCSIID      EQU     $02C4     ; target ID / phase byte
RAM_SCSIMSG     EQU     $02C6     ; message code
RAM_SCSICNT     EQU     $02CE     ; retry counter
RAM_FDC_STATE   EQU     $02D4     ; floppy state
RAM_SCSIBYTE    EQU     $02D0     ; single data byte for SCSI moves

; -------------------------
; Reset / boot path
; -------------------------
RESET:
        ; clear a chunk of RAM used by loader
        LEA     $0228,A0
        CLR.B   (A0)+           ; clear 0x0228..0x15FF
        CMPA.L  #$00001600,A0
        BNE     RESET

        BSR     INIT_PANELS     ; sets panel/VFD defaults, buttons -> FF
        BSR     INIT_DISKS      ; floppy + SCSI soft reset

BOOT_LOOP:
        BSR     TRY_FLOPPY
        TST.B   RAM_FLAGS
        BEQ     BOOT_OK

        BSR     TRY_SCSI
        TST.B   RAM_FLAGS
        BEQ     BOOT_OK

        BRA     BOOT_LOOP       ; keep trying

BOOT_OK:
        JMP     $C0729C         ; jump to RAM‑loaded 2nd stage (start of OS image at 0x332)

; -------------------------
; Panel / VFD / buttons init
; -------------------------
INIT_PANELS:
        MOVE.B  #$FF,IO_BTN0    ; drive lines high / deselect
        MOVE.B  #$FF,IO_LCD_DATA0
        MOVE.B  #$10,IO_LCD_CMD ; basic command (cursor/display on?)
        MOVE.B  #$10,IO_LCD_CTRL3 ; control bit set
        RTS

; -------------------------
; Floppy init snippet (FDC reset & motor)
; -------------------------
INIT_FDC:
        CLR.B   IO_FDC_RESET    ; (at 0x28001B) clear reset latch
        MOVE.B  #$40,FDC_DOR    ; motor on, drive 0, data rate 500k
        MOVE.B  #$03,FDC_TC     ; assert TC/int ack
        RTS

; -------------------------
; SCSI (AM33C93A) reset & poll ready
; -------------------------
SCSI_HARD_RESET:
        MOVE.B  #$F8,SCSI_STAT  ; set/reset + abort
.wait_ready:
        MOVE.B  SCSI_STAT,D1
        ANDI.B  #2,D1           ; bit1 = DRQ/ready on this bus wiring
        BEQ     .wait_ready
        RTS

; -------------------------
; SCSI microcode download / init (extract)
; -------------------------
SCSI_INIT:
        BSR     SCSI_HARD_RESET
        MOVE.B  #$0B,D3         ; 11 bytes of init script @C0AD74
        LEA     INIT_SEQ,A0
.send:
        BSR     SCSI_WAIT_RDY
        MOVE.B  (A0)+,SCSI_CMD
        DBF     D3,.send
        RTS

SCSI_WAIT_RDY:
        MOVE.B  SCSI_STAT,D1
        ANDI.B  #2,D1
        BEQ     SCSI_WAIT_RDY
        RTS

INIT_SEQ:
        DC.B $10,$10,$10,$10,$10,$10,$10,$10,$10,$10,$10 ; (observed writes of 0x10 & later 0x4E/$ID)

; -------------------------
; TRY_SCSI – simplified path
; -------------------------
TRY_SCSI:
        CLR.B   RAM_FLAGS
        BSR     SCSI_HARD_RESET
        BSR     SCSI_INIT
        ; load target ID / message
        MOVE.B  #0,RAM_SCSIID   ; host ID = 0, tgt encoded elsewhere
        MOVE.B  #3,RAM_SCSIMSG  ; message identify?
        MOVE.W  #$0332,RAM_SECT ; load buffer ptr
        MOVE.L  #$00000800,RAM_BUF ; sector count *512 (example)
        BSR     SCSI_READ_BOOT
        RTS

; -------------------------
; TRY_FLOPPY – simplified path
; -------------------------
TRY_FLOPPY:
        CLR.B   RAM_FLAGS
        BSR     INIT_FDC
        ; set DMA buffer
        MOVE.L  #$00000332,RAM_SECT
        MOVE.L  #$00000200,RAM_BUF
        BSR     FDC_READ_BOOT
        RTS

; -------------------------
; Loader buffer and jump
; -------------------------
; After FDC/SCSI read: RAM_FLAGS==0, buffer at RAM_SECT holds first part of OS
; A final RTS returns to BOOT_OK which JMPs to $C0729C (start of loaded image)

; -------------------------
; GPIO / Buttons scan (usage hints)
; -------------------------
; Reads at IO_BTN0/IO_BTN1 into 0x2C9/0x2CA at C0A6CA/C0A6D2.
; Likely matrix readback: write FF then read to capture key closures.

; -------------------------
; VFD writes (heuristic)
; -------------------------
; Routine at C0E9C4 writes:
;   MOVE.W  #$8182, IO_LCD_CTRL1
;   MOVE.B  #$00,   IO_LCD_CTRL2
;   MOVE.B  #$88,   IO_LCD_CTRL3
; followed by moving a pointer (RAM_SECT) to IO_LCD_PTR and length to IO_LCD_LEN,
; suggesting DMA‑style transfer of characters to the display.
;
; These sequences wrap floppy/SCSI error handling, so likely update the front‑panel display.

; -------------------------
; End of file
; -------------------------
