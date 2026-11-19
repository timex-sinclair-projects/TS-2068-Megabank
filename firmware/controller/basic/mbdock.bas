# Megabank: two banks in chunk 5 (A000h), with the chunk in DOCK so the
# 2068's own RAM there keeps its byte. Prints 111, 222, 77.
# zmakebas -a 10 -n MBDOCK -o mbdock.tap mbdock.bas
10 CLEAR 40959
20 POKE 40960,77
30 OUT 244,32
40 OUT 253,5: OUT 252,1: POKE 40960,111
50 OUT 253,5: OUT 252,2: POKE 40960,222
60 OUT 253,5: OUT 252,1: PRINT PEEK 40960
70 OUT 253,5: OUT 252,2: PRINT PEEK 40960
80 OUT 253,5: OUT 252,0: OUT 244,0
90 PRINT PEEK 40960
