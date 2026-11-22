# TS-2068 Megabank (specification, draft)

A battery-backed 4 MB RAM board for the 2068, built the way Timex meant
memory to grow: as **expansion banks** that take over any 8K chunk of the
address space, above HOME, EXROM and DOCK. It is a plain card for a free slot
on the expansion bus, for owners of the TS-Pico v3 card. It gives the 2068
two things it never had:

- **Paged memory.** A program maps any 8K page of the 4 MB into any chunk
  with two `OUT`s and uses it at full memory speed: fonts, icons, window
  buffers, code overlays, a second screen's worth of work space.
- **A RAM disk.** `SAVE "r:name"`, `LOAD "r:name"`, `CAT "r:"` and the rest,
  as fast as the Z80 can copy, kept through power-off by a battery. Later,
  `OPEN #` streams on its files: random-access records from BASIC.

It began with David's question (2026-10-16) whether the TS-Pico could emulate
the LarKen 256K RAMdisk, and with Jon, a 2068 GUI author, asking for a RAM disk.
Decisions taken with David on 2026-10-16 are marked **(decided)**.

**Paths** like `src/tsbus/tsbus.c` or `src/rom/TSPICO-23.ROM` are in the
TS-Pico firmware repository,
[tspico-firmware-build](https://github.com/timex-sinclair-projects/tspico-firmware-build),
where the ROM and v3 firmware changes will be made.

**Status (2026-10-16):** specification only. No hardware, firmware or ROM
change exists yet. Facts quoted from the 2068 ROM are from `src/rom/TSPICO-23.ROM`;
those still to be checked on hardware are listed under [Open questions](#open-questions).

## Contents

- [Background](#background): the LarKen RAMdisk, Timex's expansion banks, and
  why this design follows Timex
- [The board](#the-board): what it does on the bus
- [Registers](#registers): ports FCh/FDh
- [Programming it](#programming-it): for people writing 2068 software
- [ZXDesk](#zxdesk): Jon's port of a GUI desktop, and a storage backend
  for it on the board
- [Hardware](#hardware): the RP2350B controller, parts for JLCPCB
  assembly, battery backup, timing
- [Sharing the bus](#sharing-the-bus): the v3 card, PicoVideo, BUSISO
- [ROM](#rom): what ROM 2.4 adds
- [The RAM disk](#the-ram-disk): `r:`, the mover, the on-board format
- [Channels](#channels-phase-9): `OPEN #n,"r:name"`, streams and records
  on the RAM disk
- [Firmware](#firmware): the v3 card's part
- [PicoVideo](#picovideo)
- [Bus mastering (revision 2)](#bus-mastering-revision-2): what DMA
  into the 2068's RAM would take; not this board
- [Testing](#testing)
- [Phases](#phases)
- [Open questions](#open-questions)
- [Sources](#sources)

## Background

### The LarKen RAMdisk (1980s)

LarKen Electronics' RAMdisk was the 2068's best-known RAM expansion: up to
eight 32K static RAM chips with battery backup, for the LKDOS disk system.

- One chip at a time appears in the **upper 32K of the DOCK bank**: the chip is
  enabled when /MREQ and /ROSCS are low and A15 is high.
- `OUT 7,n` picks the chip. Bits 0–2 select it (in a scrambled order LKDOS
  undoes with a table: 7=A, 3=B, 5=C, 1=D, 6=E, 2=F, 4=G, 0=H), bit 6 enables
  writes. The port is write-only and decoded from A7–A3 = 0 only, so it
  answers all of 00h–07h.
- A 74LS175 latch holds the bits and is cleared at power-up (chip H,
  write-protected). A divider on the 2068's +15 V blocks writes while power
  comes and goes.

We first looked at emulating it on the TS-Pico itself (2026-10-16):

- **v2 can't:** its port decode sees only 0Eh/0Fh, and its dock SRAM is a chip
  the Pico can't read back.
- **v3 could do 128K:** four spare 32K lanes in its image SRAM, switched by
  patching one PIO instruction. But 256K needs the unfitted PSRAM, and the
  result still lives in the DOCK, where it collides with cartridge images
  and ZX48 mode.

A separate board with real battery-backed SRAM is better on every count
**(decided)**. LKDOS compatibility is dropped **(decided)**: this is for v3
owners.

### Timex's expansion banks

The 2068 was designed for far more memory than it shipped with. The Timex
*Third Party Software Guide* (TPSG, section 1) describes it:

- **256 banks of 64K**, each split into eight 8K **chunks** selected by
  A13–A15. Bank 255 is HOME, 254 is EXROM, 0 is DOCK, and **1–253 are
  expansion banks**, the banks Wes Brzozowski called "the missing 253".
- **Each chunk has exactly one owner at a time.** Expansion banks outrank
  DOCK and EXROM, which outrank HOME. HOME owns every chunk nobody else
  claims.
- **An expansion bank takes a chunk by pulling BE low**, which switches off
  all of the 2068's own memory (HOME, EXROM and DOCK selects) for that
  access, whatever ports F4h and FFh say. **Not quite, on a real 2068
  (board 1, 2026-11-18):** /BE keeps HOME RAM off the bus on a read, but
  HOME RAM still takes a write. Setting the chunk's bit in F4h stops that
  ([Sharing the bus](#sharing-the-bus), open question 2).
- **The bus expansion unit's controller** was to hold, per bank, a
  4-bit-interface "horizontal select" register naming the chunks it owns.
  The TPSG puts its registers on ports FCh (data) and FDh (command). The
  ROM that shipped uses a different, memory-mapped scheme gated by the AY
  chip's IOA5 line (Brzozowski).
- **The ROM carries a bank API** for it: `BANK_ENABLE`, `GOTO_BANK`,
  `CALL_BANK`, `XFER_BYTES`, `GET_BANK_STATUS`, copied at boot from EXROM
  1000h–162Fh to RAM at 6200h (F9C0h in 64-column mode), plus a bank table
  (SYSCON) built by a boot-time search for banks.
- **None of it ever worked.** No bus expansion unit shipped, and Brzozowski
  (*Time Designs*, 1986–87) found the ROM code riddled with bugs: the boot
  search would crash a 2068 that had a real bank. His advice: keep the
  software model, fix the bugs, and replace only the two routines that touch
  hardware (`READ_BS_REG`, `WRITE_BS_REG`). William Pedersen (*Nite-Times
  News*, 1989) proposed the hardware shape used here: an 8-entry table of
  chunk owners, indexed by A13–A15.

### Why follow Timex

**The DOCK-based approach has a wall:** bit 7 of port FFh picks EXROM or DOCK
for every chunk at once. The TS-Pico ROM's own code runs from EXROM in chunks
0–1, so it could never see a DOCK-mapped RAM page at the same time. Expansion
banks sit above both and switch through BE, so:

- the ROM can map RAM into chunk 4 while running from EXROM;
- the DOCK stays free for the v3 card's cartridge images and ZX48 mode;
- the 2068's own documented bank API can be made to work, for the first time.

## The board

**Size: 4 MB (decided)**: 512 pages of 8K, presented as **64 expansion
banks** of 64K (banks 1–64 by default).

**The chunk owner table.** Because a chunk has one owner, the board needs no
register per bank, only one per chunk:

- `OWN0`–`OWN7`: the bank number that owns chunk 0–7, or 0 for none.
- `SUB0`–`SUB7`: which 8K of that bank appears there. Timex's model always
  shows chunk *n* of a bank in chunk *n*, so a write to `OWNn` sets `SUBn` to
  *n*. A program that writes a different `SUBn` afterwards can put **any page
  in any chunk (decided)**.

**On every memory cycle** the board looks up `OWN[A15:A13]`:

1. If the owner is one of the board's banks, and /MREQ is low and /RFSH is high,
   it pulls **/BE** low (open-drain).
2. A read (/RD low) drives D0–D7 from SRAM. An M1 opcode fetch is a read like
   any other, so code runs from the banks.
3. A write (/WR low) stores the byte, unless that chunk is write-protected
   (`WPROT`) or the write-protect switch is on. A refused write is dropped;
   /BE is still pulled. Whether a write also reaches the 2068's own RAM
   depends on F4h, not /BE: see [Sharing the bus](#sharing-the-bus).
4. Otherwise the board does nothing: no /BE, data bus untouched.

**The SRAM address** is `page × 8K + A12..A0`, with
`page = (owner − BASE) × 8 + SUB[chunk]` (0–511): 22 bits. With two 2 MB
chips the top bit picks the chip; with the four-chip fallback, the top two
([Hardware](#hardware)).

**Reset** (/RESET low): every `OWNn` = 0, `SUBn` = *n*, `WPROT` = 0. The
2068 boots as if the board weren't there. The SRAM keeps its contents.

**Bank numbers.** A setting in the controller's flash sets `BASE`, the first bank: 1, 65,
129 or 193. Two boards can share a 2068 (8 MB), and bank numbers stay inside
Timex's 1–253. With `BASE` = 193 only banks 193–253 (61) are usable.

## Registers

Two ports, decoded from A7–A0 only (A8–A15 ignored, like the v3 card), and
only in a real I/O cycle: /IORQ low with /RD or /WR low, **and /M1 high
(decided)**. An interrupt acknowledge is /M1 with /IORQ, and its address bus
holds the PC, so a decode on /IORQ and A7–A0 alone would take an interrupt at
an address ending FCh or FDh for a port access. Requiring /RD or /WR already
excludes it, as on the v3 card; /M1 is checked as well because it is free (it
is already on GPIO24) and keeps the rule true if the firmware ever acts on
/IORQ before the strobe. **FDh selects a register, FCh reads or writes it
(decided).** These are the
TPSG's BCMDPT and BDATPT. Taking FDh rules out emulating a Spectrum 128's AY
(FFFDh/BFFDh) on the v3 card later; David prefers later Yamaha sound chips
anyway **(decided)**.

| Reg | Name | R/W | Reset | Meaning |
|---|---|---|---|---|
| 00h–07h | `OWN0`–`OWN7` | R/W | 00h | Owner of chunk 0–7: an expansion bank 1–253, or 0 for none. 0, FEh and FFh all mean "none". A write also sets `SUBn` = *n*. |
| 08h–0Fh | `SUB0`–`SUB7` | R/W | *n* | Bits 0–2: the 8K of the owning bank that appears in chunk *n*. Bits 3–7 read 0. |
| 10h | `WPROT` | R/W | 00h | Bit *n* set: writes to chunk *n* are dropped while the board owns it. |
| 11h | `STATUS` | R | – | Bit 0: write-protect switch on. Bit 1: battery low. Other bits 0. |
| 12h | `ID` | R | – | A5h, the signature. |
| 13h | `IDCOMP` | R | – | 5Ah, its complement. With no board the bus floats: reads give 7Fh or FFh, never this pair. |
| 14h | `BASE` | R | – | The first bank number: 1, 65, 129 or 193. |
| 15h | `SIZE` | R | – | Banks fitted: 8, 16, 32 or 64 (a setting in the controller's flash, for boards built with fewer chips). |
| 16h | `VERSION` | R | – | Board/logic version, starting at 1. |

- **IN FDh** reads back the selected register number, so an interrupt handler
  can save and restore it.
- **No auto-increment**, so a device watching the bus (the v3 card, PicoVideo)
  follows the state from the writes alone: the last value written to FDh,
  then the value written to FCh.
- Unused register numbers read FFh and ignore writes.
- **Detecting the board:** `OUT (FDh),12h`, `IN A,(FCh)` gives A5h, then
  `OUT (FDh),13h`, `IN A,(FCh)` gives 5Ah. Anything else: no board. Never
  take a single FFh as "no board": the 2068's bus floats, and D7 often reads 0.

## Programming it

For people writing 2068 software, with or without the ROM's help.

**Map a page.** Put page 2 of bank 5 into chunk 4 (8000h–9FFFh):

```
        di
        ld   a,04h        ; OWN4
        out  (0FDh),a
        ld   a,5          ; bank 5; this also sets SUB4 = 4
        out  (0FCh),a
        ld   a,0Ch        ; SUB4
        out  (0FDh),a
        ld   a,2          ; page 2 of bank 5
        out  (0FCh),a
        ei
```

Unmap it by writing 0 to `OWN4`. For Timex-style use (chunk *n* of the bank in
chunk *n*), the first two `OUT`s are enough.

**Rules.**

- **Don't pull the floor out from under yourself.** Never map over the chunk
  holding the code that's running, or the stack, without moving them first.
- **Interrupts.** A register change is two `OUT`s. If an interrupt handler
  also uses the board, keep `DI` around the pair, or have the handler save
  `IN (FDh)` and restore it.
- **Chunks 0–1 hold the ROM** and the IM 1 handler at 0038h. Mapping RAM there
  is allowed (a 64K-RAM machine), but it is your job to have a handler at 0038h
  or to run with interrupts off or in IM 2.
- **Chunk 3 (or chunk 7 in 64-column mode)** holds the 2068's bank-switching
  code and the machine stack. Timex's rules reserve it. Map over it only if you
  know you don't need either.
- **The screen.** The SCLD draws the picture from its own 16K of RAM
  (4000h–7FFFh), whatever the Z80 sees there. Mapping a bank into chunk 2 or
  3 hides that RAM from the Z80, not from the display: the 2068 keeps showing
  it, and writes go to the bank. So a bank can never be the screen; a frame
  kept in a bank has to be copied into the SCLD's RAM to be seen. Whether
  Z80 accesses to a bank in chunk 2 or 3 are still contended is open
  question 19.
- **The RAM disk's banks** belong to the RAM disk ([below](#the-ram-disk)).
  Ask the ROM for pages rather than picking bank numbers yourself.

**Speed.** `LDIR` moves a byte in 21 T-states: about 160 KB a second at the
2068's 3.53 MHz. A 6K screen copies out of a bank in about 40 ms, **plus
contention**: the screen is in the SCLD's 16K (4000h–7FFFh), and every
access there goes through the SCLD, which stretches the Z80's clock while it
fetches the picture. Reads from the bank are uncontended; the writes into
the display file pay. Copy in the border and vertical blank where you can.
For whole-screen updates in the standard mode, copy into the screen not
being shown (4000h or 6000h) and flip with bit 0 of port FFh at the frame
interrupt, keeping its other bits (bit 7 is EXROM/DOCK, bit 6 the frame
interrupt disable): no tearing. ECM uses both halves, so it cannot flip.

## ZXDesk

ZXDesk is a windowed desktop for the Spectrum 48K by
[mindbox77](https://github.com/mindbox77/zxdesk): overlapping windows,
menus, a notepad, a clock, a calendar, settings and a two-pane file manager
(Commander). Jon, the GUI author behind the RAM disk request, has ported it
to the 2068: source at
[jon0x0/ts2068, ZXDesk2068](https://github.com/jon0x0/ts2068/tree/main/ZXDesk2068)
(read at `30c5d64`, 2026-10-29), with its README, roadmap and a TS-Pico
handoff. Its two builds are copied into [examples/](../examples/):

| Build | Loader | Screen | Code | Stack, IM 2 table | Files, buffers, heap |
|---|---|---|---|---|---|
| `zxdesk2068.tap` | `CLEAR 27647`, `RANDOMIZE USR 33280` | 256 × 192 | 6C00h–B542h | BD00h, BE00h | C000h–FEFFh |
| `zxdesk2068-512.tap` | `CLEAR 30719`, `RANDOMIZE USR 36352` | 512 × 192 | 7800h–C046h | C300h, C400h | C600h–FEFFh |

**How it runs.** It takes the machine over: all chunks HOME (F4h = 0),
its own IM 2 handler, no ROM calls, and no return to BASIC. It reads the
keyboard and both joysticks through the 2068's AY (Kempston mouse detection
is disabled in these builds) and beeps through FEh.

**How it stores files.** Through a registry of storage backends, each six
vectors (`StOpen`, `StClose`, `StRead`, `StWrite`, `StDir`, `StDelete`) plus
capability bits (write, persist, random access, directory). The 2068 build
registers one: RAM, four files of 256 bytes, lost at reset. The Spectrum
builds add tape, esxDOS, a printer, and **a Spectrum 128K RAM disk**
(`src/bank.inc`): a directory in one bank and eight files of 8K, paged in
at C000h through port 7FFDh and copied through a 256-byte staging buffer in
the code region.

**With the board fitted, today: no change.** The board starts with nothing
mapped and ZXDesk never touches FCh or FDh. One thing for Jon: the 2068
build still carries the Spectrum 128's AY routine, writing FFFDh and BFFDh.
The board decodes A7–A0 only, so both writes would land on FDh and change
the selected register (harmless: only a write to FCh changes the mapping).
The flag that selects that routine is cleared at start and never set on the
2068, so the beeper is used instead. Better to remove it than rely on that.

### A Megabank storage backend

The board's first use for ZXDesk: a backend, `ST_MEGABANK`, that keeps
files in battery-backed pages. It is the 128K backend with the paging
changed, and the board is a better fit for it than the 128K: an 8K page is
exactly one 128K file, and any page goes into any chunk with `OUT`s the
Z80 makes itself. (Jon's roadmap finds the TS-Pico's DOCK RAM harder to
use: one 64K bank at a time, each 8K only at its own address, and the bank
chosen by a firmware command whose timing is unmeasured.) The work is
Jon's:

1. **The layout.** Until `tpi:ram alloc` exists, use the board's first
   bank, pages 0–7 of the board: page 0 the directory, pages 1–7 seven
   files of 8K. The RAM disk grows down from the top of the board, so the
   bottom bank is the last it reaches. The directory page starts with a
   header: a magic number ("ZXDK"), a format version and the file count.
2. **A window in chunk 7.** For each directory access or transfer, map the
   page at E000h, copy through the staging buffer, and unmap, as the 128K
   backend does at C000h with 7FFDh. Chunk 7 holds only buffers and heap in
   both builds, never the code, the stack or the IM 2 table, and the
   interrupt handler touches nothing there. (Not chunk 6: in the 512-column
   build it holds the end of the code, the stack and the IM 2 table.) The
   rule reserving chunk 7 in 64-column mode
   ([Programming it](#programming-it)) protects the ROM's bank code and
   stack, which ZXDesk doesn't use. As with 7FFDh, a page can't be mapped
   while the caller's buffer is in the chunk it covers; the staging buffer
   is what makes that safe.

   ```
   ; Map page A (0-7) of the board's first bank at E000h.
   ; MbBase holds BASE, read once at start-up.
   MbPage:
           push bc
           ld   c,a
           ld   a,07h        ; OWN7
           out  (0FDh),a
           ld   a,(MbBase)   ; the first bank; this also sets SUB7 = 7
           out  (0FCh),a
           ld   a,0Fh        ; SUB7
           out  (0FDh),a
           ld   a,c          ; the page
           out  (0FCh),a
           pop  bc
           ret

   ; Give chunk 7 back to HOME.
   MbHome:
           ld   a,07h        ; OWN7
           out  (0FDh),a
           xor  a
           out  (0FCh),a
           ret
   ```

   ZXDesk's interrupt handler never touches the board, so the `OUT` pairs
   need no `DI`.
3. **Detect, then register.** At start, check `ID` (12h) for A5h and
   `IDCOMP` (13h) for 5Ah, read `BASE` (14h) into `MbBase`, and register
   the backend only if the board answers. With no board, ZXDesk is as it
   is now.
4. **Format only if the header is missing.** The 128K backend clears its
   directory at every start, since a 128K's RAM holds nothing worth
   keeping at power-on. Here the directory is the point: check the header
   and keep it if it is good. If not (a new board, or a reset in the
   middle of a directory write), ask before formatting.
5. **Read `STATUS` (11h) at start.** Bit 0 set: the write-protect switch is
   on, so register the backend without `STCAP_WRITE`. Bit 1 set: the
   battery is low; say so. Otherwise the capabilities are write, persist
   and directory, with delete.

Larger files need more than pages: the notepad, Commander's copy (one
256-byte chunk today) and the file lists have their own limits, which
Jon's roadmap already plans to lift.

**Testing.** In the emulator model
([zesarux-tspico](https://github.com/timex-sinclair-projects/zesarux-tspico)):
save a note, reset, load ZXDesk again and open it; with the switch on, a
save is refused and the file is unchanged. Jon's automated test runs on his
TSRun emulator, which would need the board's registers and pages modelled.
On the board, the same across a power cycle.

**Later.** More files than one bank holds (pages from `tpi:ram alloc`);
window save-areas and other buffers in pages, which the 512-column build
most needs, having about 6.4K of heap to the 256-column build's 7.9K (Jon's
roadmap puts a RAM disk first); and files exchanged with BASIC as `r:`
files, which needs a machine-code entry to the RAM disk because ZXDesk
doesn't use the ROM (open question 8).

## Hardware

**Built by JLCPCB, surface-mount throughout (decided).** The board is made the
way the v3 card is, and reuses its tools from the
[tspico-hardware](https://github.com/factus10/tspico-hardware) repository:
`scripts/parts.py` (MPN and LCSC number for every part), `scripts/jlc_price.py`
(prices and stock against JLCPCB's catalog) and `scripts/gen_fab.py` (the
upload files). The aim is no hand soldering. The RP2350's core-regulator
inductor, the one part that might have needed it, is gone: an LDO supplies the
core instead ([Parts](#parts)). The CR2032 holder is the Keystone part that
JLCPCB placed on the OpenSpand card; another holder pushed the order to its
Standard PCBA.

**Design flow (decided): v3's script pipeline**, in tspico-hardware's
`hardware/scripts/`, adapted (it hardcodes the name `tspico-v3`, the board
size, the placement table, the plane nets and the pre-routes):

- **Schematic** from Python with `schlib.py`: every pin gets a net label, no
  wires; one `gen_<sheet>.py` per sheet, flat hierarchy with global labels;
  `parts.py` stamps MPN and LCSC fields. The root sheet is written by hand
  first. KiCad 10.
- **Board** rebuilt from scratch each time: `gen_board.py` (outline, the
  JLC04161H-7628 4-layer stackup, holes, fiducials), then `gen_layout.py`
  (netlist via `kicad-cli sch export netlist`, footprints and a placement
  table in code, GND and +3V3 planes on the inner layers).
- **Routing**: `flow.py --jar freerouting-2.4.1.jar` runs placement,
  `preroute.py` (locked hand routes: the RP2350B's regulator pins and core
  supply, crystal and QFN fanout), `autoroute.py` (plane fanout vias, DSN export, Freerouting, SES
  import) and `finish.py` (repairs, DRC), retrying from placement up to three
  times; then `silk.py`. `check.sh` runs ERC and DRC; `gen_fab.py` writes the
  JLCPCB files. Rules: 0.6/0.3 mm vias, 0.127 mm clearance, no tracks on the
  plane layers. Copying v3's RP2350B placement and orientation lets its
  pre-routes carry over.

**3.3 V inside, 5 V bus (decided).** A controller (RP2350B) and 3.3 V SRAM sit
behind 74LVC buffers. LVC inputs accept the 2068's 5 V, and a 3.3 V high is a
valid TTL high for the Z80, as the v3 card already shows. These are v3's parts
and toolchain. There is no CPLD and no WinCUPL.

### The controller

**An RP2350B replaces the CPLD (decided), as the memory map, not the data
path.** It decodes FCh/FDh, holds the registers, and for each memory cycle
gives /BE, the page's address bits and the SRAM enables. The bytes go straight
between the SRAM and the bus through a 74LVC245, so the RP2350B never handles
them in a memory cycle. Being software, it can grow: a write fence (open
question 4), new registers, settings, all through a firmware update.

- **/BE** uses the mechanism the v3 card proved on a 2068 (`serve` in
  `src/tsbus/tsbus.pio`). A PIO program reads A15–A13 and jumps (`out pc, 3`)
  through an 8-entry chunk table that core 1 patches whenever a register
  changes, with /BE side-set on the same instruction, at 250 MHz. Here the
  table is the owner table: a chunk owned by one of the board's banks jumps to
  the path that asserts /BE.
- **The page bits are not time-critical.** On a 3.53 MHz read about 400 ns pass
  between a valid address and the data being needed; the SRAM (45–55 ns) and a
  buffer (about 5 ns) use under 70 of them. Whether the page comes from a second
  PIO table, a DMA-chained lookup or core 1 is settled on the bench (open
  question 2).
- **Register writes** are I/O cycles of over a microsecond. A PIO program
  catches `OUT` to FDh/FCh, as v3's `io_snoop` does (v3 waits on a gated
  /IOWR; here /IORQ, /WR and /M1 arrive separately and the program checks all
  three, [Registers](#registers)), and core 1 updates the
  tables before the next memory read. `IN` from FCh/FDh is answered the way v3's
  `io_drive` answers `IN 0Eh`.
- **Settings** that were switches and jumpers (`BASE`, `SIZE`) live in the
  controller's flash (open question 11).
- **Safe at power-up.** Every controller output that enables something is
  inactive in the RP2350's reset state (external pull-ups, or an inverting
  driver like /BE's), so the board stays off the bus until its firmware runs.
  The ROM's boot-time writes then set it up ([Sharing the bus](#sharing-the-bus)).

**Pins.** The RP2350B has 48 GPIOs; the 30-GPIO RP2350A is too small.

| Signals | Pins |
|---|---|
| A0–A7 (port decode) and A13–A15 (chunk), from the address buffers | 11 |
| D0–D7, on the SRAM side of the data buffer | 8 |
| /MREQ, /IORQ, /RD, /WR, /RFSH, /M1, /RESET, from the control buffer | 7 |
| /BE request | 1 |
| Page bits: 8 to SRAM A13–A20 (the 9th picks the chip, through the /CE1s) | 8 |
| /CE1 of each SRAM, write inhibit, data buffer enable | 4 |
| Write-protect switch | 1 |
| Battery: ADC (GPIO40–47) and BAT_EN | 2 |
| /BE read back, through the control buffer's spare channel | 1 |
| /INT and /NMI in, through a 74LVC2G34; /NMI out, open-drain (open question 16) | 3 |
| **Total** | **46 of 48** |

The data buffer's direction comes from the buffered /RD, not from a pin. The
two spare GPIOs, 46 and 47, go to test pads TP4 and TP5 (silk "GP46",
"GP47"), for bench experiments such as a second controller's handshake
([Bus mastering](#bus-mastering-revision-2)).

### Parts

Candidates, with JLCPCB stock on 2026-10-16. Availability is open question 6.

| Function | Part (candidate) | LCSC | Notes |
|---|---|---|---|
| Memory | 2 × CY62167EV30LL-45ZXIT (16 Mbit, 2.2–3.6 V, 45 ns, TSOP-I 48), used as 2M×8 | C511152 | 4 MB in two chips. **×8 confirmed (decided):** BYTE tied to ground makes it 2M×8, pin 45 (I/O15/A20) becomes A20, and BHE, BLE and I/O8–I/O14 are unused. Preferred over the DV30 for its standby current (below). 42 in stock: thin. |
| Memory (alternate) | 2 × CY62167DV30LL-55ZXIT (same package drawing 51-85183 and pinout) | C2956479 | 168 in stock. The same footprint, with one difference: the DV30 marks BHE and BLE "do not use, leave floating", while the EV30 needs them tied to a logic level for its standby figures. Route each to ground through an unfitted 0 Ω pad: fitted for the EV30, left open for the DV30. Its standby is worse: 22 µA maximum per chip, with every input within 0.2 V of a rail. |
| Memory (fallback) | 4 × IS62WV51216BLL-55TLI (512K×16, 3.3 V, TSOP-II-44) | C11315 | 3,623 in stock. Used as ×8: D0–7 tied to D8–15, /LB from A0 and /UB from A0 through a 74LVC1G04; A1–A12 and the page bits form the word address. A different footprint: choose before layout. **No CS2 in TSOP-II (confirmed):** its only chip select is CS1 (pin 6); CS2 exists only on the 48-ball mini-BGA, and JLCPCB's only BGA in stock is the 1.8 V IS62WV51216ALL. So power-fail protection goes through /LB and /UB instead: both come from gates on the SRAM rail whose other input is a "power not good" line (pulled up to the SRAM rail, pulled low by an N-MOSFET from the supervisor while 3.3 V is good), and a chip with both high is deselected. Four chip selects: 44 of the RP2350B's 48 pins. **Much worse standby:** 4 µA typical but 25 µA maximum per chip (industrial), and 20 µA maximum in data retention (down to 1.2 V), with every input within 0.2 V of a rail. Four chips could draw about 100 µA: about three months on a CR2032, nine on a CR2450. Order of preference: EV30, DV30, then this. |
| Controller | RP2350B (QFN-80), with v3's core circuit (QSPI flash, crystal) and an LDO for the core | C42415655 | 4,873 in stock. Copy v3's RP2350B schematic sheet, but **supply DVDD from a 1.2 V LDO (decided 2026-11-07)**, an ME6211C12M5G-N (SOT-23-5, C236672), as on the [OpenSpand card](https://github.com/timex-sinclair-projects/openspand-smd) (U9), instead of the on-chip switching regulator. The switcher's inductor, L1 on v3 (Abracon AOTA-B201610S3R3-101-T, C42411119), was the scarcest part on the board (4 at JLCPCB on 2026-10-16, 3 in October), the only one approved for the RP235x, and would have needed JLCPCB's pre-order or Global Sourcing, or hand fitting. Per the RP2350 datasheet, section 6.3.7, VREG_LX is left open and VREG_FB goes to ground, while VREG_VIN and VREG_AVDD stay powered (the power-on reset runs from them). A fixed 1.2 V matches v3's firmware, which runs at 250 MHz after `vreg_set_voltage(VREG_VOLTAGE_1_20)` (`src/tsbus/tsbus.c`, tspico-firmware-build); here that call has nothing to do, and the firmware may put the unused regulator in its high-impedance mode to save about 400 µA. The LDO is less efficient (3.3 V in, 1.2 V out: about a third of its input power reaches the core), a few tens of milliwatts on a card powered from the 2068. |
| Buffers | 4 × 74LVC245APW: A0–A7, A8–A15 and the control lines one way; D0–D7 both ways | C6082 | The same part as v3. Their power-off protection (Ioff) means that with the 2068 off they neither drive nor load the SRAM side. |
| /BE driver | 74LVC1G06 (inverting, open-drain) | C7828 | Wire-ORs with v3's 74LVC06. The RP2350's reset state (input, pull-down) leaves /BE released. |
| /NMI driver | 74LVC1G06, a second one | C7828 | The same part, so no second loading fee. Wire-ORs with v3's driver and NMI button; a 10 k pull-down holds it released until the firmware runs, as for /BE. |
| /INT, /NMI in | SN74LVC2G34DCKR (dual buffer, SC-70-6, TI), on 3.3 V | C14461 | 21,890 in stock on 2026-10-19. 5 V-tolerant inputs, as the 74LVC245s. |
| Chip enables | The CY62167's two: /CE1 (active low) and CE2 (active high) | | CE2 low deselects the chip whatever /CE1 does, including in data retention. **Both chips' CE2 come from the supervisor**, with a pull-down to ground, so they are high only while 3.3 V is good. **Each chip's /CE1 comes straight from the controller**, qualified by /MREQ, with a pull-down so it sits at a logic level on battery. No decoder, and no logic on the SRAM rail. |
| /WE gate | SN74LVC1G32DBVR (OR, SOT-23-5, TI), on 3.3 V | C10096 | 139,210 in stock on 2026-10-16. SRAM /WE = buffered /WR OR the controller's write inhibit, which it sets per chunk from `WPROT` and the switch. The write-inhibit line has a **pull-up**, so writes are blocked until the controller's firmware runs: Z80 writes during its boot cannot reach the RAM disk. |
| Power mux | LM66200 (ideal-diode ORing, 1.6–5.5 V, 1.32 µA) | C3235556 | Feeds the SRAM rail from 3.3 V or the battery. **Not the TPS2115A:** it draws about 55 µA from the battery, which would empty a coin cell in months. **Not plain diodes:** with 3.3 V on, the SRAM rail must stay close to 3.3 V, or the buffers and the controller push current into the SRAM's input clamps. |
| Supervisor | TPS3839G33 on 3.3 V (threshold about 3.08 V) | C485802 | Drives both chips' CE2 directly: high while 3.3 V is good, low below the threshold, and the pull-down holds them low with 3.3 V gone. So no writes happen while power rises or falls, well above the SRAM's 2.2 V minimum. Push-pull output, so CE2 needs no pull-up; its output is undefined below 0.6 V, which is what the pull-down covers. Its 200 ms delay keeps the SRAM deselected for 200 ms after power-on: harmless, since nothing maps a bank until software writes the registers. |
| Battery | CR2032 in an SMD holder (a CR2450 clip for longer life) | C22363833, C7498149 | **Keystone 3034TR, C5213768 (decided 2026-11-07),** as on the OpenSpand card, where JLCPCB placed it; 654 in stock. The Q&J CR2032-BS-6-1 (C70377) made JLCPCB move the order to Standard PCBA. If stock runs out, leave it off and fit a 3034 from DigiKey (36-3034-ND) by hand. Two CY62167EV30s in standby (1.5 µA typical, 3.0 µA maximum at 25 °C and 3.5 µA at 40 °C, each) plus the mux (1.3 µA): about 8.5 µA worst case at room temperature, so about three years on a CR2032 and eight on a CR2450. At 85 °C the chips' maximum is 12 µA each. Data is kept down to 1.5 V. Measure the real board. |
| Battery monitor | The RP2350B's ADC, through a switched divider | | [Battery monitor](#battery-monitor). `STATUS` bit 1 below 2.5 V. |
| Switch | Write-protect, SMD slide | | |
| Pull resistors | 6 × 10 k, 0603 (0603WAF1002T5E) | C25804 | JLCPCB basic part, as on v3: no loading fee. **To ground:** CE2 (one, for both chips), each chip's /CE1 (two), the /BE request into the 74LVC1G06 (one, so boot does not rely on the RP2350's internal pull-down and its E9 erratum). **To 3.3 V:** write inhibit (one), the data buffer's /OE (one). 10 k on CE2, not 100 k: CE2 must stay under 0.2 V on battery, and two chips' input leakage (up to about 1 µA each) would put 0.2 V across 100 k; with 3.3 V on, the supervisor still gives at least 2.9 V into it (VDD − 0.4 V at 0.5 mA). The /CE1s are pulled *down*: a pull-up to the SRAM rail would feed the unpowered RP2350B. |
| BHE/BLE links | 2 × 0 Ω, 0603 (0603WAF0000T5E) | C21189 | JLCPCB basic. BHE and BLE to ground: fitted for the EV30, do-not-place for a DV30 build. |

**Standby with the controller off.** CE2 is held low by its pull-down, so the
chips are deselected. The EV30 datasheet allows the other inputs (address, data,
/OE, /WE) to float in standby; only CE1, CE2, BHE, BLE and BYTE must sit at
logic levels, which the pull-downs and the ties to ground give. (The DV30
specifies standby with every input at a rail.)

**From the EV30 datasheet, for layout:** in TSOP-I, input low must not exceed
0.7 V while signals switch (0.8 V static), so keep ground bounce down; and
the chip wants a 100 µs supply ramp and 200 µs before full operation, which
the RP2350B's boot more than covers.

### Battery monitor

The coin cell is measured straight off its terminal, before the LM66200,
through a switch that is off whenever the controller is unpowered or booting,
so the battery never feeds the RP2350B's ADC pin. All parts are JLCPCB basic.

| Net | Connects |
|---|---|
| BATT+ | Battery +, LM66200 IN2, Q1 source, R3 |
| Q1 gate | Q1 gate, R3, Q2 drain |
| Divider top | Q1 drain, R1 |
| ADC | R1, R2, C1, RP2350B ADC pin (one of GPIO40–47) |
| BAT_EN | RP2350B GPIO, Q2 gate, R4 |
| GND | Q2 source, R2, C1, R4 |

| Ref | Part | LCSC | Notes |
|---|---|---|---|
| Q1 | AO3401A, P-channel, SOT-23 | C15127 | Source to the battery. Turns on at about 0.9 V gate drive, so it works down to a 2.0 V cell. Its body diode points at the battery and never conducts, since the drain side is never above it. |
| Q2 | AO3400A, N-channel, SOT-23 | C20917 | Pulls Q1's gate to ground when BAT_EN is high. Threshold at most 1.45 V: margin on a 3.3 V GPIO. |
| R1, R2 | 100 k, 0603 | C25803 | Halves the cell (2.0–3.3 V) to 1.0–1.65 V, clear of the ADC's 3.3 V full scale. 16 µA only while measuring. R2 also holds the ADC pin at 0 V when off. |
| R3 | 1 M, 0603 | C22935 | Holds Q1's gate at the battery: off. |
| R4 | 10 k, 0603 | C25804 | Holds BAT_EN low while the controller is unpowered or booting. The same part as the other pull resistors. |
| C1 | 100 nF, 0603 X7R | C14663 | A low-impedance source for the ADC's sampling capacitor. |

**Off,** the battery's only load here is the two MOSFETs' leakage, well under
a microamp. **To measure,** the controller drives BAT_EN high, waits 25 ms (the
divider and C1 settle with a 5 ms time constant, 50 k × 100 nF), averages a
few ADC readings, and drives BAT_EN low again: battery = 2 × reading ×
3.3 V / 4096. It sets `STATUS` bit 1 below 2.5 V, at boot and perhaps hourly
after. While 3.3 V is present the LM66200 runs from it, so the reading is the
idle cell; a lithium coin cell holds near 3.0 V most of its life and falls
near the end, so 2.5 V is a late warning, not an early one.

### Timing

The 2068's memory cycles leave about 300–400 ns from address to data; 45–55 ns
SRAM through a 5 ns buffer fits with room to spare. The critical path is
**/BE**: it has to switch off the 2068's own memory before that memory drives
the bus. The v3 card does exactly this from the same RP2350 PIO path, and it
works on a 2068. The board's version must still be measured on a 2068 through
the expansion bus, with the v3 card in place (open question 2).

**Refresh.** On a refresh cycle (/RFSH low) the controller stays off the bus
and leaves /BE alone, so it never interferes with the 2068's DRAM refresh.

### Form factor

A standard card for the expansion bus card (TS2068_Extender): three slots and a
pass-through, of which the TS-Pico and PicoVideo normally take two. The board
needs no pass-through of its own. **Gold fingers on the PCB edge (decided)**, using v3's footprint
`TS2068_EdgeConnector_2x32` (generated by `gen_board.py`, measured from the
v2.2 Gerbers): 32 positions at 2.54 mm, 31 fingers a side, the key slot at
position 6, an 83.66 × 8.25 mm tongue with 1 mm corner chamfers; pinout from
the TS2068 Technical Manual (table 2.4.1-1). Order with hard gold if offered
(else ENIG), a 30° bevel, and no copper within 0.3 mm of the slot. v3's
assumption that side A is the top is still unverified: check it before
ordering. **The body is v3's too (2026-10-18):** 130 × 56 mm with the
tongue below, v3's four M3 holes, the RP2350B cluster at v3's coordinates so
its pre-routes carry over. The firmware loads over USB-C, as on v3, with SWD
pads as well (open question 11). The KiCad project, its generators, the GPIO
map and the placement are in [`hardware/`](../hardware/README.md).

## Sharing the bus

Three cards may answer memory cycles: the 2068 itself, the v3 card (HOME and
EXROM with /BE, DOCK without), and this board. Only one answers any access,
and none of it depends on BUSISO or IOA5:

- **The board answers only chunks whose owner is one of its banks.**
- **The v3 card watches the FDh/FCh writes** and stops serving any chunk whose
  owner is 1–253 ([Firmware](#firmware)). Without that, a DOCK chunk would
  have two drivers: /BE switches off the 2068's memory but not the v3 card.
  Since the HSR rule puts every mapped chunk in DOCK or EXROM, this is not
  optional. **Measured (2026-11-18):** with the v3 card's firmware of
  2026-11-09, which predates the yield, a mapped DOCK chunk read back wrong
  (the card served the DOCK reads); with its DOCK serving switched off
  (`tsbus.dock(False)` at its REPL) the board's bytes came through. With the
  yield (TS-Pico main `d5be583`, its `tsbus.stats()` showing `mb_owned`) and
  DOCK serving on, the test passes: 111, 222, 77. The same build also
  yields chunks 0 and 1, where it serves its HOME ROM: banks mapped there
  (with interrupts off) read and write, run code, and give the ROM back
  when unmapped.
- **/BE is open-drain on both cards**, so they never fight electrically.
- **Software sets the chunk's bit in F4h (HSR) as well as its owner
  (measured, 2026-11-18).** /BE stops the 2068's memory answering a read, but
  not HOME RAM taking a write: with the chunk HOME, every write to a mapped
  bank also lands in the 2068's RAM underneath. With the chunk's HSR bit set,
  the 2068 sends that chunk to DOCK (or EXROM, by FFh bit 7) and its RAM
  keeps its byte. So **map = OWNn, then F4h bit n; unmap = F4h bit n clear,
  then OWNn = 0**, and anything that rewrites F4h (the ROM's
  `SAVE_STATUS`/`RESTORE_STATUS`, ZXDesk) must keep the bits of owned
  chunks. Chunks 0-1 hold ROM, where a stray write is harmless anyway. The
  EXROM case works too (FFh bit 7 set as well, 2026-11-18): /BE keeps the
  2068's own EXROM off the reads, the banks read back 111 and 222, and the
  RAM underneath kept 77.
- **PicoVideo watches the same writes** and stops copying screen writes into
  its display when chunk 2 or 3 is owned by a bank ([PicoVideo](#picovideo)).
- **The ROM writes every `OWNn` = 0 at boot**, even though the board clears
  them on reset. PicoVideo can't see /RESET and the v3 card keeps its
  shadows through a plain reset, so the boot-time writes resynchronise every
  device that watches.

**BUSISO is not used.** Neither this board nor the v3 card connects B30
(BUSISO): it is a no-connect pad on both. It arbitrates through the owner
table instead.

**/BE on the TS2068_Extender (V7) is a chain, not a bus** (schematic,
2026-11-18). Only slot 1's /BE (A29) is wired to the 2068's. Each further
slot's /BE goes to the BUSISO pin (B30) of the slot before it, and to that
slot's /BE only through a slide switch: SW2 joins slot 2's /BE to slot 1's,
SW3 slot 3's to slot 2's, SW4 the legacy connector's to slot 3's. With a
switch open, a card further out pulls only the nearer card's BUSISO, and
never reaches the 2068. That is the extender's design: a card behind the
TS-Pico asserts /BE into the TS-Pico's BUSISO, and the TS-Pico sees it and
gets out of the way. The v3 card doesn't connect BUSISO, so with v3 that
path does nothing; the owner table does its job instead. So:

- **A card that drives /BE must be in slot 1, or every switch between it
  and slot 1 must be closed.** The v3 card goes in slot 1; this board in
  slot 2 with SW2 closed (or slot 3 with SW2 and SW3 closed).
- Closing the switches is harmless to both cards, since neither reads
  BUSISO.
- **Found on board 1 (2026-11-18):** in slot 2 with SW2 open, the board's
  own /BE would not reach the 2068. (The readback said so too, but the
  capture didn't sample /BE then: see testing step 2.)

**IOA5 is not used.** It is free on the bus, and the shipped ROM's bank
protocol gated register writes with it. But it is a pin of the AY's port A,
whose direction is bit 6 of AY register 7, the register that also mixes the
tone and noise channels. Sound code commonly writes all of R7, so it would
flip IOA5 under the board's feet; port A also carries the joysticks. Ports
FCh/FDh do the job without that hazard **(decided)**.

## ROM

ROM 2.4 is ROM 2.3 plus patches, built the usual way: `tools/build-rom.py`
byte patches that state the bytes they replace, and new code in the module.
Addresses below are EXROM addresses (X) and, for the bank code that runs from
RAM, the RAM address (RAM = X + 5200h; in 64-column mode the code runs 97C0h
higher). All were read from `src/rom/TSPICO-23.ROM` and compared with the
genuine EXROM (`ROMs/GENUINE-2068-exrom.bin`, the same as ZEsarUX's).

### What ROM 2.3 does with the board

**Nothing, safely.** The boot still runs Timex's bank search (X0A3E–X0DAF). It
probes for banks through the memory-mapped, IOA5-gated registers. A board that
answers only ports FCh/FDh reads as absent, so the bank count stays 0:
`MAXBNK` (5CBEh) and the copy every bank routine reads, at 6315h. Every bank
routine then takes its fast path. A board on a 2068 with ROM 2.3 is invisible
until software writes its registers directly.

**Why the bank count must stay 0 until the bank code is rewritten.** Every
`tpi:` command crosses between HOME and EXROM through `CALL_BANK` and
`GOTO_BANK`, and so does every interrupt that arrives while the EXROM is
mapped (the handler at 62AEh). With a nonzero count:

- each crossing loops over every bank in `SAVE_STATUS`, `BANK_ENABLE` and
  `RESTORE_STATUS`, through the slow memory-mapped register routines (an
  estimated 45 ms per crossing with 64 banks);
- every call that asks for all chunks as HOME (`FF00`, which the TS-Pico's own
  crossings use) first takes every chunk from every expansion bank, so a
  program's mapped pages would vanish behind each ROM call.

So the minimal ROM leaves the count at 0, and the Timex API doesn't know the
board exists. Phase 8 removes both reasons (the state is 16 bytes, and the
crossings take chunks 0–1 only), and sets the count. The RAM disk and programs use the registers directly.

### What the ROM needs from SYSCON

SYSCON (5CBCh) points to a table at 5EEAh: the AROS header (8 bytes), the
LROS header at +8 (4 bytes), then 24-byte bank entries from +0Ch, ended by
80h. The boot builds it (EXTINIT X08E7, BLDSCT X09F4) and reads it to start
a cartridge. After the boot, almost every read sits behind a cartridge or a
banked channel:

- **AROS BASIC** (GETAL, AR_LN, AR_NXT, READ, and CLEAR and RUN resetting
  DATADD) tests `ARSFLG` bit 7 first.
- **SELECT** (1265h) and **CLCHAN** (13D8h) read it only for a stream offset
  with bit 7 set ([What `f:` already settled](#what-f-already-settled)).
- **SRCHSC** (1374h) has two callers, the banked OPEN at 1488h and `SAVE *`
  at 24EEh, and nothing reaches either.
- **The bank API** never reads it.

Three reads don't wait for a cartridge. They test SYSCON+1 for 02 (an AROS)
on every 2068:

- **FOR** (1CD1h), when the loop runs zero times and searches for its NEXT;
- **RESTORE** (1EA0h), every time;
- **USR** (388Eh), every call. If SYSCON+1 is 02 and the address is 8000h or
  more, it maps the DOCK chunks named at SYSCON+4 before the call.

These HOME addresses are the same in the genuine ROM and the TS-Pico's. So
for the whole session, not only at boot:

- SYSCON stays 5EEAh, and SYSCON+1 stays 00 unless an AROS is fitted. A
  stray 02 at 5EEBh would make every RESTORE and USR page the DOCK in.
- Nothing of ours (the module, the mover, RAM disk buffers) goes in
  5EEAh–5FFFh. Open question 17 asks whether the TS-Pico plans to.

### Minimal ROM: detection, reset, mover, `r:` (phase 4)

**Built** (2026-11-19) in tspico-firmware-build as a separate test ROM,
`tools/build-rom.py --megabank` → `src/rom/TSPICO-MB.ROM`, **ROM 2.5**
(`PEEK 101` = 37, ROM_ID 25h). The release ROM, 2.4, is byte-for-byte
unchanged and stays in flash slot 1 until the board passes its hardware
tests. **Promoted (2026-11-20):** the board passed, and the test ROM became
the release ROM as `src/rom/TSPICO-25.ROM`, in flash slot 1
(timex-sinclair-projects/tspico-firmware-build#441), released as TS-Pico
2.5 (tag `v2.5`). `--megabank` now builds `build/TSPICO-25.ROM`;
`TSPICO-MB.ROM` is gone. The wire format is in that repo's
`docs/PROTOCOL.md` §6.3; the code
in `src/rom/fdd/fddcmd.asm` under `IFDEF MEGABANK`, documented in
`docs/reference/rom/exrom-fdd.md`. Decisions taken on the way:

1. **The bank search is replaced** by `JP MB_BOOT` at X0A3E (the patch states
   the bytes it replaces, `2A BC 5C`). Checked: nothing enters X0A3F–X0DAF
   from outside. X1CA8's `CALL 0A3E` (the D-key path) and X09F4's
   fall-through both land on X0A3E itself; X0057's `JP 0D31` is in the
   11 bytes X1CAE copies to RAM 6000h, and runs after `OUT (F4h),0`, so it
   reaches HOME 0D31h; X0CFB, the renumbering, is called only from inside
   the range. `MB_BOOT`:
   - writes `OWN7`…`OWN0` = 0 and `WPROT` = 0;
   - **reads nothing and reports nothing** (open question 3, below): the boot
     stays as fast as before and the firmware asks when it needs to know;
   - leaves `MAXBNK` and 6315h at 0;
   - jumps to X2082 with HL = (SYSCON) + 0Dh, as the old code's `CALL 2082h`
     did (SYSCON's 80h end marker, then CLEAR-PRB).
   - **The renumbering call (X0CFB) is dropped**: with no banks it only
     cleared the two counts and probed again.
   The old body, X0A41–X0DAF, is left as unreachable bytes.
2. **The mover** is function **89h**, a response function like 81h–88h: the
   firmware answers a command with 89h and then operations, so the mover
   needs no command of its own and works inside any `tpi:` exchange. The
   chain's last test, X2213 (2.4's `CP 87h / JP Z,3006h`), is `JP FN_EXT`,
   which keeps 88h and adds 89h. It sits after the 2.4 module, at
   3998h–3E09h (the module, the `r:` code and the mover together, 1138
   bytes; 502 left to 3FFFh). The free space at 23D4h–2FFFh is untouched;
   the 37B5h–3FFFh figure above was ROM 2.3's, and ROM 2.4's module now
   runs to 3997h. Its operations, as built, are in
   [The mover's operations](#the-movers-operations).
3. **The `r:` prefix** is tested in `F_HOOK` beside `f:` (same size, through
   `FR_TEST`). It sets **TPMODE bit 5** (unused: SESSION_SETUP and F_HOOK
   already clear bits 4–5 on every SAVE/LOAD statement) and opens the file
   with `tpi:ropen name`. The tape block entries, X0068 (`SA_BYTES`) and
   X00FC (`LD_BYTES`), jump to `R_SA`/`R_LD`, which go on to the TS-Pico's
   block code unchanged when bit 5 is clear. When it is set each block is
   one command, `tpi:rblk` with its address and length (PMR1 = SAVE, LOAD
   or VERIFY, PMR2 = the flag byte), and the firmware moves the bytes with
   the mover, then answers with a status: 2 = R (a VERIFY that differs),
   3 = F, and so on. Bit 5 is cleared after the data block and on any
   error, so the next SAVE or LOAD goes to the card.

Tested in a Z80 interpreter against a board model that enforces the HSR
rule (`src/test/rom_megabank_hosttest.py`, in CI), and end to end in
ZEsarUX's Megabank model with a stand-in RAM disk
(`tools/emu/megabank_smoke.py`; ZEsarUX ignores the HSR, see
[Testing](#testing)).

**On hardware (2026-11-20):** the board in a 2068 with a v3 card, ROM 2.5
from tspico-firmware-build `main` (`b3e6fb8`), and
`firmware/controller/basic/mbph4.bas`, which checks what ZEsarUX can't.
**All 11 checks pass:**

- Detection: `PEEK 101` = 37, ID A5h/5Ah, `BASE` 1, `SIZE` 64, and
  `SAVE "tpi:ram"` (op 7) describes the board.
- After power-on, OWN0–7, `WPROT` and the HSR all read 0. The
  controller's log (64 entries) held only the writes after the boot's own:
  two passes setting OWN0–7 = 0 and SUB0–7 = 0–7. So it shows the end
  state, not `MB_BOOT`'s writes themselves; the controller also clears its
  registers on /RESET.
- `SAVE`, `VERIFY` and `LOAD "r:…" CODE` of 2000 bytes at 57400 (chunk 7,
  the stack in chunk 3, so the window is chunk 4): the bytes come back,
  and **the 2068's own RAM at 8000h–9FFFh, zeroed first, is still all
  zero**: the window's HSR bit keeps the mover's writes out of HOME RAM.
  The HSR and OWN4 are 0 after each.
- With the board's first bank mapped in chunk 4 and HSR bit 4 set (a
  program's own page in the window chunk), `SAVE`, `LOAD` and `VERIFY`
  each leave the HSR at 10h, OWN4 at `BASE`, SUB4 at 4 and the page's
  bytes in place.

### The Timex bank API on the board (phase 8)

The owner table makes this much cheaper than Timex's design: the state the
API loops over, one register set per bank, is just 16 bytes on this board
(`OWN0`–`OWN7`, `SUB0`–`SUB7`), whatever the bank count.

**Built (2026-11-19), all seven items, in the Megabank test ROM (2.5)**
([tspico-firmware-build#429](https://github.com/timex-sinclair-projects/tspico-firmware-build/pull/429)).
Tested in a Z80 model of the board with the HSR rule
(`rom_bankapi_hosttest.py`, from RAM 6200h and from the 64-column copy) and
in ZEsarUX (`BANK_ENABLE` there and back). **On hardware (2026-11-20):**
`firmware/controller/banktest/` (`mbbank.tap`), 18 of 18 on the board with
ROM 2.5 from tspico-firmware-build `main` (`b3e6fb8`): the count and `BASE`
set at boot; `BANK_ENABLE` of a board bank into chunk 4 and back to HOME,
with the 2068's own RAM there (zeroed first) still all zero after; the page
and its HSR bit kept through a second of interrupts and a `tpi:` command;
`GET_STATUS`, `GET_NUMBER`, `PUT_WORD`/`GET_WORD`; `XFER_BYTES` of 384
bytes directly and through its buffer, correct on the board, A = 0 and a
bank mapped in chunk 6 kept; `SAVE_STATUS`/`RESTORE_STATUS`. Not tested
there: `GOTO_BANK`, `CALL_BANK`, the DOCK and the EXROM, and 64 columns.
Where it differs from the plan below:

- **No nibble translators.** There is no Timex bank hardware or software to
  stay compatible with, so `WRITE_BS_REG` and `READ_BS_REG` are gone, not
  translated (nothing calls them: the boot's search was their only user),
  and their 169 bytes hold the new code's helpers. `BANK_ENABLE`,
  `GET_STATUS`, `GET_NUMBER`, `SAVE_STATUS` and `RESTORE_STATUS` talk to the
  board directly (`src/rom/fdd/bankapi.asm`), keeping every documented entry
  point. That also makes item 6's `DI`/`EI` moot: `BANK_ENABLE` and
  `RESTORE_STATUS` turn interrupts off and put them back as they were
  (`LD A,I`, read twice for the NMOS Z80).
- **The HSR rule** ([Hardware](#hardware)) is kept in `BANK_ENABLE` and
  `RESTORE_STATUS`: OWN before its HSR bit when a chunk is mapped, the bit
  before OWN when it goes back.
- **The DOCK and the EXROM get exactly the chunks asked for.** Timex's code
  (genuine, not a TS-Pico change) tied the EXROM to chunks 0–1. Charlie
  Day's LEAP ROMs put the EXROM in chunk 6, and they are to run on the
  TS-Pico and the board one day, so the API no longer assumes chunks 0–1.
  LEAP itself pages the EXROM by writing F4h and FFh directly (F4h = 40h,
  then 0), which would clear the HSR bit of a board bank mapped elsewhere
  and let writes through to HOME RAM: a LEAP build for the board must
  read-modify-write F4h, or the board must tolerate it (open question 20).
- **The frame is 19 bytes**, not the bank count + 2: FFh, F4h, the 16
  registers and the register select (FDh), so an interrupt between a
  program's `OUT (FDh)` and its `IN (FCh)` doesn't lose the select. Only FFh
  bit 7 comes back (6.5.5). The interrupt handler, `CALL_BANK` and
  `XFER_BYTES` reserve 19.
- **Item 3:** `MB_DETECT`, at the end of the boot reset, sets `MAXBNK` and
  6315h to `SIZE`, cut so the last bank is 253, and keeps `BASE` at 6404h.
- **Item 5:** the relocation table is rebuilt by the build from two
  assemblies (64 entries); three of the four wrong entries were in the
  rewritten code, the fourth (674Fh → 6744h) is fixed.
- **Item 4:** only the EXROM's three crossings needed narrowing (03DDh,
  08DDh, 0017h); HOME's and the interrupt handler's already took chunks 0–1
  or 0–2. `CALL_HOME` (03DDh) goes through a new thunk: patched in place,
  its `LD H,0` turned FFFCh's L into a parameter count of FCh, and
  CALL_BANK's 266-byte copy from SP, just below 6200h, ran over the bank
  code during the call (a hang and reset).
- **658Eh–65CDh is the bank stack**, not free space (open question 10).

1. **Rewrite `WRITE_BS_REG` and `READ_BS_REG`** (RAM 635Ch–6404h, X115C–X1204,
   169 bytes, contiguous) as a translation onto the ports. Keep both entry
   points: their callers and the 64-column fixups (X1D2A, 2C, 2E, 36) use them.
   - A BNA write (register 80h) goes to a RAM latch.
   - An HS write (40h) for bank *b*: `OWNn` = *b* for each bit set; `OWNn` = 0
     where `OWNn` was *b* and the bit is clear.
   - A universal deselect (A0h with BNA 0): `OWNn` = 0 for each bit set.
   - A status read: present (bit 2 = 0) if *b* is within `BASE`…`BASE+SIZE−1`.
   - This also removes a bug: today's `READ_BS_REG` copies the screen byte at
     4000h into 8000h on its way out.
2. **Rewrite `SAVE_STATUS` and `RESTORE_STATUS`** (651Eh/654Ah) to save F4h,
   FFh and the 16 board bytes: constant time instead of a loop per bank.
   `CALL_BANK` reserves (bank count + 2) bytes per frame for this; that size
   changes to match. `RESTORE_STATUS` writes FFh before F4h, which briefly
   maps the DOCK: keep it under `DI`, as the TS-Pico's crossings already are.
3. **Only then** set `MAXBNK` and 6315h to the bank count.
4. **Narrow the TS-Pico's own crossings** from "all chunks to HOME" (`FF00`)
   to chunks 0–1 (`FFFC`, the mask is active-low), so a program's pages in
   chunks 2–7 survive ROM calls. **Measured (2026-11-19, ZEsarUX, ROM 2.4 and
   the 2.5 test ROM):** after any `tpi:` command the HSR reads 0, whatever
   it was. OWNn and SUBn are untouched, so on the board a program's mapped
   page still reads as the board's, but with its HSR bit gone its writes
   also reach HOME RAM. Until this item is done, a program that maps pages
   must set its HSR bits again after every `tpi:` command (and every `r:`
   SAVE/LOAD). Check each: HOME 3CE3h and 0A50h, HOME 03FCh
   (through 040Dh), EXROM 03DDh, 08DDh, the error path at 0017h–002Dh, and the
   interrupt handler at 62AEh.
5. **Fix the 64-column relocation table.** Four of its 61 entries are wrong
   (X1D34: 64ACh should be 64A9h; X1D38: 650Eh → 650Fh; X1D3A: 6516h → 6517h;
   X1D68: 674Fh → 6744h). In 64-column mode they break `BANK_ENABLE`'s
   expansion-bank paths and all of `XFER_BYTES`. Harmless today, because the
   count is 0. A patch to the EXROM image reaches both copies, since the high one
   is copied from the low.
6. **Brzozowski's patches.** All ten sites still hold the genuine bytes in ROM
   2.3, so every bug is present:

   | RAM | X | Now | New | Fixes |
   |---|---|---|---|---|
   | 649A | 129A | C5 | F3 (DI) | `BANK_ENABLE` not atomic |
   | 651B | 131B | C1 | FB (EI) | same |
   | 66DF | 14DF | 75 | 73 | `MOVE_BYTES` stores HL instead of DE |
   | 66E2 | 14E2 | 74 | 72 | same |
   | 66F3–66F5 | 14F3 | 07 0F 38 | 0B 3C 28 | `CREATE_BITMAP` end byte |
   | 6722 | 1522 | F5 | 00 | `XFER_BYTES`: drop `PUSH AF` |
   | 672B | 152B | 0A | 08 | its parameter offset |
   | 676A | 156A | 47 | 5F | `XFER_BYTES` same-bank mask |
   | 67C2 | 15C2 | 2B | 2C | keep A = 01 on abort |
   | 67FF | 15FF | F1 | 00 | drop `POP AF`; A returns 00 done, 01 aborted |

   Two cautions. His `EI` is unconditional, which would re-enable interrupts
   inside the TS-Pico's own `DI` sections: restore the previous state instead
   (`LD A,I` gives it in P/V). And the `XFER_BYTES` patches change A on return;
   the old bank search relied on it, but the minimal ROM removes that code, and
   the remaining callers (HOME 1899h, 1E0Dh) don't use A.
7. **For the whole API**, also: `GET_STATUS` returns the mask in C for
   expansion banks but in B for the others; `GET_NUMBER` is broken once banks
   exist (its `DJNZ` count is overwritten by `GET_STATUS`, and its EXROM test is
   unreachable); `PUT_WORD`'s `LD D,B` destroys D before storing it; Timex
   Technical Manual fixes 6.5.2–6.5.6.

Items 1–5 make `BANK_ENABLE`, `GOTO_BANK`, `CALL_BANK` and `XFER_BYTES` work
with the board; 6 and 7 complete the API. Which of these to do is open
question 5.

**Two reference gaps** found on the way, for the programmer's reference
whatever happens to this plan: the TS-Pico ROM changes `CALL_BANK` at X1410
(`LD B,(IX+08h)` → `LD B,(IX+09h)`, apparently a fix: the genuine code read
PARAM_IN's low byte twice), and the boot's D-key path at X1C86, which skips the
DOCK header read. Neither is in `docs/reference/rom/`.


## The RAM disk

**The split: the v3 card keeps the file system, the Z80 moves the bytes.**
The TS-Pico can't reach the board's memory, but the Z80 can, at `LDIR`
speed. So:

- **The firmware** (Python, on the v3 card) owns the directory, allocation,
  names, `CAT` output and errors. It is written and host-tested like the rest
  of the firmware, and costs no ROM space.
- **The ROM** runs a small loop, the **mover**, that does what the firmware
  tells it, one operation at a time, over the existing port 0Eh link.
- **The file system's data lives on the board**, so it survives power-off
  with the files. The firmware holds only a cache.

### The mover's operations

As built (phase 4). The firmware sends function 89h and then operations;
the ROM does each and replies with a status byte, then waits for READY
before reading the next. Words are little-endian; "page:offset" is an 8K
page counted from `BASE` (0–511) and an offset into it (0–1FFFh). A transfer
may cross pages and, on the 2068 side, chunks.

| Op | Parameters | Does | Reply | Used by |
|---|---|---|---|---|
| 1 | addr, len, page, off | copy 2068 memory at *addr* to page:offset | 1 | SAVE |
| 2 | addr, len, page, off | copy page:offset to 2068 memory at *addr* | 1 | LOAD, MERGE |
| 3 | addr, len, page, off | compare | 1 same, 2 differ | VERIFY |
| 4 | len, page, off | send page:offset to the TS-Pico, then the bytes' XOR | 1 | reading the directory; MOVE from `r:`; backup |
| 5 | len, page, off | receive *len* bytes and their XOR into page:offset | 1, 5 XOR wrong | writing the directory; MOVE to `r:`; restore |
| 6 | len, page, off, page2, off2 | copy page:offset to page2:off2 (forwards) | 1 | compaction |
| 7 | — | send found (1/0), `BASE`, `SIZE`, `STATUS` | `VERSION` | `tpi:ram`, and the firmware learning about the board (open question 3) |
| 0 | — | done: the next byte is the command's answer, a status (1 = OK, 2 = R, 3 = F, …) or another response function | — | every command |

Replies 3 (no board) and 4 (outside the board: offset ≥ 2000h, or past its
last page) refuse an operation without losing step: a refused op 4 still
sends *len* zeros and a zero XOR, a refused op 5 still reads its bytes. An
op of 8 or more ends the mover with Report J. Bytes cross port 0Eh about
75–90 µs apart, unhandshaked, as a LOAD block's do.

**How the mover maps a page.** For each piece of a transfer that lies in one
8K chunk of 2068 memory and one board page, it picks a **window chunk**, the
first of 4–7 that holds neither the piece's 2068 address nor the stack
(SP − 32 to SP + 48, its own frame included); op 6 takes two windows. With
four candidates and at most three excluded, one is always free. With
interrupts off for the whole operation:

1. Save `OWNw`, `SUBw` (read back through FCh) and the HSR (F4h).
2. Map the page: `OWNw` = its bank, `SUBw` = its 8K, **then set HSR bit w**:
   /BE keeps HOME RAM off a read, but a write to an owned chunk still lands
   in HOME RAM unless the chunk's HSR bit is set (measured on the board).
3. `LDIR`, a compare loop, or a port loop.
4. Put back what was there: the HSR bit first if the chunk was the 2068's
   own, then `OWNw` and `SUBw`, then the HSR. A program's own page mapped in
   the window chunk comes back with its HSR bit. The interrupt state is
   restored (`LD A,I`), not forced on.

The mover runs from EXROM chunks 0–1 and never maps a window there, so it
never unmaps itself; for the same reason addresses below 4000h are the
EXROM's while it runs, not HOME's. It doesn't depend on the Timex bank API,
so the RAM disk can ship before that API is fixed ([Phases](#phases)).
There is no BREAK test inside an operation.

### From BASIC

As built (phase 5, tspico-firmware-build: `src/TS/ramdisk.py` and
`tspico.py`'s `RD_*`). Only the Megabank ROM (2.5) has the mover, so on any
other ROM an `r:` command is Report J, "r: needs the Megabank ROM". CAT,
ERASE, FORMAT, MOVE and COPY already reach the firmware as `tpi:dir`,
`tpi:erase`, `tpi:format`, `tpi:ren` and `tpi:copy` with the name as typed
(the 2.4 disk module), so `r:` names needed no ROM change beyond phase 4's.

| Command | Does |
|---|---|
| `SAVE "r:name" …` | every form `SAVE` takes: `LINE`, `CODE`, `SCREEN$`, `DATA`. A file of the same name is replaced |
| `LOAD "r:name"`, `VERIFY "r:name"`, `MERGE "r:name"` | as from tape, with the same reports; a missing name is F |
| `CAT "r:"` | the directory, by name, with free space |
| `ERASE "r:name"` | delete one file |
| `ERASE "r:pattern"` (`*`, `?`) | the first time, says which files it would erase; the same statement again within a minute, the disk unchanged, erases them **(decided in the build: see below)** |
| `MOVE "r:a" TO "r:b"` | rename. MOVE between the card and the RAM disk is Report Q: MOVE renames on one disk |
| `COPY "name" TO "r:[name]"`, `COPY "r:name" TO "name"` | copy between the SD card and the RAM disk: a `.tap`'s first file (a header and its data block) to the RAM disk, a file from it as a `.tap` of two blocks. **COPY copies and MOVE renames, as the 2.4 disk module already has them** (the draft had MOVE copying) |
| `COPY "r:a" TO "r:b"` | a copy on the board (op 6) |
| `FORMAT "r:"`, `FORMAT "r:size"` | a new, empty RAM disk at the top of the board: half the board, or *size* K (a multiple of 8). Over a RAM disk that holds files, the first FORMAT says how many and the same FORMAT again within a minute does it |
| `SAVE "tpi:ram backup name"` / `"tpi:ram restore name"` | the RAM disk's pages, raw, to or from `name.rdi` on the SD card; a restore goes to the top of the board whatever board it came from, and over files asks to be repeated, as FORMAT does |
| `SAVE "tpi:ram"` | board found or not, `BASE`, `SIZE`, version, battery, write-protect switch, RAM disk size, pages, files and free space |

**Repeat to confirm, not Y/N (decided in the build).** The draft had FORMAT
ask Y/N. It can't: the mover's operations come before the command's answer,
and a Y/N prompt *is* the answer (function 86h), so nothing can be written
after it. Asking first and acting on a second command needs no ROM change
and no state but a minute-long note of what was asked.

**Reports.** No board, no RAM disk (unformatted, or a superblock that fails
its checks) and the write-protect switch are J; a full disk or a full
directory is Q; a missing name is F; a VERIFY that differs, a LOAD of the
wrong type (the ROM asks for another header) or a length that doesn't match
is R. A SAVE, LOAD or VERIFY answers with the bare status (it is in the
middle of the statement); the other commands print their message as `tpi:`
commands do (errors always, the rest with `tpi:verbose`).

Names follow the tape rules (1 to 10 characters), so a file moves between
tape, SD card and RAM disk unchanged; they compare exactly, as a tape's do.
Because `LOAD "r:name"` goes through the ROM's own LOAD, a BASIC program,
arrays and `SCREEN$` all behave exactly as they do from tape.

### Where it lives on the board

As built (phase 5). The RAM disk is **pages first to last at the top of the
board** (last is always the board's last page), so the banks below are free
for programs and ZXDesk's first bank stays clear. The top page holds the
metadata, the pages below it the data:

- **Superblock**, 32 bytes at the top page's 0000h: "TSRD", format version,
  log2 of the cluster size, the first and last page, a **generation
  counter**, the directory's high-water mark (slots below it are read), and a
  16-bit sum.
- **Staging**, 17 bytes at 0100h: a SAVE's header is copied there (op 1) and
  read back (op 4), since the mover has no 2068-to-Pico operation.
- **Directory**, 224 entries of 32 bytes from 0400h: a used mark, the 17-byte
  tape header (whose name is always the entry's name), the first cluster,
  the length, the data block's flag, the generation it was written at, and a
  check byte, last. LOAD copies the header to the 2068 straight from the
  entry.
- **No allocation bitmap (decided in the build).** Which clusters are used
  follows from the entries, so nothing can disagree with them after a power
  cut. A file's data is written first, then its entry in one operation (its
  check byte lands last, so a cut entry fails its check), then the
  superblock; a replaced file's old entry goes only after that, and of two
  entries of one name the newer generation wins. A power cut during a SAVE
  loses at most the file being written.
- **Files are contiguous**, in 1K clusters, and files of 8K or more start on a
  page boundary. A program can then map a file's pages directly: `LOAD
  "r:icons"` once, then page the icons in by number. When free space is too
  fragmented for a new file, the firmware compacts the disk with op 6, each
  file in pieces no longer than the distance it moves (op 6 doesn't handle
  overlapping runs). A power cut during compaction can lose the file being
  moved.
- **Caching.** The firmware reads op 7 and the superblock (32 bytes) at the
  start of every `r:` command and re-reads the directory (high-water x 32
  bytes) only if the generation counter changed. Every change to the
  directory increments it. That covers resets, power cycles and a second
  TS-Pico.

**Allocating pages for a program**, so programs and files never collide, is
a `tpi:ram alloc` command that reserves pages below the RAM disk and records
them in the superblock (0020h-00FFh of the top page are free for it). Its
exact form is for Jon, the GUI author, to shape (open question 8); not in
phase 5.

## Channels (phase 9)

`OPEN #n,"r:name"[,"mode"[,reclen]]` opens a RAM disk file as a stream,
the way `OPEN #n,"f:name"` opens a file on the SD card: then `PRINT #n`,
`INPUT #n`, `INKEY$ #n`, `LIST #n` and `CLOSE #n`. The RAM disk becomes
random-access data storage for BASIC: record files read and written in
place, kept through power-off by the battery. **Built (2026-11-19)** as
written below, in the Z80 model and ZEsarUX (tspico-firmware-build#430: the
ROM's `src/rom/fdd/rchan.asm`, the firmware's `ramdisk.py` slots and
`tspico.py`'s `RD_RCH*`); not yet on hardware. Where the build settled or
departed from this section is in [Channels as built](#channels-as-built).

The model is the TS-Pico's `f:` channel, which works today. Its references,
in tspico-firmware-build: `docs/reference/flows/channels.md` (the flow end to
end), `docs/reference/rom/exrom-fdd.md` ("The channel driver"),
`docs/reference/rom/home.md` (the three hooks and the HOME stubs at
1488h–14C5h), `docs/reference/firmware/channels.md` (modes, records, TAB).

### What `f:` already settled

These hold for any channel on the 2068, and `r:` keeps every one:

- **The record lives in CHANS.** MAKE-ROOM's POINTERS moves CURCHL, so a
  record above the workspace breaks `INPUT #`; a record in the program loses
  its offset when CHANS grows.
- **Both bytes of the stream's offset stay below 80h.** SELECT (HOME 1230h)
  takes a STRMS entry whose two bytes OR to 80h or more as a banked channel
  and looks it up through SYSCON (1265h). The `f:` OPEN pads each record to
  keep clear of that.
- **Allocations are multiples of 256 bytes**, so closing one moves other
  streams' offsets by their high byte only, and the pads stay right.
- **Records hold fixed HOME addresses** (14A0h, 14A9h), never module
  addresses, so nothing in a record goes stale when the module is rebuilt.
- **Every entry from HOME runs under `DI`** (the bank switch writes FFh then
  F4h; an interrupt between them runs `RST 38h` over the empty DOCK), and
  **inside GUARDED**, so a report raised inside the module restores the bank
  stack through H_TRAP (HOME 14B2h).
- **CURCHL and IY are kept.** A channel routine runs inside someone else's
  `PRINT #`; anything it prints would move the current channel to the screen.
- **CLOSE # takes the record out** and fixes up the others: streams above it
  move down, CURCHL goes to S if it pointed into the record. Stock CLOSE #
  crashes on an unknown letter (its table at 1407h has no terminator), so
  the hook must catch every letter the module creates.

### Question 1: CHANS records or Timex's banked channels

The 2068 ROM has Timex's path for channels whose code lives in an expansion
bank: SELECT's SYSCON lookup, the bank number kept in CRCBN (5CBFh), the
routines called through `CALL_BANK`, and CLCHAN's banked close (13D8h). The
Megabank is made of expansion banks, so it is tempting. **Recommended:
ordinary CHANS records, letter `R`, through the `f:` hooks.** The banked path
loses on every count:

- **It needs phase 8 first.** `CALL_BANK` does nothing useful until the bank
  API works on the board and `MAXBNK` is nonzero.
- **It is slow per character.** Every `RST 10h` would go through `CALL_BANK`
  (parameter frame, `SAVE_STATUS`, `BANK_ENABLE`, `RESTORE_STATUS`), against
  the `f:` stub's one thunk.
- **The code has to live in a bank.** The board is RAM: the driver would be
  copied there at boot from the EXROM, into pages the RAM disk and programs
  also use, and would run from memory any program can overwrite.
- **There is no way in.** The banked OPEN path at HOME 1488h was never
  reachable, and the TS-Pico ROM has rewritten those 55 bytes as its own
  stubs. A new OPEN path would be needed anyway, and the `f:` one exists.
- **SYSCON holds at most 10 entries** (open question 9), and Timex's banked
  path is untested code on a ROM where every other part of the bank API has
  turned out broken.

So `r:` needs **no new HOME bytes**: an `R` record holds the same 14A0h and
14A9h as an `F` record, and the module's `G_OUT`/`G_IN` choose `CH_OUT` or
the `r:` routines by the record's letter (one compare per character). The
changes to the existing driver:

- `CH_OPEN_HOOK` recognises `r:` beside `f:` and `d:`.
- `CH_CLOSE_HOOK` closes `R` records as well as `F` ones; the fix-up of other
  streams moves them by the closed record's own allocation (512 bytes for
  `F`, 256 for `R`) instead of a constant.
- `G_BEEP` skips the key click for `R` records too, so reading is silent.

The banked-channel route stays unused; nothing here depends on open
question 9.

### Question 2: where the bytes go

Two ways to carry a `PRINT #` byte to the board:

- **Through the firmware**, as `f:` does: the ROM buffers 64 bytes, sends
  `tpi:chwr` with the bytes as hex over port 0Eh, and the firmware, which
  can't reach the board, has the mover copy them back from the TS-Pico into
  the page (op 5). Every byte crosses the bus twice, and reading back is the
  `tpi:chrd` data phase at about 75 µs a byte.
- **Straight to the board** from the ROM, through the mover's window: map
  the file's page into a window chunk, store or fetch the byte, restore the
  chunk.

**Recommended: straight to the board, with the firmware involved only at
OPEN, CLOSE and when a file must grow.** The firmware stays the owner of
the file system (names, allocation, the directory); the ROM owns only an
open file's bytes, inside an extent the firmware gave it.

- **Speed.** The window costs about a dozen port accesses (save `OWNw` and
  `SUBw`, map, restore) and the position arithmetic: an estimated 400–500
  T-states per character, about 0.15 ms, on top of the stub, thunk and
  GUARDED cost every `f:` character already pays. No exchange with the
  TS-Pico, no SD card, nothing to wait for. (Estimates; measure in the
  emulator.) **Measured (2026-11-19): about 4.3 ms a character** on the
  board, 3.8 ms in ZEsarUX, with the entry included
  ([Testing](#testing), Channels step 8). With the fast entry and the
  record's own output stub ([Open questions](#open-questions) 21): 396
  T-states in ZEsarUX, and **at most about 70 µs on average on the board
  (2026-11-20)**.
- **No buffers.** Each `PRINT #` byte is on the board when `RST 10h`
  returns, and `INPUT #` reads one byte at a time with no read-ahead. So the
  record is small (256 bytes, mostly pad, against `f:`'s 512), a `u` stream
  needs no rule to stop a read running past a line, and nothing is lost in a
  buffer at NEW or power-off.
- **The window rule** is the mover's: a chunk from 4–7 holding neither the
  stack nor the `R` record (CHANS can reach chunk 4 with many streams open),
  saved and restored, under the `DI` every entry already has. A program's
  own mapped pages survive every `PRINT #`.

**OPEN, CLOSE and growth go to the firmware** through the channel driver's
`CH_SEND`, with the mover running inside the exchange (the firmware may
read or write the directory), ending with op 0's report code:

1. **OPEN** sends the stream, mode, record length, name and `STATUS` (so the
   firmware refuses writing with the write-protect switch on). The firmware
   finds or creates the file and gives it an **extent**: its start on the
   board and the bytes allocated. A file opened for writing gets the free
   run after it as room to grow (trimmed at CLOSE). It writes the stream's
   **slot** on the board (below), and the ROM copies the slot into the
   record.
2. **Growth:** a write past the extent asks for more. The firmware extends
   the file in place, or moves it (op 6) and gives a new extent; no room is
   the RAM disk's full report, with the file intact up to there.
3. **CLOSE** writes the final length and position to the slot, pads an open
   record, and tells the firmware, which writes the length into the
   directory and trims the extent.

### Slots, power-off, NEW and reset

**The open-file slots**: one per stream 0–15, 32 bytes each, in the RAM
disk's top page after the superblock and outside its checksum. A slot names
the directory entry, the extent, the length, the position, the mode and the
record length. The firmware writes it at OPEN; while the stream is open the
ROM keeps the working copy in the record and **writes the length and
position back to the slot at every CR, whenever the position crosses a
256-byte boundary, and at CLOSE** (one window mapping each time). The ROM
reads `STATUS` at each write-back: the switch turned on mid-stream is
reported then, not silently dropped writes.

**Every `r:` command carries an open-stream mask**: a bit for each stream
0–15 whose STRMS entry points at an `R` record. The firmware closes every
slot whose bit is clear: its length goes into the directory and the slot is
freed. That covers, with nothing to clean up by hand:

- **Power-off and reset:** the boot reports nothing (open question 3), so
  the firmware closes every slot when it sees the 2068's boot exchanges, and
  the first `r:` command after a reset carries a mask with no stale bits
  anyway. (As built, only the mask closes them: see
  [Channels as built](#channels-as-built).) A file being written keeps everything up to
  its last CR or 256-byte boundary; the bytes after it are on the board but
  outside the length.
- **NEW,** which empties CHANS and STRMS without the ROM noticing: the next
  `r:` command's mask closes the slots. (`f:` handles NEW by replacing a
  stale entry at the next OPEN of the same stream; the mask is stricter.)
- **A program that clears a STRMS entry by `POKE`.**

The firmware reads the slots with the superblock at the start of each `r:`
command, so `CAT "r:"` shows an open file's current length.

**While a file is open** the firmware never moves it except to grow it for
its own stream, and refuses (Report F, as `f:` documents for a second OPEN
but never enforced): `ERASE` of it, `SAVE` over it, `MOVE` to it, a second
OPEN of it for writing, and `FORMAT "r:"` while any slot is open. Several
streams may read one file. Compaction treats open files as fixed.

### Question 3: modes, records, text

**Modes,** as `f:`: `r` read (the file must exist, else F), `w` write
(created or emptied), `a` append (created if missing, position at the end),
`u` update (created if missing, position 0, read and write), each with an
optional `b`. No mode is `r`. Output to an `r` stream is dropped (INPUT #'s
prompt goes to the stream too); `INPUT #` on `w` or `a` is Q. End of file is
NC NZ from the input routine, which WAIT-KEY turns into **8 End of file**.

**Records,** as `f:`: `OPEN #4,"r:data","u",30` makes a record file (1–254;
otherwise Q).

- `PRINT #4;TAB n;…` and `INPUT #4;TAB n;…` go to record *n*: TAB arrives
  as 23, lo, hi through the output routine, and the ROM computes the
  position, (*n* − 1) × reclen, itself. On a stream file `TAB n` is byte *n*.
- A CR ends the record and isn't stored; a short record is padded (spaces,
  or 0 in binary); a record that would run over its length is Q on the PRINT
  that made it, with nothing written; a record left open by `;` is padded at
  CLOSE; a TAB past the end pads the gap.
- Reading gives one record and a CR. `TAB 0` then a read gives the number of
  records (or bytes); `TAB 0` with data is Q.

Random access is where `r:` is far ahead of `f:`: each `f:` seek and record
is a command to the TS-Pico and an SD card mount, each `r:` one a
multiplication and a window.

**Text and binary.** `f:` translates on the Pico because its files are read
on a PC: keywords spelt out, LF line ends, UTF-8. An `r:` file stays on the
2068, so the ROM keeps its bytes as the 2068 wrote them, with a small
translation in text mode:

- **Text (default), writing:** colour and position controls (16–21 with one
  parameter, 22 with two) are dropped, the comma control (6) becomes spaces
  to the next 16-column stop (the record keeps a column), other controls
  except CR are dropped. Keywords stay single token bytes, so `LIST #4` then
  `INPUT #4;LINE a$` gives the line as the 2068 shows it, and `"a|b"`
  round-trips (unlike `f:`).
- **Text, reading:** LF and CRLF become CR, so a text file MOVEd in from the
  SD card reads line by line; other control bytes are dropped, so `INPUT #`
  never acts on them as editing keys.
- **Binary:** every byte as it is, except that 23 is always TAB (as `f:`).
  `INKEY$ #` is the way to read raw bytes.

Channel files are stored as `CODE` files with start 0, so `LOAD "r:name"
CODE addr` works on them, and any `r:` file (a SAVEd program, a screen) can
be opened `rb` to read its bytes. `MOVE "r:name" TO "tpi:name"` copies the
bytes unchanged: whether it should translate text files to PC text, as an
`f:` file would be, is open question 14.

### Channels as built

What the build (2026-11-19) settled, and where it differs from the sections
above:

- **The mask's transport** (open question 14): the channel commands
  (`tpi:rchopen`, `tpi:rchgrow`, `tpi:rchclose`) carry it as the first two
  bytes of their hex payload; every other `r:` command is preceded by
  `tpi:rmask`, from `TPI_SEND` (ERASE, MOVE, COPY, FORMAT, CAT `"r:…"`) and
  from `SET_MODE` (SAVE, LOAD, VERIFY, MERGE `"r:…"`). The firmware does not
  close slots on the boot exchanges: the first `r:` command after a reset
  carries a mask with no stale bits, which does the same.
- **`MOVE "r:…" TO "tpi:…"`** copies the bytes unchanged (open question 14):
  an `r:` text file is the 2068's bytes.
- **Listings take a pattern**, as `d:`'s do (open question 14).
- **A record that overflows** is Q with the position back at the record's
  start, but the bytes already written stay on the board (the section above
  says nothing is written; `f:` writes nothing because it sends the record
  whole). The length doesn't count them unless they were past it.
- **`RESTORE #` and `CAT #`** on an `r:` stream are Report O: the module's
  `STRM_REC` takes only `F` records.
- **A channel file holds at most 65535 bytes** (a tape header's length): Q
  past it.
- **ROM space:** 1767 bytes at EXROM 2400h–2AE6h (free in 2.4, in chunk 1),
  over the 600–900 estimated; the module itself gained only same-size hooks.
- **The window:** each board access maps a chunk of 4–7 holding neither the
  record nor the stack, with the mover's own routines (`MV_WIN`, `MV_MAP`,
  `MV_UNMAP`), so the HSR rule holds.

### Other streams on the RAM disk

- **A listing:** `OPEN #5,"r:"` reads the RAM disk's names, one per
  `INPUT #5`, then 8 End of file; `TAB 0` gives the count. As `d:`, it is
  read-only, and takes a pattern (`OPEN #5,"r:l*"`). The firmware writes the
  names, 10-byte records padded with 0, into clusters held only by the
  stream's slot (no directory entry) and opens it `r`, so the ROM reads it
  like any other file.
- **Capturing the printer:** `OPEN #3,"r:listing","w"`, then `LLIST` or
  `LPRINT`, then `CLOSE #3`. Needs checking that `CLOSE #3` puts P back, as
  it should for `f:` (testing).
- **Data that outlives a program:** a file written by one program and read
  by the next after `NEW` or `LOAD`, without the SD card. This is what the
  settings and save games of Jon's GUI need (open question 8).

### Coexisting with `f:`

Both kinds of record sit in CHANS under the same hooks and the same rules,
told apart by the letter. An `F` and an `R` stream can be open together; an
OPEN of a stream already open to either is **O Invalid stream**, as today.
`f:` commands never touch the board, and `r:` characters never talk to the
TS-Pico, so neither slows the other; their OPEN and CLOSE exchanges share
`CH_SEND`'s IDLE wait. The `f:` driver's own code doesn't change beyond the
three points under Question 1.

**ROM space.** The `r:` routines (open and close glue, the output and
input routines with text mode, records and TAB, the slot write-back) are
estimated at 600–900 bytes, in EXROM chunk 1's free space beside the
mover (5,239 bytes free before phase 4).

## Firmware

All on the v3 card; v2 is out of scope **(decided)**.

**Core 1 (C, `tsbus.c`): share the bus.**

1. In the OUT snoop (`tsbus.c` 858–906, beside the F4h/FFh shadow): port FDh
   stores the register number; port FCh, when that number is 00h–07h, stores
   the owner in `sh_own[n]` and resets `sh_sub[n]`. A change rewrites the
   chunk table.
2. `chunk_target` (`tsbus.c` 322) returns `tail` (not ours) for any chunk with
   `sh_own[c]` in 1–253, before looking at F4h and FFh.
3. The DOCK-write path (`tsbus.c` 908) skips owned chunks.
4. `sh_own` clears at power-on and on the card's own reset handling, and is
   resynchronised by the ROM's boot-time writes.

That's a few dozen lines on the existing, proven path: the same mechanism
that already follows F4h and FFh. Owner writes are I/O cycles, which last
over a microsecond, so the table is current before the next memory read.

**Core 0 (Python): the RAM disk.** Built in phase 5: the file system in a
new module, `TS/ramdisk.py` (pure, host-tested), the bus side in `tspico.py`
(`RD_MOVER` and the `RD_*` handlers):

- the `r:` file system: the format above, allocation, compaction;
- the mover's side of the protocol: sending operations, checking status;
- the `CAT`/`ERASE`/`MOVE`/`FORMAT` handlers for `r:` names;
- `tpi:ram` (info, backup, restore, alloc);
- later (phase 9), the `r:` channels' OPEN, growth and CLOSE, the open-file
  slots and the open-stream mask ([Channels](#channels-phase-9));
- the board's state: board found, `BASE`, `SIZE`, `STATUS`, from mover op 7
  at the start of every `r:` command (open question 3: the boot reports
  nothing). The firmware can't read the board itself, since it can't run bus
  cycles.

## PicoVideo

PicoVideo (github.com/timex-sinclair-projects/PicoVideo, v4.2) already sees
everything it needs: its GAL passes every I/O cycle to the Pico, and every
memory write to 4000h–7FFFh. It already skips a screen write when port F4h
maps chunk 2 or 3 away from HOME. The change, in `coreB/busMain.c`:

1. Two cases in the I/O-write `switch (gZ80AdrL)`: FDh stores the register
   number; FCh stores the value if that number is 02h or 03h (the owners of
   chunks 2 and 3). PicoVideo needs only whether a bank owns the chunk, not
   which 8K it shows, so it ignores `SUB`.
2. The two memory-write tests skip the write when the chunk's owner is 1–253,
   alongside the existing F4h tests.

About 15 lines. It applies unchanged to an RP2350 re-spin. The boards already
built need only a new UF2. **Without the update,** a PicoVideo shows stray
writes whenever a program maps a bank into chunk 2 or 3; nothing else is
affected.

**Built (2026-11-19)** in PicoVideo's `firmware/coreB/busMain.c` as above
(`mbReg`, `mbOwn2`, `mbOwn3`; PicoVideo PR #11), with a host test (`test_megabank` in
`firmware/tests/video_regression.c`): a bank 1–253 in `OWN2`/`OWN3` stops the
copy, 0/FEh/FFh and the other registers don't, a Spectrum 128 AY write
(FFFDh/BFFDh) lands on FDh as on the board, and the DF2 mirror and F4h tests
still apply. Firmware 4.4: `SOFTWARE_VER` (register 7Fh) reads 44h. Not yet built as a UF2 or run
on hardware. The missing `break` after `case 0xF4:` noted here before was
already fixed upstream.

**Exploration:** PicoVideo folded into this card as a second RP2350B, with
DVI/HDMI out of its HSTX, the AY in the HDMI audio, and a private path to
the SRAM for Jon's accelerated video:
[picovideo-merge.md](picovideo-merge.md).

## Bus mastering (revision 2)

Not this board. Jon (TS2068 list, 2026-10-20 and 22) proposes REU-style DMA:
the board taking the bus with /BUSRQ and writing frames from the banks
straight into the screen. That is possible through the SCLD's normal path,
not around it: the master has to make memory cycles the SCLD accepts. This
records what it would take, so the decision is there when a revision is.

**What the SCLD needs** (from Load ZX's TC2068 model, `rtl/scld.vhd` and
`rtl/scld_contention.vhd`; the US SCLD to be measured):

- **Z80-shaped cycles on the 2068's clock.** The SCLD latches its DRAM
  address multiplexer on a rising CPU clock edge after /MREQ falls, and makes
  the write strobe from /MREQ, /WR and the address. A master holds /MREQ and
  /WR over the same clock edges as a 3-T Z80 write, and steps on CLK (finger
  B9), since contention works by stopping that clock.
- **/BE released.** The SCLD's write strobe needs /BE high: a master writing
  HOME must not pull it, and the board's own `serve` path, which sees the
  master's cycles on the bus like any others, must be held off meanwhile.
- **The free windows.** The SCLD fetches the picture in the first half of
  each of the 192 picture lines: about 27 µs a line, about 5.3 ms a frame,
  are free, plus about 4.4 ms of border and blanking. About 58% of a frame.
- **Refresh.** The Z80 does no refresh while it has given up the bus, and it
  does not float /RFSH, so a master can't make refresh cycles. Bus holds must
  stay short: a natural pattern is to hold the bus in the free half of each
  picture line and give it back in the fetch half, when the Z80 would be
  contended anyway. A single hold across all of the blanking would pass the
  DRAM's 4 ms.

**What the board would change:**

| What | This board | Revision 2 |
|---|---|---|
| U1/U2, address | Fixed inward, always on | Direction and enable from the controller: in (normal), off (the controller addresses the SRAM privately), out (a master cycle). |
| A8–A12 | To the SRAM only | Also to the controller, for private SRAM addresses and master cycles: 5 GPIOs, or 2 for a 74LVC573 loaded from SD0–SD7 (the high byte changes every 256 bytes). |
| /MREQ, /RD, /WR, /IORQ | Inward on U4 | A 74LVC245 of their own, sharing U1/U2's direction and enable. With it off, the controller drives the local /RD and /WR nets, which already feed SRAM /OE and the /WE gate. |
| U3, data | DIR = bus /RD | DIR = /RD XOR "master" (a 74LVC1G86), since a master write holds /RD high. |
| /BUSRQ (A23) | Not connected | Out through a third 74LVC1G06, open-drain: 1 GPIO. |
| /BUSAK (A21), CLK (B9) | Not connected | In, on the U4 channels freed above: 2 GPIOs. |
| /RFSH | Inward | Unchanged (above). |

**Independent source access** is what makes the private SRAM state needed:
one address bus can't select a byte in a bank and a different byte in HOME
at once. The controller reads the source with U1/U2 off, then drives the
host write.

**Pins are the obstacle.** That is about 10 more GPIOs; the RP2350B has 2
spare (46 of 48 used), on test pads TP4 and TP5 so that the handshake can be
tried on the bench with a second board wired to them. A latch for A8–A12 saves 3, an I²C expander for
BAT_EN, the write-protect switch and the /NMI output 1 more, dropping the
/NMI input 1 more (/INT stays: a master needs it for frame sync). Still about
3 short. So revision 2 probably adds a **second controller**, an RP2350A on
the board's local buses as the bus master, with the RP2350B keeping the
memory map. The same chip is where Jon's onboard processing (decoding,
compositing) would run.

**Other cards** see a master's writes as Z80 writes: PicoVideo mirrors them
into its display, as wanted. The v3 card must not answer chunks 2–3. Another
card using /BUSRQ would need arbitration.

**The first test** (Jon's): a few host-clocked writes to HOME outside the
picture fetch, from a small local buffer, /BE released, watching address,
data, the DRAM strobes and the buffer directions; then the same during
contention, checking that the stretched cycle completes. It needs master
hardware: neither this board nor the v3 card can run bus cycles.

## Testing

**Before any hardware:**

1. **Does the 2068 itself answer FCh or FDh?** The Spectrum's ULA answers
   any even port. If the 2068's SCLD decodes FEh partially, `OUT 252,n` would
   change the border and `IN 252` would read the keyboard, colliding with the
   board. In BASIC: `OUT 252,2` (border red?), `OUT 253,2`, and
   `PRINT IN 252, IN 253` with no keys pressed (FFh means nobody answered).
   **Done (2026-10-16), on David's 2068 with the TS-Pico fitted:** `OUT 252,2`
   and `OUT 253,2` left the border alone; `IN 252` and `IN 253` each drift
   between 127 and 255 from one read to the next, whatever keys are held,
   while `IN 254` reads the keyboard (30 with ENTER still down). So nothing
   answers FCh or FDh: the reads are a floating bus, D7 keeping whatever
   charge is left on it. **FCh/FDh stand (decided).**
2. **The 2068's own data buffers stay off for FCh/FDh.** Step 1 cannot
   show this: a buffer turned on facing an undriven bus reads as a floating
   bus too. If the SCLD turns on the 2068's data buffers (U5 and U9,
   74LS245) for an I/O cycle it does not answer, then on `IN` from FCh/FDh
   they fight the board's data buffer. Gustavo's test: a logic analyser on
   pins 1 (DIR) and 19 (/G) of U5 and U9, with /IORQ, /RD, /WR and /M1, and
   this program, assembled with ZEUS and run:

   ```
   10 org 40000
   20 ent
   30 DI
   40 LD B,0          ; 256 passes
   50 LBL1: IN A,(#FC)
   60 IN A,(#FD)
   70 OUT (#FC),A
   80 OUT (#FD),A
   90 DJNZ LBL1
   100 EI
   110 RET
   ```

   (Gustavo's listing has `LD B,256`; B is 8 bits, and 0 gives the same 256
   passes.) Pass: none of the four pins changes state in any of these I/O
   cycles. Run it with nothing on the bus and again with the TS-Pico fitted.
   Eight channels at 24 MHz are enough (an I/O cycle is about 1 µs): a
   USB analyser of the fx2lafw kind with PulseView takes 5 V inputs. The v3
   card can't stand in for one: U5 and U9 are on the 2068's board, not the
   edge connector, and v3 doesn't bring in /M1.
   The v3 card answering `IN 0Eh` without trouble is a good sign, not proof:
   FCh and FDh sit next to the SCLD's own F4h, FEh and FFh. If they do
   change, see open question 15.
3. **An emulator model.** Add the board to our ZEsarUX fork (memory reads and
   writes through the owner table, ports FCh/FDh). The ROM and firmware can
   then be developed and tested with no hardware, through the existing
   `tools/emu` bridge. **Done (2026-10-16), merged:**
   [zesarux-tspico#2](https://github.com/timex-sinclair-projects/zesarux-tspico/pull/2),
   documented in its `MEGABANK.md`. Off unless `MEGABANK_FILE=path` (the RAM
   kept in that file: the battery) or `MEGABANK=1` is set; `MEGABANK_BASE`,
   `MEGABANK_SIZE` and `MEGABANK_WP` configure it. `tspico/megabank_test.py`
   checks it over ZRCP on the stock ROM, in CI. Not modelled: timing, the
   battery-low bit, snapshots. Note for test scripts: ZEsarUX's hard reset
   does nothing on a TS-2068; use its plain reset (`reset-cpu`). **Also not
   modelled: the HSR rule.** The model treats an owned chunk as the
   board's whatever F4h says, so a write that would leak into HOME RAM on
   hardware (HSR bit clear) passes there.
4. **ROM host tests** for the mover's window choice and the boot-time
   detection, in the style of the existing `rom_*_hosttest.py`. **Done
   (phase 4):** `src/test/rom_megabank_hosttest.py` runs the boot reset,
   every mover operation and the `r:` gates in a Z80 interpreter against a
   board model that does enforce the HSR rule, and fails on any write it
   would leak.
5. **Firmware host tests** for the file system: format, save/load/erase,
   fragmentation and compaction, a corrupt superblock, the generation counter.
   **Done (phase 5):** tspico-firmware-build's `src/test/ramdisk_hosttest.py`
   (in CI), with power cuts mid-entry and between a replace's two writes, a
   full directory and write-protect besides; `tools/emu/megabank_smoke.py`
   runs every `r:` command end to end in ZEsarUX's model.
6. **The controller's firmware** on an RP2350B development board, with the
   v3 card's PIO programs as the starting point: register writes and reads,
   the owner table, /BE and the page bits against a logic analyser, before
   any Megabank PCB exists.

**On hardware:**

1. A prototype on the extender, with a scope on /BE, /MREQ, /RD and D0–D7, in
   every slot: /BE must win against the 2068's own memory with margin
   (open question 2).
2. A memory test from the 2068: every page, every chunk, pattern and
   address-in-address, with the v3 card and PicoVideo both plugged in.
   **Passed (2026-11-20)** with the v3 card (`mbmem`; PicoVideo not yet
   fitted).
3. Battery: fill the board, power off for a day, verify; measure the standby
   current.
4. Power-fail: switch off during a long `SAVE "r:"`. Other files must survive;
   the one being written may be lost. **Passed (2026-11-20)**
   (`firmware/controller/basic/mbcut.bas`): `SAVE "r:long" CODE 32768,32000`
   in a loop, each round replacing the last, and the 2068 switched off
   mid-loop with the controller's USB unplugged, so the board lost its
   supply too and ran from the battery; the TS-Pico soft-reset before
   power came back. Then 0 FAIL: `keep` verified, `long` verified (a
   whole copy survived the SAVE that was cut), and a new SAVE and two
   ERASEs worked.
5. The RAM disk end to end: every SAVE/LOAD form, MOVE both ways, backup and
   restore, a full disk, compaction.
6. `tools/hwcheck.py` grows a RAM-board section. **Written (2026-11-19):**
   `--megabank`, tests 2, 4 (by hand) and 5.

**Channels (phase 9),** in the emulator first, as `f:` was:

1. `PRINT #`, `INPUT #`, `LIST #`, `INKEY$ #` in every mode; records with
   `TAB` both ways; reading past the end; a file that must grow and move.
2. A few thousand characters through a stream with a program's own page
   mapped in each of chunks 4–7: the page must survive, the stream must not
   crash.
3. 32 errors in a row inside the channel (the `f:` bank-stack test).
4. `f:` and `r:` streams open together; closing them in every order (the
   offset fix-up with 256- and 512-byte allocations).
5. NEW, a reset and power-off with a stream open: the file keeps everything
   to its last CR, its slot is closed, `ERASE` then works.
6. The write-protect switch turned on mid-stream.
7. `OPEN #3,"r:…"` with `LLIST`, then `CLOSE #3`: P must come back.
8. The cost per character, against the estimate.

**Run on hardware (2026-11-19)** on a v3 card in a 2068 with the board:
ROM 2.5 in flash slot 1, firmware `40cd82d` (tspico-firmware-build#430),
`tools/hwcheck.py --megabank-channels` with `basic/SD/TAP/test/mbchan.bas`
and `mbchpg.bas` (the same programs pass in ZEsarUX). **All 40 passed**
(NEW on a repeat), **2 for information:**

- Steps 1, 2, 3, 4 and 7: all 32 automatic tests pass. That covers every
  mode, records with `TAB` both ways, Q for a record overflow, `TAB 0` with
  data and `INPUT #` on a `w` stream, binary against text, listings with a
  pattern, F for `ERASE` of an open file, and a 20000-byte file that must
  move to grow past another. A board page in each of chunks 4–7 survives
  3000 characters, 32 End of file errors in a row leave the stream working,
  `f:` and `r:` open together close cleanly in all six orders, and `LLIST`
  into `OPEN #3,"r:list"` hands P back at `CLOSE #3`.
- Step 5: a reset and a power-off each keep the file to its last CR, and
  `ERASE` works after both. **NEW** keeps `nw` at 5 bytes (`kept` and its
  CR, not the unterminated `lost`), and `ERASE` works. The first try gave
  10 bytes, `kept` CR `lost` CR, which is what the second `PRINT #`
  without its trailing `;` gives; typed again with it, 5.
- Step 6: with the switch on, `PRINT #4;"two"` stops with J, and `CLOSE #4`
  with the switch off is 0 OK. But `wp` is then **8 bytes**, `one` CR `two`
  CR: CLOSE passes the ROM's own length, which counted the bytes the board
  refused, so the file's last 4 bytes are whatever the board held. A fix:
  after J, the stream's length goes back to the slot's last written-back
  value.
- Step 8, **the cost: about 4.3 ms a character**, timed from the firmware's
  telemetry. 2000 characters through `PRINT #` took 11.17 s, the same loop
  without them 2.60 s, and ZEsarUX gives 3.8 ms. That is nearly 30 times
  the 0.15 ms estimate, and the estimate left out the per-character entry.
  Reading the code puts about 4,000 T-states (1.1 ms) in the `r:` code
  itself, and most of the rest in the entry around it, both ways, every
  character: the HOME stub's `CALL 03FC`, the stock bank call through its
  RAM copy at 65D0h, and `GUARDED`'s trap setup and teardown. The window
  is mapped and unmapped (`MV_WIN`, `MV_MAP`, `MV_UNMAP`) for every byte,
  and every CR writes the length and position back through four more.
  [Open questions](#open-questions) 21 has the way out, now built and run
  on the board (below).

**Run on hardware again (2026-11-20)**, with the fast entry
([Open questions](#open-questions) 21): the same card, firmware and
programs, with `TSPICO-MB.ROM` (since renamed `TSPICO-25.ROM`, the release
ROM) from tspico-firmware-build `main` at
`b3e6fb8` (sha1 `c56ae6c3…`) in flash slot 1. That ROM has the
write-protect fix (timex-sinclair-projects/tspico-firmware-build#432), the
fast entry (#433), its late-switch fix (#434) and the record's output
stub (#436). **All 40 passed, 2 for information**, with no firmware errors
in the telemetry:

- Steps 1, 2, 3, 4 and 7: all 32 automatic tests pass, as before.
- Step 5: NEW, a reset and a power-off each keep the file to its last CR
  (5 bytes), and `ERASE` works after each. NEW passed first time.
- Step 6: J with the switch on, 0 OK for `CLOSE #4` with it off, and `wp`
  is now **4 bytes**, `one` and its CR: after J the stream's length goes
  back to the slot's last written-back value (#432).
- Step 8: 2000 characters through `PRINT #` took 2.71 s, the same loop
  without them 2.57 s. **That puts the average at no more than about
  70 µs a character, against 4.3 ms**. The figure is a difference between
  two nearly equal times, and the second loop does a `LET` where the first
  prints, so it is a bound, not a measurement: 100 CRs at ZEsarUX's 4 904
  T-states each would account for most of the 0.14 s on their own. A
  finer timing needs a longer run or the board's own cycle count.
- Typing in the editor: no dropped keys. #433 alone, the first ROM tried
  that day, dropped random keys with the error rasp; that was the
  late-switch defect #434 fixes ([Open questions](#open-questions) 21).

## Phases

Each phase is its own branch and PR, and each PR updates the programmer's
reference for what it changes.

1. **Port test** on a 2068 (testing step 1). **Done (2026-10-16):** FCh/FDh
   stand. Still to do: the buffer test (testing step 2).
2. **Emulator model** in the ZEsarUX fork. **Done (2026-10-16):**
   [zesarux-tspico#2](https://github.com/timex-sinclair-projects/zesarux-tspico/pull/2),
   merged into `tspico-device`; its test passes on macOS, Linux and Windows.
3. **v3 firmware: bus sharing** (core 1). Small, and testable in the emulator
   and on a v3 card with no board (nothing changes until something writes
   FDh/FCh). **Built (2026-11-18)** in tspico-firmware-build#425 (`tsbus.c`
   yields the chunks a board bank owns). **On hardware:** every board test
   in phases 4, 5, 7, 8 and 9 runs through it.
4. **ROM: detection, boot reset and the mover**, plus the `r:` hook.
   **Built (2026-11-19)** as test ROM 2.5 in tspico-firmware-build
   ([ROM](#minimal-rom-detection-reset-mover-r-phase-4)). **On hardware
   (2026-11-20):** `mbph4.bas`, 11 of 11 (detection, the state after boot,
   and the mover's window: no leak into HOME RAM, a program's page put
   back). **Released (2026-11-20):** ROM 2.5 is the release ROM,
   `TSPICO-25.ROM` in flash slot 1 (tspico-firmware-build#441), in TS-Pico
   2.5 (tag `v2.5`). On that release, with the board fitted, `tools/hwcheck.py
   --megabank` gave 75 passed, 0 failed.
5. **Firmware: the RAM disk** and `tpi:ram`. **Built (2026-11-19)** in the
   emulator: tspico-firmware-build, `src/TS/ramdisk.py` and `tspico.py`'s
   `RD_*` (PR #427); host-tested (`src/test/ramdisk_hosttest.py`,
   `rd_cmds_hosttest.py`) and run end to end in ZEsarUX's Megabank model
   (`tools/emu/megabank_smoke.py`, 39/39). **On hardware (2026-11-20):**
   `mbdisk` (hardware test 5), 27 of 28 on firmware `40cd82d`: every
   SAVE/LOAD/VERIFY form, MOVE, ERASE, COPY both ways, a full disk, a 64K
   disk and restore pass, with no firmware errors in the telemetry. Test 22,
   `tpi:ram backup` into the current folder, wrote the whole image but
   ended with J: the listing's refresh ran with the SD card unmounted.
   Fixed in tspico-firmware-build#440 (in 2.5): a backup gives 0 OK. `tpi:ram alloc` is still to
   shape (open question 8).
6. **PicoVideo** update (its own repo). **Built (2026-11-19)** and
   host-tested ([PicoVideo](#picovideo)); not yet on hardware.
7. **Board:** schematic, the controller's firmware, PCB (assembled by
   JLCPCB), prototype, hardware tests. **Routed (2026-10-18; re-routed 2026-10-19 with /INT and /NMI):** schematic
   generated (ERC: one expected warning), v3's form factor, routed by v3's
   flow, DRC clean ([hardware/](../hardware/README.md)). **Built:** boards 1
   and 2 run the controller firmware (`firmware/controller/`) in a 2068: banks
   map into every chunk, DOCK and EXROM included, /BE timing measured, battery
   retention passed (2026-11-18 and 19). **Software for the rest (2026-11-19):**
   `BASE`/`SIZE` in the controller's flash, set over USB (open question 11);
   the battery measured hourly; the memory test (`firmware/controller/memtest/`,
   hardware test 2, checked in ZEsarUX); tspico-firmware-build's
   `tools/hwcheck.py --megabank` (test 6), which runs it and a RAM disk test
   (`basic/SD/TAP/test/mbdisk.bas`, test 5) and walks through the
   write-protect switch and a power cut (test 4). In ZEsarUX's Megabank model
   the memory test passes (and catches a planted fault), and `mbdisk` passes
   28 of 28. It first found a phase 5 firmware bug: the `r:` statements'
   bare statuses said IDLE, so a program's LOAD or VERIFY straight after a
   SAVE sometimes lost its first block command (report 29); they now say
   READY without IDLE, as the channel commands do (tspico-firmware-build
   #428). **The memory test on the board (2026-11-20):** `mbmem`, all 64
   banks through chunks 4–7 (read back through 2 and 4–7), pattern and
   complement: **PASS**, with the v3 card fitted (no PicoVideo). Test 5
   has run too (phase 5, above). Test 4, a power cut during a
   `SAVE`, passed too (Testing). Still to run: test 3's standby current.
8. **ROM: the Timex bank API** made to work on the board (see [ROM](#rom)).
   Last, because nothing above depends on it. **Built (2026-11-19), all of
   it** ([The Timex bank API on the board](#the-timex-bank-api-on-the-board-phase-8),
   tspico-firmware-build#429): in the Z80 model and ZEsarUX. **On hardware
   (2026-11-20):** `mbbank`, 18 of 18. Hardware also found that `TIMEX_HSR`
   left port FFh's bit 7 (EXROM) set when it gave chunks back to HOME, so
   ZX48 mode (`OUT 244,3`) restarted the 2068; it now clears it (`TO_HOME`,
   in #441), and ZX48 mode boots.
9. **ROM and firmware: `r:` channels** ([Channels](#channels-phase-9)).
   Depends on 4 and 5 only, not on 8, so it can go before it. **Built
   (2026-11-19)** ([Channels as built](#channels-as-built)): the Z80 model
   (`src/test/rom_rchan_hosttest.py`, 30/30, the ROM against the firmware's
   own `ramdisk.Disk`), the firmware's host tests (`ramdisk_hosttest.py`,
   78/78) and ZEsarUX end to end (`tools/emu/megabank_smoke.py`, 54/54).
   **On hardware (2026-11-19):** every Channels testing step on a v3 card
   with ROM 2.5 and firmware `40cd82d`, all 40 passed (NEW on a repeat),
   with a character costing about 4.3 ms ([Testing](#testing), Channels).
   **Again (2026-11-20)** with the fast entry of open question 21: all 40
   passed (NEW first time), `wp` is 4 bytes after J, and a character costs
   at most about 70 µs on average.

## Open questions

1. **Do ports FCh/FDh collide with the 2068 itself?** **Answered
   (2026-10-16): no** ([Testing](#testing), step 1). A repeat with nothing on
   the expansion bus would remove the last doubt; the TS-Pico answers only
   0Eh/0Fh. That the 2068's data buffers stay off is still to be measured
   (testing step 2, open question 15).
2. **/BE timing** through the expansion bus, in every slot, with the v3 card
   and PicoVideo fitted. Measured on the prototype. The v3 card's PIO path
   already wins this race on a 2068; the board's must too. Also measured: how
   the page bits are produced (a second PIO table, a DMA-chained lookup or
   core 1), and that they settle well inside the read.
   **First results (2026-11-18, board 1, slot 2 with SW2 closed, v3 card in
   slot 1, 150 MHz):** /BE from the `be` PIO table and the page bits from a
   DMA-chained lookup (`firmware/controller/`). Reads from a bank in chunk 5
   come from the board's SRAM, and two banks hold different bytes at the
   same address. **But /BE does not stop a write reaching the 2068's own
   RAM:** after writing 111 into bank 1 and 222 into bank 2 at A000h, the
   2068's own RAM there read 222. So every write to a mapped bank also lands
   in the 2068 RAM underneath it.
   **Not our timing (2026-11-18, 250 MHz).** The same test gives 111, 222,
   222 at 250 MHz. The controller's own capture (console `c`: GP16-GP47
   every 8 ns, from PIO2) of a mapped write to chunk 5, in ns from /MREQ
   falling: BE_REQ and nDOE at +40, /CE1_0 low at +80, /WR low at +280,
   /MREQ and /WR high at +560, BE_REQ off at +576. (The /BE and WINH rows
   of that capture were not real: see below. Measured properly on board 2,
   /BE is low at +32 to +40.) /BE is down about 240 ns before /WR, and the
   2068's HOME RAM still takes the write.
   **Answered: set the chunk's HSR bit (2026-11-18).** With `OUT 244,32`
   (chunk 5 to DOCK) as well, writes reach the board's SRAM and the 2068's
   RAM keeps its byte (77 before, 77 after). Reads in DOCK then came back
   wrong (1, 0) until the v3 card's DOCK serving was switched off, after
   which the whole test passed: 111, 222, 77, and 33 written HOME and read
   in DOCK. The rule is in [Sharing the bus](#sharing-the-bus). Later the
   same day, with the v3 card yielding owned chunks: DOCK, EXROM (FFh bit 7)
   and chunks 0-1 over the ROM all pass.
   **/BE's timing, measured (2026-11-19, board 2 in slot 1, the v3 card in
   slot 2 behind SW2, 250 MHz).** The capture had never sampled GP32-GP47:
   the SDK moves a PIO's GPIO window only with no programs loaded, the
   console loaded them first, and the call failed silently, so the rows for
   the bus's /BE (GP42) and WINH (GP38) showed GP0-GP15. Every earlier /BE
   timing from it, the "slow rise" (released at +576, high at +640 or
   later) included, was that. Fixed (`la_setup`), and checked with the 2068
   held in reset: BE_REQ by hand pulls /BE low, and it reads high again
   within about 30 ns of release. Then with code running from a bank in
   chunk 5 (`basic/mbloop.bas`; console `b`, 5 s, about 205,000 owned
   cycles), in ns: /BE low 32-40 after /MREQ falls; BE_REQ off 16-24 after
   /MREQ rises; /BE back high 64-96 after it rises (about 50-70 ns after
   release, with the v3 card's 2.2 k pull-up). The tightest gap is a fetch
   followed by its refresh, 56-80 ns, and the board never pulls /BE on a
   refresh; between two owned cycles there are at least 192 ns. With
   nothing mapped (`a`), /BE is low only on chunk 0-1 reads, the v3 card
   serving its ROM, and it releases 80 ns after /MREQ rises.
   **Board 2, and the boards swapped.** Board 1 in slot 1 with the v3 card
   in slot 2, then board 2 in slot 1: DOCK (`mbdock`), EXROM (`mbexrom`)
   and chunks 0-1 (`mbrom`) pass on both. The v3 card's /BE still reaches
   the 2068 from slot 2, and the card still yields the board's chunks.
   Still open: other 2068s and SCLD revisions; slot 3, and PicoVideo
   fitted.
3. **How the ROM tells the TS-Pico about the board at boot**: a new command,
   or a field in an existing boot exchange. **Decided (David, 2026-11-19):
   it doesn't; the firmware asks.** Mover op 7 returns found, `BASE`, `SIZE`,
   `STATUS` and `VERSION`, so the firmware learns about the board the first
   time an `r:` or `tpi:ram` command needs it, from inside that command's
   answer, gated on the pre-header's ROM_ID (25h or later). The boot sends
   nothing and adds no time. A boot-time push would need a new command at
   the end of the boot (the boot exchanges carry no spare field) and would
   only be read by the v3 card's bus-sharing code, which watches OWNn
   writes already.
4. **A write fence for the RAM disk?** `WPROT` protects only chunks that are
   mapped. A program that maps a RAM disk page by mistake can still corrupt
   files. A `FENCE` register (banks at or above it are read-only unless
   unlocked) would close that, at the cost of a register the ROM must set at
   boot.
5. **Scope of the Timex bank API work**
   ([ROM](#the-timex-bank-api-on-the-board-phase-8)). **Answered
   (2026-11-19): all of it**, items 1–7, with no nibble-wide translators
   (nothing old needs them).
6. **Parts, for JLCPCB assembly** ([Hardware](#parts)):
   - **The SRAM.** The ×8 mode is confirmed in both datasheets. Still open:
     whether the DV30 is current (its only datasheet found is from 2006; the
     EV30's is Infineon's, 2020), and stock, which is thin at JLCPCB (42 EV30s
     and 168 DV30s on 2026-10-16). The BHE/BLE difference between them is
     handled by the 0 Ω pads. If it runs out, switch to the
     IS62WV51216 (3,623 in stock), a different footprint, before layout. The
     earlier candidates were not at JLCPCB on that date: AS6C4008 (5 left),
     AS6C8008, CY62177G30, IS62WV20488, and Everspin MRAM.
   - **The RP2350B's stepping.** Use a stepping without the E9 pad erratum
     (a pull-down that latches), or don't rely on internal pull-downs.
   - **The edge fingers' sides:** confirm that side A is the top, as v3
     assumes.
7. **A name** for the board.
8. **What Jon, the GUI author, needs** from `tpi:ram alloc` and from the ROM: page
   allocation from BASIC, a machine-code entry point, a page size other than
   8K? Worth showing him this document. **Partly answered (2026-10-27)** by
   his ZXDesk, a GUI that takes the machine over ([ZXDesk](#zxdesk)):
   - **Its first need takes nothing from the ROM:** a storage backend in
     eight pages (a directory and seven 8K files), mapped one at a time
     into chunk 7 by the board's registers. 8K pages suit it: they are
     the size of its 128K backend's files.
   - **It never calls the ROM** once running, so a ROM entry point is no
     use to it while it runs. Allocation must be something its BASIC loader
     does before `RANDOMIZE USR` (`SAVE "tpi:ram alloc zxdesk 8"`, say,
     leaving the first page's number where the program can find it), or
     something the program can read for itself from the superblock, by
     mapping the RAM disk's top page. The allocation needs a name, so the
     program finds the same pages at every start.
   - **Until then** it uses the board's first bank, pages 0–7.
     `tpi:ram alloc` must then hand those pages to ZXDesk rather than to
     someone else, or ZXDesk must move its files when it first sees an
     allocation.
   - **Still open:** whether `r:` files from a program that has taken the
     machine over (notes swapped with BASIC) are worth a machine-code
     entry to the mover, and that entry's form.
9. **SYSCON entries for RAM banks.** SYSCON holds at most 10 entries (its
   scratch space at 5FE9h–5FFFh is hard-coded). Nothing validates bank numbers
   against it, so RAM banks may not need entries at all. **Phase 8 adds
   none:** nothing in the bank API reads SYSCON, and the boot still ends it
   with the 80h marker. Open only if a program turns up that looks for
   banks there.
10. **The bank stack** holds 16 frames of 4 bytes with no overflow check.
    Deep `CALL_BANK` nesting could overrun it. Phase 8 leaves it as it is:
    65CEh's pointer starts at 65CEh and grows down through 658Eh–65CDh,
    FFh bytes that look free (so nothing new is kept there). Still open:
    whether a check is worth the bytes.
11. **The controller's firmware and settings.** How its firmware is loaded
    (a surface-mount USB-C connector, or SWD pads), and how `BASE` and `SIZE`
    are set: over USB, or from the 2068 through a guarded register
    (`SAVE "tpi:ram base 65"`, say). **Over USB for now (2026-11-19):** the
    console's `S` writes them to the last sector of the controller's flash;
    firmware loads over the USB-C connector. A 2068-side route is still open.
12. **Standby with the controller off.** Settled on paper for the EV30
    ([Parts](#parts)). Still to check: that no path lets the battery feed the
    RP2350B or the buffers, and the real standby current on the prototype
    (hardware test 3), with the DV30 too if it is fitted.
13. **Where the bus's /BE pull-up is.** The board's /BE driver is open-drain
    and the board adds no pull-up, relying on the one already on the bus.
    **Answered (2026-11-19):** it is on the v3 card, 2.2 k to 5 V, added
    after the card's first design, and it is enough: /BE is back high 50-70
    ns after the board releases it, at least 56 ns before the next cycle
    (testing step 2). Still open: a 2068 without the v3 card, where nothing
    would pull /BE up at all (R3's pad, DNP, is for that).
14. **Channels** ([Channels](#channels-phase-9)): agree the recommendations
    (CHANS records with the `f:` hooks, direct access through the window,
    the slots and the open-stream mask, text mode in the ROM). **Answered
    (2026-11-19):** adopted as written; `MOVE "r:…" TO "tpi:…"` copies the
    bytes unchanged; listings take a pattern; the mask travels in the
    channel commands' payloads and in `tpi:rmask` before every other `r:`
    command (tspico-firmware-build's `docs/PROTOCOL.md` §6.3).
    [Channels as built](#channels-as-built) has the rest.

15. **If the 2068's data buffers turn on for FCh/FDh** (testing step 2),
    how the board keeps them off. First check whether /BE disables them in
    an I/O cycle as it does in a memory cycle; if so, the controller pulls
    /BE for its own `IN`s too (a firmware change). If not, other ports, or a
    board change. Either way the board must not drive D0–D7 against them.
16. **/INT and /NMI on the board** (Gustavo's suggestion). **Added
    (2026-10-19):** /INT (finger A14) and /NMI (A15) come in through a
    74LVC2G34 to GPIO43 and GPIO44, and GPIO45 pulls /NMI low through a
    second 74LVC1G06, open-drain like /BE. The controller can see the frame
    interrupt and raise NMIs of its own, for mapping that the Z80 does not
    set up through FCh/FDh. Still open: what the firmware does with them. No
    register uses them yet. Note that the v3 card also drives /NMI (its
    button and firmware), so an NMI handler can't tell who raised it unless
    the board says so in a register.
17. **SYSCON's space and the TS-Pico.** An analysis of Gustavo's ROM
    describes 5DDDh–633Fh as reserved for future TS-Pico releases. That
    range takes in SYSCON's table (5EEAh–5FFFh), which FOR, RESTORE and USR
    read on every 2068 ([What the ROM needs from
    SYSCON](#what-the-rom-needs-from-syscon)). Ask Gustavo what is planned
    there. The firmware repo itself shows no such reservation: its ROM uses
    only 5D37h and 5DCDh–5DDBh in that range.
18. **Extended RAM for BASIC.** Using the board for BASIC's own program
    and data, as BASIC XE and BASIC 7.0 did, is explored in
    [basic-extended-ram.md](basic-extended-ram.md): overlays and page
    files first, then the program text in banks (EXTEND), sharing the
    ROM's AROS path with the cartridges that use it. The direction is
    decided; the design is not.
19. **Is a bank in chunk 2 or 3 contended?** The SCLD manages the 2068's
    lowest 16K (4000h–7FFFh): Z80 accesses there go through it and are
    slowed while it fetches the picture, and it keeps generating video when
    the Z80 maps something else over those chunks. When the board owns
    chunk 2 or 3 and pulls /BE, does the SCLD still stretch the clock? That
    depends on whether its contention logic looks at /BE (its HOME select) or
    only at A14–A15 and /MREQ. If the latter, the board in chunks 2 and 3
    runs at contended speed, not "full memory speed". To test: time a loop
    of reads from a bank in chunk 2 and in chunk 4, during the picture, or
    watch CLK on a scope. Raised by David (2026-10-20) after Jon's video
    proposal, which also suggests a use if the answer is "uncontended":
    PicoVideo could take uploads written into a bank-owned chunk 2 or 3 at
    full speed (its change under [PicoVideo](#picovideo) would then need a
    mode that keeps such writes rather than ignoring them).
    **The TC2068 model says contended (2026-10-22).** In Load ZX's VHDL
    model of the Portuguese SCLD (`rtl/scld_contention.vhd`), the CPU is
    held off when A15–A14 = 01 or /IORQ is low, during the first half of
    each of the 192 picture lines. It looks at neither /BE nor /MREQ (the
    /MREQ term is commented out). So a bank in chunk 2 or 3 is contended
    like the 2068's own RAM, and so are I/O cycles during the picture,
    FCh/FDh included. The model marks its /BE input "TBD", and the US SCLD
    may differ: the bench test stands, but plan on "contended". The model
    also has the SCLD strobing the upper 32K's CAS lines, uncontended.
20. **LEAP ROMs on the board.** Charlie Day's LEAP ROMs keep their system
    variables at 8xxxh, don't use the Timex bank code, and page the EXROM
    into chunk 6 by writing F4h = 40h and then 0 directly. On the board that
    clears the HSR bit of any bank mapped elsewhere, so writes there reach
    HOME RAM too. A LEAP build for the TS-Pico and the board needs either
    LEAP to read-modify-write F4h, or the board (or the TS-Pico) to keep a
    board-owned chunk's writes out of HOME RAM whatever F4h says. The TS-Pico
    must also serve the EXROM outside chunks 0–1. Raised by David
    (2026-11-19); the bank API already lets the EXROM go in any chunk.
21. **A fast entry for channel I/O.** **Built (2026-11-19) and run on the
    board (2026-11-20): all 40 Channels checks pass**
    (timex-sinclair-projects/tspico-firmware-build#433, with #434 and #436
    after it; [Testing](#testing), "Run on hardware again"). A character
    through `r:` cost about 4.3 ms on the board ([Testing](#testing), Channels step 8), most of it
    the stock Timex bank call that every `f:` and `r:` character paid going
    into the module and coming back. That mechanism was broken as designed
    and nothing used it, so the ROM was free to replace it. T-states per
    `RST 10h`, measured in ZEsarUX's Megabank model with ZRCP's T-state
    counter (`tools/emu/chan_speed.py`, the loop's own cost taken off):

    | Case | Before | After |
    |---|---|---|
    | `r:` text, a stream file | 13 338 | 817 |
    | `r:` CR (with its write-back) | 22 375 | 4 904 |
    | `f:` text (64-byte buffer, `tpi:chwr`) | 19 154 | 10 608 |

    So an `r:` character went from about 3.8 ms to 0.23 ms in the
    emulator. The 817 T-states are the whole of `RST 10h`, HOME's own
    output path included, against a target of a few hundred for the
    channel. What was built, against what was proposed:
    - **The entry**, as proposed. HOME's `F` and `R` record stubs (14A0h,
      14A9h) and the BEEPER thunk go to `FASTH` at 1265h, in the bytes of
      CHAN-OPEN's banked-channel branch (no such channel exists; that branch
      is now Report O). Under `DI` it sets FFh bit 7 and F4h bits 0–1 with
      direct `OUT`s, at matched addresses so the switch needs no RAM code:
      the `OUT` at HOME 002Eh is followed by the EXROM's `JP` at 0030h, and
      the `OUT` at EXROM 1399h by HOME's `RET` at 139Bh (on the board that
      raced the card; #434 replaced both, below). No bank-call frame.
      An error raised in the EXROM goes through its RST 8, which pages HOME
      without a frame, so it needs no trap at all; the exchanges with the
      TS-Pico that can reach HOME code (`f:`'s buffer, `r:`'s growth) set
      one, once an exchange rather than once a statement. The 32-errors test
      (Channels step 3) passes in ZEsarUX, as do the rest of `mbchan.bas`
      (38 checks, `tools/emu/mbchan_emu.py`). The old path stays for a call
      made with chunk 0 or 1 not HOME's.
    - **The window: chunk 0, not a window that stays mapped.** A window
      left mapped while BASIC runs would hide a chunk of 4–7, all of which
      the 2068 uses (the program, the stack, a program's own pages). So
      each byte is still mapped and unmapped, but now in chunk 0, the
      EXROM's first half, which the `r:` code (all in chunk 1) never needs
      while it runs: four `OUT`s to map, two to unmap, nothing to save or
      check. In place of the remembered window there is a cursor in the
      channel record: the board bank, the 8K page, the address and a count
      of bytes that can go straight in, set up after each byte the full
      path handles. Bytes of 32 or more go through it with no position
      arithmetic until the next 256-byte boundary, the extent's end or the
      page's end. (#436 replaced the cursor's output side with the
      record's buffer, below.)
    - **The write-backs stay where they were**, at every CR and every 256
      bytes, because they are cheap now (a CR with its write-back is 4 904
      T-states). The cursor never crosses those points, so NEW, a reset and
      power-off keep exactly what Channels step 5 says they keep, and that
      guarantee needs no change. ZEsarUX's run of step 5's NEW case keeps
      the file to its last CR.

    - **The switch must not race the card** (#434). On the board, #433 alone
      dropped random keys in the editor with the error rasp: the key click
      goes through `FASTH` too. The `OUT` at HOME 002Eh needed the very next
      fetch to come from the EXROM, but the v3 card follows an F4h write
      from a polled loop on core 1 and can serve one more read from the
      old ROM. HOME's 0030h is BC-SPACES, and the return at EXROM 1399h had
      the same shape. ZEsarUX switches at once, so it passed there. The
      switches are now loops at 128Bh (in) and 128Fh (out), `OUT (F4h),A` /
      `JR`, the same bytes in both ROMs but for the `JR`'s displacement,
      which repeat the `OUT` until a read comes from the other ROM. Chunk 1
      is checked through 2C8Ch (HOME's `JP (HL)` spins, the EXROM's
      `JP FX_ENTRY` goes on). About 13 T-states a character; the host tests
      model the card lagging by any number of reads. Found by a review of
      #433 after the board run; the rule for any later bank switch from ROM:
      **never rely on the fetch right after the `OUT`.**
    - **The record's own output stub** (#436). An `R` record's output
      routine is a 28-byte stub in the channel record, copied there by
      `OPEN #`. A byte of 32 or more goes into a 57-byte buffer in the
      record with interrupts on and no bank switch at all; a CR, a control
      or a full buffer goes through `FASTH` as before, and one map of chunk
      0 and one `LDIR` write the buffer to the board. `r:` text: **396
      T-states** in ZEsarUX, against 829 with #434. The buffer is written at
      every CR and never crosses a 256-byte boundary, so the write-backs,
      and what NEW, a reset and power-off keep, are unchanged; Channels step
      5 confirms it on the board. Because `R_OUT` now depends on where the
      record is, closing an earlier `F` or `R` record (which moves the later
      ones) points each `R` record back at its own stub.

    **On the board (2026-11-20):** all 40 Channels checks pass, no keys
    drop, and 2000 characters cost no more than about 70 µs each on
    average, against 4.3 ms ([Testing](#testing), "Run on hardware
    again"; that figure is a coarse bound, not a measurement).

    Still open: a finer timing on the board, and input, which takes the
    fast entry but not a cursor or a buffer (`INPUT #` was not timed). `f:`
    gets the fast entry only: its 64-byte buffer and `tpi:chwr` exchange
    are as they were. Raised by David (2026-11-19).

## Sources

- LarKen Electronics, *Assembling the LARKEN RAMDISK* and *Adding the LARKEN
  RAMDisk to existing LKDOS System* (scans from David, with the 256K
  schematic, the parts placement diagram, and Jack Dohany's 2 × 128K
  variant).
- Timex Computer Corp., *Timex Sinclair 2068 Third Party Software Guide*,
  section 1 (memory and banks) and section 6 (SYSCON, the bank API);
  Technical Memo #0006, Connector Specification. Both in the archive.org item
  `timex-computer-corp`.
- Wes Brzozowski, "The Mystery of the Missing 253" and its four follow-ups,
  *Time Designs*, 1986–87, on timexsinclair.com.
- William Pedersen, "Bank Switching the Timex Sinclair 2068 Computer", parts
  I–IV, *T-S Horizons* #19–21; "TS 2068 Memory Expansion", *Nite-Times News*
  v3n2, 1989; both on timexsinclair.com.
- L. Crawford, "Other uses for the T/S 2068 LarKen RAMdisk", *Update*, April
  1990, on timexsinclair.com.
- PicoVideo v4.2, github.com/timex-sinclair-projects/PicoVideo
  (`PICOVID.PLD`, `coreB/busMain.c`).
- Load ZX Museum, A. Lopes, P. Cortesão, *timex_scld*, a VHDL model of the
  TC2068's SCLD (CC BY-SA 4.0), github.com/LoadZX/timex_scld
  (`rtl/scld.vhd`, `rtl/scld_contention.vhd`); links from Jon on the TS2068
  list, 2026-10-22.
- TS2068_Extender v2.1, github.com/timex-sinclair-projects/TS2068_Extender.
- tspico-firmware-build: `src/rom/TSPICO-23.ROM`, `ROMs/GENUINE-2068-exrom.bin`,
  `src/tsbus/tsbus.c` and `tsbus.pio` (the v3 card's `serve`, `io_snoop` and
  `io_drive` PIO programs), `docs/reference/rom/`.
- David's TS2068 reference library: the genuine HOME and EXROM disassemblies
  (`ts2068_home_rom_U16_stock.txt`, `ts2068_exrom_U20_stock.txt`), and
  Gustavo Pane's TS-Pico ROMs disassembled (`gus-home.asm`, `gus-exrom.asm`)
  with an analysis (`gus-rom-analysis.md`), for SYSCON's readers.
- tspico-hardware (the v3 card): the RP2350B core circuit, the 74LVC bus
  interface, `hardware/scripts/parts.py`, `jlc_price.py` and `gen_fab.py`.
- JLCPCB's parts catalog, queried with `jlc_price.py` on 2026-10-16 for the
  stock and prices under [Parts](#parts).
- Infineon, *CY62167EV30 Industrial MoBL 16-Mbit (1M × 16/2M × 8) Static
  RAM*, document 002-24706 Rev. \*B, April 2020; Cypress, *CY62167DV30 MoBL
  16-Mbit (1M x 16) Static RAM*, document 38-05328 Rev. \*G, July 2006.
- ISSI, *IS62WV51216ALL/BLL 512K x 16 Low Voltage, Ultra Low Power CMOS
  Static RAM*, Rev. B, February 2005 (through JLCPCB's link for C11315).
