#!/usr/bin/env python3
"""Write the project symbol library, hardware/lib/megabank.kicad_sym.

  TS2068_Edge        the 2068 edge connector, copied from v3's gen_edge_symbol.py.
                     Pin numbers A1..A32 / B1..B32 (no A6/B6, the keying slot),
                     names per the TS2068 Technical Manual table 2.4.1-1. All pins
                     passive so ERC judges the nets by what is attached on the card.
  CY62167EV30xx-ZXI  16 Mbit SRAM, 48-pin TSOP I (Infineon 002-24706 Rev *B,
                     figure 2; the same pinout as the CY62167DV30). Drawn with every
                     pin; the memory sheet uses it as 2M x 8 (BYTE to ground, pin 45
                     I/O15/A20 as A20, BHE, BLE and I/O8-I/O14 unused).
  LM66200            dual ideal diode, SOT-583 (TI SLVSG04, table 5-1): the higher
                     of VIN1 and VIN2 feeds VOUT; ~ON low enables; ST open drain.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_board import SIDE_A, SIDE_B, HW  # noqa: E402

PITCH = 2.54
PIN_LEN = 5.08
FONT = '(effects (font (size 1.27 1.27)))'


def _props(ref, name, fp, descr, kw, top, bottom):
    return [f'    (property "Reference" "{ref}" (at 0 {top:.2f} 0) {FONT})',
            f'    (property "Value" "{name}" (at 0 {bottom:.2f} 0) {FONT})',
            f'    (property "Footprint" "{fp}" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))',
            '    (property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))',
            f'    (property "Description" "{descr}" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))',
            f'    (property "ki_keywords" "{kw}" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))']


def _pin(kind, x, y, ang, name, num):
    return (f'      (pin {kind} line (at {x:.2f} {y:.2f} {ang}) (length {PIN_LEN}) '
            f'(name "{name}" {FONT}) (number "{num}" {FONT}))')


def edge_symbol():
    name, w, rows = "TS2068_Edge", 25.4, 32
    top = (rows - 1) * PITCH / 2
    lines = [f'  (symbol "{name}" (pin_names (offset 1.016)) (exclude_from_sim no) (in_bom yes) (on_board yes)']
    lines += _props("P", name, "megabank:TS2068_EdgeConnector_2x32",
                    "TS2068 rear expansion connector, 2x32 card edge, slot at position 6",
                    "TS2068 Timex Sinclair edge connector", top + 3.81, -top - 3.81)
    lines += [f'    (symbol "{name}_0_1"',
              f'      (rectangle (start {-w/2:.2f} {top + 1.27:.2f}) (end {w/2:.2f} {-top - 1.27:.2f}) (stroke (width 0.254) (type default)) (fill (type background)))',
              '    )', f'    (symbol "{name}_1_1"']
    for i in range(rows):
        pos = i + 1
        if pos == 6:
            continue
        y = top - i * PITCH
        lines.append(_pin("passive", -w/2 - PIN_LEN, y, 0, SIDE_A[i], f"A{pos}"))
        lines.append(_pin("passive", w/2 + PIN_LEN, y, 180, SIDE_B[i], f"B{pos}"))
    lines += ['    )', '  )']
    return lines


# CY62167EV30 48-pin TSOP I, pin number -> name
SRAM_PINS = {
    1: "A15", 2: "A14", 3: "A13", 4: "A12", 5: "A11", 6: "A10", 7: "A9", 8: "A8",
    9: "A19", 10: "NC", 11: "~{WE}", 12: "CE2", 13: "NC", 14: "~{BHE}", 15: "~{BLE}", 16: "A18",
    17: "A17", 18: "A7", 19: "A6", 20: "A5", 21: "A4", 22: "A3", 23: "A2", 24: "A1",
    25: "A0", 26: "~{CE1}", 27: "VSS", 28: "~{OE}", 29: "I/O0", 30: "I/O8", 31: "I/O1", 32: "I/O9",
    33: "I/O2", 34: "I/O10", 35: "I/O3", 36: "I/O11", 37: "VCC", 38: "I/O4", 39: "I/O12", 40: "I/O5",
    41: "I/O13", 42: "I/O6", 43: "I/O14", 44: "I/O7", 45: "I/O15/A20", 46: "VSS", 47: "BYTE", 48: "A16",
}


def sram_symbol():
    name, w = "CY62167EV30xx-ZXI", 20.32
    by = {v: k for k, v in SRAM_PINS.items() if v not in ("NC", "VSS")}
    left = [f"A{i}" for i in range(20)] + ["I/O15/A20"]
    right = [f"I/O{i}" for i in range(15)]
    ctl = ["~{CE1}", "CE2", "~{OE}", "~{WE}", "~{BHE}", "~{BLE}", "BYTE"]
    rows = max(len(left), len(right) + 1 + len(ctl))
    top = (rows - 1) * PITCH / 2
    top = round(top / 1.27) * 1.27
    lines = [f'  (symbol "{name}" (pin_names (offset 1.016)) (exclude_from_sim no) (in_bom yes) (on_board yes)']
    lines += _props("U", name, "Package_SO:TSOP-I-48_18.4x12mm_P0.5mm",
                    "16 Mbit (1M x 16 / 2M x 8) static RAM, 2.2-3.6 V, 45 ns, 48-pin TSOP I",
                    "SRAM static RAM MoBL Cypress Infineon", top + 6.35, -top - 6.35)
    bot = top - (rows - 1) * PITCH
    lines += [f'    (symbol "{name}_0_1"',
              f'      (rectangle (start {-w/2:.2f} {top + 1.27:.2f}) (end {w/2:.2f} {bot - 1.27:.2f}) (stroke (width 0.254) (type default)) (fill (type background)))',
              '    )', f'    (symbol "{name}_1_1"']
    for i, n in enumerate(left):
        kind = "bidirectional" if n.startswith("I/O") else "input"
        lines.append(_pin(kind, -w/2 - PIN_LEN, top - i * PITCH, 0, n, by[n]))
    for i, n in enumerate(right):
        lines.append(_pin("bidirectional", w/2 + PIN_LEN, top - i * PITCH, 180, n, by[n]))
    for i, n in enumerate(ctl):
        lines.append(_pin("input", w/2 + PIN_LEN, top - (len(right) + 1 + i) * PITCH, 180, n, by[n]))
    # power on the top and bottom edges; the second VSS stacked on the first
    lines.append(_pin("power_in", 0, top + 1.27 + PIN_LEN, 270, "VCC", 37))
    lines.append(_pin("power_in", 0, bot - 1.27 - PIN_LEN, 90, "VSS", 27))
    lines.append(_pin("passive", 0, bot - 1.27 - PIN_LEN, 90, "VSS", 46))
    for k, num in enumerate((10, 13)):
        lines.append(_pin("no_connect", -w/2 + 5.08 + 2.54 * k, bot - 1.27 - PIN_LEN, 90, "NC", num))
    lines += ['    )', '  )']
    return lines


def lm66200_symbol():
    name, w = "LM66200", 15.24
    left = [("VIN1", 3, "power_in"), ("VIN2", 6, "power_in"), (None, 0, ""), ("~{ON}", 4, "input")]
    right = [("VOUT", 2, "power_out"), ("VOUT", 7, "passive"), (None, 0, ""), ("ST", 8, "open_collector")]
    top = 3.81
    lines = [f'  (symbol "{name}" (pin_names (offset 1.016)) (exclude_from_sim no) (in_bom yes) (on_board yes)']
    lines += _props("U", name, "Package_TO_SOT_SMD:SOT-583-8",
                    "Dual ideal diode (power ORing), 1.6-5.5 V, 2.5 A, 1.32 uA, SOT-583",
                    "ideal diode ORing power mux", top + 3.81, -top - 8.89)
    lines += [f'    (symbol "{name}_0_1"',
              f'      (rectangle (start {-w/2:.2f} {top + 2.54:.2f}) (end {w/2:.2f} {top - 10.16:.2f}) (stroke (width 0.254) (type default)) (fill (type background)))',
              '    )', f'    (symbol "{name}_1_1"']
    for i, (n, num, kind) in enumerate(left):
        if n:
            lines.append(_pin(kind, -w/2 - PIN_LEN, top - i * PITCH, 0, n, num))
    for i, (n, num, kind) in enumerate(right):
        if n:
            lines.append(_pin(kind, w/2 + PIN_LEN, top - i * PITCH if num != 7 else top, 180, n, num))
    lines.append(_pin("power_in", 0, top - 10.16 - PIN_LEN, 90, "GND", 1))
    lines.append(_pin("passive", 0, top - 10.16 - PIN_LEN, 90, "GND", 5))
    lines += ['    )', '  )']
    return lines


def main():
    lines = ['(kicad_symbol_lib (version 20241209) (generator "gen_symbols.py") (generator_version "1")']
    lines += edge_symbol() + sram_symbol() + lm66200_symbol() + [')']
    path = os.path.join(HW, "lib", "megabank.kicad_sym")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote", path)


if __name__ == "__main__":
    main()
