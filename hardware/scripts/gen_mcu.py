#!/usr/bin/env python3
"""Generate mcu.kicad_sch: the RP2350B controller.

The core circuit is v3's mcu sheet (tspico-hardware, scripts/gen_mcu.py), copied with
its reference designators so that v3's placement and pre-routes (regulator, crystal,
QFN fan-out) carry over: U20 RP2350B, L1/C20/C22 core regulator, R28/C39 AVDD filter,
Y1/R20/C35/C36 crystal, C21/C23-C31/C47/C48 decoupling, U21 QSPI flash, J1 USB-C
(data, VBUS to the power sheet), SW3 RUN, SW4 BOOTSEL, TP1-TP3 SWD pads.
Dropped from v3: the PSRAM (U22, C38). Added: SW5, the write-protect switch.

GPIO map (docs/spec.md "Pins"). PIO windows: a state machine sees 32 pins, from GPIO0
or GPIO16, so everything both programs need sits in GPIO16-31:
  GPIO0-7    SD0-SD7       SRAM-side data (port reads and writes)        window 0
  GPIO8-15   BA0-BA7       port decode                                     window 0
  GPIO16-18  BA13-BA15     chunk number (out pc, 3)                        both
  GPIO19-25  nMREQ_L nIORQ_L nRD_L nWR_L nRFSH_L nM1_L nRESET_L            both
  GPIO26     nDOE          data buffer enable (R2 pull-up)                 both
  GPIO27     BE_REQ        to U5 -> /BE (R1 pull-down)                     both
  GPIO28-29  nCE1_0/1      SRAM chip enables (R10/R11 pull-downs)          window 16
  GPIO30-37  PA13-PA20     page bits, contiguous after the ~CE1s           window 16
  GPIO38     WINH          write inhibit (R12 pull-up)                     window 16
  GPIO39     nWP           write-protect switch, low = protected (internal pull-up)
  GPIO40     BAT_ADC       ADC0
  GPIO41     BAT_EN
  GPIO42     nBE_L         /BE read back
  GPIO43     nINT_L        /INT, through U7                                window 16
  GPIO44     nNMI_L        /NMI, through U7                                window 16
  GPIO45     NMI_REQ       to U6 -> /NMI (R4 pull-down)                    window 16
  GPIO46-47  spare
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from schlib import Sheet  # noqa: E402

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
L_FP = "megabank:L_Abracon_AOTA-B201610"
C0402 = "Capacitor_SMD:C_0402_1005Metric"
SW_FP = "Button_Switch_SMD:SW_Push_1P1T_XKB_TS-1187A"
SLIDE_FP = "Button_Switch_SMD:SW_SPDT_Shouhan_MSK12C02"
TP_FP = "TestPoint:TestPoint_Pad_1.5x1.5mm"

OUT, IN, BI = "output", "input", "bidirectional"

# GPIO number -> (net, shape)
GPIO = {
    **{i: (f"SD{i}", BI) for i in range(8)},
    **{8 + i: (f"BA{i}", IN) for i in range(8)},
    16: ("BA13", IN), 17: ("BA14", IN), 18: ("BA15", IN),
    19: ("nMREQ_L", IN), 20: ("nIORQ_L", IN), 21: ("nRD_L", IN), 22: ("nWR_L", IN),
    23: ("nRFSH_L", IN), 24: ("nM1_L", IN), 25: ("nRESET_L", IN),
    26: ("nDOE", OUT), 27: ("BE_REQ", OUT), 28: ("nCE1_0", OUT), 29: ("nCE1_1", OUT),
    **{30 + i: (f"PA{13 + i}", OUT) for i in range(8)},
    38: ("WINH", OUT), 39: ("nWP", IN), 40: ("BAT_ADC", BI), 41: ("BAT_EN", OUT), 42: ("nBE_L", IN),
    43: ("nINT_L", IN), 44: ("nNMI_L", IN), 45: ("NMI_REQ", OUT),
}
SPARE = [g for g in range(48) if g not in GPIO]


def main():
    s = Sheet("megabank.kicad_sch", "mcu.kicad_sch", "Megabank: RP2350B Controller")
    s.text("RP2350B core per the RP2350 hardware design guide, as on v3. The controller is the memory map, not the data path: bytes go SRAM <-> U3 <-> bus.", 20, 285, 2.5)
    s.text("GPIO map in scripts/gen_mcu.py. USB-C: firmware and settings (BASE, SIZE); VBUS to the power sheet so the board runs from USB on the bench.", 20, 290, 2.0)

    s.power("+3V3", 300, 30, "#PWR05", flag=False)
    s.power("GND", 330, 40, "#PWR06", flag=False)

    # ---- U20 RP2350B ---------------------------------------------------------------
    U = s.place("MCU_RaspberryPi:RP2350B", "U20", "RP2350B", "Package_DFN_QFN:QFN-80-1EP_10x10mm_P0.4mm_EP3.4x3.4mm", 130, 150)
    names = U.names()
    by_name = {}
    for num, nm in names.items():
        by_name.setdefault(nm.split("/")[0], num)
    for g, (net, shape) in GPIO.items():
        s.glabel(net, U.pin(by_name[f"GPIO{g}"]), shape)
    for g in SPARE:
        s.nc(U.pin(by_name[f"GPIO{g}"]))
    for num, nm in names.items():
        if nm in ("IOVDD", "QSPI_IOVDD", "USB_OTP_VDD", "ADC_AVDD", "VREG_VIN"):
            s.label("+3V3", U.pin(num))
        elif nm == "VREG_AVDD":
            s.label("VREG_AVDD", U.pin(num))
        elif nm in ("GND", "VREG_PGND"):
            s.label("GND", U.pin(num))
        elif nm == "DVDD":
            s.label("DVDD", U.pin(num))
    s.label("VREG_LX", U.pin(by_name["VREG_LX"]))
    s.label("DVDD", U.pin(by_name["VREG_FB"]))
    s.label("XIN", U.pin(by_name["XIN"])); s.label("XOUT", U.pin(by_name["XOUT"]))
    s.label("SWCLK", U.pin(by_name["SWCLK"])); s.label("SWDIO", U.pin(by_name["SWDIO"]))
    s.label("RUN", U.pin(by_name["RUN"]))
    s.label("USB_DM", U.pin(by_name["USB_DM"])); s.label("USB_DP", U.pin(by_name["USB_DP"]))
    for nm in ("QSPI_SD0", "QSPI_SD1", "QSPI_SD2", "QSPI_SD3", "QSPI_SCLK"):
        s.label(nm, U.pin(by_name[nm]))
    s.label("nQSPI_SS", U.pin(by_name["~{QSPI_SS}"]))

    # ---- core regulator: VREG_LX -> L1 -> DVDD, 4.7 uF x2, 4.7 uF on VREG_VIN --------
    # Fitted by JLCPCB (parts pre-order or Global Sourcing) per the spec; if neither comes through,
    # set dnp=True and fit it by hand, as v3 proto2 did. Polarised: dot on pad 2, DVDD.
    L1 = s.place("Device:L", "L1", "3.3uH", L_FP, 250, 60)
    s.label("VREG_LX", L1.pin("1")); s.label("DVDD", L1.pin("2"))   # LCSC numbering: dot-marked terminal = pad 2 = DVDD (1.1 V) side
    for k, (ref, net, val, fp) in enumerate([("C20", "DVDD", "4.7u", C0402), ("C22", "+3V3", "4.7u", C0402), ("C21", "DVDD", "100n", C0603), ("C47", "DVDD", "100n", C0603), ("C48", "DVDD", "100n", C0603)]):
        C = s.place("Device:C", ref, val, fp, 265 + 10 * k, 60)
        s.label(net, C.pin("1")); s.label("GND", C.pin("2"))
    s.text("L1 (Abracon AOTA-B201610S3R3-101-T, polarised: dot = pad 2 to DVDD), C20 output, C22 at VREG_VIN (0402 as in the design guide). C21/C47/C48: 100 nF at DVDD pins 51, 32, 10.", 245, 75, 1.5)
    R28 = s.place("Device:R", "R28", "33", R0603, 300, 85)
    s.label("+3V3", R28.pin("1")); s.label("VREG_AVDD", R28.pin("2"))
    C39 = s.place("Device:C", "C39", "4.7u", C0402, 315, 85)
    s.label("VREG_AVDD", C39.pin("1")); s.label("GND", C39.pin("2"))
    s.text("R28/C39: 33 R + 4.7 uF filter on VREG_AVDD (design guide R3/C9).", 245, 95, 1.5)
    FL2 = s.place("power:PWR_FLAG", "#FLG08", "PWR_FLAG", "", 330, 85)
    s.label("VREG_AVDD", FL2.pin("1"))
    FL = s.place("power:PWR_FLAG", "#FLG06", "PWR_FLAG", "", 300, 60)
    s.label("DVDD", FL.pin("1"))

    # ---- IOVDD and analog decoupling: 12 x 100 nF ----------------------------------
    for k in range(9):
        C = s.place("Device:C", f"C{23 + k}", "100n", C0603, 250 + 9 * k, 100)
        s.label("+3V3", C.pin("1")); s.label("GND", C.pin("2"))
    s.text("C23-C31: one per 3.3 V supply pin or adjacent pair (5, 15, 24, 29, 41, 50, 59/60, 68/69, 76); pin 64 has C22.", 245, 115, 1.5)

    # ---- crystal ---------------------------------------------------------------------
    Y1 = s.place("Device:Crystal_GND24", "Y1", "12MHz", "Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm", 270, 150)
    s.label("XIN", Y1.pin("1")); s.label("XOUT_R", Y1.pin("3")); s.label("GND", Y1.pin("2")); s.label("GND", Y1.pin("4"))
    R20 = s.place("Device:R", "R20", "1k", R0603, 295, 150)
    s.label("XOUT", R20.pin("1")); s.label("XOUT_R", R20.pin("2"))
    C35 = s.place("Device:C", "C35", "15p", C0603, 255, 165)
    s.label("XIN", C35.pin("1")); s.label("GND", C35.pin("2"))
    C36 = s.place("Device:C", "C36", "15p", C0603, 285, 165)
    s.label("XOUT_R", C36.pin("1")); s.label("GND", C36.pin("2"))
    s.text("Y1: 12 MHz, e.g. ABM8-272-T3 (CL 10 pF). C35/C36 15 pF, R20 1 k in series with XOUT.", 245, 180, 1.5)

    # ---- QSPI flash and PSRAM ---------------------------------------------------------
    F = s.place("Memory_Flash:W25Q128JVS", "U21", "GD25Q128ESIGR", "Package_SO:SOIC-8_5.3x5.3mm_P1.27mm", 340, 150)
    s.label("nQSPI_SS", F.pin("1")); s.label("QSPI_SD1", F.pin("2")); s.label("QSPI_SD2", F.pin("3")); s.label("GND", F.pin("4"))
    s.label("QSPI_SD0", F.pin("5")); s.label("QSPI_SCLK", F.pin("6")); s.label("QSPI_SD3", F.pin("7")); s.label("+3V3", F.pin("8"))
    C = s.place("Device:C", "C37", "100n", C0603, 375, 175)
    s.label("+3V3", C.pin("1")); s.label("GND", C.pin("2"))
    R21 = s.place("Device:R", "R21", "10k", R0603, 395, 150)
    s.label("+3V3", R21.pin("1")); s.label("nQSPI_SS", R21.pin("2"))
    s.text("U21 boot flash (XIP CS0), C37 at U21. Holds the firmware and the settings (BASE, SIZE).", 320, 225, 1.5)

    # ---- RUN and BOOTSEL -------------------------------------------------------------
    R22 = s.place("Device:R", "R22", "10k", R0603, 250, 210)
    s.label("+3V3", R22.pin("1")); s.label("RUN", R22.pin("2"))
    SW3 = s.place("Switch:SW_Push", "SW3", "RUN", SW_FP, 270, 225)
    s.label("RUN", SW3.pin("1")); s.label("GND", SW3.pin("2"))
    R23 = s.place("Device:R", "R23", "1k", R0603, 250, 245)
    s.label("nQSPI_SS", R23.pin("1")); s.label("BOOTSEL_SW", R23.pin("2"))
    SW4 = s.place("Switch:SW_Push", "SW4", "BOOTSEL", SW_FP, 270, 260)
    s.label("BOOTSEL_SW", SW4.pin("1")); s.label("GND", SW4.pin("2"))

    # ---- USB-C, data only -----------------------------------------------------------
    J1 = s.place("Connector:USB_C_Receptacle_USB2.0_16P", "J1", "USB-C", "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12", 40, 60)
    for pin in ("A4", "A9", "B4", "B9"):
        s.glabel("VBUS", J1.pin(pin), OUT)
    for pin in ("A8", "B8"):
        s.nc(J1.pin(pin))
    for pin in ("A1", "A12", "B1", "B12", "SH"):
        s.label("GND", J1.pin(pin))
    s.label("USB_DP_C", J1.pin("A6")); s.label("USB_DP_C", J1.pin("B6"))
    s.label("USB_DM_C", J1.pin("A7")); s.label("USB_DM_C", J1.pin("B7"))
    s.label("CC1", J1.pin("A5")); s.label("CC2", J1.pin("B5"))
    R24 = s.place("Device:R", "R24", "5k1", R0603, 80, 40)
    s.label("CC1", R24.pin("1")); s.label("GND", R24.pin("2"))
    R25 = s.place("Device:R", "R25", "5k1", R0603, 95, 40)
    s.label("CC2", R25.pin("1")); s.label("GND", R25.pin("2"))
    R26 = s.place("Device:R", "R26", "33", R0603, 80, 85)
    s.label("USB_DP_C", R26.pin("1")); s.label("USB_DP", R26.pin("2"))
    R27 = s.place("Device:R", "R27", "33", R0603, 95, 85)
    s.label("USB_DM_C", R27.pin("1")); s.label("USB_DM", R27.pin("2"))
    s.text("J1: USB 2.0. VBUS -> power sheet. SBU unconnected.", 20, 100, 1.5)
    s.text("R24/R25 5.1 k Rd. R26/R27 33 R series (JLC basic; 27 R is not).", 20, 103, 1.5)

    # ---- write-protect switch -------------------------------------------------------
    SW5 = s.place("Switch:SW_SPDT", "SW5", "WP", SLIDE_FP, 270, 280)
    s.glabel("nWP", SW5.pin("B"), IN); s.label("GND", SW5.pin("A")); s.nc(SW5.pin("C"))
    s.text("SW5: write protect. Common to GPIO39 (internal pull-up); one throw to ground = protected, the other open.", 245, 292, 1.5)

    # ---- SWD pads ---------------------------------------------------------------------
    for k, (ref, net) in enumerate([("TP1", "SWCLK"), ("TP2", "SWDIO"), ("TP3", "GND")]):
        T = s.place("Connector:TestPoint", ref, net, TP_FP, 40 + 15 * k, 250)
        s.label(net, T.pin("1"))
    s.text("TP1-TP3: SWD debug pads.", 30, 262, 1.5)

    s.write()
    print("wrote mcu.kicad_sch")


if __name__ == "__main__":
    main()
