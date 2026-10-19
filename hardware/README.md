# Megabank KiCad project

Status 2026-10-18: **schematic done, parts placed, not routed.** The board has the
TS-Pico v3's form factor and is built with v3's script pipeline (tspico-hardware,
`hardware/scripts/`), copied here and renamed. Spec: [../docs/spec.md](../docs/spec.md).

- **Schematic:** four sheets, generated. ERC: one warning, expected (below).
- **Board:** outline, gold fingers and mounting holes are v3's. Every part is
  placed, no courtyard overlaps. v3's RP2350B pre-routes (regulator, crystal,
  DVDD spine, QFN fan-out, U1's address via fan) run unchanged: 135 tracks, 34
  vias, no DRC errors. The rest is unrouted: Freerouting's jar was not on hand.
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

The finger geometry and the side-A-is-top assumption are v3's; see v3's
`hardware/README.md`, "Edge connector geometry". Check side A before ordering
(spec, open question 6).

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
| 40 | BAT_ADC | ADC0 |
| 41 | BAT_EN | |
| 42 | nBE_L | /BE read back through U4's spare channel |
| 43-47 | spare | |

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
  (62, 26), 90°), so `preroute.py` runs unchanged. Flash U21 to its left.
- **SRAM** U10 and U11 on the right, leads top and bottom; the /WE gate, the
  supervisor and the pulls in the strip between them and the crystal.
- **Power:** diodes and LDO top left, LM66200 and the battery monitor in the
  free strip above the RP2350B, the CR2032 bottom left. USB-C on the left edge
  (v3's place). RUN and BOOTSEL on the top edge (v3's places). Write-protect
  slide switch on the right edge.

## Next

1. **Route:** with Freerouting 2.4.1,
   ```bash
   cd hardware
   PY=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3
   $PY scripts/flow.py --jar <freerouting-2.4.1.jar>
   ```
   then `scripts/check.sh` and `python3 scripts/gen_fab.py`. Expect to adjust
   placement: each SRAM takes 33 signals from three directions.
2. **Check footprints against drawings:** the battery holder (Q&J
   CR2032-BS-6-1, C70377) uses Keystone 3034's footprint; the MSK12C02's
   actuator should face the right edge; TSOP-I 48 against Infineon's 51-85183.
3. **The SRAM rail with a fresh cell:** the LM66200 passes whichever input is
   higher, and a new CR2032 reads up to about 3.3 V open circuit, against +3V3's
   3.25-3.35 V. At SRAM standby currents the cell may carry the rail some of the
   time with the 2068 on. Harmless to the data; it costs battery life. Measure on
   the prototype (spec, hardware test 3); a Schottky in series with the cell
   would settle it if needed.
4. **Stock:** 42 CY62167EV30 at JLCPCB on 2026-10-18, enough for 20 boards.

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
