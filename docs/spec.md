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
  as fast as the Z80 can copy, kept through power-off by a battery.

It began with David's question (2026-10-16) whether the TS-Pico could emulate
the LarKen 256K RAMdisk, and with a 2068 GUI author asking for a RAM disk.
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
- [Hardware](#hardware): the RP2350B controller, parts for JLCPCB
  assembly, battery backup, timing
- [Sharing the bus](#sharing-the-bus): the v3 card, PicoVideo, BUSISO
- [ROM](#rom): what ROM 2.4 adds
- [The RAM disk](#the-ram-disk): `r:`, the mover, the on-board format
- [Firmware](#firmware): the v3 card's part
- [PicoVideo](#picovideo)
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
  access, whatever ports F4h and FFh say.
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
   /BE is still pulled, so it doesn't fall through to the 2068's RAM either.
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

Two ports, decoded from A7–A0 only (A8–A15 ignored, like the v3 card):
**FDh selects a register, FCh reads or writes it (decided).** These are the
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
| 13h | `IDCOMP` | R | – | 5Ah, its complement (an absent board reads FFh twice). |
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
  `OUT (FDh),13h`, `IN A,(FCh)` gives 5Ah. Anything else: no board.

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
- **The screen.** Mapping a bank into chunk 2 (4000h–5FFFh) hides the screen
  memory from the Z80, not from the display: the 2068 keeps showing its own
  RAM. Writes there go to the bank.
- **The RAM disk's banks** belong to the RAM disk ([below](#the-ram-disk)).
  Ask the ROM for pages rather than picking bank numbers yourself.

**Speed.** `LDIR` moves a byte in 21 T-states: about 160 KB a second at the
2068's 3.53 MHz. A 6K screen copies out of a bank in about 40 ms.

## Hardware

**Built by JLCPCB, surface-mount throughout (decided).** The board is made the
way the v3 card is, and reuses its tools from the
[tspico-hardware](https://github.com/factus10/tspico-hardware) repository:
`scripts/parts.py` (MPN and LCSC number for every part), `scripts/jlc_price.py`
(prices and stock against JLCPCB's catalog) and `scripts/gen_fab.py` (the
upload files). The aim is no hand soldering. Today one part, the RP2350's L1,
still needs it, as on v3.

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
  catches `OUT` to FDh/FCh, as v3's `io_snoop` does, and core 1 updates the
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
| Battery: ADC and its switch | 2 |
| **Total** | **42 of 48** |

The data buffer's direction comes from the buffered /RD, not from a pin.

### Parts

Candidates, with JLCPCB stock on 2026-10-16. Availability is open question 6.

| Function | Part (candidate) | LCSC | Notes |
|---|---|---|---|
| Memory | 2 × CY62167EV30LL-45ZXIT (16 Mbit, 2.2–3.6 V, 45 ns, TSOP-I 48), used as 2M×8 | C511152 | 4 MB in two chips. **×8 confirmed (decided):** BYTE tied to ground makes it 2M×8, pin 45 (I/O15/A20) becomes A20, and BHE, BLE and I/O8–I/O14 are unused. Preferred over the DV30 for its standby current (below). 42 in stock: thin. |
| Memory (alternate) | 2 × CY62167DV30LL-55ZXIT (same package drawing 51-85183 and pinout) | C2956479 | 168 in stock. The same footprint, with one difference: the DV30 marks BHE and BLE "do not use, leave floating", while the EV30 needs them tied to a logic level for its standby figures. Route each to ground through an unfitted 0 Ω pad: fitted for the EV30, left open for the DV30. Its standby is worse: 22 µA maximum per chip, with every input within 0.2 V of a rail. |
| Memory (fallback) | 4 × IS62WV51216BLL-55TLI (512K×16, 3.3 V, TSOP-II-44) | C11315 | 3,623 in stock. Used as ×8: D0–7 tied to D8–15, /LB from A0 and /UB from A0 through a 74LVC1G04; A1–A12 and the page bits form the word address. A different footprint: choose before layout. **No CS2 in TSOP-II (confirmed):** its only chip select is CS1 (pin 6); CS2 exists only on the 48-ball mini-BGA, and JLCPCB's only BGA in stock is the 1.8 V IS62WV51216ALL. So power-fail protection goes through /LB and /UB instead: both come from gates on the SRAM rail whose other input is a "power not good" line (pulled up to the SRAM rail, pulled low by an N-MOSFET from the supervisor while 3.3 V is good), and a chip with both high is deselected. Four chip selects: 44 of the RP2350B's 48 pins. **Much worse standby:** 4 µA typical but 25 µA maximum per chip (industrial), and 20 µA maximum in data retention (down to 1.2 V), with every input within 0.2 V of a rail. Four chips could draw about 100 µA: about three months on a CR2032, nine on a CR2450. Order of preference: EV30, DV30, then this. |
| Controller | RP2350B (QFN-80), with v3's core circuit: QSPI flash, crystal, core regulator | C42415655 | 4,873 in stock. Copy v3's RP2350B schematic sheet. L1, the core regulator's inductor (Abracon AOTA-B201610S3R3-101-T, C42411119), had 4 in stock: fit it by hand, as on v3, unless restocked. |
| Buffers | 4 × 74LVC245APW: A0–A7, A8–A15 and the control lines one way; D0–D7 both ways | C6082 | The same part as v3. Their power-off protection (Ioff) means that with the 2068 off they neither drive nor load the SRAM side. |
| /BE driver | 74LVC1G06 (inverting, open-drain) | C7828 | Wire-ORs with v3's 74LVC06. The RP2350's reset state (input, pull-down) leaves /BE released. |
| Chip enables | The CY62167's two: /CE1 (active low) and CE2 (active high) | | CE2 low deselects the chip whatever /CE1 does, including in data retention. **Both chips' CE2 come from the supervisor**, with a pull-down to ground, so they are high only while 3.3 V is good. **Each chip's /CE1 comes straight from the controller**, qualified by /MREQ, with a pull-down so it sits at a logic level on battery. No decoder, and no logic on the SRAM rail. |
| /WE gate | SN74LVC1G32DBVR (OR, SOT-23-5, TI), on 3.3 V | C10096 | 139,210 in stock on 2026-10-16. SRAM /WE = buffered /WR OR the controller's write inhibit, which it sets per chunk from `WPROT` and the switch. The write-inhibit line has a **pull-up**, so writes are blocked until the controller's firmware runs: Z80 writes during its boot cannot reach the RAM disk. |
| Power mux | LM66200 (ideal-diode ORing, 1.6–5.5 V, 1.32 µA) | C3235556 | Feeds the SRAM rail from 3.3 V or the battery. **Not the TPS2115A:** it draws about 55 µA from the battery, which would empty a coin cell in months. **Not plain diodes:** with 3.3 V on, the SRAM rail must stay close to 3.3 V, or the buffers and the controller push current into the SRAM's input clamps. |
| Supervisor | TPS3839G33 on 3.3 V (threshold about 3.08 V) | C485802 | Drives both chips' CE2 directly: high while 3.3 V is good, low below the threshold, and the pull-down holds them low with 3.3 V gone. So no writes happen while power rises or falls, well above the SRAM's 2.2 V minimum. Push-pull output, so CE2 needs no pull-up; its output is undefined below 0.6 V, which is what the pull-down covers. Its 200 ms delay keeps the SRAM deselected for 200 ms after power-on: harmless, since nothing maps a bank until software writes the registers. |
| Battery | CR2032 in an SMD holder (a CR2450 clip for longer life) | C22363833, C7498149 | Two CY62167EV30s in standby (1.5 µA typical, 3.0 µA maximum at 25 °C and 3.5 µA at 40 °C, each) plus the mux (1.3 µA): about 8.5 µA worst case at room temperature, so about three years on a CR2032 and eight on a CR2450. At 85 °C the chips' maximum is 12 µA each. Data is kept down to 1.5 V. Measure the real board. |
| Battery monitor | The RP2350B's ADC, through a MOSFET it switches on only to measure | | So the battery never feeds the unpowered RP2350's pins. `STATUS` bit 1 below about 2.5 V. |
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
needs no pass-through of its own. How it connects to the slot (gold fingers, or
a connector) and how its firmware is loaded (USB-C, or SWD pads) are open
questions 6 and 11.

## Sharing the bus

Three cards may answer memory cycles: the 2068 itself, the v3 card (HOME and
EXROM with /BE, DOCK without), and this board. Only one answers any access,
and none of it depends on BUSISO or IOA5:

- **The board answers only chunks whose owner is one of its banks.**
- **The v3 card watches the FDh/FCh writes** and stops serving any chunk whose
  owner is 1–253 ([Firmware](#firmware)). Without that, a DOCK chunk would
  have two drivers: /BE switches off the 2068's memory but not the v3 card.
- **/BE is open-drain on both cards**, so they never fight electrically.
- **PicoVideo watches the same writes** and stops copying screen writes into
  its display when chunk 2 or 3 is owned by a bank ([PicoVideo](#picovideo)).
- **The ROM writes every `OWNn` = 0 at boot**, even though the board clears
  them on reset. PicoVideo can't see /RESET and the v3 card keeps its
  shadows through a plain reset, so the boot-time writes resynchronise every
  device that watches.

**BUSISO is not used.** On the TS2068_Extender, a per-slot jumper feeds /BE
to the BUSISO pin of the next-lower slot, so a device there can hold off its
own /BE. The TS-Pico goes in slot 1 with its jumper open (closed, it sees
its own /BE and stops working). This board goes in any free slot, jumper
open, and arbitrates through the owner table instead.

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
board exists. The RAM disk and programs use the registers directly.

### Minimal ROM: detection, reset, mover, `r:` (phase 4)

1. **Replace the bank search.** A `JP` at X0A3E to new code frees the old
   search's body, X0A3E–X0DAF: 882 bytes in EXROM chunk 0, which today has
   about 84 bytes free. It is entered two ways, both landing at X0A3E: `CALL 0A3E`
   at X1CA8 (the boot path taken when D is held at power-on, undocumented
   until now) and the fall-through from X09F4, which reads the DOCK's
   LROS/AROS headers on the normal path. The block X0C4C–X0CFA is reachable only
   from HOME 2487h, which nothing calls. (To verify when patching: no other
   entry into the range.) The new code:
   - writes `OWN0`–`OWN7` = 0 and `WPROT` = 0, so every device watching the bus
     starts in step;
   - checks `ID`/`IDCOMP`, reads `BASE`, `SIZE` and `STATUS`;
   - reports them to the TS-Pico (how: open question 3);
   - leaves `MAXBNK` and 6315h at 0;
   - ends as the old code did: `CALL 2082h` (writes SYSCON's 80h end marker).
     The old code also called the renumbering at X0CFB, which does nothing with
     no banks; keep or drop it once checked.
2. **The mover** ([The RAM disk](#the-ram-disk)): the operation loop, the
   window choice and the copy loops, in EXROM chunk 1's free space
   (23D4h–2FFFh, 3116 bytes, and 37B5h–3FFFh, 2123 bytes). Estimated under 512
   bytes.
3. **The `r:` prefix**: a test in `F_HOOK` (`fddcmd.asm` 585) beside the
   `f:` one, a new TPMODE bit (bits 7 and 6 are `tpi:` and `net:`), and the
   tape block routines calling the mover when it is set. `CAT`, `ERASE`,
   `MOVE` and `FORMAT` already reach the firmware as `tpi:` strings, so `r:`
   names need firmware work only.

### The Timex bank API on the board (phase 8)

The owner table makes this much cheaper than Timex's design: the state the
API loops over, one register set per bank, is just 16 bytes on this board
(`OWN0`–`OWN7`, `SUB0`–`SUB7`), whatever the bank count.

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
   chunks 2–7 survive ROM calls. Check each: HOME 3CE3h and 0A50h, HOME 03FCh
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

The firmware sends an operation; the ROM does it and replies with a status
byte. All lengths are 1–65535; "page:offset" is an 8K page (0–511) and an
offset into it, and a transfer may cross pages.

| Op | Does | Used by |
|---|---|---|
| 1 | Copy *len* bytes of 2068 memory at *addr* to page:offset | SAVE |
| 2 | Copy page:offset to 2068 memory at *addr* | LOAD, MERGE |
| 3 | Compare page:offset with 2068 memory at *addr*; reply with the result | VERIFY |
| 4 | Send page:offset to the TS-Pico through port 0Eh | reading the directory; MOVE from `r:`; backup |
| 5 | Receive bytes from the TS-Pico into page:offset | writing the directory; MOVE to `r:`; restore |
| 6 | Copy page:offset to page:offset (both on the board) | compaction |
| 0 | Done; carries the report code to end the command with (0 = OK) | every command |

**How the mover maps a page.** For each piece of a transfer that lies in one
8K chunk of 2068 memory, it picks a **window chunk** from 4–7 that holds
neither that piece nor the stack. With four candidates and at most two
excluded, one is always free. Then, with interrupts off:

1. Save `OWNw` and `SUBw` (read back through FCh).
2. Map the page: `OWNw` = its bank, `SUBw` = its 8K.
3. `LDIR` (or `CPIR`-style compare, or `INIR`/`OTIR`-style port transfer).
4. Restore `OWNw` and `SUBw`, re-enable interrupts.

The mover runs from EXROM chunks 0–1 and never maps a window there, so it
never unmaps itself. It doesn't depend on the Timex bank API, so the RAM disk
can ship before that API is fixed ([Phases](#phases)).

### From BASIC

| Command | Does |
|---|---|
| `SAVE "r:name" …` | every form `SAVE` takes: `LINE`, `CODE`, `SCREEN$`, `DATA` |
| `LOAD "r:name"`, `VERIFY "r:name"`, `MERGE "r:name"` | as from tape, with the same reports |
| `CAT "r:"` | the directory, with free space |
| `ERASE "r:name"` | delete |
| `MOVE "tpi:name" TO "r:name"`, and back | copy between the SD card and the RAM disk; MOVE copies, as it does today |
| `FORMAT "r:"` | create an empty RAM disk (asks Y/N, as `tpi:` FORMAT does) |
| `SAVE "tpi:ram backup name"` / `"tpi:ram restore name"` | the whole RAM disk to or from an image file on the SD card |
| `SAVE "tpi:ram"` | board found or not, `BASE`, `SIZE`, battery, write-protect switch, RAM disk size and free space |

Names follow the tape rules (up to 10 characters), so a file moves between
tape, SD card and RAM disk unchanged. Because `LOAD "r:name"` goes through
the ROM's own LOAD, a BASIC program, arrays and `SCREEN$` all behave exactly as
they do from tape.

### Where it lives on the board

The RAM disk occupies **the top of the board's banks**, growing down; banks
below it are free for programs. `FORMAT "r:"` asks for a size (default: half
the board).

- **Superblock** in the first 256 bytes of the RAM disk's top page: a magic
  number ("TSRD"), format version, the first and last page of the RAM disk, the
  cluster size, a **generation counter**, and a checksum.
- **Allocation bitmap** and **directory** in the rest of the top page(s).
  A directory entry is 32 bytes: the name, the 17-byte tape header's fields
  (type, length, start, program/array parameters), the first cluster and
  length, flags.
- **Files are contiguous**, in 1K clusters, and files of 8K or more start on a
  page boundary. A program can then map a file's pages directly: `LOAD
  "r:icons"` once, then page the icons in by number. When free space is too
  fragmented for a new file, the firmware compacts the disk with op 6.
- **Caching.** The firmware reads the superblock (op 4, 256 bytes) at the start
  of every `r:` command and re-reads the directory only if the generation
  counter changed. Every change to the directory increments it. That covers
  resets, power cycles and a second TS-Pico.

**Allocating pages for a program**, so programs and files never collide, is
a `tpi:ram alloc` command that reserves pages below the RAM disk and records
them in the superblock. Its exact form is for the GUI author to shape (open
question 8).

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

**Core 0 (Python): the RAM disk.** A new module beside `extcmd.py`:

- the `r:` file system: the format above, allocation, compaction;
- the mover's side of the protocol: sending operations, checking status;
- the `CAT`/`ERASE`/`MOVE`/`FORMAT` handlers for `r:` names;
- `tpi:ram` (info, backup, restore, alloc);
- what the ROM reports at boot: board found, `BASE`, `SIZE`, `STATUS`.
  The firmware can't read the board itself, since it can't run bus cycles.

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

While there: `case 0xF4:` has no `break` and falls into `case HIRES_ADRL`,
so every `OUT F4h` also overwrites the low byte of the hi-res address. One
line to fix.

## Testing

**Before any hardware** (David can do the first today, on a bare 2068):

1. **Does the 2068 itself answer FCh or FDh?** The Spectrum's ULA answers
   any even port. If the 2068's SCLD decodes FEh partially, `OUT 252,n` would
   change the border and `IN 252` would read the keyboard, colliding with the
   board. In BASIC: `OUT 252,2` (border red?), `OUT 253,2`, and
   `PRINT IN 252, IN 253` with no keys pressed (FFh means nobody answered). Timex
   assigned these ports, so the SCLD probably ignores them, but it decides the
   design, so check it first (open question 1).
2. **An emulator model.** Add the board to our ZEsarUX fork (memory reads and
   writes through the owner table, ports FCh/FDh). The ROM and firmware can
   then be developed and tested with no hardware, through the existing
   `tools/emu` bridge.
3. **ROM host tests** for the mover's window choice and the boot-time
   detection, in the style of the existing `rom_*_hosttest.py`.
4. **Firmware host tests** for the file system: format, save/load/erase,
   fragmentation and compaction, a corrupt superblock, the generation counter.
5. **The controller's firmware** on an RP2350B development board, with the
   v3 card's PIO programs as the starting point: register writes and reads,
   the owner table, /BE and the page bits against a logic analyser, before
   any Megabank PCB exists.

**On hardware:**

1. A prototype on the extender, with a scope on /BE, /MREQ, /RD and D0–D7, in
   every slot: /BE must win against the 2068's own memory with margin
   (open question 2).
2. A memory test from the 2068: every page, every chunk, pattern and
   address-in-address, with the v3 card and PicoVideo both plugged in.
3. Battery: fill the board, power off for a day, verify; measure the standby
   current.
4. Power-fail: switch off during a long `SAVE "r:"`. Other files must survive;
   the one being written may be lost.
5. The RAM disk end to end: every SAVE/LOAD form, MOVE both ways, backup and
   restore, a full disk, compaction.
6. `tools/hwcheck.py` grows a RAM-board section.

## Phases

Each phase is its own branch and PR, and each PR updates the programmer's
reference for what it changes.

1. **Port test** on a bare 2068 (testing step 1). Decides whether FCh/FDh
   stand.
2. **Emulator model** in the ZEsarUX fork.
3. **v3 firmware: bus sharing** (core 1). Small, and testable in the emulator
   and on a v3 card with no board (nothing changes until something writes
   FDh/FCh).
4. **ROM: detection, boot reset and the mover**, plus the `r:` hook.
5. **Firmware: the RAM disk** and `tpi:ram`.
6. **PicoVideo** update (its own repo).
7. **Board:** schematic, the controller's firmware, PCB (assembled by
   JLCPCB), prototype, hardware tests. Can run
   in parallel with 2–6 once the register map is agreed.
8. **ROM: the Timex bank API** made to work on the board (see [ROM](#rom)).
   Last, because nothing above depends on it. David leans towards only what
   the board and RAM disk need, and is open to the whole API (open question 5).

## Open questions

1. **Do ports FCh/FDh collide with the 2068 itself?** Testing step 1, on a
   bare 2068. If the SCLD answers them, the registers move to another pair.
2. **/BE timing** through the expansion bus, in every slot, with the v3 card
   and PicoVideo fitted. Measured on the prototype. The v3 card's PIO path
   already wins this race on a 2068; the board's must too. Also measured: how
   the page bits are produced (a second PIO table, a DMA-chained lookup or
   core 1), and that they settle well inside the read.
3. **How the ROM tells the TS-Pico about the board at boot**: a new command,
   or a field in an existing boot exchange. Decided when the ROM work starts,
   with `docs/PROTOCOL.md`.
4. **A write fence for the RAM disk?** `WPROT` protects only chunks that are
   mapped. A program that maps a RAM disk page by mistake can still corrupt
   files. A `FENCE` register (banks at or above it are read-only unless
   unlocked) would close that, at the cost of a register the ROM must set at
   boot.
5. **Scope of the Timex bank API work**
   ([ROM](#the-timex-bank-api-on-the-board-phase-8)): items 1–5 (the API
   works with the board) or all of it. David leans towards the minimum.
6. **Parts, for JLCPCB assembly** ([Hardware](#parts)):
   - **The SRAM.** The ×8 mode is confirmed in both datasheets. Still open:
     whether the DV30 is current (its only datasheet found is from 2006; the
     EV30's is Infineon's, 2020), and stock, which is thin at JLCPCB (42 EV30s
     and 168 DV30s on 2026-10-16). The BHE/BLE difference between them is
     handled by the 0 Ω pads. If it runs out, switch to the
     IS62WV51216 (3,623 in stock), a different footprint, before layout. The
     earlier candidates were not at JLCPCB on that date: AS6C4008 (5 left),
     AS6C8008, CY62177G30, IS62WV20488, and Everspin MRAM.
   - **L1**, the RP2350's core inductor, had 4 in stock: hand-fit, as on v3,
     unless restocked.
   - **The RP2350B's stepping.** Use a stepping without the E9 pad erratum
     (a pull-down that latches), or don't rely on internal pull-downs.
   - **The slot connection:** gold fingers (no part; JLCPCB charges for the
     gold and the bevel) or a connector, which may be through-hole.
   - **The battery monitor's switch:** draw it, then pick its MOSFET and
     resistors.
7. **A name** for the board.
8. **What the GUI author needs** from `tpi:ram alloc` and from the ROM: page
   allocation from BASIC, a machine-code entry point, a page size other than
   8K? Worth showing them this document.
9. **SYSCON entries for RAM banks.** SYSCON holds at most 10 entries (its
   scratch space at 5FE9h–5FFFh is hard-coded). Nothing validates bank numbers
   against it, so RAM banks may not need entries at all; decide in phase 8.
10. **The bank stack** holds 16 frames of 4 bytes with no overflow check.
    Deep `CALL_BANK` nesting could overrun it. Phase 8.
11. **The controller's firmware and settings.** How its firmware is loaded
    (a surface-mount USB-C connector, or SWD pads), and how `BASE` and `SIZE`
    are set: over USB, or from the 2068 through a guarded register
    (`SAVE "tpi:ram base 65"`, say).
12. **Standby with the controller off.** Settled on paper for the EV30
    ([Parts](#parts)). Still to check: that no path lets the battery feed the
    RP2350B or the buffers, and the real standby current on the prototype
    (hardware test 3), with the DV30 too if it is fitted.
13. **Where the bus's /BE pull-up is.** The board's /BE driver is open-drain
    and the board adds no pull-up, relying on the one already on the bus.
    Confirm where that is (the 2068, or the v3 card) and that it is strong
    enough for /BE to rise between cycles with the board fitted.

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
- TS2068_Extender v2.1, github.com/timex-sinclair-projects/TS2068_Extender.
- tspico-firmware-build: `src/rom/TSPICO-23.ROM`, `ROMs/GENUINE-2068-exrom.bin`,
  `src/tsbus/tsbus.c` and `tsbus.pio` (the v3 card's `serve`, `io_snoop` and
  `io_drive` PIO programs), `docs/reference/rom/`.
- tspico-hardware (the v3 card): the RP2350B core circuit, the 74LVC bus
  interface, `hardware/scripts/parts.py`, `jlc_price.py` and `gen_fab.py`.
- JLCPCB's parts catalog, queried with `jlc_price.py` on 2026-10-16 for the
  stock and prices under [Parts](#parts).
- Infineon, *CY62167EV30 Industrial MoBL 16-Mbit (1M × 16/2M × 8) Static
  RAM*, document 002-24706 Rev. \*B, April 2020; Cypress, *CY62167DV30 MoBL
  16-Mbit (1M x 16) Static RAM*, document 38-05328 Rev. \*G, July 2006.
- ISSI, *IS62WV51216ALL/BLL 512K x 16 Low Voltage, Ultra Low Power CMOS
  Static RAM*, Rev. B, February 2005 (through JLCPCB's link for C11315).
