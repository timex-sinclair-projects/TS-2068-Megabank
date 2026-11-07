# Megabank KiCad project

Status 2026-11-07: **routed, DRC clean at every severity** (no errors, no warnings,
nothing unconnected), not yet ordered. The board has the TS-Pico v3's form factor
and is built with v3's script pipeline (tspico-hardware, `hardware/scripts/`),
copied here and renamed. Spec: [../docs/spec.md](../docs/spec.md).

- **Schematic:** four sheets, generated. ERC: one warning, expected (below).
- **Board:** outline, gold fingers and mounting holes are v3's. Routed by
  `scripts/flow.py` with Freerouting 2.4.1 and no hand edits: 1945 track
  segments (1315 top, 630 bottom), 355 vias, all 0.6/0.3 mm, narrowest track
  0.11 mm, nothing on the plane layers. Closed on the first attempt (re-routed
  2026-11-07 for the core LDO, which replaced the RP2350B's switcher and L1,
  and the GPIO46/47 test pads). Silkscreen: all 90 designators placed.
- **Fab files:** `python3 scripts/gen_fab.py` writes the Gerbers, BOM and CPL
  to `fab/jlcpcb/` (ignored by git); `fab/NOTES.md` lists what to check on upload.
- **Parts:** every part JLCPCB fits has an LCSC number; BT1 (Keystone 3034) is fitted by hand. $26.27 a board in parts at
  10 boards (`jlc_price.py`, 2026-11-07; it counts U1-U4 once, so about $27),
  plus $42 in extended-part fees once; the two SRAMs are $20.86 of it. The
  ME6211C12 LDO (32,146 in stock) is an extended part like the L1 it replaced;
  dropping BT1 saved one fee. Add the Keystone 3034 from DigiKey.

## Form factor

The same card as v3, so the two share the extender, brackets and standoffs:

| Item | Value |
|---|---|
| Board | 130.00 x 64.25 mm, 1.6 mm, 4 layers (JLC04161H-7628): F.Cu, In1 GND, In2 +3V3, B.Cu |
| Body | 130.00 x 56.00 mm |
| Tongue | 83.66 x 8.25 mm, left edge 18.48 mm from the board's left edge, 1 mm chamfers |
| Fingers | `TS2068_EdgeConnector_2x32`, 32 positions at 2.54 mm, 31 a side, key slot at position 6 |
| Holes | M3 at (5, 5), (113.6, 4.2), (5, 51), (112, 51.5): v3's |

The finger geometry and the side-A-is-top assumption are v3's; see v3's
`hardware/README.md`, "Edge connector geometry". Check side A before ordering
(spec, open question 6).

## Sheets

| Sheet | Generator | Contents |
|---|---|---|
| `megabank.kicad_sch` | by hand | Root: four sheets, flat, global labels |
| `interface.kicad_sch` | `gen_interface.py` | P1 fingers; U1/U2 address buffers (always on: BA0-BA15), U3 data buffer (DIR = /RD, /OE = nDOE), U4 control buffer; U5 74LVC1G06 /BE driver; U6 74LVC1G06 /NMI driver; U7 74LVC2G34 /INT and /NMI in; pulls R1-R4 |
| `memory.kicad_sch` | `gen_memory.py` | U10/U11 CY62167EV30 as 2M x 8; U12 /WE gate; ~CE1 pull-downs, WINH pull-up, BHE/BLE links |
| `mcu.kicad_sch` | `gen_mcu.py` | v3's RP2350B core, same designators, but DVDD from U23, a 1.2 V LDO (no L1); USB-C; RUN, BOOTSEL; SW5 write protect; TP4/TP5 on the spare GPIOs; the GPIO map |
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
| 40 | BAT_ADC | ADC0 |
| 41 | BAT_EN | |
| 42 | nBE_L | /BE read back through U4's spare channel |
| 43 | nINT_L | /INT, through U7 |
| 44 | nNMI_L | /NMI, through U7 |
| 45 | NMI_REQ | to U6, pulled down (R4) |
| 46-47 | GP46, GP47 | spare, on test pads TP4, TP5 (silk "GP46", "GP47") for bench experiments |

### Decisions taken here, beyond the spec

- **USB-C, as on v3** (open question 11): firmware by BOOTSEL and the settings
  (`BASE`, `SIZE`) over USB. VBUS is diode-ORed with the edge +5V, so the board
  runs from USB alone on the bench. SWD pads TP1-TP3 too.
- **/BE read back:** U4's eighth channel brings /BE in as nBE_L (GPIO42), for
  timing on the bench and to see v3 serve a chunk.
- **BT1 fitted by hand** (2026-11-07): the CR2032 holder is the only part
  that forces JLCPCB's Standard PCBA (edge rails, which would sit against the
  fingers), so it is DNP there. Use a Keystone 3034 (DigiKey 36-3034-ND), the
  footprint's own part, so there is no drawing to check it against.
- **R3, a /BE pull-up to +5V_EDGE, DNP** (open question 13): the bus already has
  v3's 2k2; the pad is there if the prototype shows /BE rising too slowly.
- **An LDO for the core, not the on-chip switcher** (2026-11-07): U23, an
  ME6211C12M5G-N (SOT-23-5, C236672), supplies DVDD at 1.2 V, as on the
  OpenSpand card. The switcher's inductor (v3's L1, Abracon
  AOTA-B201610S3R3-101-T) was the scarcest part on the board (3 at JLCPCB in
  October) and needed a pre-order or hand fitting. VREG_LX is open, VREG_FB is
  grounded, VREG_VIN (C22) and VREG_AVDD (R28/C39) stay powered (RP2350
  datasheet 6.3.7). 1.2 V is what v3's firmware sets for 250 MHz; the firmware
  may put the unused regulator in HIZ to save about 400 µA. C40 (1 µF) at the
  LDO's input, C20 (4.7 µF) at its output; C21/C47/C48 stay at DVDD pins 51,
  32 and 10.
- **Test pads on the spare GPIOs:** TP4 (GPIO46) and TP5 (GPIO47), for bench
  experiments such as a second controller's handshake (spec, Bus mastering).
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
  /INT and /NMI over their fingers (A14, A15) between C3 and U2: U7 (x 51.5),
  U6 and R4 (x 56.5).
  Low address U1 above U2.
- **RP2350B cluster** at v3's coordinates with v3's designators (U20 at
  (62, 26), 90°), so v3's crystal and fan-out pre-routes carry over. Flash U21
  to its left.
- **Core supply:** with VREG_FB grounded, pin 51 is the only DVDD pin a track
  can reach from outside the pad ring (on v3 the switcher's output came in
  through FB). So the LDO U23 sits above pin 51, at (60.55, 14.6), pins 4/5
  towards the chip: its output runs straight down to pin 51, 0.3 mm wide,
  past C21, with C20 off its side, and v3's DVDD spine under the package
  carries it on to pins 10 and 32. C40 above U23; plane vias for its input
  and ground. FB goes to a GND via where L1 was. TP4/TP5 at (53.5, 13.5) and
  (53.5, 16), up and left of the chip's GPIO46/47 pins.
  One pre-route changed: on v3, supply pin 29 dropped into the lanes of pads 27
  and 28 to reach its via, because those GPIOs (26, 27) were spare. Here they
  are nDOE and BE_REQ, and the first run left both sealed in on all three
  attempts. The 24/29 supply link now carries on under the package to pin 15,
  whose fan via is already on the +3V3 plane.
- **SRAM** U10 and U11 on the right, leads top and bottom; the /WE gate, the
  supervisor and the pulls in the strip between them and the crystal.
- **Power:** diodes and LDO top left, LM66200 and the battery monitor in the
  free strip above the RP2350B, the CR2032 bottom left. USB-C on the left edge
  (v3's place). RUN and BOOTSEL on the top edge (v3's places). Write-protect
  slide switch on the right edge.

## Next

1. **Check footprints against drawings:** the MSK12C02's
   actuator should face the right edge; TSOP-I 48 against Infineon's 51-85183.
   Then check side A of the fingers (spec, open question 6).
2. **The SRAM rail with a fresh cell:** the LM66200 passes whichever input is
   higher, and a new CR2032 reads up to about 3.3 V open circuit, against +3V3's
   3.25-3.35 V. At SRAM standby currents the cell may carry the rail some of the
   time with the 2068 on. Harmless to the data; it costs battery life. Measure on
   the prototype (spec, hardware test 3); a Schottky in series with the cell
   would settle it if needed.
3. **Stock:** 42 CY62167EV30 at JLCPCB on 2026-10-18, 40 on 2026-11-07, enough for 20 boards.

## Routing

```bash
cd hardware
PY=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3
$PY scripts/flow.py                                   # jar from $FREEROUTING_JAR (or --jar); needs Homebrew's openjdk
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
