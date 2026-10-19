#!/usr/bin/env python3
"""Generate memory.kicad_sch: the 4 MB of SRAM.

  U10, U11  CY62167EV30LL-45ZXIT (16 Mbit, TSOP I 48), each used as 2M x 8: BYTE to
            ground, pin 45 (I/O15/A20) is A20, I/O8-I/O14 unused. 4 MB in two chips.
            A0-A12   <- BA0-BA12, the buffered Z80 address
            A13-A20  <- PA13-PA20, the page bits from the controller
            I/O0-7  <-> SD0-SD7, the data buffer's SRAM side (and the controller's GPIO0-7)
            ~CE1     <- nCE1_0 / nCE1_1 from the controller: the 9th page bit picks the chip
            CE2      <- CE2 from the supervisor (power sheet): low deselects whatever ~CE1 does
            ~OE      <- nRD_L;  ~WE <- nSWE from U12
            VCC      <- VSRAM, the battery-backed rail
  U12       SN74LVC1G32: nSWE = nWR_L OR WINH (the controller's per-chunk write inhibit)
  R10, R11  ~CE1 pull-downs: at a logic level on battery (a pull-up to VSRAM would feed the
            unpowered RP2350B); CE2 low keeps the chips deselected
  R12       WINH pull-up: Z80 writes cannot reach the SRAM until the firmware runs
  R13, R14  0 R, BHE and BLE to ground: fitted for the EV30 (needs them at a logic level
            for its standby figures), do not place for a DV30 (marks them "leave floating")
SRAM address and data lines may be permuted freely at layout (both chips see the same
permutation through the same Z80 and page bits), except that SDn must stay the
controller's GPIOn: the controller answers IN FCh/FDh on the same bus.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from schlib import Sheet  # noqa: E402

TSOP48 = "Package_SO:TSOP-I-48_18.4x12mm_P0.5mm"
SOT23_5 = "Package_TO_SOT_SMD:SOT-23-5"
R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"

OUT, IN, BI = "output", "input", "bidirectional"


def main():
    s = Sheet("megabank.kicad_sch", "memory.kicad_sch", "Megabank: SRAM")
    s.text("4 MB: two CY62167EV30 as 2M x 8. SRAM address = page x 8K + A12..A0; the page's 9th bit picks the chip through the ~CE1s.", 20, 285, 2.5)
    s.text("Alternate fit: CY62167DV30LL-55ZXIT, same footprint and pinout; leave R13 and R14 off for it.", 20, 290, 2.0)

    s.power("+3V3", 330, 30, "#PWR03", flag=False)
    s.power("GND", 360, 40, "#PWR04", flag=False)

    for k, (ref, ce1) in enumerate((("U10", "nCE1_0"), ("U11", "nCE1_1"))):
        U = s.place("megabank:CY62167EV30xx-ZXI", ref, "CY62167EV30LL-45ZXIT", TSOP48, 80 + 120 * k, 130)
        for i in range(13):
            s.glabel(f"BA{i}", U.pin(f"A{i}"), IN)
        for i in range(13, 20):
            s.glabel(f"PA{i}", U.pin(f"A{i}"), IN)
        s.glabel("PA20", U.pin("I/O15/A20"), IN)
        for i in range(8):
            s.glabel(f"SD{i}", U.pin(f"I/O{i}"), BI)
        for i in range(8, 15):
            s.nc(U.pin(f"I/O{i}"))
        s.glabel(ce1, U.pin("~{CE1}"), IN)
        s.glabel("CE2", U.pin("CE2"), IN)
        s.glabel("nRD_L", U.pin("~{OE}"), IN)
        s.label("nSWE", U.pin("~{WE}"))
        s.label("BHE", U.pin("~{BHE}")); s.label("BLE", U.pin("~{BLE}"))
        s.label("GND", U.pin("BYTE"))
        s.glabel("VSRAM", U.pin("37"), IN)
        s.label("GND", U.pin("27")); s.label("GND", U.pin("46"))

    # ---- /WE gate ---------------------------------------------------------------
    U12 = s.place("74xGxx:74LVC1G32", "U12", "SN74LVC1G32DBVR", SOT23_5, 320, 120)
    s.glabel("nWR_L", U12.pin("1"), IN); s.glabel("WINH", U12.pin("2"), IN); s.label("nSWE", U12.pin("4"))
    s.label("+3V3", U12.pin("5")); s.label("GND", U12.pin("3"))
    s.text("U12: ~WE = nWR_L OR WINH. WINH is set per chunk from WPROT and the switch, and pulled up (R12).", 290, 135, 1.5)

    # ---- pulls and the BHE/BLE links ------------------------------------------------
    pulls = [("R10", "nCE1_0", "GND", "10k"), ("R11", "nCE1_1", "GND", "10k"), ("R12", "WINH", "+3V3", "10k"),
             ("R13", "BHE", "GND", "0"), ("R14", "BLE", "GND", "0")]
    for k, (ref, net, rail, val) in enumerate(pulls):
        R = s.place("Device:R", ref, val, R0603, 300 + 15 * k, 170)
        if net in ("BHE", "BLE"):
            s.label(net, R.pin("1"))
        else:
            s.glabel(net, R.pin("1"), IN)
        s.label(rail, R.pin("2"))
    s.text("R10/R11: ~CE1 pull-downs (to ground, not VSRAM). R13/R14: fit for the EV30, DNP for a DV30 build.", 290, 185, 1.5)

    # ---- decoupling: 100 nF at each SRAM (VSRAM), 100 nF at U12 ----------------------
    for k, (ref, net) in enumerate((("C10", "VSRAM"), ("C11", "VSRAM"), ("C12", "+3V3"))):
        C = s.place("Device:C", ref, "100n", C0603, 40 + 9 * k, 255)
        if net == "VSRAM":
            s.glabel(net, C.pin("1"), IN)
        else:
            s.label(net, C.pin("1"))
        s.label("GND", C.pin("2"))

    s.write()
    print("wrote memory.kicad_sch")


if __name__ == "__main__":
    main()
