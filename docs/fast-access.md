# Faster access to the board (design note, draft)

Three speed-ups, for three different users of the board. It sits beside the
[specification](spec.md) and assumes its registers, RAM disk and channels.
Speed-up 1 is built on a branch of tspico-firmware-build (`claude/print-stub`,
the stub in the record with a fix-up at `CLOSE #`, a 57-byte buffer): 396 T a
character measured in ZEsarUX against 829 before, and on the board hwcheck
passes (39 of 39) at 72 µs a character against 210 (tspico-firmware-build PR
435, 2026-11-20).
The other two are not built. Figures marked *measured* come from ZEsarUX with the
Megabank model and the ROM of tspico-firmware-build PR 434; figures marked
*counted* are T-states added up from the instructions; the rest are
estimates and say so.

| # | For | What | Now | After |
|---|---|---|---|---|
| 1 | `PRINT #` to `r:` | characters collect in the channel record, in RAM; the ROM is entered once per line or buffer | 851 T a character (measured) | 396 T (measured by `chan_speed.py`, which gives 829 for the old path) |
| 2 | Programs moving blocks through a stream | one call: address and length, one map, one `LDIR` | 851 T a byte | 21 T a byte plus a fixed cost per call |
| 3 | Video frames | no file system: banks mapped at 8000h and up, copied to the display file | not built | 13 to 21 T a byte, plus contention |

**Recommended order:** 3 first if Jon is waiting on it (it needs no ROM or
firmware change), then 1, then 2.

## Where a character's 851 T-states go

One `RST 10h` with a character of 32 or more, to stream 4 open on an `r:`
file for writing, the cursor already set up. Measured with ZRCP breakpoints
that copy ZEsarUX's T-state counter at each address (`tools/emu/chan_speed.py`'s
method, one pair of addresses a run).

| Stage | From | T | |
|---|---|---|---|
| Timex's `RST 10h` and PRINT-A-2, to the record's stub | the `RST` to 14A0h | 146 | Timex's |
| The stub: `LD HL,3009h` / `JP FASTH` | 14A0h to 1265h | 20 | crossing |
| FASTH: `DI`, test F4h, set FFh bit 7 | 1265h to 128Bh | 105 | crossing |
| The switch in: `SW_IN`, the landing, the chunk 1 check | 128Bh to 2400h | 51 | crossing |
| `FX_ENTRY`: which vector, which kind of record | 2400h to 271Fh | 102 | work |
| `RCH_FOUT`: the cursor, map chunk 0, the byte, unmap | 271Fh to 242Bh | 263 | work |
| `PUSH AF`, F4h, to `SW_OUT` | 242Bh to 128Fh | 39 | crossing |
| The switch out | 128Fh to 127Ch | 25 | crossing |
| `FH_BACK` (66, counted) and PRINT-A-2's tail (34) | 127Ch to the return | 100 | crossing, Timex's |
| **Total** | | **851** | |

So: **180 T is Timex's** (21%), and any channel pays it; **306 T is
crossing** into the EXROM and back (36%); **365 T is the work** (43%), of
which mapping and unmapping chunk 0 is about 95 (six `OUT`s and their loads,
counted).

`chan_speed.py` reports 829 for the same thing: it subtracts a `CALL` and
`RET` (27 T) as loop overhead.

**The board's 210 µs is probably low.** 851 T is 241 µs at 3.528 MHz, more
than hwcheck's 210 µs, which also includes BASIC. hwcheck times by FRAMES,
and the fast entry runs with interrupts off, so frame interrupts are lost:
the same run counted fewer frames with `PRINT #` (100) than without (106).
Treat 210 µs as a lower bound until it is timed another way.

## 1. `PRINT #`: collect characters in RAM

HOME has no free run of even 24 bytes, so the code cannot go there. It goes
in RAM.

**The stub.** 28 position-independent bytes (as built), reached straight from
PRINT-A-2 as the record's output address:

```
        ld   c,a
        cp   32
        jr   c,slow         ; CR, TAB, any control
        ld   hl,(CURCHL)
        ld   de,RR_FCNT     ; room left (0: no buffer)
        add  hl,de
        ld   a,(hl)
        and  a
        jr   z,slow
        dec  (hl)
        inc  hl
        ld   e,(hl)         ; bytes held
        inc  (hl)
        inc  hl             ; the buffer follows
        add  hl,de          ; D = 0
        ld   (hl),c
        ret
slow:   ld   a,c
        ld   hl,3009h
        jp   FASTH
```

About 140 T (counted). With Timex's 180 that is about 320 T a character, no
bank switch, interrupts on.

**The flush** is today's path, taken on a CR, a control, a full buffer,
`INPUT #`, `CLOSE #`: `RCH_SYNC` gains one map of chunk 0 and an `LDIR` of
the held bytes (21 T each) before it adds them to the position, length and
column. I counted the flush at about 35 T a character with a 64-byte
buffer, 355 T in all. **As built it measures 396 T by `chan_speed.py`,
against 829 for the old path: 2.1 times faster.** The buffer came out at 57
bytes, and a flush costs more than the count allowed: it is the whole old
path for one character, with `RCH_PUTC` and `RCH_FILL`, plus the copy. The
floor for any design is Timex's 180.

**What stays the same.**

- `RR_FCNT` keeps its rule, capped at the buffer's size: never past a
  256-byte boundary, the extent or the 8K page. So the flush never grows the
  file or crosses a page.
- What NEW and a reset keep: the length already goes back to the slot only
  at a CR and every 256 bytes. Bytes after the last CR are lost today too.
- The record is 256 bytes, 128 of them usable after its padding, and used
  43: that leaves 57 for the buffer beside the 28-byte stub.

**What is new, and open.**

- **Where the stub lives (decided in the build): in the record.** It moves
  when an earlier stream is closed, and the record's output address points
  into itself, so `CLOSE #` of an `f:` or `r:` stream fixes up the `r:`
  records after the one it removes (`RCH_FIX`). The other choice was one
  shared copy at a fixed RAM address; the 12 unused bytes at 648Bh are not
  enough, and no other fixed RAM was looked for.
- **`f:` records** could use the same stub. Not planned here.
- **An alternative with no buffer:** a RAM stub that sets F4h bit 0, maps
  chunk 0, writes the byte and puts F4h back. Around 200 T, nothing held in
  RAM. But it returns into HOME right after an `OUT (F4h)`, the late-switch
  case PR 434 exists for, so it needs its own wait loop. The buffer avoids
  the question.

## 2. A block entry for open streams

For machine code (ZXDesk, a loader) that has a buffer to write or fill.
`SAVE "r:name" CODE` and `LOAD` already move whole files by `LDIR` through
the mover; this is the same for a stream at its position.

- **In:** stream, address, length. **Does:** `RCH_SYNC`; for each piece
  inside one 8K page, map it in a window chunk as the mover does (the first
  of 4 to 7 holding neither the address nor the stack) and `LDIR`; then the
  position and length, and one write-back.
- **Inside the extent the ROM does it alone.** Past it, one `tpi:rchgrow`
  first, as `PRINT #` does now.
- **Cost:** 21 T a byte, about 160 KB a second, plus the entry, which is the
  fast entry's 306 T of crossing and the page arithmetic. Not measured.
- **Text translation is skipped:** bytes go as they are, as in a binary
  stream.

**Open: how it is reached.** A vector in the EXROM's table for machine code
is the minimum. From BASIC, a `tpi:` command form (no syntax change) or
nothing at all, since BASIC has `SAVE … CODE` for whole files. To be asked
of Jon with open question 8.

## 3. Video: map the banks and copy

A frame store should not go through files. Machine code writes `OWN` and
`SUB` itself ([Programming it](spec.md#programming-it)), has up to 32K of
the board at 8000h–FFFFh at a time, and copies into the display file. The
SCLD draws only from the 2068's own RAM, so every frame shown is a copy;
Jon's bus-mastering proposal ([revision 2](spec.md#bus-mastering-revision-2))
is the way round that, and is not this board.

**Sizes.** A standard screen is 6912 bytes. The extended colour mode is two
6144-byte files (4000h and 6000h): 12288 bytes, one and a half pages, so two
frames fill three chunks exactly. A 512K board holds 42 such frames, or 75
standard screens.

**Copy speeds**, counted, at 3.528 MHz, before contention:

| Method | T a byte | 6912 bytes | 12288 bytes | Notes |
|---|---|---|---|---|
| `LDIR` | 21 | 41 ms | 73 ms | interrupts may stay on |
| `LDI` × 16, `JP PE` | about 16.6 | 33 ms | 58 ms | interrupts may stay on; 35 bytes of code |
| Stack: `POP` × 7 from the bank, `LD SP`, `PUSH` × 7 to the screen | about 13 to 15 | 26–29 ms | 45–52 ms | interrupts off while SP is borrowed |

`POP` and `PUSH` alone are 10.5 T a byte, the figure I gave earlier; that
leaves out reloading SP twice a block and stepping the two addresses, which
a loop that isn't unrolled into kilobytes of code has to pay. 13 is about
the best in practice.

**Contention is on top.** By the TC2068 model the SCLD holds the Z80 off
4000h–7FFFh for the first half of each picture line, which leaves about 58%
of a frame ([open question 19](spec.md#open-questions)). A copy loop that
writes the display every few instructions stalls for most of the other 42%,
so plan on these times growing by up to 1.7 times: an extended-colour frame
in roughly 80 to 125 ms, 8 to 12 frames a second. The US SCLD is not
measured. This is the first thing to time on the board.

**Rules the routine has to follow** (all from the spec):

- Set the HSR bit (F4h) of each chunk it maps before writing through it,
  and put F4h back after.
- With the second display file in use the machine stack and the
  bank-switching code are in chunk 7: map sources in chunks 4 to 6 only, and
  keep the routine out of them.
- Frames live in banks below the RAM disk (`FORMAT "r:"` takes the top half
  by default). Reserving them properly is open question 8 (`tpi:ram alloc`).
- Standard mode can copy into the screen not shown and flip with FFh bit 0;
  the extended mode uses both files and cannot, so it tears unless the copy
  follows the beam.

**The first build:** a routine and a demo in `examples/`: load a few frames
from the SD card into banks, then show them in a loop, timing `LDIR`, `LDI`
and the stack copy on a real 2068. It works with the ROM and controller
firmware on the board today. Partial updates (changed cells only) are the
obvious next step and are where a 2068 gets past 12 frames a second.

## Open

1. ~~Where the `PRINT #` stub lives~~: in the record, with a fix-up at
   `CLOSE #` (built).
2. How the block entry is reached from BASIC, if at all.
3. The US SCLD's contention, on the board: it sets every video figure here.
4. A per-character time on the board that doesn't depend on FRAMES.
5. What Jon's extended colour work needs: full frames, partial updates,
   or something else. Nothing in this note comes from him.
