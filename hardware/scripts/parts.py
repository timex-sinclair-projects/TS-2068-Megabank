"""Orderable part numbers for the Megabank BOM.

Keyed by (Value, footprint basename). Each entry: (MPN, LCSC, note).
"basic" in the note means a JLCPCB basic part (no extended-part fee).
Carried over from v3's parts.py where the part is the same; the Megabank's own
parts were looked up with jlc_price.py on 2026-10-18. Re-check stock at order time.
"""

R = "R_0603_1608Metric"
C6 = "C_0603_1608Metric"
T20 = "TSSOP-20_4.4x6.5mm_P0.65mm"

PARTS = {
    # ---- resistors (0603, 1%) ------------------------------------------------
    ("0", R): ("0603WAF0000T5E", "C21189", "basic"),
    ("33", R): ("0603WAF330JT5E", "C23140", "basic"),
    ("1k", R): ("0603WAF1001T5E", "C21190", "basic"),
    ("2k2", R): ("0603WAF2201T5E", "C4190", "basic"),
    ("5k1", R): ("0603WAF5101T5E", "C23186", "basic"),
    ("10k", R): ("0603WAF1002T5E", "C25804", "basic"),
    ("100k", R): ("0603WAF1003T5E", "C25803", "basic"),
    ("1M", R): ("0603WAF1004T5E", "C22935", "basic"),
    # ---- capacitors ----------------------------------------------------------
    ("15p", C6): ("CL10C150JB8NNNC", "C1644", "basic, C0G"),
    ("100n", C6): ("CC0603KRX7R9BB104", "C14663", "basic, X7R 50V"),
    ("1u", C6): ("CL10A105KB8NNNC", "C15849", "basic, X5R 25V"),
    ("4.7u", "C_0402_1005Metric"): ("CL05A475MP5NRNC", "C23733", "basic, X5R 10V, 0402 as in the RP2350 design guide"),
    ("10u", C6): ("CL10A106KP8NNNC", "C19702", "basic, X5R 10V"),
    # ---- semiconductors ------------------------------------------------------
    ("CY62167EV30LL-45ZXIT", "TSOP-I-48_18.4x12mm_P0.5mm"): ("CY62167EV30LL-45ZXIT", "C511152", "extended, Infineon; 42 in stock 2026-10-18 (thin). Alternate CY62167DV30LL-55ZXIT C2956479 (168), same footprint, R13/R14 DNP"),
    ("RP2350B", "QFN-80-1EP_10x10mm_P0.4mm_EP3.4x3.4mm"): ("RP2350B", "C42415655", "extended"),
    ("GD25Q128ESIGR", "SOIC-8_5.3x5.3mm_P1.27mm"): ("GD25Q128ESIGR", "C2928891", "extended, as v3"),
    ("74LVC245APW", T20): ("74LVC245APW,118", "C6082", "extended, Nexperia, as v3"),
    ("74LVC1G06", "SOT-353_SC-70-5"): ("SN74LVC1G06DCKR", "C7828", "extended, TI, SC-70-5"),
    ("74LVC2G34", "SOT-363_SC-70-6"): ("SN74LVC2G34DCKR", "C14461", "extended, TI, SC-70-6; 21,890 in stock 2026-10-19"),
    ("SN74LVC1G32DBVR", "SOT-23-5"): ("SN74LVC1G32DBVR", "C10096", "extended, TI"),
    ("TLV1117LV33", "SOT-223-3_TabPin2"): ("TLV1117LV33DCYR", "C15578", "extended, TI, as v3"),
    ("TPS3839G33", "SOT-23"): ("TPS3839G33DBZR", "C485802", "extended, TI, 3.08 V, push-pull active low"),
    ("LM66200", "SOT-583-8"): ("LM66200DRLR", "C3235556", "extended, TI"),
    ("AO3401A", "SOT-23"): ("AO3401A", "C15127", "basic"),
    ("AO3400A", "SOT-23"): ("AO3400A", "C20917", "basic"),
    ("SS34", "D_SMA"): ("SS34", "C8678", "basic, MDD"),
    ("3.3uH", "L_Abracon_AOTA-B201610"): ("AOTA-B201610S3R3-101-T", "C42411119", "extended, Abracon polarised inductor from the RP2350 design guide; dot = pin 2 = DVDD side. 3 at JLCPCB on 2026-10-18: pre-order, Global Sourcing, or DNP and hand-fit (spec, Parts)"),
    ("12MHz", "Crystal_SMD_3225-4Pin_3.2x2.5mm"): ("ABM8-272-T3", "C20625731", "extended, Abracon, CL 10 pF, as v3"),
    # ---- electromechanical ---------------------------------------------------
    ("USB-C", "USB_C_Receptacle_HRO_TYPE-C-31-M-12"): ("TYPE-C-31-M-12", "C165948", "extended, HRO, as v3"),
    ("RUN", "SW_Push_1P1T_XKB_TS-1187A"): ("TS-1187A-B-A-B", "C318884", "basic, XKB"),
    ("BOOTSEL", "SW_Push_1P1T_XKB_TS-1187A"): ("TS-1187A-B-A-B", "C318884", "basic, XKB"),
    ("WP", "SW_SPDT_Shouhan_MSK12C02"): ("MSK12C02", "C431540", "extended, SHOU HAN slide switch"),
    ("CR2032", "BatteryHolder_Keystone_3034_1x20mm"): ("CR2032-BS-6-1", "C70377", "extended, Q&J SMD holder; footprint is Keystone 3034's, to be checked against the Q&J drawing before layout"),
}


def lookup(value, footprint):
    """Return (MPN, LCSC, note) or None."""
    fp = footprint.split(":")[-1] if footprint else ""
    return PARTS.get((value, fp))
