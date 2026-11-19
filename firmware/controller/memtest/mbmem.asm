; Megabank memory test (docs/spec.md, Testing, "On hardware" step 2).
;
; Every page of every bank from P_FIRST for P_COUNT banks, twice: once with a
; pattern, once with its complement. Each page is written through one chunk
; (4-7, in turn) and read back through another (2, 4-7, in turn), so every
; usable chunk and every page line is exercised. The first three bytes of
; each 256-byte block hold the block's own bank, sub-page and block number
; (complemented on the second pass), so a page that aliases another shows
; up as a wrong address, not just a wrong bit.
;
; Not tested: chunks 0-1 (the ROM; firmware/controller/basic/mbrom.bas), and
; chunk 3, where this code and BASIC's stack live. Chunk 2 is only read: a
; write there would also reach the 2068's screen.
;
; Destroys everything on the board, the RAM disk included, and the 2068's own
; RAM from 8000h up (without the HSR bit, writes to a mapped chunk also land
; in the 2068's RAM: firmware/controller/README.md). mbmem.bas warns first.
;
; Interrupts are off while it runs. The border shows the bank's low 3 bits.
; Results at R_..., read by mbmem.bas.

        DEVICE NOSLOT64K
        ORG 7E00h

FD      EQU 0FDh                ; select a register
FC      EQU 0FCh                ; its value

        jp start                ; 7E00
P_FIRST db 1                    ; 7E03 first bank
P_COUNT db 64                   ; 7E04 banks
R_STATE db 0                    ; 7E05 0 done, 1 no board, 2 write-protected
R_ERRS  dw 0                    ; 7E06 bad pages (stops at 65535)
R_PASS  db 0                    ; 7E08 first error: pass (1 pattern, 2 complement)
R_BANK  db 0                    ; 7E09   bank
R_SUB   db 0                    ; 7E0A   sub-page (8K of the bank)
R_CHUNK db 0                    ; 7E0B   chunk it was read through
R_OFF   dw 0                    ; 7E0C   offset in the page
R_WANT  db 0                    ; 7E0E   byte expected
R_GOT   db 0                    ; 7E0F   byte read
R_PAGES dw 0                    ; 7E10 pages checked, both passes

v_inv   db 0                    ; 00h or FFh: the pass
v_bank  db 0
v_sub   db 0
v_chunk db 0                    ; chunk for this page
v_rot   db 0                    ; rotation counter
v_mode  db 0                    ; 0 writing, 1 reading
v_last  db 0                    ; last bank + 1

start:
        di
        xor a
        ld (R_STATE),a
        ld hl,0
        ld (R_ERRS),hl
        ld (R_PAGES),hl
        ; detection: register 12h reads A5h, 13h reads 5Ah
        ld a,12h
        out (FD),a
        in a,(FC)
        cp 0A5h
        jr nz,noboard
        ld a,13h
        out (FD),a
        in a,(FC)
        cp 5Ah
        jr nz,noboard
        ; STATUS bit 0: the write-protect switch
        ld a,11h
        out (FD),a
        in a,(FC)
        and 1
        jr z,.wp_off
        ld a,2
        ld (R_STATE),a
        jr finish
.wp_off:
        call unmap_all
        ld a,0
        ld (v_inv),a
        call pass
        ld a,0FFh
        ld (v_inv),a
        call pass
        jr finish
noboard:
        ld a,1
        ld (R_STATE),a
finish:
        call unmap_all
        ld a,7
        out (0FEh),a            ; white border
        ei
        ret

; One pass: write every page, then verify every page.
pass:
        ld a,(P_FIRST)
        ld b,a
        ld a,(P_COUNT)
        add a,b
        jr c,.cap               ; past 255
        cp 254+1
        jr c,.ok
.cap:   ld a,254                ; banks stop at 253
.ok:    ld (v_last),a
        xor a
        ld (v_rot),a
        ld (v_mode),a
        ld hl,write_page
        call each_page
        xor a
        ld (v_rot),a
        inc a
        ld (v_mode),a
        ld hl,verify_page
        ; fall through
; Calls (HL) for every page, with v_bank, v_sub, v_chunk set and the page
; mapped into v_chunk.
each_page:
        ld (.call+1),hl
        ld a,(P_FIRST)
        ld (v_bank),a
.bank:
        ld a,(v_last)
        ld b,a
        ld a,(v_bank)
        cp b
        ret nc
        and 7
        out (0FEh),a            ; progress in the border
        xor a
        ld (v_sub),a
.sub:
        call pick_chunk
        call map
.call:  call 0                  ; patched
        call unmap
        ld a,(v_sub)
        inc a
        ld (v_sub),a
        cp 8
        jr c,.sub
        ld a,(v_bank)
        inc a
        ld (v_bank),a
        jr .bank

; v_chunk for the next page: writes (v_mode 0) go to 4-7 in turn, reads
; (v_mode 1) to 2, 4, 5, 6, 7 in turn, so a page is mostly read back through
; a different chunk from the one it was written through.
pick_chunk:
        ld a,(v_rot)
        inc a
        ld (v_rot),a
        ld b,a
        ld a,(v_mode)
        or a
        ld a,b
        jr nz,.read
        and 3
        add a,4
        ld (v_chunk),a
        ret
.read:
.mod5:  cp 5
        jr c,.r5
        sub 5
        jr .mod5
.r5:    ld hl,read_chunks
        add a,l
        ld l,a
        jr nc,.nc
        inc h
.nc:    ld a,(hl)
        ld (v_chunk),a
        ret

read_chunks:
        db 2,4,5,6,7

; Map v_bank/v_sub into v_chunk: OWNc = bank (sets SUBc = c), then SUBc = sub.
map:
        ld a,(v_chunk)
        out (FD),a
        ld a,(v_bank)
        out (FC),a
        ld a,(v_chunk)
        add a,8
        out (FD),a
        ld a,(v_sub)
        out (FC),a
        ret

unmap:
        ld a,(v_chunk)
        out (FD),a
        xor a
        out (FC),a
        ret

unmap_all:
        ld b,8
        ld c,0
.l:     ld a,c
        out (FD),a
        xor a
        out (FC),a
        inc c
        djnz .l
        ret

; The block's seed in C: (bank * 8 + sub) mixed with the block number in D,
; and the pass. Byte at L = L xor C.
seed:
        ld a,(v_bank)
        add a,a
        add a,a
        add a,a
        ld c,a
        ld a,(v_sub)
        add a,c
        rlca
        xor d
        ld c,a
        ld a,(v_inv)
        xor c
        ld c,a
        ret

; HL = the chunk's first address, D = 0, E = 32 blocks.
page_start:
        ld a,(v_chunk)
        add a,a
        add a,a
        add a,a
        add a,a
        add a,a                 ; chunk * 20h
        ld h,a
        ld l,0
        ld de,0020h
        ret

write_page:
        call page_start
.blk:   call seed
.byte:  ld a,l
        xor c
        ld (hl),a
        inc l
        jr nz,.byte
        ; the block's address in its first three bytes
        ld a,(v_inv)
        ld c,a
        ld a,(v_bank)
        xor c
        ld (hl),a
        inc l
        ld a,(v_sub)
        xor c
        ld (hl),a
        inc l
        ld a,d
        xor c
        ld (hl),a
        ld l,0
        inc h
        inc d
        dec e
        jr nz,.blk
        ret

verify_page:
        ld hl,(R_PAGES)
        inc hl
        ld (R_PAGES),hl
        call page_start
.blk:   call seed
        ld l,3
.byte:  ld a,l
        xor c
        cp (hl)
        jr nz,bad
        inc l
        jr nz,.byte
        ld a,(v_inv)
        ld c,a
        ld a,(v_bank)
        xor c
        cp (hl)
        jr nz,bad
        inc l
        ld a,(v_sub)
        xor c
        cp (hl)
        jr nz,bad
        inc l
        ld a,d
        xor c
        cp (hl)
        jr nz,bad
        ld l,0
        inc h
        inc d
        dec e
        jr nz,.blk
        ret

; A wrong byte at HL, A the byte wanted. Count the page; the first one's
; details go to R_...
bad:
        ld b,a
        ld de,(R_ERRS)
        ld a,d
        or e
        jr nz,.count
        ld a,b
        ld (R_WANT),a
        ld a,(hl)
        ld (R_GOT),a
        ld a,(v_inv)
        and 1
        inc a
        ld (R_PASS),a
        ld a,(v_bank)
        ld (R_BANK),a
        ld a,(v_sub)
        ld (R_SUB),a
        ld a,(v_chunk)
        ld (R_CHUNK),a
        ld a,h
        and 1Fh
        ld h,a
        ld (R_OFF),hl
.count:
        inc de
        ld a,d
        or e
        ret z                   ; stays at 65535
        ld (R_ERRS),de
        ret

        DISPLAY "end ", $
        ASSERT $ <= 8000h
        SAVEBIN "build/mbmem.bin", 7E00h, $ - 7E00h
