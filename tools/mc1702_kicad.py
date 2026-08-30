#!/usr/bin/env python3
"""Generate a KiCad 8 schematic of the MC 1702 coprocessor board from the
reverse-engineered netlist docs/kb/mc1702/netlist.csv.

The netlist is the single source of truth (see docs/kb/mc1702-schematic.md);
this script turns it into hw/mc1702/mc1702.kicad_sch: every component is a
box symbol carrying the real pin numbers and names, and every pin is tied to
a global label named after its net - so the drawing is electrically the
netlist, laid out as a grid of blocks to be tidied by hand in KiCad later.
Pins whose net is not traced yet get a "?<ref>.<pin>" label; the netlist's
'?' notes are carried into the pin name so the open items stay visible.

Usage: python tools/mc1702_kicad.py [netlist.csv] [out.kicad_sch]
"""
import csv
import sys
import uuid
from collections import OrderedDict, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NETLIST = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "docs" / "kb" / "mc1702" / "netlist.csv"
OUT = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "hw" / "mc1702" / "mc1702.kicad_sch"

# Component types from the element list (docs/refs/MC1702-TO.md, ПЭ3), the
# schematic symbols winning where the ПЭ3 transcription conflicts.
TYPES = {
    "D1": "К555ИЕ19", "D2": "К555ИЕ19", "D3": "К155ЛН1", "D4": "КР1810ГФ84",
    "D5": "К573РФ5", "D6": "КР559ИП13", "D7": "КР559ИП13", "D8": "К155ТМ5",
    "D9": "К555АП3", "D10": "КМ1810ВМ86", "D11": "КМ1810ВМ87",
    "D12": "КР559ИП13", "D13": "КР559ИП13", "D14": "КР580ИР82", "D15": "К555ИД7",
    "D16": "К155ЛА3", "D17": "К155ЛА13", "D18": "КР1810ВГ88",
    "D19": "КР580ИР82", "D20": "КР580ИР82", "D21": "КР580ИР82",
    "D22": "КР580ИР82", "D23": "КР580ИР82", "D24": "К155ЛЛ1", "D25": "К155ЛИ1",
    "D26": "КР580ВА86", "D27": "КР580ВА86", "D28": "К155ЛА4", "D29": "К155АГ3",
    "D30": "К155ТМ2", "D31": "К155ЛА3", "D32": "К155ЛЛ1",
    "D33": "SRAM 8Kx8", "D34": "SRAM 8Kx8", "D35": "К155ТМ2", "D36": "К555ИР23",
    "D37": "К573РФ4А", "D38": "К573РФ4А", "D39": "К155ТМ2", "D40": "К155ЛИ1",
    "D41": "К155ЛЛ1", "D42": "К155ЛЛ1", "D43": "К155ТМ2", "D44": "КР556РТ5",
    "D45": "К555ИР22", "D46": "К555ИР22", "D47": "КР556РТ4А", "D48": "К155ТМ2",
    "D53": "К155ТМ2", "D54": "К555ИР23", "D55": "К155ЛИ1",
    "D78": "К555ВЖ1", "D79": "К555ВЖ1", "D80": "К155ЛН1", "D81": "К155ЛА2",
    "XS1": "ОНп-КС-66-60/87x12-Р51",
}
DRAM = [f"D{n}" for n in (49, 50, 51, 52, *range(56, 78))]
for r in DRAM:
    TYPES[r] = "КР565РУ7"
# Passive values from the ПЭ3 (docs/refs/MC1702-TO.md)
TYPES.update({'R1': '510', 'R2': '510', 'R9': '20k', 'R13': '20k', 'C2': '1000p', 'C3': '470u', 'C4': '470u', 'BQ1': 'РК169МА-6АН-15М7 15 MHz', 'XS1': 'ОНп-КС-66-60/87x12-Р51', 'S1': 'jumper', 'S2': 'jumper', 'S3': 'jumper', 'S4': 'jumper', 'S5': 'jumper', 'R3': '1k', 'R4': '1k', 'R5': '1k', 'R6': '1k', 'R7': '1k', 'R8': '1k', 'R10': '1k', 'R11': '1k', 'R12': '1k', 'R14': '1k', 'R15': '1k', 'R16': '1k', 'R17': '330', 'R18': '330', 'R19': '330', 'R20': '330', 'R21': '330', 'R22': '330', 'R23': '330', 'R24': '330', 'R25': '330', 'R26': '330', 'R27': '330', 'R28': '330', 'R29': '330', 'R30': '330', 'R31': '330', 'R32': '1k', 'C1': '0.047u', 'C5': '0.047u', 'C6': '0.047u', 'C7': '0.047u', 'C8': '0.047u', 'C9': '0.047u', 'C10': '0.047u', 'C11': '0.047u', 'C12': '0.047u', 'C13': '0.047u', 'C14': '0.047u', 'C15': '0.047u', 'C16': '0.047u', 'C17': '0.047u', 'C18': '0.047u', 'C19': '0.047u', 'C20': '0.047u', 'C21': '0.047u', 'C22': '0.047u', 'C23': '0.047u', 'C24': '0.047u', 'C25': '0.047u', 'C26': '0.047u', 'C27': '0.047u', 'C28': '0.047u', 'C29': '0.047u', 'C30': '0.047u', 'C31': '0.047u', 'C32': '0.047u', 'C33': '0.047u', 'C34': '0.047u', 'C35': '0.047u', 'C36': '0.047u', 'C37': '0.047u', 'C38': '0.047u', 'C39': '0.047u', 'C40': '0.047u', 'C41': '0.047u', 'C42': '0.047u', 'C43': '0.047u', 'C44': '0.047u', 'C45': '0.047u', 'C46': '0.047u', 'C47': '0.047u', 'C48': '0.047u', 'C49': '0.047u', 'C50': '0.047u', 'C51': '0.047u', 'C52': '0.047u', 'C53': '0.047u', 'C54': '0.047u', 'C55': '0.047u', 'C56': '0.047u', 'C57': '0.047u', 'C58': '0.047u', 'C59': '0.047u', 'C60': '0.047u', 'C61': '0.047u', 'C62': '0.047u', 'C63': '0.047u', 'C64': '0.047u', 'C65': '0.047u', 'C66': '0.047u'})

DRAM_COMMON = [("5", "A0", "MA.1"), ("7", "A1", "MA.2"), ("6", "A2", "MA.3"), ("12", "A3", "MA.4"),
               ("11", "A4", "MA.5"), ("10", "A5", "MA.6"), ("13", "A6", "MA.7"), ("9", "A7", "MA.8"),
               ("1", "A8", "MA.9"), ("3", "WE", "MA.10"), ("4", "RAS", "MA.11"), ("15", "CAS", "MA.12")]
# CAS is split by byte: row 1 (D0-D7) + check bank A -> MA.12 (drawn 12/13); row 2 (D8-D15) + check bank B -> MA.13 (13/13)
DRAM_CAS_B = {"D50", "D52", "D57", "D59", "D63", "D67", "D71", "D75", "D61", "D65", "D69", "D73", "D77"}


def load(netlist):
    """-> {ref: OrderedDict{pin: (name, net, note)}} in netlist order."""
    comps = OrderedDict()
    with open(netlist, encoding="utf-8") as f:
        for row in csv.reader(l for l in f if l.strip() and not l.startswith("#")):
            if row[0] == "net":
                continue
            net, ref, pin, name, note = (row + [""] * 5)[:5]
            pins = comps.setdefault(ref, OrderedDict())
            if pin in pins and pins[pin][1] != net:
                note = (note + " | also " + pins[pin][1]).strip(" |")
            pins[pin] = (name, net, note)
    for ref in DRAM:
        pins = comps.setdefault(ref, OrderedDict())
        for pin, name, net in DRAM_COMMON:
            if pin == "15" and ref in DRAM_CAS_B:
                net = "MA.13"   # CAS of the high-byte data bank and check bank B (drawn 13/13)
            pins.setdefault(pin, (name, net, ""))
    return comps


_UID_NS = uuid.UUID("6d7b0f2e-1702-4c00-8000-000000000000")
_UID_SEQ = [0]


def uid():
    """Sequence-based UUIDs: regenerating an unchanged netlist yields an identical file."""
    _UID_SEQ[0] += 1
    return str(uuid.uuid5(_UID_NS, str(_UID_SEQ[0])))


def esc(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


PITCH = 2.54
BOX_W = 25.4


def symbol_def(ref, pins):
    """A box symbol with all pins on the left, one per row."""
    n = len(pins)
    h = max(2, n + 1) * PITCH
    body = [f'    (symbol "MC1702:{ref}" (pin_names (offset 1.016)) (exclude_from_sim no) (in_bom yes) (on_board yes)',
            f'      (property "Reference" "{ref}" (at 0 {h/2 + 1.27:.2f} 0) (effects (font (size 1.27 1.27))))',
            f'      (property "Value" "{esc(TYPES.get(ref, "?"))}" (at 0 {-h/2 - 1.27:.2f} 0) (effects (font (size 1.27 1.27))))',
            f'      (property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))',
            f'      (property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))',
            f'      (symbol "{ref}_0_1"',
            f'        (rectangle (start 0 {h/2:.2f}) (end {BOX_W} {-h/2:.2f}) (stroke (width 0.254) (type default)) (fill (type background)))',
            f'      )',
            f'      (symbol "{ref}_1_1"']
    for i, (pin, (name, net, note)) in enumerate(pins.items()):
        y = h / 2 - (i + 1) * PITCH
        pname = name or "~"
        if note.startswith("?") or "?" in note:
            pname += "?"
        body.append(f'        (pin passive line (at -2.54 {y:.2f} 0) (length 2.54) (name "{esc(pname)}" (effects (font (size 1.0 1.0)))) (number "{esc(pin)}" (effects (font (size 1.0 1.0)))))')
    body += ['      )', '    )']
    return "\n".join(body)


def instance(ref, pins, x, y):
    """Symbol instance at (x, y) = box left edge / vertical centre; returns (text, {pin: (tip_x, pin_y)}, height)."""
    n = len(pins)
    h = max(2, n + 1) * PITCH
    out = [f'  (symbol (lib_id "MC1702:{ref}") (at {x:.2f} {y:.2f} 0) (unit 1) (exclude_from_sim no) (in_bom yes) (on_board yes) (dnp no) (uuid "{uid()}")',
           f'    (property "Reference" "{ref}" (at {x + 2.54:.2f} {y - h/2 - 1.27:.2f} 0) (effects (font (size 1.27 1.27)) (justify left)))',
           f'    (property "Value" "{esc(TYPES.get(ref, "?"))}" (at {x + 2.54:.2f} {y + h/2 + 1.27:.2f} 0) (effects (font (size 1.27 1.27)) (justify left)))',
           f'    (property "Footprint" "" (at {x:.2f} {y:.2f} 0) (effects (font (size 1.27 1.27)) (hide yes)))',
           f'    (property "Datasheet" "" (at {x:.2f} {y:.2f} 0) (effects (font (size 1.27 1.27)) (hide yes)))']
    for pin in pins:
        out.append(f'    (pin "{esc(pin)}" (uuid "{uid()}"))')
    out.append(f'    (instances (project "mc1702" (path "/{ROOT_UUID}" (reference "{ref}") (unit 1))))')
    out.append('  )')
    pos = {}
    for i, pin in enumerate(pins):
        pos[pin] = (x - 2.54, y - (h / 2 - (i + 1) * PITCH))   # pin tip, sheet y grows downward
    return "\n".join(out), pos, h


ROOT_UUID = uid()

POWER_SYMBOLS = """    (symbol "MC1702:+5V" (power) (pin_names (offset 0)) (exclude_from_sim no) (in_bom yes) (on_board yes)
      (property "Reference" "#PWR" (at 0 -3.81 0) (effects (font (size 1.27 1.27)) (hide yes)))
      (property "Value" "+5V" (at 0 3.81 0) (effects (font (size 1.27 1.27))))
      (property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
      (property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
      (symbol "+5V_0_1"
        (polyline (pts (xy -0.762 1.27) (xy 0 2.54)) (stroke (width 0) (type default)) (fill (type none)))
        (polyline (pts (xy 0 0) (xy 0 2.54)) (stroke (width 0) (type default)) (fill (type none)))
        (polyline (pts (xy 0 2.54) (xy 0.762 1.27)) (stroke (width 0) (type default)) (fill (type none)))
      )
      (symbol "+5V_1_1"
        (pin power_in line (at 0 0 90) (length 0) (name "+5V" (effects (font (size 1.27 1.27)))) (number "1" (effects (font (size 1.27 1.27)))))
      )
    )
    (symbol "MC1702:GND" (power) (pin_names (offset 0)) (exclude_from_sim no) (in_bom yes) (on_board yes)
      (property "Reference" "#PWR" (at 0 -6.35 0) (effects (font (size 1.27 1.27)) (hide yes)))
      (property "Value" "GND" (at 0 -3.81 0) (effects (font (size 1.27 1.27))))
      (property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
      (property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
      (symbol "GND_0_1"
        (polyline (pts (xy 0 0) (xy 0 -1.27) (xy 1.27 -1.27) (xy 0 -2.54) (xy -1.27 -1.27) (xy 0 -1.27)) (stroke (width 0) (type default)) (fill (type none)))
      )
      (symbol "GND_1_1"
        (pin power_in line (at 0 0 270) (length 0) (name "GND" (effects (font (size 1.27 1.27)))) (number "1" (effects (font (size 1.27 1.27)))))
      )
    )
    (symbol "MC1702:PWR_FLAG" (power) (pin_names (offset 0)) (exclude_from_sim no) (in_bom yes) (on_board yes)
      (property "Reference" "#FLG" (at 0 1.905 0) (effects (font (size 1.27 1.27)) (hide yes)))
      (property "Value" "PWR_FLAG" (at 0 3.81 0) (effects (font (size 1.0 1.0))))
      (property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
      (property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
      (symbol "PWR_FLAG_0_0"
        (pin power_out line (at 0 0 90) (length 0) (name "pwr" (effects (font (size 1.27 1.27)))) (number "1" (effects (font (size 1.27 1.27)))))
      )
      (symbol "PWR_FLAG_0_1"
        (polyline (pts (xy 0 0) (xy 0 1.27) (xy -1.016 1.905) (xy 0 2.54) (xy 1.016 1.905) (xy 0 1.27)) (stroke (width 0) (type default)) (fill (type none)))
      )
    )"""
_PWR_SEQ = [0]


def power_symbol(net, x, y, lib=None):
    """A +5V / GND power symbol (or a PWR_FLAG) whose pin sits at (x, y)."""
    _PWR_SEQ[0] += 1
    ref = ("#FLG%04d" if lib == "PWR_FLAG" else "#PWR%04d") % _PWR_SEQ[0]
    vy = y - 3.81 if (net == "+5V" or lib == "PWR_FLAG") else y + 3.81
    return "\n".join([
        f'  (symbol (lib_id "MC1702:{lib or net}") (at {x:.2f} {y:.2f} 0) (unit 1) (exclude_from_sim no) (in_bom yes) (on_board yes) (dnp no) (uuid "{uid()}")',
        f'    (property "Reference" "{ref}" (at {x:.2f} {y:.2f} 0) (effects (font (size 1.27 1.27)) (hide yes)))',
        f'    (property "Value" "{lib or net}" (at {x:.2f} {vy:.2f} 0) (effects (font (size 1.0 1.0))))',
        f'    (property "Footprint" "" (at {x:.2f} {y:.2f} 0) (effects (font (size 1.27 1.27)) (hide yes)))',
        f'    (property "Datasheet" "" (at {x:.2f} {y:.2f} 0) (effects (font (size 1.27 1.27)) (hide yes)))',
        f'    (pin "1" (uuid "{uid()}"))',
        f'    (instances (project "mc1702" (path "/{ROOT_UUID}" (reference "{ref}") (unit 1))))',
        '  )'])


# Columns of the schematic = columns of the board (assembly drawing МС1702-СБ л.1,
# read left to right, parts top to bottom); resistors/capacitors sit with the IC
# they serve.  Parts not listed go into a trailing column.
BLOCKS = [
    ("board col A", ["D69", "D73", "D50", "D75", "D67", "D66", "D56", "D64", "D60", "D54", "D44", "R14", "R20", "R21", "R22", "R23", "R24", "R25", "R26", "R27"]),
    ("board col B", ["D65", "D77", "D57", "D71", "D70", "D62", "D51", "D72", "D68", "C3"]),
    ("board col C", ["D61", "D52", "D59", "D63", "D74", "D58", "D49", "D76", "D46", "D45", "R15"]),
    ("board col D", ["D79", "D38", "D33", "D37", "D78"]),
    ("board col E", ["D26", "D19", "D27", "D20", "D21", "D22", "D14", "D12", "D6", "D5", "R1"]),
    ("board col F", ["D42", "D3", "D10", "D23", "D13", "D7", "D2", "D1", "R17", "R18", "R19"]),
    ("board col G", ["D4", "BQ1", "S1", "S2", "S3", "C1", "R2", "D40", "D18", "R6", "D24", "D36", "D29", "R12", "R13", "C2", "S4", "S5"]),
    ("board col H", ["D43", "D48", "R32", "D80", "D47", "R28", "R29", "R30", "R31", "D41", "D35", "D28"]),
    ("board col I", ["D53", "D39", "D81", "D55", "D32", "D25", "D17", "R11"]),
    ("board col J", ["D31", "D30", "D16", "D15", "D8", "D9", "XS1", "C4"]),
    ("decoupling", ["C%d" % n for n in range(5, 67)]),
]
# Nets drawn as labels (bus boxes and power on the original), not as wires
LABEL_PREFIXES = ("LB.", "LA.", "MD.", "MA.", "IB.", "CK.", "XS1_", "+5V", "GND", "NC")
MAX_COL_H = 700.0        # mm of symbols per column before it is split
TOP = 16 * 1.27          # top margin for the highway (on the 1.27 mm grid)


def is_label_net(net, npins):
    return (not net) or net.startswith(LABEL_PREFIXES) or net.startswith(("?", "G_", "G")) and npins < 2 or npins < 2


def layout(comps):
    """-> list of columns; each column = list of refs (block order, split by height)."""
    placed = set()
    cols = []
    for name, refs in BLOCKS:
        col, hcol = [], 0.0
        for ref in refs:
            if ref not in comps or ref in placed:
                continue
            h = max(2, len(comps[ref]) + 1) * PITCH + 6 * PITCH
            if col and hcol + h > MAX_COL_H:
                cols.append(col)
                col, hcol = [], 0.0
            col.append(ref)
            hcol += h
            placed.add(ref)
        if col:
            cols.append(col)
    rest = [r for r in sorted(comps, key=lambda r: (r[0], int("".join(ch for ch in r[1:] if ch.isdigit()) or 0), r)) if r not in placed]
    for i in range(0, len(rest), 12):
        cols.append(rest[i:i + 12])
    return cols


def route(comps, cols):
    """Place the columns, wire the non-bus nets through channels + a top highway, label the rest."""
    # net -> [(ref, pin)] and pin counts
    netpins = defaultdict(list)
    for ref, pins in comps.items():
        for pin, (name, net, note) in pins.items():
            netpins[net or f"?{ref}.{pin}"].append((ref, pin))
    wired = {n for n, pl in netpins.items() if not is_label_net(n, len(pl))}
    # which wired nets touch which column
    col_of = {ref: ci for ci, col in enumerate(cols) for ref in col}
    net_cols = {n: sorted({col_of[r] for r, p in netpins[n]}) for n in wired}
    tracks = [sorted([n for n in wired if ci in net_cols[n]], key=lambda n: min(1 for _ in [0]) and n) for ci in range(len(cols))]
    multi = sorted([n for n in wired if len(net_cols[n]) > 1], key=lambda n: (net_cols[n][0], net_cols[n][-1], n))
    hw_y = {n: TOP + (i + 1) * PITCH for i, n in enumerate(multi)}
    body_top = TOP + (len(multi) + 4) * PITCH
    body_top = round(body_top / 1.27) * 1.27
    # geometry of columns: channel width from the number of tracks
    inst, wires, pinpos = [], [], {}
    x = 4 * PITCH
    track_x = {}     # (net, ci) -> x
    col_x = []
    for ci, col in enumerate(cols):
        ch_w = (len(tracks[ci]) + 2) * PITCH
        for ti, n in enumerate(tracks[ci]):
            track_x[(n, ci)] = x + (ti + 1) * PITCH
        bx = x + ch_w + 2 * PITCH
        col_x.append(bx)
        y = body_top
        for ref in col:
            pins = comps[ref]
            h = max(2, len(pins) + 1) * PITCH
            s, pos, _ = instance(ref, pins, bx, y + h / 2)
            inst.append(s)
            pinpos[ref] = pos
            y += h + 6 * PITCH
        x = bx + BOX_W + 8 * PITCH
    width, height = x + 4 * PITCH, 0.0
    for ci, col in enumerate(cols):
        h = body_top + sum(max(2, len(comps[r]) + 1) * PITCH + 6 * PITCH for r in col)
        height = max(height, h)
    # labels for bus / power / open nets
    flagged = set()
    for ref, pins in comps.items():
        for pin, (name, net, note) in pins.items():
            n = net or f"?{ref}.{pin}"
            if n in wired:
                continue
            tx, py = pinpos[ref][pin]
            px = tx - 2 * PITCH
            wires.append(f'  (wire (pts (xy {px:.2f} {py:.2f}) (xy {tx:.2f} {py:.2f})) (stroke (width 0) (type default)) (uuid "{uid()}"))')
            if n in ("+5V", "GND"):
                inst.append(power_symbol(n, px, py))
                if n not in flagged:      # one PWR_FLAG per power net, on the stub next to the first symbol
                    flagged.add(n)
                    wires.pop()           # re-issue the stub in two pieces meeting at the flag
                    wires.append(f'  (wire (pts (xy {px:.2f} {py:.2f}) (xy {px + PITCH:.2f} {py:.2f})) (stroke (width 0) (type default)) (uuid "{uid()}"))')
                    wires.append(f'  (wire (pts (xy {px + PITCH:.2f} {py:.2f}) (xy {tx:.2f} {py:.2f})) (stroke (width 0) (type default)) (uuid "{uid()}"))')
                    inst.append(power_symbol(n, px + PITCH, py, lib="PWR_FLAG"))
            else:
                wires.append(f'  (global_label "{esc(n)}" (shape passive) (at {px:.2f} {py:.2f} 180) (fields_autoplaced yes) (effects (font (size 1.0 1.0)) (justify right)) (uuid "{uid()}"))')
    # wires for the rest
    def W(x1, y1, x2, y2):
        wires.append(f'  (wire (pts (xy {x1:.2f} {y1:.2f}) (xy {x2:.2f} {y2:.2f})) (stroke (width 0) (type default)) (uuid "{uid()}"))')

    def J(x1, y1):
        wires.append(f'  (junction (at {x1:.2f} {y1:.2f}) (diameter 0) (color 0 0 0 0) (uuid "{uid()}"))')

    for n in wired:
        trunk_x = []
        for ci in net_cols[n]:
            tx_ = track_x[(n, ci)]
            ys = [pinpos[r][p][1] for r, p in netpins[n] if col_of[r] == ci]
            top = hw_y[n] if n in multi else min(ys)
            bottom = max(ys)
            if top != bottom:
                W(tx_, top, tx_, bottom)
            for r, p in netpins[n]:
                if col_of[r] != ci:
                    continue
                tipx, py = pinpos[r][p]
                W(tx_, py, tipx, py)
                if top < py < bottom or (py == top and n in multi and py != bottom):
                    J(tx_, py)
            if n in multi:
                trunk_x.append(tx_)
            else:
                wires.append(f'  (label "{esc(n)}" (at {tx_:.2f} {top:.2f} 0) (fields_autoplaced yes) (effects (font (size 1.0 1.0)) (justify left bottom)) (uuid "{uid()}"))')
        if n in multi:
            W(min(trunk_x), hw_y[n], max(trunk_x), hw_y[n])
            for tx_ in trunk_x:
                if min(trunk_x) < tx_ < max(trunk_x):
                    J(tx_, hw_y[n])
            wires.append(f'  (label "{esc(n)}" (at {min(trunk_x):.2f} {hw_y[n]:.2f} 0) (fields_autoplaced yes) (effects (font (size 1.0 1.0)) (justify left bottom)) (uuid "{uid()}"))')
    return inst, wires, width, height, len(wired), len(multi)


def main():
    comps = load(NETLIST)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    order = sorted(comps, key=lambda r: (r[0] != "D", int("".join(ch for ch in r[1:] if ch.isdigit()) or 0), r))
    sym_defs = "\n".join(symbol_def(r, comps[r]) for r in order)
    cols = layout(comps)
    inst, wires, width, height, nwired, nmulti = route(comps, cols)
    nets = defaultdict(int)
    open_pins = 0
    for pins in comps.values():
        for _, net, note in pins.values():
            nets[net or "?"] += 1
            open_pins += ("?" in note) or not net
    pw, ph = round(width + 20), round(height + 40)
    doc = [f'(kicad_sch (version 20231120) (generator "ms0515_mc1702") (generator_version "8.0")',
           f'  (uuid "{ROOT_UUID}")',
           f'  (paper "User" {pw} {ph})',
           f'  (title_block (title "Электроника МС 1702 - reconstruction from 3.098.002 Э3") (company "ms0515 project") (comment 1 "generated from docs/kb/mc1702/netlist.csv - do not edit by hand"))',
           '  (lib_symbols', sym_defs, POWER_SYMBOLS, '  )',
           "\n".join(inst), "\n".join(wires),
           f'  (sheet_instances (path "/" (page "1")))', ')']
    OUT.write_text("\n".join(doc), encoding="utf-8")
    pro = OUT.with_suffix(".kicad_pro")
    if not pro.exists():   # a minimal project file so the schematic opens from the KiCad manager
        pro.write_text('{\n  "meta": {"filename": "%s", "version": 1},\n  "sheets": [],\n  "text_variables": {}\n}\n' % pro.name, encoding="utf-8")
    print(f"{OUT}: {len(comps)} components, {sum(len(p) for p in comps.values())} pins, "
          f"{len(nets)} nets ({nwired} wired, {nmulti} across columns), {len(cols)} columns, page {pw}x{ph} mm, "
          f"{open_pins} pins still flagged '?' in the netlist")


def h_half(pins):
    return max(2, len(pins) + 1) * PITCH / 2


def parse_sexpr(text):
    """Minimal S-expression reader (strings, atoms, lists) for the self-check."""
    stack, atom, i, n = [[]], [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            stack[-1].append(text[i:j + 1])
            i = j + 1
        elif c == "(":
            stack.append([])
            i += 1
        elif c == ")":
            done = stack.pop()
            stack[-1].append(done)
            i += 1
        elif c.isspace():
            i += 1
        else:
            j = i
            while j < n and not text[j].isspace() and text[j] not in '()"':
                j += 1
            stack[-1].append(text[i:j])
            i = j
    return stack[0][0]


def check(path):
    """Structural self-check: every placed symbol's lib_id exists and its pin
    set equals the library symbol's; the file parses as one S-expression."""
    doc = parse_sexpr(Path(path).read_text(encoding="utf-8"))
    lib = {}
    for node in doc:
        if isinstance(node, list) and node and node[0] == "lib_symbols":
            for sym in node[1:]:
                pins = set()
                for unit in sym:
                    if isinstance(unit, list) and unit and unit[0] == "symbol":
                        for p in unit:
                            if isinstance(p, list) and p and p[0] == "pin":
                                pins.add([q for q in p if isinstance(q, list) and q[0] == "number"][0][1])
                lib[sym[1]] = pins
    placed = [n for n in doc if isinstance(n, list) and n and n[0] == "symbol"]
    labels = sum(1 for n in doc if isinstance(n, list) and n and n[0] == "global_label")
    bad = 0
    for s in placed:
        lib_id = [q for q in s if isinstance(q, list) and q[0] == "lib_id"][0][1]
        pins = {q[1] for q in s if isinstance(q, list) and q[0] == "pin"}
        if pins != lib.get(lib_id):
            bad += 1
            print("MISMATCH", lib_id, pins ^ lib.get(lib_id, set()))
    print(f"check: {len(lib)} library symbols, {len(placed)} placed, {labels} labels, {bad} mismatches")
    return bad == 0


if __name__ == "__main__":
    if "--check" in sys.argv:
        sys.exit(0 if check(OUT) else 1)
    main()
