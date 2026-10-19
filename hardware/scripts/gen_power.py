#!/usr/bin/env python3
"""Generate power.kicad_sch: supplies, battery backup and the battery monitor.

  D1, D2   SS34: +5V_EDGE (edge pin B4) and USB VBUS diode-ORed into +5V, as on v3, so
           the board can be flashed and set up from USB alone on the bench
  U30      TLV1117LV33: +3V3 for the controller, buffers and gates
  U31      TPS3839G33 supervisor on +3V3 (about 3.08 V, 200 ms): drives both SRAMs' CE2
           high only while 3.3 V is good; R50 holds CE2 low with 3.3 V gone
  U32      LM66200 ideal-diode OR: VSRAM from +3V3 (VIN1) or the battery (VIN2), 1.32 uA
  BT1      CR2032 coin cell
  Q1, Q2   battery monitor switch (docs/spec.md "Battery monitor"): the cell is read
           straight off its terminal through R51/R52 only while BAT_EN is high
Spec refs in "Battery monitor" map to: Q1 -> Q1, Q2 -> Q2, R1/R2 -> R51/R52,
R3 -> R53, R4 -> R54, C1 -> C55.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from schlib import Sheet  # noqa: E402

C0603 = "Capacitor_SMD:C_0603_1608Metric"
R0603 = "Resistor_SMD:R_0603_1608Metric"
D_SMA = "Diode_SMD:D_SMA"
SOT223 = "Package_TO_SOT_SMD:SOT-223-3_TabPin2"
SOT23 = "Package_TO_SOT_SMD:SOT-23"
SOT583 = "Package_TO_SOT_SMD:SOT-583-8"
BAT_FP = "megabank:BatteryHolder_QJ_CR2032-BS-6-1"   # from Q&J's drawing; KiCad has no footprint for it

OUT, IN, BI = "output", "input", "bidirectional"


def main():
    s = Sheet("megabank.kicad_sch", "power.kicad_sch", "Megabank: Power and Battery")
    s.text("+5V = diode-OR of the edge +5V (B4) and USB VBUS. +3V3 from U30. VSRAM = the higher of +3V3 and the coin cell (U32).", 20, 285, 2.5)
    s.text("CE2 (U31) deselects both SRAMs below about 3.08 V, so nothing is written while power rises or falls.", 20, 290, 2.0)

    s.power("+5V", 60, 40, "#PWR10")
    s.power("+3V3", 120, 40, "#PWR11", flag=False)   # driven by U30
    s.power("GND", 180, 50, "#PWR12")

    # ---- inputs -----------------------------------------------------------------
    D1 = s.place("Device:D_Schottky", "D1", "SS34", D_SMA, 80, 90)
    s.glabel("+5V_EDGE", D1.pin("A"), IN); s.label("+5V", D1.pin("K"))
    D2 = s.place("Device:D_Schottky", "D2", "SS34", D_SMA, 80, 110)
    s.glabel("VBUS", D2.pin("A"), IN); s.label("+5V", D2.pin("K"))
    F1 = s.place("power:PWR_FLAG", "#FLG13", "PWR_FLAG", "", 50, 90)
    s.glabel("+5V_EDGE", F1.pin("1"), IN)
    F2 = s.place("power:PWR_FLAG", "#FLG14", "PWR_FLAG", "", 50, 110)
    s.glabel("VBUS", F2.pin("1"), IN)

    # ---- U30 3.3 V LDO --------------------------------------------------------------
    U30 = s.place("Regulator_Linear:TLV1117-33", "U30", "TLV1117LV33", SOT223, 140, 90)
    s.label("+5V", U30.pin("VI")); s.label("+3V3", U30.pin("VO")); s.label("GND", U30.pin("GND"))
    for k, (ref, net) in enumerate((("C50", "+5V"), ("C51", "+3V3"))):
        C = s.place("Device:C", ref, "10u", C0603, 125 + 30 * k, 110)
        s.label(net, C.pin("1")); s.label("GND", C.pin("2"))
    s.text("Load: the RP2350B and five logic parts, well under 100 mA.", 115, 125, 1.5)

    # ---- U31 supervisor -> CE2 ----------------------------------------------------------
    U31 = s.place("Power_Supervisor:TPS3839DBZ", "U31", "TPS3839G33", SOT23, 220, 90)
    s.label("+3V3", U31.pin("VDD")); s.label("GND", U31.pin("GND"))
    s.glabel("CE2", U31.pin("~{RESET}"), OUT)
    R50 = s.place("Device:R", "R50", "10k", R0603, 245, 90)
    s.glabel("CE2", R50.pin("1"), OUT); s.label("GND", R50.pin("2"))
    C52 = s.place("Device:C", "C52", "100n", C0603, 205, 110)
    s.label("+3V3", C52.pin("1")); s.label("GND", C52.pin("2"))
    s.text("U31: push-pull, so CE2 needs no pull-up. R50 10 k (not 100 k): CE2 must stay under 0.2 V on battery", 195, 125, 1.5)
    s.text("against two chips' input leakage; with 3.3 V on U31 still gives at least 2.9 V into it.", 195, 128, 1.5)

    # ---- U32 SRAM rail --------------------------------------------------------------------
    U32 = s.place("megabank:LM66200", "U32", "LM66200", SOT583, 140, 170)
    s.label("+3V3", U32.pin("VIN1")); s.label("BATT+", U32.pin("VIN2"))
    s.glabel("VSRAM", U32.pin("2"), OUT); s.glabel("VSRAM", U32.pin("7"), OUT)
    s.label("GND", U32.pin("~{ON}")); s.nc(U32.pin("ST"))
    s.label("GND", U32.pin("1")); s.label("GND", U32.pin("5"))
    C53 = s.place("Device:C", "C53", "1u", C0603, 180, 185)
    s.glabel("VSRAM", C53.pin("1"), OUT); s.label("GND", C53.pin("2"))
    C54 = s.place("Device:C", "C54", "100n", C0603, 110, 185)
    s.label("BATT+", C54.pin("1")); s.label("GND", C54.pin("2"))
    s.text("U32: not the TPS2115A (55 uA from the cell) and not plain diodes (the rail must sit near 3.3 V while the", 100, 200, 1.5)
    s.text("controller and buffers drive the SRAM). C53 >= 0.1 uF for the LM66200's soft start. ST unused.", 100, 203, 1.5)
    s.text("Check on the prototype: a fresh CR2032 reads up to about 3.3 V open-circuit, close to +3V3; see hardware/README.md.", 100, 206, 1.5)

    BT1 = s.place("Device:Battery_Cell", "BT1", "CR2032", BAT_FP, 60, 170)
    s.label("BATT+", BT1.pin("+")); s.label("GND", BT1.pin("-"))
    F4 = s.place("power:PWR_FLAG", "#FLG15", "PWR_FLAG", "", 40, 160)
    s.label("BATT+", F4.pin("1"))

    # ---- battery monitor ---------------------------------------------------------------
    Q1 = s.place("Transistor_FET:AO3401A", "Q1", "AO3401A", SOT23, 260, 170)
    s.label("BATT+", Q1.pin("S")); s.label("Q1_G", Q1.pin("G")); s.label("BAT_DIV", Q1.pin("D"))
    Q2 = s.place("Transistor_FET:Q_NMOS_GSD", "Q2", "AO3400A", SOT23, 300, 190)
    s.label("Q1_G", Q2.pin("D")); s.glabel("BAT_EN", Q2.pin("G"), IN); s.label("GND", Q2.pin("S"))
    parts = [("R51", "100k", "BAT_DIV", "BAT_ADC"), ("R52", "100k", "BAT_ADC", "GND"),
             ("R53", "1M", "BATT+", "Q1_G"), ("R54", "10k", "BAT_EN", "GND")]
    for k, (ref, val, a, b) in enumerate(parts):
        R = s.place("Device:R", ref, val, R0603, 330 + 15 * k, 160)
        for pin, net in (("1", a), ("2", b)):
            if net in ("BAT_ADC", "BAT_EN"):
                s.glabel(net, R.pin(pin), BI)
            else:
                s.label(net, R.pin(pin))
    C55 = s.place("Device:C", "C55", "100n", C0603, 390, 185)
    s.glabel("BAT_ADC", C55.pin("1"), BI); s.label("GND", C55.pin("2"))
    s.text("Battery monitor: BAT_EN high -> Q2 pulls Q1's gate down -> Q1 connects the cell to R51/R52 (half the cell).", 250, 210, 1.5)
    s.text("Wait 25 ms, average a few ADC readings, BAT_EN low. Off, the cell sees only the MOSFETs' leakage.", 250, 213, 1.5)
    s.text("R54 holds BAT_EN low while the controller is unpowered or booting; R52 holds the ADC pin at 0 V.", 250, 216, 1.5)

    s.write()
    print("wrote power.kicad_sch")


if __name__ == "__main__":
    main()
