# МС 1702 — KiCad reconstruction

`mc1702.kicad_sch` is a KiCad 8 schematic of the «Электроника МС 1702» IBM PC
coprocessor board, **generated** from the reverse-engineered netlist
[`docs/kb/mc1702-netlist.csv`](../../docs/kb/mc1702-netlist.csv) by
[`tools/mc1702_kicad.py`](../../tools/mc1702_kicad.py):

```
python tools/mc1702_kicad.py          # regenerates hw/mc1702/mc1702.kicad_sch
```

Do not edit the `.kicad_sch` by hand — fix the netlist (or the generator) and
regenerate. The netlist is the single source of truth; how it was read off the
factory drawing, block by block, is in
[`docs/kb/mc1702-schematic.md`](../../docs/kb/mc1702-schematic.md). The
unlabelled point-to-point wiring is read off the scan by
[`tools/mc1702_trace.py`](../../tools/mc1702_trace.py) (needs `opencv-python`
and `numpy`; the scans themselves stay outside the repository).

## What the schematic is

- Every part of the element list (ПЭ3): the 8086/8087 pair, the bus
  controller, latches and transceivers, decode PROM, program EPROM, video RAM,
  all 26 DRAM chips, the Hamming СОКОО pair, the DRAM controller (microcode
  PROM D44, register D54, address multiplexers, sequencer), the bus interface
  (XS1, buffers, command register/decoder, status register, test ROM with its
  counter chain), plus resistors, capacitors, the crystal and the jumpers.
- **Layout follows the board**: the columns of the sheet are the columns of
  ICs on the assembly drawing (МС1702-СБ л.1), left to right, parts top to
  bottom as mounted, resistors next to the IC they serve — the DRAM array on
  the left, the CPU in the middle, the interface and XS1 on the right.
- **Wiring as on the original**: bus-type nets (local CPU bus `LB.`, latched
  address `LA.`, memory data `MD.`, DRAM address/controls `MA.`, interface
  data `IB.`, check bits `CK.`, the connector data lines, +5В/0В) are global
  labels at the pins, like the numbered bus boxes of the factory drawing; every
  other net is drawn as real wire — a vertical track in the channel left of
  each column, horizontal stubs to the pins, a horizontal "highway" above the
  parts for nets that span columns, junction dots at T-joints and the net
  name at the top of each track.
- Each part is a box symbol with the **real pin numbers and names**; pins
  whose wire has not been followed to its label on the scan carry a trailing
  `?` in the name and a placeholder net (`G…`) — the open items of the read-out.
- The page is a custom size (about 1.45 × 0.96 m) so that nothing overlaps;
  zoom in KiCad or in the exported PDF.

## Verification

Checked with KiCad 10.0.5 (`kicad-cli`): the file loads, ERC reports **no
errors**, and the netlist KiCad derives from the drawing is identical to the
CSV — every pin set the same (176 parts, 1307 pins, 330 nets at the time of writing). Remaining ERC warnings are
expected: `lib_symbol_issues` (the symbols are embedded, there is no `MC1702`
library on disk) and `isolated_pin_label` for nets that so far have only one
recorded end (placeholders and the latch/transceiver/counter sides still to
trace — listed in the checklist of `mc1702-schematic.md`).

The generator emits sequence-based UUIDs, so regenerating an unchanged netlist
gives a byte-identical file. `mc1702.kicad_pro` is a minimal project file so
the schematic opens from the KiCad project manager; KiCad's per-user state
(`*.kicad_prl`, `*-backups/`) is git-ignored.

To open: KiCad → *File → Open Project…* → `hw/mc1702/mc1702.kicad_pro`, then
double-click the schematic; or run the Schematic Editor standalone and open
`mc1702.kicad_sch` directly. Command-line checks:

```
kicad-cli sch erc --severity-all -o erc.rpt hw/mc1702/mc1702.kicad_sch
kicad-cli sch export netlist --format kicadxml -o mc1702.xml hw/mc1702/mc1702.kicad_sch
kicad-cli sch export svg -o out/ hw/mc1702/mc1702.kicad_sch
```
