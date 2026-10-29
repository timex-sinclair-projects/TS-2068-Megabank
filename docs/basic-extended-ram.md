# Extended RAM for BASIC (exploration, draft)

How 2068 BASIC could use the Megabank's RAM directly, the way BASIC XE used
the 130XE's banks and BASIC 7.0 used the C128's second bank. The direction
is settled ([Decisions so far](#decisions-so-far)); the design is not. It
sits beside the [specification](spec.md) and assumes its board, registers
and RAM disk.

## Contents

- [Summary](#summary)
- [Decisions so far](#decisions-so-far)
- [What other BASICs did](#what-other-basics-did)
- [What we have to work with](#what-we-have-to-work-with)
- [The options](#the-options)
- [EXTEND: the program in banks](#extend-the-program-in-banks)
- [2068-Leap](#2068-leap)
- [The TS-Pico](#the-ts-pico)
- [Open questions](#open-questions)
- [Sources](#sources)

## Summary

**Recommended: three stages, each useful on its own.**

1. **Explicit use, no interpreter changes.** The RAM disk and `r:`
   channels the spec already plans, plus overlays (`MERGE` from `r:` keeps
   variables) and LOAD/SAVE `CODE` straight to board pages. Nearly free
   once phases 4, 5 and 9 exist.
2. **EXTEND: the program text in banks, everything else in HOME RAM.** The
   BASIC XE model. A program can grow to hundreds of K, and variables,
   strings and arrays get nearly all of the 2068's 38K. **The 2068 ROM
   already half does this:** its AROS cartridge path runs a BASIC program
   held in another bank, a line at a time through a buffer in HOME RAM, and
   the HOME ROM tests `ARSFLG` bit 7 at every place where execution moves to
   another line. EXTEND **shares** that code with the cartridges that still
   use it (Pro/File 2068 is one): the AROS code's calls to the bank
   routines go through three jump vectors that lead to the stock routines
   for a cartridge and to EXTEND's routines for a program on the board.
3. **Banked arrays, maybe, later.** `DIM` in the board's pages through a
   fixed window. It touches the calculator and costs 8–16K of HOME RAM for
   the window. Worth doing only if stage 2 proves out.

**Not recommended:** variables in a second bank (the C128 model), or
widening every BASIC pointer to page and offset (the SAM Coupé model). Both
mean rewriting much of the interpreter, and the TS-Pico's HOME ROM has no
room for it.

**2068-Leap** replaces both ROMs with its own language, so it never runs
alongside any of this. What it needs from us is a board-level contract that
doesn't depend on the TS-Pico: detection, and page ownership recorded on the
board. It could then use the board through one of its own packages.

## Decisions so far

From David, 2026-10-29:

- **Stage A first, then EXTEND.** Ship the explicit features; design
  EXTEND in full once stage A exists and its costs are measured.
- **The work lives in this repo** as a design and a patch set, and goes to
  the TS-Pico project once it runs in the emulator.
- **AROS cartridges keep working.** BASIC AROS cartridges shipped and are
  still in use: Pro/File 2068 (Thomas B. Woods, 1984) is one (its header:
  language 1, AROS, start 800Ah, autostart), and David's Pro/File v6 plans
  to be a cartridge too. David also names Tom Wood's programmer's toolkit
  and Timex's States & Capitals; whether those are BASIC or machine code
  AROS is unchecked (open question 2).
- **EXTEND shares the AROS code through RAM vectors**, rather than taking
  over its bytes ([Running](#running)). Taking the function dispatcher's
  RAM (6200h–62ADh, about 174 bytes) was considered and set aside: it
  would break any program that calls the dispatcher.
- **New commands are `tpi:` strings**, answered by the firmware with
  operations the ROM runs, the way the mover works. No new HOME hooks for
  them.
- **Page ownership is a simple table on the board** that a few dozen
  bytes of Z80 can read without the TS-Pico, so Leap can respect it.
- **EXTEND supports 64-column mode.** This puts the vectors in chunk 2,
  which never moves, and fixes the AROS path's own 64-column fault on
  the way ([64-column mode](#64-column-mode)).
- **Charlie Day hears about it once the allocation table is settled**, so
  there is a concrete contract to offer a Leap package.

## What other BASICs did

| BASIC | What goes in the extra RAM | How it reaches it | Cost |
|---|---|---|---|
| **BASIC XE** (Atari 130XE), `EXTEND` | Program text only, in four 16K banks. Variables, strings, arrays and the stack stay in main RAM. | A 16K window at $4000. A line never spans two banks; every bank keeps at least 256 bytes free. | Reviewers measured no slowdown. String literals are copied out of the bank, so `ADR("…")` of a literal and machine code kept in literals break. Extended programs don't load in the plain BASIC. |
| **BASIC 7.0** (C128) | Program in bank 0, all variables in bank 1. | Fetch and store routines in common RAM switch banks on every access. | About 45% slower than a C64 at 1 MHz (a forum benchmark). `BANK` selects what `PEEK`, `POKE` and `SYS` see. |
| **SAM Coupé BASIC** (a Sinclair dialect) | Program and variables in any 16K pages. | Every area pointer is 3 bytes, page and offset. Two pages are always mapped side by side, so anything up to 16K is contiguous. | The interpreter was designed around it from the start; early ROMs were buggy once BASIC began paging. |
| **Spectrum 128, MSX 2** | RAM disk only (`SAVE!`, `CALL MEMINI`). | Commands. | No gain in program or variable space. |
| **BBC Master BAS128** | Program and data in four 16K sideways banks. | Paged on access. | Acorn called it "much slower". |

Rules worth borrowing: XE's never-split lines and per-bank slack, XE's
separate free-space reports for program and data, SAM's double-width
window so that any object up to one window is contiguous, and XE's lesson
that a program saved in extended mode needs its own file type.

## What we have to work with

**BASIC on the 2068.** Program, variables, edit line, workspace and
calculator stack run in one block from `PROG` (6856h) to `RAMTOP` (FF57h):
about 38K over chunks 3–7. `MAKE-ROOM` and `POINTERS` move everything above
an insertion, and every area pointer is an absolute 16-bit address. The
machine stack and the bank code sit in chunk 3 at 6000h–683Fh, below BASIC,
not at RAMTOP as on a Spectrum. GO TO, GO SUB, FOR/NEXT, CONTINUE and
`ON ERR` all record **line numbers**, not addresses, which makes moving the
program elsewhere far easier than on BASICs that cache line addresses.

**The ROM's AROS path.** Timex let a cartridge hold a BASIC program (an
AROS) and run it from the DOCK bank with variables in HOME RAM. With
`ARSFLG` (5CC6h) bit 7 set:

- **Moving to the next line** (ENDTEM 1B4Ah, EXECUTE 1B00h, STOP's path)
  jumps to `AR_NXT` (17FFh). It copies the line out of the cartridge with
  `XFER_BYTES` into `ARSBUF`, a buffer in HOME RAM (pointer at 5CC4h, 208
  bytes, grown for longer lines), and runs it from there.
- **Jumps** (GO TO, GO SUB, RETURN, NEXT; 1AC0h) go to `AR_LN` (17EAh),
  which searches the cartridge for the line (`GETAL`, 17CFh).
- **READ** has its own AROS branch (1DADh–1E51h), with the current DATA
  line kept in `ADATLN` and its length in `DTLNLN` (5CC7h–5CCAh).
- **CLEAR and RUN** (CLR_BC, 1F41h) point `DATADD` at the cartridge's first
  line.
- **Entering a numbered line** is refused (`JP NZ,1BEDh` at 1A37h: BAD
  BASIC).
- **POINTERS** moves `ARSBUF` with everything else (12CAh).

The code behind these tests, 17B5h–1944h (400 bytes) and READ's
1DADh–1E51h (165 bytes), is reached only with an AROS cartridge, and such
cartridges are still in use, so it stays. It reaches the cartridge through
three RAM routines at fixed addresses: `BANK_ENABLE` (6499h, 15 calls),
`XFER_BYTES` (6722h, 2 calls) and its own line search `GETAL` (17CFh, 3
calls). The TS-Pico took some other dead AROS bytes for itself
(24C5h–24CEh, the stream dispatch). A few
statements test the cartridge's type byte, SYSCON+1, instead of `ARSFLG`:
FOR, RESTORE and USR ([spec](spec.md#what-the-rom-needs-from-syscon)).

**The board.** 4 MB in 512 pages of 8K. Any page into any chunk with two
register writes, `OWNn` then `SUBn` (four `OUT`s). Reset leaves every chunk
to the 2068. The mover already has a rule for borrowing a chunk: a
**window** from 4–7 that holds neither the stack nor the bytes being
copied, saved and restored around the access.

**The TS-Pico ROM** (details in [The TS-Pico](#the-ts-pico)). HOME has
practically no free bytes; EXROM chunk 1 has 3116 + 2123 free, which the
mover and `r:` will share. New statements arrive as `tpi:` command strings,
not new keywords. A crossing from HOME into the EXROM costs a CALL_BANK, so
anything run on every line should not need one.

## The options

| | What moves to the board | Interpreter changes | HOME RAM for data | Compatibility | ROM cost |
|---|---|---|---|---|---|
| **A. Explicit** | Files, records, overlays, CODE blocks | None | Unchanged (38K) | Total | Already planned, plus small firmware work |
| **B. EXTEND** | The program text | About 20 AROS call operands repointed at vectors, about 15 more call sites redirected | Nearly all 38K | Programs that keep machine code in REM lines, or `PEEK`/`POKE` their own lines, break in EXTEND mode only | No new HOME code; a few hundred bytes of RAM while EXTEND is on; 1.5–2.5K in EXROM chunk 1 (estimate) |
| **C. Banked arrays** | Arrays declared to be banked | The array element path in the calculator | 38K minus an 8–16K window | Only programs that ask for it | Unknown; larger than B |
| D. Banked variables (C128) | All variables | Every variable access | All of it for the program | Breaks `PEEK`ing variables, string pointers | Far more than the ROM has |
| E. Page:offset pointers (SAM) | Everything | All pointer arithmetic, MAKE-ROOM, SAVE/LOAD | Unlimited | Breaks machine code that reads system variables | A rewrite |

### A. Explicit use

What the spec's phases 4, 5 and 9 give, plus three cheap additions:

- **Overlays.** Sinclair `MERGE` keeps variables, and from `r:` it takes a
  moment, not a tape load. A program split into parts can bring the next
  part in with `MERGE "r:part2"`. A `tpi:` command that first deletes a
  line range would make a proper CHAIN.
- **Page files.** `SAVE "r:@12" CODE 49152,8192` and `LOAD "r:@12" CODE`
  name a board page instead of a file, using mover ops 1 and 2. Screens,
  sprite sheets and tables move in and out at `LDIR` speed. Firmware only.
- **Workspaces.** Copy the whole BASIC area (6840h to RAMTOP) to pages and
  back: several programs resident at once, switched in about a quarter of a
  second. Firmware plus the mover.

### B. EXTEND

The program text lives in board pages and runs a line at a time through
`ARSBUF`, exactly as an AROS program does; variables and everything else
stay where they are. Described in full [below](#extend-the-program-in-banks).

### C. Banked arrays

`DIM a(100000)` with a marker that puts the array in board pages. The
element lookup maps the right page into a fixed window and returns an
address inside it; numbers go onto the calculator stack at once, so the
window only has to hold still for the rest of that access. String arrays
are harder: a slice of an element is a pointer the expression keeps using,
and a second banked access in the same expression could remap the window
under it. The window also has to come out of HOME RAM (lower RAMTOP below
E000h and move the UDGs), and a two-chunk window (SAM's trick) is needed to
keep a string element up to 8K contiguous. **Deferred until B exists.**

### D and E

Both put pages behind the interpreter's own pointers, so every routine
that follows a pointer into the variables would need to map first. SAM BASIC
shows it can be done, but it was designed that way from the start.
Retrofitting it into the 2068's ROM, with no free HOME bytes, is a
different language implementation.

## EXTEND: the program in banks

**Proposed, for discussion.**

### The program's pages

- **Lines are kept in the ROM's own format**: line number (2 bytes,
  high byte first), length (2), text, `0Dh`. The interpreter runs them
  unchanged.
- **A line never spans two pages** (the BASIC XE rule), so one mapping
  reaches a whole line. A line longer than a page, about 8K, is refused.
- **Each page keeps slack**, at least 256 bytes after EXTEND or
  compaction, so most edits stay inside one page.
- **A page index in HOME RAM**: for each program page, its board page
  number and its first line number (4 bytes). It lives in the block that
  holds `ARSBUF`, above it, so `POINTERS` moves it too. 64 pages (512K of
  program) need 256 bytes.
- **Pages come from the RAM disk's allocator** (`tpi:ram alloc`, open
  question 8 in the spec), so programs and files never collide, and the
  allocation survives NEW and reset.

### Running

`ARSFLG` bit 7 set sends execution into the AROS code, for a cartridge or
for EXTEND alike. That code stays as it is; what changes is where its bank
calls lead.

- **Vectors.** A handful of 3-byte jumps in the reserved system-variable
  area of chunk 2: `V_BANK`, `V_XFER` and `V_FIND` for the AROS code, and
  three more for the hooks outside it (`FIND_L`'s callers, `LOOK-PROG`'s,
  and line entry). The AROS code's 20 calls to `BANK_ENABLE`, `XFER_BYTES`
  and `GETAL` get their operands changed to the vectors: the same length,
  no new HOME code. At boot the vectors lead to the stock routines, so a
  cartridge runs as before. EXTEND repoints them at its own code; NEW puts
  them back.
- **`V_BANK`** with bank 00 (the DOCK) maps the current program page into
  the window instead. If `NXTLIN` has reached the end of a page, it first
  moves it to the start of the next: `AR_NXT` reads `NXTLIN` straight after
  this call. With bank FFh (HOME) it restores the window chunk.
- **`V_XFER`**: the line is already visible in the window, so the copy into
  `ARSBUF` is a plain `LDIR`.
- **`V_FIND`** (GO TO, GO SUB, RETURN, NEXT, RESTORE n): find the page in
  the index by its first line number, map it, and scan that page alone. For
  a large program this is **faster than the stock `FIND_L`**, which scans
  from the start every time. (That's why Sinclair programmers put
  subroutines at the top.)
- **The position** of the next line is a window address in `NXTLIN`, as the
  AROS code expects, plus one new system variable for its page-index slot.
- **The window** is the mover's: a chunk from 4–7 holding neither the
  stack nor `ARSBUF`. The stock IM 1 handler touches only chunks 2 and 3
  and never selects a board register, so **interrupts can stay on**: no
  `FRAMES` ticks lost, the problem 2068-Leap ran into with long `DI`s.
  (A user's IM 2 handler that uses the board must save `IN (FDh)`, as the
  spec already says.)
- **Cost**: about 300 T-states plus 21 per byte of the line, around
  0.3 ms for a 30-byte line. An ordinary Sinclair BASIC line takes several
  milliseconds to interpret, so expect a slowdown of perhaps 5–10% in tight
  loops of short lines, less elsewhere. (Estimate; measure in the
  emulator.)
- **EXTEND's routines run from RAM**: a block copied from EXROM chunk 1
  when EXTEND starts, kept beside `ARSBUF` with the page index. No crossing
  to the EXROM per line. `POINTERS` moves that block with everything else,
  so the code must either be position-independent or be found through
  `ARSBUF` on every entry (open question 5).

### Editing and listing

- **Entering a line.** The syntax check is unchanged (the edit line is in
  HOME). The refusal's `JP NZ` at 1A37h is repointed at a vector that
  leads to BAD BASIC (1BEDh) as now, for a cartridge, and in EXTEND mode to
  the banked insert: find the page, open a gap inside it (a move of at most
  8K, through the window), or
  split the page when the slack runs out. Deleting a line is the reverse.
  Worst case about 50 ms.
- **LIST, the automatic listing, EDIT.** Each copies a line into a buffer
  and hands the buffer to the stock line printer or the edit line, instead
  of pointing into `PROG`. The listing loops step through pages instead of
  adding line lengths to a pointer.
- **Where the hooks go.** `FIND_L` has 13 callers: INSA, DELSYM, UNKEDIT,
  TSLIST (three), K_LIST, NEXT_L, EXECUTE, RESTORE (two) and `DELETE`
  (two, from 2105h). `LOOK-PROG` (1D28h), which scans the program for a
  token, has two more that matter: FOR and FN. Each that matters has its
  `CALL` operand repointed at a vector, which leads to the stock routine
  except in EXTEND mode. The TS-Pico reports say to patch callers, never
  the routines: their entries are build anchors.

### DATA, RESTORE, FOR, DEF FN, USR

- **READ** takes its existing AROS branch. Through `V_BANK` and `V_XFER`
  it maps the DATA line's page and copies the line out, keeping its
  position in `ADATLN`/`DTLNLN` as it already does for a cartridge.
- **CLEAR and RUN** set `DATADD` from SYSCON+2 in AROS mode. EXTEND writes
  a marker there meaning "the first program line". It never sets SYSCON+1
  to 02, so the type byte stays clear.
- **RESTORE n and FOR** test SYSCON+1, not `ARSFLG`, so they take the normal
  path and would search HOME. They get redirects too: RESTORE at its
  `FIND_L` call, FOR at its search for the matching NEXT (`CALL 1D28h` at
  1CFBh).
- **DEF FN** is found by scanning the whole program from `PROG` on every
  `FN` call (`CALL 1D28h` at 2BCAh, looking for token CEh), so that call is
  redirected too. A banked scan of a large program on every `FN` would be
  slow; caching each DEF FN's position at RUN would fix it.
- **`DELETE m,n`** (2068) finds both ends with `FIND_L`; in EXTEND mode it
  deletes across pages.
- **USR** needs nothing. An address in program text makes no sense in EXTEND
  mode, and the cartridge branch stays off because SYSCON+1 isn't 02.

### Getting in and out

- **`EXTEND`** (direct mode only, as in BASIC XE) moves the program from
  `PROG` into pages, builds the index, sets `ARSFLG` bit 7, and gives the
  freed HOME RAM to variables. It arrives as a `tpi:` command whose Z80
  side runs in EXROM chunk 1. How a `tpi:` command starts Z80 code is open.
- **NEW** returns to normal mode and frees the pages.
- **SAVE** in EXTEND mode writes a new file type to `r:` or `tpi:`: the
  index and the pages. **Tape is refused**: a tape block holds at most 64K,
  and a program bigger than 38K couldn't load into a normal 2068 anyway.
  `LOAD` of such a file re-enters EXTEND mode. `LOAD` of an ordinary program
  leaves it, as in BASIC XE.
- **MERGE** into an extended program goes line by line through the insert.
- **A docked BASIC AROS cartridge and EXTEND exclude each other**: both
  use `ARSFLG` and `ARSBUF`. EXTEND refuses to start while SYSCON+1 is 02.

### 64-column mode

- **The bank code moves.** Opening the second display file moves
  6000h–683Fh (stack, dispatcher, bank code) up by 97C0h to F7C0h–FFFFh, so
  `BANK_ENABLE` goes from 6499h to FC59h and `XFER_BYTES` from 6722h to
  FEE2h. The HOME ROM's AROS code calls the low addresses whatever the
  mode, so **AROS cartridges already fail in 64-column mode**.
- **The vectors fix that.** They sit in chunk 2, which never moves, and
  their stock targets check `VIDMOD` and jump to whichever copy is live. Pro/File
  2068 should then work in 64-column mode too: worth a test.
- **CHANS and PROG move** to 7B00h. `POINTERS` moves `ARSBUF`, and EXTEND's
  RAM block with it; whether OPEN-DFILE goes through `POINTERS` is
  unverified.
- **The window** then comes from chunks 4–6, since the stack is in chunk 7.

### What breaks in EXTEND mode

Only programs that assume their text lives in `PROG`:

- machine code in a REM line (`USR (PEEK 23635+256*PEEK 23636+5)`);
- programs that `PEEK` or `POKE` their own lines;
- reading `NXTLIN` or `CH_ADD` to find the program.

All of these keep working in normal mode, which is the default.

### ROM budget (estimates)

| Where | What | Bytes |
|---|---|---|
| HOME, AROS code | 20 call operands repointed at the vectors | none added |
| HOME, elsewhere | about 15 call operands (`FIND_L`'s and `LOOK-PROG`'s callers) and the `JP NZ` at 1A37h repointed | none added |
| RAM, chunk 2 | about six vectors, the `VIDMOD` checks for their stock targets, the slot variable | ~30–40 |
| RAM, beside `ARSBUF`, only while EXTEND is on | line fetch, page search, window map, the DATA buffer | a few hundred, plus the page index (4 per page) |
| EXROM chunk 1 | EXTEND itself, insert/delete/split, the LIST loop, SAVE/LOAD/MERGE, and the RAM block's image | 1.5–2.5K of the 5239 free (37B5h–3FFFh is inside the 4K the TS-Pico's fdd module claims) |

## 2068-Leap

[2068-Leap](https://github.com/nchiker/2068-Leap) (Charlie Day) is a new
language for the 2068, not an extension of Sinclair BASIC: its own 16K HOME
ROM and 8K EXROM, a full-screen editor without line numbers, and a bytecode
VM that compiles the program on every `RUN`. Release v2.1.0-emu.1
(2026-10-27) is qualified on ZEsarUX and not yet on hardware.

**It never runs alongside any of the above.** Leap and the TS-Pico ROM are
separate ROM sets; a 2068 runs one or the other. "Coexisting" means sharing
the board, the 2068 and the RAM disk's contents across a ROM swap.

What that asks of us:

- **The board must not depend on the TS-Pico or SYSCON.** Leap has no
  system variables at 5C00h; SYSCON's addresses (5CBCh, 5EEAh) fall in
  memory Leap leaves uninitialised. Detection is by `ID`/`IDCOMP` only,
  which the spec already requires.
- **Page ownership must live on the board.** The RAM disk's superblock and
  the program allocations (`tpi:ram alloc`) should be readable by a few
  dozen bytes of Z80, with no TS-Pico, so a Leap program can find free
  pages without trampling files. That argues for a simple allocation table
  in the superblock's page rather than a list only the firmware
  understands.
- **Reset to transparent stays.** Leap boots unchanged with the board
  fitted, and it never touches FCh/FDh (only a user `OUT` could).
- **Never map chunks 0, 1 or 6 under Leap.** The board's /BE wins over the
  2068's HSR, so owning chunk 6 would hide Leap's EXROM while it runs.

What Leap could do with the board, if Charlie wants it:

- **It needs the space most.** Leap's main pool is about 9K
  (825Ah–A654h), and its ROMs have 313 and 111 bytes free, so any support
  would come as a **package**, one of its tape-loaded 1K modules at F400h,
  not a ROM change.
- **Chunk 3 (6000h–7FFFh) is the natural window**: Leap leaves it free
  unless `MODE 1` (extended colour) is on.
- Banked source text would suit Leap well, since its editor keeps plain
  ASCII lines; banked arrays would suit its 1-D integer arrays. Both need
  compiler changes, so they are Charlie's call.

## The TS-Pico

From `tspico-firmware-build`, `origin/main`, and the branch
`megabank-bus-sharing`:

- **HOME is full.** The free bytes are scattered: 2 at 3CFEh, NOP pads of 4
  and 5. EXTEND adds no HOME code, only changed call operands, most of them
  in AROS code that ROM 2.3 leaves untouched (its 17 changed ranges don't
  include 17B5h–1944h or 1DADh–1E51h).
- **The vectors need 30–40 bytes in chunk 2's reserved area** (5CCCh–5EE9h),
  where the TS-Pico uses 5D37h and 5DCDh–5DDBh. Agree the address with
  Gustavo (with spec open question 17).
- **EXROM chunk 1** has 23D4h–2FFFh (3116 bytes) and 37B5h–3FFFh (2123),
  shared with the mover and `r:`. The module enters through vectors at
  X3000–X301E; EXTEND's cold paths would add vectors.
- **Hooks are redirects at callers.** MAKE-ROOM (12BBh), RECLAIM (1750h)
  and SELECT (1230h) are anchors in `tools/build-rom.py`; their entry bytes
  must not change.
- **CHANS stays directly below PROG.** `CH_OPEN_HOOK` finds CHANS' end
  marker at `(PROG)−1` or `−2`. `ARSBUF` and the page index sit below CHANS
  (inserted at 6840h, as the AROS code does), so the rule holds.
- **Crossings.** `DI` around every HOME→EXROM call; `GUARDED` and H_TRAP
  restore the bank stack after an error. A thunk clobbers 5DCDh.
- **MAXBNK stays 0.** EXTEND uses the ports directly, like the mover, and
  never the Timex bank API.
- **SAVE, LOAD and MERGE** already pass through the TS-Pico's SAVE-ETC hook
  (2548h → X01ABh), the place to recognise an extended program.
- **The v3 card yields owned chunks** on `megabank-bus-sharing`
  (`src/tsbus/megabank.c`): any chunk the board owns is not served from
  the DOCK or EXROM. On `main` the card already never serves HOME RAM
  chunks 2–7, so windows in 4–7 are safe with either.
- **No reservation of 5DDDh–633Fh** appears in the firmware repo. The
  claim comes from the reference library's analysis of Gustavo's ROM
  (open question 17 in the spec).

## Open questions

1. **Handing over.** When the patch set runs in the emulator, who on the
   TS-Pico side takes it into `tools/build-rom.py`, and as which ROM
   version?
2. **The other AROS cartridges.** Are Tom Wood's toolkit and States &
   Capitals BASIC or machine code AROS? Their dumps would say (byte 0 of
   the header at 8000h). Test Pro/File 2068 with the vectors in place, in
   both video modes.
3. **The `tpi:` reply.** How the firmware's answer to `tpi:basic extend`
   (and CHAIN, workspaces) starts EXTEND's Z80 code: a new mover operation
   that names an EXROM entry, perhaps.
4. **The allocation table's format** (with spec open question 8): one
   owner byte per page, and the owner codes (free, RAM disk, EXTEND
   program, `tpi:ram alloc`, Leap, other software).
5. **EXTEND's RAM code moves** whenever `POINTERS` moves `ARSBUF`.
   Position-independent code, or a fixed stub in chunk 2 that finds it
   through `ARSBUF`?
6. **The vectors' address** in 5CCCh–5EE9h, agreed with the TS-Pico (spec
   open question 17).
7. **Measure** the per-line cost and the page-split cost in ZEsarUX before
   designing EXTEND in full.
8. **Stage A's firmware work**: the line-range delete for CHAIN, the
   `r:@n` page names, workspaces. All in tspico-firmware-build.

## Sources

- The 2068's HOME and EXROM disassemblies and system-variable notes in
  David's TS2068 reference library (`ts2068_home_rom_U16_stock.txt`,
  `ts2068_exrom_U20_stock.txt`, `2068_DEFS.ASM`,
  `docs/ts2068_system_variables.md`, `docs/ts2068_memory_map.md`).
- tspico-firmware-build, `origin/main` and `megabank-bus-sharing`:
  `src/rom/TSPICO-23.ROM`, `docs/reference/rom/home.md`,
  `docs/reference/rom/exrom-fdd.md`, `tools/build-rom.py`,
  `src/tsbus/tsbus.c`, `src/tsbus/megabank.c`.
- 2068-Leap, branch `v2-development` at b30ca0c: `README.md`,
  `docs/manual/04_concepts.md`, `docs/manual/07_appendices.md`,
  `include/v2_layout.inc`, `kernel/bank/bank.asm`, `docs/PACKAGE_ABI.md`.
- OSS, *BASIC XE Reference Manual*, pp. 37–38 and appendices B and D
  (atarimania.com); reviews in *ANTIC* v4n9 and *COMPUTE!* 67.
- Commodore, *C128 Programmer's Reference Guide*, chapter 13 and the
  memory map (pagetable.com); a BASIC 7.0 benchmark thread on
  commodore-128.org.
- MGT, *SAM Coupé Technical Manual* v3.0, pp. 27–29 and 56–77
  (worldofsam.org).
- Acorn, application note 041 (BAS128); the Spectrum 128 manual, chapter 9;
  the MSX wiki on the memory disk.
