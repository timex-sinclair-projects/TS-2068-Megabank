# Megabank: run code from a bank, for timing /BE (console `b`). Maps bank 1
# into chunk 5 (DOCK), puts a loop at A000h that reads and writes A100h-A1FFh
# for about 30 s (opcode fetches, reads, writes and refreshes, all in the
# chunk), runs it, and unmaps.
# zmakebas -a 10 -n MBLOOP -o build/MBLOOP.TAP basic/mbloop.bas
10 CLEAR 40959
20 OUT 253,5: OUT 252,1: OUT 244,32
30 RESTORE 100: FOR i=0 TO 18: READ b: POKE 40960+i,b: NEXT i
40 PRINT "running": RANDOMIZE USR 40960
50 OUT 244,0: OUT 253,5: OUT 252,0: PRINT "done"
60 STOP
100 DATA 17,152,58,6,0,33,0,161,126,119,35,16,251,27,122,179,32,241,201
