# Megabank phase 4 on hardware: the ROM 2.5 side of the board (the spec,
# "Minimal ROM: detection, reset, mover, r:"). It checks what ZEsarUX's
# model can't: that the mover's window chunk takes its HSR bit, so a SAVE or
# LOAD through it never writes the 2068's own RAM, and that it puts back
# whatever was in the window chunk (OWN, SUB and the HSR). The reset at boot
# is checked from the controller's console (`l`), not from here.
#
# The data is 2000 bytes at 57400 (chunk 7) and the stack is below 32768
# (chunk 3), so the mover's window is chunk 4 (8000h). The 2068's own RAM
# there is zeroed first and counted after: any byte not 0 is a leak.
# Machine code: 64000 zeroes 8000h-9FFFh, 64020 counts its non-zero bytes.
# The board's first bank, 8K 4, stands in for a program's own page; the RAM
# disk is at the top of the board, so it is left alone. The test file r:mb4t
# is erased at the end. Each check ends "ok" or "FAIL"; the last line
# names the failed checks. A SAVE asks to start the tape: press a key.
#
# zmakebas -a 10 -n MBPH4 -o build/MBPH4.TAP basic/mbph4.bas
10 CLEAR 32767: LET f=0: LET t=0: LET f$=""
20 RESTORE 9000: FOR i=0 TO 13: READ b: POKE 64000+i,b: NEXT i: FOR i=0 TO 22: READ b: POKE 64020+i,b: NEXT i
30 CLS : PRINT "Megabank phase 4 test"
40 PRINT "ROM ";PEEK 101;: LET c=PEEK 101=37: GO SUB 8000
50 OUT 253,18: LET a=IN 252: OUT 253,19: LET d=IN 252: PRINT "ID ";a;" ";d;: LET c=a=165 AND d=90: GO SUB 8000
60 OUT 253,20: LET b=IN 252: OUT 253,21: LET s=IN 252: OUT 253,22: PRINT "BASE ";b;" SIZE ";s;" VER ";IN 252
70 PRINT "OWN 0-7";: LET c=1: FOR i=0 TO 7: OUT 253,i: LET v=IN 252: PRINT " ";v;: IF v<>0 THEN LET c=0
80 NEXT i: GO SUB 8000
90 PRINT "HSR ";IN 244: OUT 253,16: PRINT "WPROT ";IN 252
100 SAVE "tpi:ram"
110 PRINT "Filling...": FOR i=0 TO 1999: POKE 57400+i,1+i-255*INT (i/255): NEXT i
120 RANDOMIZE USR 64000
200 PRINT "SAVE r:";: SAVE "r:mb4t"CODE 57400,2000: GO SUB 7000
210 PRINT "VERIFY r:";: VERIFY "r:mb4t"CODE : LET c=1: GO SUB 8000
220 POKE 57400,0: POKE 58399,0: POKE 59399,0
230 PRINT "LOAD r:";: LOAD "r:mb4t"CODE : GO SUB 7000
240 PRINT "bytes ";PEEK 57400;" ";PEEK 58399;" ";PEEK 59399;: LET c=PEEK 57400=1 AND PEEK 58399=235 AND PEEK 59399=215: GO SUB 8000
# A program's own page in the window chunk, with its HSR bit.
300 OUT 253,4: OUT 252,b: OUT 244,16: POKE 32768,123: POKE 40959,45
310 PRINT "SAVE, page in 4";: SAVE "r:mb4t"CODE 57400,2000: GO SUB 7500
320 PRINT "LOAD, page in 4";: LOAD "r:mb4t"CODE : GO SUB 7500
330 PRINT "VERIFY, page in 4";: VERIFY "r:mb4t"CODE : GO SUB 7500
340 OUT 244,0: OUT 253,4: OUT 252,0
350 PRINT "HOME 8000h";: LET n=USR 64020: PRINT " ";n;" leaked";: LET c=n=0: GO SUB 8000
400 ERASE "r:mb4t"
410 PRINT : PRINT "Done: ";t;" checks, ";f;" FAIL";f$
420 STOP
# After a SAVE or LOAD with nothing in chunk 4: no leak, nothing left mapped.
7000 LET n=USR 64020: LET h=IN 244: OUT 253,4: LET o=IN 252
7010 PRINT " leak ";n;" HSR ";h;" OWN4 ";o;: LET c=n=0 AND h=0 AND o=0: GO TO 8000
# After one with the page in chunk 4: the page, its SUB and HSR bit back.
7500 LET h=IN 244: OUT 253,4: LET o=IN 252: OUT 253,12: LET u=IN 252
7510 PRINT " HSR ";h;" OWN4 ";o;" SUB4 ";u;" ";PEEK 32768;" ";PEEK 40959;
7520 LET c=h=16 AND o=b AND u=4 AND PEEK 32768=123 AND PEEK 40959=45: GO TO 8000
8000 POKE 23692,255: LET t=t+1: IF c THEN PRINT " ok": RETURN
8010 PRINT " FAIL": LET f=f+1: LET f$=f$+" "+STR$ t: RETURN
# 9000, at 64000: LD HL,8000h; LD DE,8001h; LD BC,1FFFh; LD (HL),0; LDIR; RET
9000 DATA 33,0,128,17,1,128,1,255,31,54,0,237,176,201
# 9010, at 64020: count the non-zero bytes in 8000h-9FFFh, returned in BC
9010 DATA 33,0,128,1,0,32,17,0,0,126,183,40,1,19,35,11,120,177,32,245,66,75,201
