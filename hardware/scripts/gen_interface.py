#!/usr/bin/env python3
"""Generate interface.kicad_sch: the 2068 side of the Megabank.

Contents (docs/spec.md, "Hardware"):
  P1   TS2068 edge connector (v3's gold fingers)
  U1   74LVC245  Z80 A0-A7  -> BA0-BA7     always on (fixed B->A): port decode, SRAM A0-A7
  U2   74LVC245  Z80 A8-A15 -> BA8-BA15    always on: SRAM A8-A12, chunk A13-A15
  U3   74LVC245  Z80 D0-D7 <-> SD0-SD7     DIR = /RD, /OE = nDOE from the controller
  U4   74LVC245  Z80 control -> 3.3 V copies for the controller (fixed B->A)
  U5   74LVC1G06 open-drain inverter: BE_REQ -> /BE (wire-ORs with v3's 74LVC06)
  R1   BE_REQ pull-down (boot does not rely on the RP2350's internal pull-down, erratum E9)
  R2   nDOE pull-up: the data buffer stays off the bus until the firmware runs
  R3   /BE pull-up to the 2068's +5V, DNP: the bus already has v3's 2k2 (R14 on v3);
       fit it only if open question 13 says the board needs its own
Unlike v3 the address buffers are not a byte mux: the SRAM needs A0-A12 on
every memory cycle, so every address bit has its own 3.3 V net (BAn).
Net naming: n<NAME> = active low; _L = 3.3 V copy of a bus signal.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from schlib import Sheet  # noqa: E402

TSSOP20 = "Package_SO:TSSOP-20_4.4x6.5mm_P0.65mm"
SC70_5 = "Package_TO_SOT_SMD:SOT-353_SC-70-5"
R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"

OUT, IN, BI = "output", "input", "bidirectional"


def main():
    s = Sheet("megabank.kicad_sch", "interface.kicad_sch", "Megabank: 2068 Interface")
    s.text("2068 bus interface. Nets: n = active low, _L = 3.3 V copy of a bus signal, BAn = buffered address, SDn = SRAM-side data.", 20, 285, 2.5)
    s.text("Power: edge +5V (B4) goes to the power sheet as +5V_EDGE. +15V (B3) unused. LVC inputs take the 2068's 5 V; a 3.3 V high is a TTL high.", 20, 290, 2.0)

    # ---- P1 edge connector -------------------------------------------------
    P1 = s.place("megabank:TS2068_Edge", "P1", "TS2068_Edge", "megabank:TS2068_EdgeConnector_2x32", 60, 120)
    used = {
        "A1": "GND", "B1": "GND", "A32": "GND", "B32": "GND", "B7": "GND", "B8": "GND",
        "A4": "D7", "A7": "D0", "A8": "D1", "A9": "D2", "A10": "D6", "A11": "D5", "A12": "D3", "A13": "D4",
        "A17": "nMREQ", "A18": "nIORQ", "A19": "nRD", "A20": "nWR", "A24": "nRESET",
        "A25": "nM1", "A26": "nRFSH", "A29": "nBE",
        "B10": "A0", "B11": "A1", "B12": "A2", "B13": "A3",
        "B14": "A15", "B15": "A14", "B16": "A13", "B17": "A12", "B18": "A11", "B19": "A10", "B20": "A9", "B21": "A8",
        "B22": "A7", "B23": "A6", "B24": "A5", "B25": "A4",
    }
    for pin, net in used.items():
        s.label(net, P1.pin(pin))
    s.glabel("+5V_EDGE", P1.pin("B4"), OUT)
    for side in "AB":
        for pos in range(1, 33):
            pin = f"{side}{pos}"
            if pos != 6 and pin not in used and pin != "B4":
                s.nc(P1.pin(pin))

    s.power("+3V3", 330, 30, "#PWR01", flag=False)
    s.power("GND", 360, 40, "#PWR02", flag=False)

    def transceiver(ref, x, y):
        return s.place("74xx:74LS245", ref, "74LVC245APW", TSSOP20, x, y)

    # ---- U1/U2 address buffers, B (Z80) -> A (3.3 V), always enabled ----------------
    # Finger order (left to right): A0 A1 A2 A3 | A15 ... A8 | A7 A6 A5 A4. As on v3, U2 sits
    # over the middle eight fingers and U1 in the row above, its B pins in finger order
    # (A0-A3 from the left, A7-A4 from the right); at rot 270 B7 is the leftmost pin.
    U1 = transceiver("U1", 150, 50)
    for b_, idx in zip(range(7, -1, -1), [0, 1, 2, 3, 7, 6, 5, 4]):
        s.label(f"A{idx}", U1.pin(f"B{b_}"))
        s.glabel(f"BA{idx}", U1.pin(f"A{b_}"), OUT)
    U2 = transceiver("U2", 150, 110)
    for b_, idx in zip(range(7, -1, -1), [15, 14, 13, 12, 11, 10, 9, 8]):
        s.label(f"A{idx}", U2.pin(f"B{b_}"))
        s.glabel(f"BA{idx}", U2.pin(f"A{b_}"), OUT)
    for U in (U1, U2):
        s.label("GND", U.pin("1"))       # DIR low: B -> A
        s.label("GND", U.pin("19"))      # always enabled
        s.label("+3V3", U.pin("20")); s.label("GND", U.pin("10"))

    # ---- U3 data transceiver: A = Z80 (faces the fingers), B = SD bus ---------------
    # DIR = /RD straight from the bus: low on a read = B -> A (board -> Z80). The SRAM's
    # data bits may be permuted freely at layout; the controller's GPIO0-7 may not.
    U3 = transceiver("U3", 150, 170)
    for b_, idx in zip(range(8), [7, 0, 1, 2, 6, 5, 3, 4]):
        s.label(f"D{idx}", U3.pin(f"A{b_}"))
        s.glabel(f"SD{idx}", U3.pin(f"B{b_}"), BI)
    s.label("nRD", U3.pin("1"))
    s.glabel("nDOE", U3.pin("19"), IN)
    s.label("+3V3", U3.pin("20")); s.label("GND", U3.pin("10"))

    # ---- U4 control inputs, B (Z80) -> A (3.3 V), in finger order ----------------------
    U4 = transceiver("U4", 150, 230)
    ctl = ["nMREQ", "nIORQ", "nRD", "nWR", "nRESET", "nM1", "nRFSH", "nBE"]
    for b_, src in zip(range(7, -1, -1), ctl):
        s.label(src, U4.pin(f"B{b_}"))
        s.glabel(src + "_L", U4.pin(f"A{b_}"), OUT)
    s.label("GND", U4.pin("1")); s.label("GND", U4.pin("19"))
    s.label("+3V3", U4.pin("20")); s.label("GND", U4.pin("10"))
    s.text("U4's eighth channel reads /BE back (nBE_L), for bench timing and to see v3 serving a chunk.", 120, 265, 1.5)

    # ---- U5 /BE driver ------------------------------------------------------------------
    U5 = s.place("74xGxx:74LVC1G06", "U5", "74LVC1G06", SC70_5, 260, 60)
    s.glabel("BE_REQ", U5.pin("2"), IN); s.label("nBE", U5.pin("4"))
    s.label("+3V3", U5.pin("5")); s.label("GND", U5.pin("3"))
    R1 = s.place("Device:R", "R1", "10k", R0603, 290, 60)
    s.glabel("BE_REQ", R1.pin("1"), IN); s.label("GND", R1.pin("2"))
    R2 = s.place("Device:R", "R2", "10k", R0603, 305, 60)
    s.label("+3V3", R2.pin("1")); s.glabel("nDOE", R2.pin("2"), IN)
    R3 = s.place("Device:R", "R3", "2k2", R0603, 320, 60, dnp=True)
    s.glabel("+5V_EDGE", R3.pin("1"), IN); s.label("nBE", R3.pin("2"))
    s.text("U5: open drain, so it wire-ORs with v3's 74LVC06 on /BE. R1: BE_REQ held low (released) until the firmware runs.", 245, 80, 1.5)
    s.text("R2: data buffer held off. R3 (DNP): /BE pull-up to the 2068's +5V, only if the bus's own (v3's 2k2) proves weak.", 245, 83, 1.5)

    # ---- decoupling ------------------------------------------------------------
    for k in range(5):          # C1-C4: U1-U4, C5: U5
        C = s.place("Device:C", f"C{k + 1}", "100n", C0603, 40 + 9 * k, 255)
        s.label("+3V3", C.pin("1")); s.label("GND", C.pin("2"))

    s.write()
    print("wrote interface.kicad_sch")


if __name__ == "__main__":
    main()
