# Megabank hardware test 4: a power cut during a SAVE to the RAM disk (the
# spec, Testing, "On hardware" step 4). Other files must survive; the one
# being written may be lost, but must not come back corrupt.
#
# long's bytes are the ROM copied twice to 32768-64767 (machine code at
# 65000), so they can be checked after the cut: VERIFY gives 0 OK (intact),
# F (lost) or R (corrupt: a fail).
#
# 1: FORMAT "r:" (asked first), SAVE "r:keep" (the ROM's first 100 bytes),
#    then SAVE "r:long" CODE 32768,32000 over and over, the count in the
#    top corner. Each SAVE asks to start the tape: hold a key down (not
#    SPACE) and every prompt is answered. After a few rounds, switch the
#    2068 off at any moment.
# 2: after switching on and loading this again: CAT "r:", then checks that
#    keep verifies against the ROM, long verifies (0 OK) or is gone (F), a new
#    file saves and both erase. Each check ends "ok" or "FAIL".
#
# zmakebas -a 10 -n MBCUT -o build/MBCUT.TAP basic/mbcut.bas
   10 CLEAR 32767: RESTORE 9900: FOR i=0 TO 19: READ b: POKE 65000+i,b: NEXT i: RANDOMIZE USR 65000
   15 CLS : PRINT "Megabank power cut test"''"1  format, then SAVE in a loop"'"2  check after the cut"
   20 PAUSE 0: LET k$=INKEY$: IF k$="1" THEN GO TO 100
   30 IF k$="2" THEN GO TO 500
   40 GO TO 20
  100 CLS : PRINT "This FORMATs the RAM disk."'"Press Y to go on.": PAUSE 0: LET k$=INKEY$: IF k$<>"y" AND k$<>"Y" THEN STOP
# Over files FORMAT asks to be repeated; on an empty disk the second is harmless.
  110 FORMAT "r:": FORMAT "r:"
  140 SAVE "r:keep"CODE 0,100
  150 CLS : PRINT AT 2,0;"Hold a key down (not SPACE):"'"each SAVE asks for one. After a"'"few rounds, switch the 2068 off."
  160 LET i=0
  170 LET i=i+1: PRINT AT 0,0;"round ";i;"  ": SAVE "r:long"CODE 32768,32000: GO TO 170
  500 CLS : POKE 23692,255: LET f=0: LET t=0: LET f$=""
  510 CAT "r:"
  520 PRINT "keep verifies";: LET t=1: LET n=530: ON ERR GO TO 9000: VERIFY "r:keep"CODE 0,100: LET c=1: GO SUB 8000
  530 PRINT "long";: LET t=2: LET n=540: ON ERR GO TO 9000: VERIFY "r:long"CODE 32768,32000: PRINT " verifies";: LET c=1: GO SUB 8000
  540 PRINT "new file";: LET t=3: LET n=550: ON ERR GO TO 9000: SAVE "r:after"CODE 0,10: LET c=1: GO SUB 8000
  550 PRINT "ERASE after";: LET t=4: LET n=560: ON ERR GO TO 9000: ERASE "r:after": LET c=1: GO SUB 8000
  560 PRINT "ERASE keep";: LET t=5: LET n=570: ON ERR GO TO 9000: ERASE "r:keep": LET c=1: GO SUB 8000
  570 ON ERR RESET
  580 PRINT : PRINT "Done: ";f;" FAIL";f$
  590 STOP
 8000 POKE 23692,255: IF c THEN PRINT " ok": RETURN
 8010 PRINT " FAIL": LET f=f+1: LET f$=f$+" "+STR$ t: RETURN
# A trapped report. long may be F (lost); anything else fails.
 9000 LET e=PEEK 23739: ON ERR RESET
 9010 IF t=2 AND e=15 THEN PRINT " gone (F)";: LET c=1: GO SUB 8000: GO TO n
 9020 PRINT " report ";e;: LET c=0: GO SUB 8000: GO TO n
# 65000: LD HL,0; LD DE,32768; LD BC,16384; LDIR; LD HL,0; LD BC,15616; LDIR; RET
 9900 DATA 33,0,0,17,0,128,1,0,64,237,176,33,0,0,1,0,61,237,176,201
