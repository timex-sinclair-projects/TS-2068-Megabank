# Megabank controller firmware

The board's real firmware (docs/spec.md, "The board", "Registers", "The
controller"), first version. It answers ports FCh/FDh, keeps the registers,
and maps the board's 4 MB into the 2068's memory 8K at a time.

## How it works

The bytes never go through the RP2350B: on a memory cycle the SRAM and the
Z80 talk through U3, whose direction follows /RD. The controller decides,
per cycle, whether to answer and which page the SRAM sees.

| Program | Where | Does |
|---|---|---|
| `be` | PIO0, offset 0 | On each memory cycle that isn't a refresh, jumps through an 8-entry chunk table on A15-A13. For a chunk the board owns: /BE and U3 on until /MREQ rises. |
| `page` | PIO1 (GPIO base 16) | On the same cycles, pushes the address of the chunk's page-table entry; a DMA chain reads it back and it goes out on GP28-GP38 (both /CE1s, PA13-PA20, WINH). Idle: all high. |
| `io` | PIO0, offset 16 | Hands every I/O cycle to core 1, which answers with a handler: drive a reply byte (IN FCh/FDh), capture the data (OUT FCh/FDh), or nothing. |
| core 1 | RAM | The registers; rewrites the `be` table and the page table on each register write, and on /RESET; follows the write-protect switch. |
| core 0 | | A USB console. |

`be` and `io` fill PIO0's 32 instructions. Clock: 250 MHz, as the v3 card
runs; the flash divider goes from 2 to 3 first (83 MHz), from RAM, since the
boot divider would run the GD25Q128E at 125 MHz. The core runs from the
board's 1.2 V LDO, so there's no regulator to raise.

PIO2 holds a logic analyser for the board's own timing (console `c`): `la`
samples GP16-GP47 into a 16 KB DMA ring every 8 ns, `trig` flags a write to
an owned chunk, and core 0 stops the ring 8 us later and prints that cycle
(and the last mapped read in the ring) as a waveform and a list of edges.
`C` triggers on a mapped read instead and moves the window to GP0-GP31, so
it shows the data lines on the SRAM's side of U3 (SD0-SD7) and A7-A0, but
not WINH or the bus's /BE.

**Chunks 0 and 1 are locked** by default: the TS-Pico serves HOME and EXROM
there with its own /BE, and until its firmware follows the owner table both
cards would drive the bus. OWN0/OWN1 read back what was written; the board
never answers there. `k` toggles the lock.

Not yet: `BASE` and `SIZE` from flash (fixed at 1 and 64); the battery bit
is read once at boot.

## Build and flash

```bash
export PICO_SDK_PATH=~/pico/pico-sdk
export PICO_TOOLCHAIN_PATH=~/pico/arm-gnu-toolchain-13.3.rel1-darwin-arm64-arm-none-eabi
cmake -S . -B build -Dpicotool_DIR=/opt/homebrew/lib/cmake/picotool
make -C build -j8
picotool load -f -x build/megabank.uf2
```

## Console

`screen /dev/cu.usbmodem101`, then a key: `r` registers and counters, `l`
the last 64 register writes, `d` diagnostics (the state machines, the DMA
chain, the page table, which page pins moved in 50 ms), `v` battery, `k` the
chunk 0-1 lock, `c` capture a mapped write, `C` a mapped read with the data lines, `mCB` OWN of chunk C = bank B, `u` reboot to BOOTSEL.

## From BASIC

Detection: `OUT 253,18: PRINT IN 252: OUT 253,19: PRINT IN 252` prints 165
and 90.

Two banks in chunk 5 (A000h), with RAMTOP below it:

```
10 CLEAR 40959
20 OUT 253,5: OUT 252,1: POKE 40960,111
30 OUT 253,5: OUT 252,2: POKE 40960,222
40 OUT 253,5: OUT 252,1: PRINT PEEK 40960
50 OUT 253,5: OUT 252,2: PRINT PEEK 40960
60 OUT 253,5: OUT 252,0: PRINT PEEK 40960
```

That prints 111, 222, 222: the last is the leak. With the chunk in DOCK
(the v3 card's DOCK serving off, or its firmware following the owner table):

```
10 CLEAR 40959
20 POKE 40960,77
30 OUT 244,32
40 OUT 253,5: OUT 252,1: POKE 40960,111
50 OUT 253,5: OUT 252,2: POKE 40960,222
60 OUT 253,5: OUT 252,1: PRINT PEEK 40960
70 OUT 253,5: OUT 252,2: PRINT PEEK 40960
80 OUT 253,5: OUT 252,0: OUT 244,0
90 PRINT PEEK 40960
```

should print 111, 222, 77 (run with DOCK serving on, it printed 1, 0, 77; the
version run with it off wrote in DOCK, read back HOME and in DOCK, and passed).

## Results

**Board 1, 2026-11-18** (TS2068_Extender: v3 card in slot 1, this board in
slot 2, SW2 closed):

- Detection: 165, 90. Every I/O cycle answered in time (about 250,000 over
  the session, none dropped).
- Register writes arrive as written (the console's log).
- Two banks: 111, 222, 222. Reads come from the board's SRAM, the banks are
  separate, and **writes also land in the 2068's own RAM** under the
  mapped chunk (the last line: the 2068's RAM holds 222). Spec, open
  question 2.
- At 250 MHz (v2): still 111, 222, 222. The capture (`c`, with
  `80 POKE 40960,PEEK 40960: GO TO 80` running), in ns from /MREQ falling:
  /BE low at +40, /CE1_0 and WINH at +80, /WR low at +280, /MREQ and /WR
  high at +560, /BE high again at +640. /BE is down well before /WR, so the
  write leak is the 2068's: its HOME RAM takes a write even with /BE low.
- With the chunk in DOCK as well (`OUT 244,32`), writes reach the SRAM and
  the 2068's RAM keeps its byte, but reads came back wrong: the v3 card
  serves DOCK, and its released firmware doesn't yet follow the owner table.
  With the v3 card's DOCK serving off (`tsbus.dock(False)` at its REPL),
  everything passes: 111, 222, 77 (the 2068's RAM untouched), 33. So a
  mapped chunk needs its HSR bit set (spec, "Sharing the bus").
- First run, before a fix: the DMA chain never ran (an endless transfer
  count of 0; the RP2350 needs a non-zero count with the endless mode), so
  the SRAM was never selected and mapped reads returned whatever the data
  lines last carried.
