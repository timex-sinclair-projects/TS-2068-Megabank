# Megabank: map a bank into chunk 0 (0000h) and chunk 1 (2000h), over the
# ROM, from machine code at 50000 with interrupts off. For each chunk: OWNc
# = bank 1, then F4h bit c; write 111 at 1000h/3000h; bank 2, write 222 and
# a RET after it, and CALL that RET (an opcode fetch from the board); read
# back bank 1 and bank 2; then F4h = 0 and OWNc = 0, and read the ROM again.
# Expect: 111 222 then the ROM's byte twice, call 1.
# Parameters at 49990 (chunk, HSR mask, address), results at 49994-49997.
# zmakebas -a 10 -n MBROM -o build/MBROM.TAP basic/mbrom.bas
10 CLEAR 49989
20 RESTORE 900: FOR i=0 TO 87: READ b: POKE 50000+i,b: NEXT i
30 FOR c=0 TO 1
40 LET a=4096+8192*c: POKE 49990,c: POKE 49991,2^c: POKE 49992,a-256*INT (a/256): POKE 49993,INT (a/256)
50 POKE 49997,0: LET r=USR 50000
60 PRINT "chunk ";c;": ";PEEK 49994;" ";PEEK 49995;" ";PEEK 49996;" rom ";PEEK a;" call ";PEEK 49997
70 NEXT c
80 STOP
900 DATA 243,58,70,195,211,253,62,1,211,252,58,71,195,211,244,42
910 DATA 72,195,54,111,58,70,195,211,253,62,2,211,252,54,222,35
920 DATA 54,201,17,119,195,213,233,43,62,1,50,77,195,58,70,195
930 DATA 211,253,62,1,211,252,126,50,74,195,58,70,195,211,253,62
940 DATA 2,211,252,126,50,75,195,175,211,244,58,70,195,211,253,175
950 DATA 211,252,126,50,76,195,251,201
