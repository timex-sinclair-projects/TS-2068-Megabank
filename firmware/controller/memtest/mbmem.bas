#! zmakebas -n mbmem -a 10
# The Megabank memory test's front end (mbmem.asm). Loads the test to 32256,
# checks for the board, warns, runs it, prints the result and saves the
# result bytes to the card as mbmemres (32261, 13 bytes) for
# tspico-firmware-build's tools/hwcheck.py.
#
# The test erases the whole board, the RAM disk included, and the 2068's own
# RAM from 32768 up. Interrupts are off while it runs (about 3 minutes for
# 64 banks); the border shows its progress.
   10 CLEAR 32255: LOAD ""CODE 32256
   20 CLS : PRINT "Megabank memory test"''
   30 OUT 253,18: LET a=IN 252: OUT 253,19: IF a<>165 OR IN 252<>90 THEN PRINT "No Megabank answers on ports"'"252/253.": STOP
   40 OUT 253,20: LET b=IN 252: OUT 253,21: LET n=IN 252: IF b+n>254 THEN LET n=254-b
   50 OUT 253,17: IF IN 252-2*INT (IN 252/2)=1 THEN PRINT "The write-protect switch is on."'"Turn it off first.": STOP
   60 PRINT "Banks ";b;" to ";b+n-1;": ";n*64;"K."''"This ERASES the whole board, the"'"RAM disk too, and the 2068's own"'"RAM from 32768 up."''"Press Y to go on, any other key"'"to stop."
   70 PAUSE 0: LET k$=INKEY$: IF k$<>"y" AND k$<>"Y" THEN STOP
   80 POKE 32259,b: POKE 32260,n
   90 PRINT '"Testing (about ";INT (n*3/64+.99);" min)..."
  100 RANDOMIZE USR 32256
  110 LET s=PEEK 32261: LET e=PEEK 32262+256*PEEK 32263: LET p=PEEK 32272+256*PEEK 32273
  120 IF s=1 THEN PRINT "No board answered.": GO TO 200
  130 IF s=2 THEN PRINT "Write-protected.": GO TO 200
  140 PRINT p;" pages read back."
  150 IF e=0 THEN PRINT PAPER 4;" PASS ": GO TO 200
  160 PRINT PAPER 2; INK 7;" FAIL ";: PRINT " ";e;" bad page";("s" AND e>1)
  170 PRINT "First: pass ";PEEK 32264;", bank ";PEEK 32265;","'"8K ";PEEK 32266;" via chunk ";PEEK 32267;", offset ";PEEK 32268+256*PEEK 32269
  180 PRINT "wanted ";PEEK 32270;", read ";PEEK 32271
  200 SAVE "mbmemres"CODE 32261,13
  210 PRINT '"Reset the 2068 before going on."
