;==============================================================================
; mbbank.asm -- the Megabank phase 8 hardware test's code (mbbank.bas runs
; it): thin wrappers that call ROM 2.5's Timex bank API in RAM with the
; registers BASIC pokes, and helpers to fill, compare and count chunk 4.
; Assembled to F000h (chunk 7), clear of every chunk the test maps (4-6);
; BASIC's stack is below 32768 (CLEAR 32767), in chunk 3.
;
; Parameters at F200h: P0 bank (B), P1 chunk mask (C, low active), P2-3 an
; address (HL), P4-5 a word (DE) or a destination, P6-7 a length. Results at
; F208h: R0, R1, R2-3. SAVE_STATUS's frame at F210h (19 bytes).
;
; Each API call keeps HL' (BASIC needs it) and IY; IX is saved round it.
;==============================================================================
        DEVICE NOSLOT64K
        ORG 0F000h

GET_WORD        EQU 6316h
PUT_WORD        EQU 633Bh
GET_STATUS      EQU 6405h
GET_NUMBER      EQU 645Eh
BANK_ENABLE     EQU 6499h
SAVE_STATUS     EQU 651Eh
RESTORE_STATUS  EQU 654Ah
XFER_BYTES      EQU 6722h

P0      EQU 0F200h
P1      EQU 0F201h
P2      EQU 0F202h
P4      EQU 0F204h
P6      EQU 0F206h
R0      EQU 0F208h
R1      EQU 0F209h
R2      EQU 0F20Ah
FRAME   EQU 0F210h

        jp be           ; F000 BANK_ENABLE B=P0, C=P1
        jp gs           ; F003 GET_STATUS B=P0: R0 = C, R1 = B
        jp gn           ; F006 GET_NUMBER HL=P2: R0 = A
        jp pw           ; F009 PUT_WORD B=P0, HL=P2, DE=P4
        jp gw           ; F00C GET_WORD B=P0, HL=P2: R2 = HL
        jp xf           ; F00F XFER_BYTES P0 -> P1 banks, P2 -> P4, P6 bytes: R0 = A
        jp ss           ; F012 SAVE_STATUS to FRAME: R2 = IX after
        jp rs           ; F015 RESTORE_STATUS from FRAME
        jp zero4        ; F018 8000h-9FFFh = 0
        jp count4       ; F01B BC = the non-zero bytes in 8000h-9FFFh
        jp fill         ; F01E P6 bytes from ROM 0000h to P2
        jp cmp          ; F021 BC = the bytes at P2 (P6 of them) unlike ROM 0000h

; In: an API call's address in HL. Saves HL', IX, IY round it.
api:    exx
        push hl
        exx
        push ix
        push iy
        ld (.go+1),hl
        ld a,(P0)
        ld b,a
        ld a,(P1)
        ld c,a
        ld hl,(P2)
        ld de,(P4)
.go:    call 0
        ld (R0),a
        pop iy
        pop ix
        exx
        pop hl
        exx
        ret

be:     ld hl,BANK_ENABLE
        jp api

gs:     ld hl,.gs
        jp api
.gs:    call GET_STATUS
        ld a,c
        ld (R0),a
        ld a,b
        ld (R1),a
        ld a,c
        ret

gn:     ld hl,GET_NUMBER
        jp api

pw:     ld hl,PUT_WORD
        jp api

gw:     ld hl,.gw
        jp api
.gw:    call GET_WORD
        ld (R2),hl
        ret

; XFER_BYTES's parameters, pushed as rom_bankapi_hosttest.py pushes them:
; source bank * 256 + destination bank, source, destination, length, 0. It
; takes them off the stack itself.
xf:     ld hl,.xf
        jp api
.xf:    ld a,(P0)
        ld h,a
        ld a,(P1)
        ld l,a
        push hl
        ld hl,(P2)
        push hl
        ld hl,(P4)
        push hl
        ld hl,(P6)
        push hl
        ld hl,0
        push hl
        call XFER_BYTES
        ret

ss:     ld hl,.ss
        jp api
.ss:    ld ix,FRAME
        call SAVE_STATUS
        ld (R2),ix
        ret

rs:     ld hl,.rs
        jp api
.rs:    ld ix,FRAME
        call RESTORE_STATUS
        ld (R2),ix
        ret

zero4:  ld hl,8000h
        ld de,8001h
        ld bc,1FFFh
        ld (hl),0
        ldir
        ret

count4: ld hl,8000h
        ld bc,2000h
        ld de,0
.c:     ld a,(hl)
        or a
        jr z,.z
        inc de
.z:     inc hl
        dec bc
        ld a,b
        or c
        jr nz,.c
        ld b,d
        ld c,e
        ret

fill:   ld hl,0
        ld de,(P2)
        ld bc,(P6)
        ldir
        ret

cmp:    ld hl,0
        ld de,(P2)
        ld bc,(P6)
        exx
        push hl
        ld hl,0
        exx
.m:     ld a,(de)
        cp (hl)
        jr z,.s
        exx
        inc hl
        exx
.s:     inc hl
        inc de
        dec bc
        ld a,b
        or c
        jr nz,.m
        exx
        push hl
        exx
        pop bc
        exx
        pop hl
        exx
        ret

        SAVEBIN "build/mbbank.bin", 0F000h, $ - 0F000h
