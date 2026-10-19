#!/usr/bin/env python3
"""Build the placed (unrouted) board from the schematic netlist.

Copied from v3 (tspico-hardware, scripts/gen_layout.py) with a new placement table.
Run with KiCad's bundled Python (it has the pcbnew module):
  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 scripts/gen_layout.py

Steps: regenerate the skeleton (outline, edge connector, holes, fiducials) with
gen_board.py, export the netlist with kicad-cli, load every footprint from the
KiCad or project library, assign nets, place parts per P below, add GND (In1.Cu)
and +3V3 (In2.Cu) zones, save megabank.kicad_pcb.

Coordinates: mm, board origin at the top-left corner of the body, y down.
Body 130 x 56 (v3's); tongue below y = 56 between x = 18.48 and 102.14.
Rotation: degrees counter-clockwise as in KiCad.
"""
import os
import re
import subprocess
import sys

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.dirname(HERE)
KICAD_FP = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints"
KICAD_CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"
PCB = os.path.join(HW, "megabank.kicad_pcb")
NET = "/tmp/megabank.net"

# ---------------------------------------------------------------------------
# Placement: ref -> (x, y, rot). Anything not listed is parked off-board.
# ---------------------------------------------------------------------------
P = {}


def row(refs, x0, y, dx, rot=0):
    for i, r in enumerate(refs):
        P[r] = (x0 + i * dx, y, rot)


BODY_H = 56.0
R3Y, R2Y = BODY_H - 5.5, BODY_H - 13.5      # transceiver row (over the fingers), second row

# ---- bus interface, as v3 -----------------------------------------------------------
# Row 3, nearest the fingers: data U3 over D0-D7 (fingers x 28-52, top copper; its A side
# is the Z80 side), high address byte U2 directly over its fingers A15-A8 (x 54-72, bottom
# copper), control U4 over /MREQ../RFSH (x 61-85), /BE driver U5 by finger A29 (x 92).
# Row 2: low address byte U1 above U2; its fingers are split (A0-A3 at x 44-52, A7-A4 at
# x 74-82) and come in from both sides on the bottom layer.
P["U3"], P["C3"] = (40, R3Y, 90), (48, R3Y, 90)
P["U2"], P["C2"] = (63, R3Y, 270), (69, R3Y, 90)
P["U4"], P["C4"] = (80, R3Y, 270), (86, R3Y, 90)
P["U5"], P["C5"] = (92, R3Y - 2.5, 0), (95.5, R3Y - 2.5, 90)
P["R1"], P["R3"] = (92, R3Y + 0.5, 0), (96.5, R3Y + 1.5, 90)      # BE_REQ pull-down; /BE pull-up (DNP)
P["U1"], P["C1"] = (63, R2Y, 270), (68.4, R2Y, 90)
P["R2"] = (55.5, 42.7, 0)                                           # nDOE pull-up, where v3 has its enable pull-ups

# ---- RP2350B core: v3's placement, unchanged, so preroute.py carries over -----------
# Rotated so GPIO0-7 (here the SD bus) sit at the bottom-left corner, the control GPIOs
# along the bottom, QSPI/USB/regulator on the left, crystal on the right.
P["U20"] = (62, 26, 90)
# One change from v3: C27 (pin 41) moves up from (67.4, 17.4) to (67.4, 15.2), out of the
# lanes of the top-row fan (preroute.py), which carries PA16-PA20, WINH and nWP to the SRAMs.
P["C22"], P["C20"], P["L1"] = (55.25, 22.8, 90), (54.05, 22.8, 90), (52.25, 22.8, 270)
P["C39"], P["R28"] = (55.1, 21.2, 180), (55.6, 19.2, 270)
P["Y1"], P["R20"], P["C35"], P["C36"] = (75.5, 26.0, 0), (71.7, 25.2, 0), (74.4, 29.3, 270), (76.6, 22.7, 90)
P["C23"], P["C24"] = (51.2, 26.4, 0), (51.2, 29.4, 0)
P["C25"], P["C48"], P["C26"] = (57.6, 37.0, 270), (61.35, 37.0, 270), (66.0, 37.0, 270)
P["C28"], P["C29"], P["C47"] = (69.4, 30.6, 0), (72.6, 33.0, 0), (70.6, 19.8, 0)
P["C27"], P["C30"], P["C21"], P["C31"] = (67.4, 15.2, 0), (64.2, 14.0, 0), (62.3, 18.2, 90), (57.4, 16.4, 0)
P["U21"], P["C37"], P["R21"] = (44, 20, 0), (38, 20, 90), (38, 25, 0)
P["R27"], P["R26"] = (47.6, 23.7, 0), (47.6, 26.3, 0)
P["J1"] = (3.2, 21, 270)                      # USB-C on the left edge, as v3
P["R24"], P["R25"] = (12, 21, 0), (15, 21, 0)
# RUN and BOOTSEL where v3 has them (top edge, 0.5 in centres); v3's TS Reset and NMI are not fitted
P["SW3"], P["SW4"] = (72.4, 4, 0), (85.1, 4, 0)
P["R22"], P["R23"] = (72, 9.5, 0), (78.5, 9.5, 0)
P["TP1"], P["TP2"], P["TP3"] = (40, 30.5, 0), (40, 33.5, 0), (40, 36.5, 0)

# ---- SRAM: two TSOP I 48 on the right, leads top and bottom -------------------------
P["U10"], P["U11"] = (101, 19.5, 90), (116.5, 19.5, 90)
P["C10"], P["C11"] = (101, 32.5, 0), (116.5, 32.5, 0)
P["R13"], P["R14"] = (105, 32.5, 0), (120.5, 32.5, 0)            # BHE / BLE links
P["U12"], P["C12"] = (90, 24, 0), (90, 27.5, 0)                  # /WE gate
P["R10"], P["R11"], P["R12"] = (90, 31, 0), (90, 33.5, 0), (90, 36, 0)
# supervisor driving CE2, beside the SRAMs
P["U31"], P["C52"], P["R50"] = (90, 14, 0), (90, 17.5, 0), (90, 20.5, 0)
# write-protect switch on the right edge
P["SW5"] = (127.1, 41.0, 90)                # actuator tip at x 130.0, flush with the board edge (MSK12C02 drawing: 1.45 mm proud of the body)

# ---- power: inputs and the LDO top left, battery bottom left ------------------------
P["D1"], P["D2"] = (17, 4.5, 0), (17, 9.0, 0)
P["U30"], P["C50"], P["C51"] = (29, 7.5, 0), (22.5, 13.0, 0), (34.5, 13.0, 0)
P["BT1"] = (20.1, 36.7, 90)                 # Q&J holder, tabs left and right: clear of the USB resistors and FID1
# SRAM rail and the battery monitor, in the free strip above the RP2350B
P["U32"], P["C53"], P["C54"] = (42, 5, 0), (45.5, 5, 90), (38.5, 5, 90)
P["Q1"], P["Q2"] = (51, 5, 0), (56, 8.5, 0)
P["R53"], P["R51"], P["R52"], P["C55"], P["R54"] = (51, 8.5, 0), (55, 3.5, 0), (59, 6.5, 90), (61.5, 6.5, 90), (56, 11.5, 0)

# ---------------------------------------------------------------------------


def sexpr(text):
    """Minimal s-expression parser: returns nested lists of str."""
    tokens = re.findall(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()"]+', text)
    stack = [[]]
    for t in tokens:
        if t == '(':
            stack.append([])
        elif t == ')':
            top = stack.pop()
            stack[-1].append(top)
        else:
            stack[-1].append(t[1:-1] if t.startswith('"') else t)
    return stack[0]


def find(node, key):
    return [n for n in node if isinstance(n, list) and n and n[0] == key]


def first(node, key):
    f = find(node, key)
    return f[0] if f else None


def load_netlist(path):
    root = sexpr(open(path).read())[0]
    comps = {}
    for c in find(first(root, "components"), "comp"):
        ref = first(c, "ref")[1]
        dnp = any(len(pr) > 1 and isinstance(pr[1], list) and pr[1][:2] == ["name", "dnp"] for pr in find(c, "property"))
        comps[ref] = {"value": first(c, "value")[1], "footprint": (first(c, "footprint") or ["", ""])[1], "dnp": dnp}
    nets = []
    for n in find(first(root, "nets"), "net"):
        name = first(n, "name")[1]
        nodes = [(first(nd, "ref")[1], first(nd, "pin")[1]) for nd in find(n, "node")]
        nets.append((name, nodes))
    return comps, nets


def load_footprint(fpid):
    lib, name = fpid.split(":")
    path = os.path.join(HW, "lib", lib + ".pretty") if lib == "megabank" else os.path.join(KICAD_FP, lib + ".pretty")
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None:
        raise SystemExit(f"footprint not found: {fpid}")
    return fp


def mm(x, y):
    return pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y))


def main():
    subprocess.run([sys.executable if "KiCad" not in sys.executable else "python3", os.path.join(HERE, "gen_board.py")], check=True)
    subprocess.run([KICAD_CLI, "sch", "export", "netlist", "--format", "kicadsexpr", "--output", NET,
                    os.path.join(HW, "megabank.kicad_sch")], check=True)
    comps, nets = load_netlist(NET)
    board = pcbnew.LoadBoard(PCB)

    # nets
    netinfo = {}
    for name, _ in nets:
        ni = pcbnew.NETINFO_ITEM(board, name)
        board.Add(ni)
        netinfo[name] = ni

    # footprints (P1 already exists in the skeleton)
    fps = {}
    for fp in board.GetFootprints():
        fps[fp.GetReference()] = fp
    park_x = 140.0
    for i, (ref, c) in enumerate(sorted(comps.items())):
        if ref.startswith("#"):
            continue
        if ref in fps:
            continue
        fp = load_footprint(c["footprint"])
        fp.SetReference(ref)
        fp.SetValue(c["value"])
        if c.get("dnp"):
            fp.SetDNP(True)
        if ref in P:
            x, y, rot = P[ref]
        else:
            x, y, rot = park_x, 5 + (i % 40) * 2.5, 0
            print("unplaced (parked):", ref)
        fp.SetPosition(mm(x, y))
        fp.SetOrientationDegrees(rot)
        board.Add(fp)
        fps[ref] = fp

    # pad nets
    pad_index = {}
    for ref, fp in fps.items():
        for pad in fp.Pads():
            pad_index.setdefault((ref, pad.GetNumber()), []).append(pad)
    missing = 0
    for name, nodes in nets:
        for ref, pin in nodes:
            pads = pad_index.get((ref, pin))
            if not pads:
                missing += 1
                print("no pad for", ref, pin)
                continue
            for pad in pads:
                pad.SetNet(netinfo[name])
    print("nets assigned; missing pads:", missing)

    # keepout: no tracks or vias on B.Cu under the RP2350B, as four rectangles
    # around a 4 x 4 mm centre window that leaves the thermal-pad via legal
    # (Freerouting crashes on KiCad's tracks-only "wire_keepout", so this is a full keepout)
    ux, uy = P["U20"][0], P["U20"][1]
    # only the chip body (10 x 10 mm): the 0.4 mm QFN needs vias in its fanout ring
    for name, (x0, y0, x1, y1) in {"L": (-5.3, -5.3, -2.0, 5.3), "R": (2.0, -5.3, 5.3, 5.3),
                                   "T": (-2.0, -5.3, 2.0, -2.0), "B": (-2.0, 2.0, 2.0, 5.3)}.items():
        ko = pcbnew.ZONE(board)
        ko.SetIsRuleArea(True)
        ko.SetDoNotAllowTracks(True)
        ko.SetDoNotAllowVias(True)
        ko.SetLayer(pcbnew.B_Cu)
        ko.SetZoneName("U20_keepout_B_" + name)
        kop = ko.Outline()
        kop.NewOutline()
        for x, y in ((ux + x0, uy + y0), (ux + x1, uy + y0), (ux + x1, uy + y1), (ux + x0, uy + y1)):
            kop.Append(pcbnew.FromMM(x), pcbnew.FromMM(y))
        board.Add(ko)

    # fiducials: the router does not see their 0.6 mm pad clearance, so give each a keepout square
    for fp in board.GetFootprints():
        if not fp.GetReference().startswith("FID"):
            continue
        cx, cy = pcbnew.ToMM(fp.GetPosition().x), pcbnew.ToMM(fp.GetPosition().y)
        ko = pcbnew.ZONE(board)
        ko.SetIsRuleArea(True); ko.SetDoNotAllowTracks(True); ko.SetDoNotAllowVias(True)
        ko.SetDoNotAllowPads(False); ko.SetDoNotAllowFootprints(False); ko.SetDoNotAllowZoneFills(False)
        ko.SetLayer(pcbnew.F_Cu); ko.SetZoneName(fp.GetReference() + "_keepout")
        kop = ko.Outline(); kop.NewOutline()
        for x, y in ((cx - 1.3, cy - 1.3), (cx + 1.3, cy - 1.3), (cx + 1.3, cy + 1.3), (cx - 1.3, cy + 1.3)):
            kop.Append(pcbnew.FromMM(x), pcbnew.FromMM(y))
        board.Add(ko)

    # zones: GND on In1.Cu, +3V3 on In2.Cu, covering the body and the tongue
    body_h = BODY_H
    for layer, netname in ((pcbnew.In1_Cu, "GND"), (pcbnew.In2_Cu, "+3V3")):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(netinfo[netname])
        z.SetZoneName(netname + "_plane")
        outline = z.Outline()
        outline.NewOutline()
        for x, y in ((0.5, 0.5), (129.5, 0.5), (129.5, body_h - 0.5), (101.6, body_h - 0.5), (101.6, body_h + 7.5),
                     (35.2, body_h + 7.5), (35.2, body_h - 0.5), (32.2, body_h - 0.5), (32.2, body_h + 7.5),
                     (19.0, body_h + 7.5), (19.0, body_h - 0.5), (0.5, body_h - 0.5)):
            outline.Append(pcbnew.FromMM(x), pcbnew.FromMM(y))
        z.SetLocalClearance(pcbnew.FromMM(0.25))
        z.SetMinThickness(pcbnew.FromMM(0.25))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        board.Add(z)
    filler = pcbnew.ZONE_FILLER(board)
    filler.Fill(board.Zones())

    pcbnew.SaveBoard(PCB, board)
    print("saved", PCB, "footprints:", len(board.GetFootprints()), "nets:", board.GetNetCount())


if __name__ == "__main__":
    main()
