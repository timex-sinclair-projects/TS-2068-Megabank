# Megabank KiCad project

Status 2026-10-18: **routed, DRC clean at every severity** (no errors, no warnings,
nothing unconnected), not yet ordered. The board has the TS-Pico v3's form factor
and is built with v3's script pipeline (tspico-hardware, `hardware/scripts/`),
copied here and renamed. Spec: [../docs/spec.md](../docs/spec.md).

- **Schematic:** four sheets, generated. ERC: one warning, expected (below).
- **Board:** outline, gold fingers and mounting holes are v3's. Routed by
  `scripts/flow.py` with Freerouting 2.4.1 and no hand edits: 1765 track
  segments (1250 top, 515 bottom), 320 vias, all 0.6/0.3 mm, narrowest track
  0.13 mm, nothing on the plane layers. Closed on the second attempt (the first
  left three nets open). Silkscreen:
  81 of 82 designators placed; C39 is hidden, as on v3.
- **Fab files:** `python3 scripts/gen_fab.py` writes the Gerbers, BOM and CPL
  to `fab/jlcpcb/` (ignored by git); `fab/NOTES.md` lists what to check on upload.
- **Parts:** every fitted part has an LCSC number. About $25-27 a board in parts
  at 10 boards, plus about $42 in extended-part fees once; the two SRAMs are
  $19.46 of it.

## Form factor

The same card as v3, so the two share the extender, brackets and standoffs:

| Item | Value |
|---|---|
| Board | 130.00 x 64.25 mm, 1.6 mm, 4 layers (JLC04161H-7628): F.Cu, In1 GND, In2 +3V3, B.Cu |
| Body | 130.00 x 56.00 mm |
| Tongue | 83.66 x 8.25 mm, left edge 18.48 mm from the board's left edge, 1 mm chamfers |
| Fingers | `TS2068_EdgeConnector_2x32`, 32 positions at 2.54 mm, 31 a side, key slot at position 6 |
| Holes | M3 at (5, 5), (113.6, 4.2), (5, 51), (112, 51.5): v3's |

The finger geometry is v3's; see v3's `hardware/README.md`, "Edge connector
geometry". **Side A is the top (component) side, confirmed (2026-10-19)** by
v3 proto1 in a 2068, whose finger footprint is this one: all 62 pads match
v3.0-proto1's `TS2068_EdgeConnector_2x32` in position, size and layer, and
the pin tables match too. On 2026-10-07 (tspico-hardware,
`firmware/bringup/README.md`) that card logged every address the Z80 read
through the side-B address fingers, the boot log matching the ROM exactly,
then served the HOME ROM onto side A: /MREQ and /RD to time each read, /BE on
A29, data on D0-D7. The 2068 booted to its "TS-Pico v3 ROM" banner, which
only the card's copy carries. With the sides swapped, the address fingers
would sit on data and control lines and none of that could work.

## Sheets

| Sheet | Generator | Contents |
|---|---|---|
| `megabank.kicad_sch` | by hand | Root: four sheets, flat, global labels |
| `interface.kicad_sch` | `gen_interface.py` | P1 fingers; U1/U2 address buffers (always on: BA0-BA15), U3 data buffer (DIR = /RD, /OE = nDOE), U4 control buffer; U5 74LVC1G06 /BE driver; pulls R1-R3 |
| `memory.kicad_sch` | `gen_memory.py` | U10/U11 CY62167EV30 as 2M x 8; U12 /WE gate; ~CE1 pull-downs, WINH pull-up, BHE/BLE links |
| `mcu.kicad_sch` | `gen_mcu.py` | v3's RP2350B core, same designators; USB-C; RUN, BOOTSEL; SW5 write protect; the GPIO map |
| `power.kicad_sch` | `gen_power.py` | +5V diode-OR (edge, USB), TLV1117LV33, TPS3839 -> CE2, LM66200 -> VSRAM, CR2032, battery monitor |

`gen_symbols.py` writes `lib/megabank.kicad_sym`: the edge connector (v3's),
the CY62167EV30 (from the Infineon datasheet, 002-24706 Rev \*B, figure 2) and
the LM66200 (TI SLVSG04, table 5-1). `parts.py` maps value and footprint to MPN
and LCSC number; `schlib.place()` stamps them on each symbol.

### GPIO map

A PIO state machine sees 32 pins, from GPIO0 or GPIO16. The port program needs
data, A0-A7 and the strobes; the memory program needs the chunk bits, the
strobes, /BE, the ~CE1s and the page bits. Everything both need sits in 16-31:

| GPIO | Net | |
|---|---|---|
| 0-7 | SD0-SD7 | SRAM-side data: `IN`/`OUT` to FCh/FDh |
| 8-15 | BA0-BA7 | port decode |
| 16-18 | BA13-BA15 | chunk (`out pc, 3`) |
| 19-25 | nMREQ_L, nIORQ_L, nRD_L, nWR_L, nRFSH_L, nM1_L, nRESET_L | |
| 26 | nDOE | data buffer enable, pulled up |
| 27 | BE_REQ | to U5, pulled down |
| 28-29 | nCE1_0, nCE1_1 | SRAM enables, pulled down |
| 30-37 | PA13-PA20 | page bits, contiguous with the ~CE1s |
| 38 | WINH | write inhibit, pulled up |
| 39 | nWP | write-protect switch, low = protected |
| 40 | spare | pin 49, boxed in by the top-row fan and C21 |
| 41 | BAT_EN | |
| 42 | nBE_L | /BE read back through U4's spare channel |
| 43 | BAT_ADC | ADC3, pin 54, on the side facing the battery monitor |
| 44-47 | spare | |

### Decisions taken here, beyond the spec

- **USB-C, as on v3** (open question 11): firmware by BOOTSEL and the settings
  (`BASE`, `SIZE`) over USB. VBUS is diode-ORed with the edge +5V, so the board
  runs from USB alone on the bench. SWD pads TP1-TP3 too.
- **/BE read back:** U4's eighth channel brings /BE in as nBE_L (GPIO42), for
  timing on the bench and to see v3 serve a chunk.
- **R3, a /BE pull-up to +5V_EDGE, DNP** (open question 13): the bus already has
  v3's 2k2; the pad is there if the prototype shows /BE rising too slowly.
- **L1 fitted** (JLCPCB pre-order or Global Sourcing), per the spec; set
  `dnp=True` in `gen_mcu.py` to hand-fit it instead.
- **One 0 R each for BHE and BLE** (R13, R14), shared by both chips.
- **Battery-monitor designators** are R51-R54, C55 (the spec's R1-R4, C1).
- **SRAM pins may be swapped at layout:** any permutation of A0-A20 or of
  I/O0-I/O7 is harmless, since both chips always see the same mapping. Only
  SDn must stay on GPIOn. Worth using if the router struggles.

### The ERC warning

`U5 pin 4 (open collector) and U4 pin 18 (tri-state)`: the /BE driver and the
buffer that reads /BE back. That is the intent.

## Layout

Coordinates in mm from the body's top-left corner (`gen_layout.py`, table `P`).

- **Over the fingers,** as v3: data U3 (x 40), high address U2 (x 63) over
  A15-A8, control U4 (x 80) over /MREQ to /RFSH, /BE driver U5 by finger A29.
  Low address U1 above U2.
- **RP2350B cluster** at v3's coordinates with v3's designators (U20 at
  (62, 26), 90°), so v3's pre-routes carry over. Flash U21 to its left.
  One pre-route changed: on v3, supply pin 29 dropped into the lanes of pads 27
  and 28 to reach its via, because those GPIOs (26, 27) were spare. Here they
  are nDOE and BE_REQ, and the first run left both sealed in on all three
  attempts. The 24/29 supply link now carries on under the package to pin 15,
  whose fan via is already on the +3V3 plane.
  One pre-route added: a staircase fan for the top pad row's pins 42-48 (PA16-
  PA20, WINH, nWP), straight up into lanes 0.4 mm apart and right to x 73. All
  seven head for the SRAMs; left to the router they came out as a tangle of
  vias, with one or two sealed in on most attempts. For room, C27 (pin 41's
  100 nF) moved up from v3's (67.4, 17.4) to (67.4, 15.2), and BAT_ADC moved
  from GPIO40 (pin 49, which the fan would box in against C21) to GPIO43.
- **SRAM** U10 and U11 on the right, leads top and bottom; the /WE gate, the
  supervisor and the pulls in the strip between them and the crystal.
- **Power:** diodes and LDO top left, LM66200 and the battery monitor in the
  free strip above the RP2350B, the CR2032 bottom left (tabs left and right).
  USB-C on the left edge (v3's place). RUN and BOOTSEL on the top edge (v3's
  places). Write-protect slide switch on the right edge, actuator tip flush
  with it.

### Footprints checked against the makers' drawings (2026-10-19)

- **Battery holder, Q&J CR2032-BS-6-1 (C70377), drawing QJ.BS-6:** KiCad has
  no footprint for it, and Keystone 3034's (used until now) was wrong: the 3034
  has two + tabs at the sides and a - pad under the cell, while the BS-6 has a
  + tab at one end and a - tab at the other. Now
  `lib/megabank.pretty/BatteryHolder_QJ_CR2032-BS-6-1`, from the drawing's PCB
  layout: two 4.2 x 3.0 mm pads, 26 mm apart inside and 32 mm outside; housing
  16 x 28.5 mm (31.3 over the tabs), 5.5 mm tall; the 20 mm cell overhangs the
  housing's sides, so the courtyard is 20.5 x 32.5 mm.
- **Slide switch, SHOU HAN MSK12C02 (C431540):** KiCad's
  `SW_SPDT_Shouhan_MSK12C02` matches the drawing, rotated 180 degrees (pins at
  -y, actuator at +y): pins 1-2 3.0 mm apart and 2-3 1.5 mm, the 0.85 mm
  locating holes 3 mm apart, the four 1.05 x 0.7 mm mounting pads. The actuator
  stands 1.45 mm proud of the body; it faced the right edge but stopped 2.1 mm
  short of it, so SW5 moved to x 127.1, which puts its tip at the edge (x 130).
- **SRAM, TSOP I 48, Infineon 51-85183 \*F** (CY62167EV30 datasheet 002-24706
  Rev \*B, figure 12; JEDEC MO-142 DD; the DV30 uses the same drawing): KiCad's
  `TSOP-I-48_18.4x12mm_P0.5mm` matches. Pin 1 top left, 1-24 down the left
  side, 25-48 up the right, as the drawing's standard pin-out (top view), which
  is what the -ZXI parts are. Pitch 0.50 mm, 24 pins spanning 11.50 mm. The
  pads (1.575 x 0.30 mm, centred 9.688 mm out) run from 8.90 to 10.475 mm: 0.475
  mm past the 10.00 mm lead tips (D = 20.00) and 0.40 mm inside the heel at
  the longest foot (L = 0.70), against leads 0.17-0.27 mm wide, which leaves
  0.20 mm between pads. Body outline 18.5 x 12.1 mm on F.Fab, against 18.40 x
  12.00.

## Next

Every footprint check before ordering is done: the battery holder, the slide
switch, the TSOP I 48 (above) and side A of the fingers (Form factor).

1. **The SRAM rail with a fresh cell:** the LM66200 passes whichever input is
   higher, and a new CR2032 reads up to about 3.3 V open circuit, against +3V3's
   3.25-3.35 V. At SRAM standby currents the cell may carry the rail some of the
   time with the 2068 on. Harmless to the data; it costs battery life. Measure on
   the prototype (spec, hardware test 3); a Schottky in series with the cell
   would settle it if needed.
2. **Stock:** 42 CY62167EV30 at JLCPCB on 2026-10-18, enough for 20 boards.

## Routing

```bash
cd hardware
PY=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3
$PY scripts/flow.py --jar <freerouting-2.4.1.jar>    # needs Homebrew's openjdk (autoroute.py's JAVA)
scripts/check.sh                                      # ERC (one expected warning) and DRC
python3 scripts/gen_fab.py
```

`flow.py` regenerates the placement and discards all routing, then pre-routes,
autoroutes, finishes and places silkscreen, up to three attempts; Freerouting
does not give the same result twice. Each attempt took 4-8 minutes to route
here. A failed attempt is kept as `fab/route/attempt-N.kicad_pcb`.

## Regenerating

```bash
cd hardware
python3 scripts/gen_symbols.py                       # project symbols
for s in interface memory mcu power; do python3 scripts/gen_$s.py; done
PY=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3
$PY scripts/gen_layout.py                            # skeleton, netlist, placement, planes (discards routing)
$PY scripts/preroute.py                              # v3's locked pre-routes
```

Regenerating a sheet overwrites hand edits; once a sheet is edited in KiCad,
retire its generator. The same goes for `gen_layout.py` once routing starts by
hand.

BOM and price:

```bash
cd hardware
KC=/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli
$KC sch export bom --output bom/megabank-jlcpcb-bom.csv --fields "Value,Reference,Footprint,LCSC" --labels "Comment,Designator,Footprint,LCSC Part #" --group-by "Value,Footprint" --exclude-dnp megabank.kicad_sch
python3 scripts/jlc_price.py --boards 10 --bom bom/megabank-jlcpcb-bom.csv
```

(`jlc_price.py` counts a range like `U1-U4` as one part, so its total runs a
little low.)
