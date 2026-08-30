# MC 1702 — schematic read-out (Э3, netlist reconstruction)

**Work in progress.** A component-by-component transcription of the factory
schematic «МС1702-Э3» (3 sheets, `3.098.002 Э3`) into a structured netlist, as
the basis for a KiCad reconstruction of the real board. Source scans are the
external `scans/MC1702-SH-png/` (kept out of git); component types come from the
element list [`../refs/MC1702-TO.md`](../refs/MC1702-TO.md) §ПЭ3; the register
behaviour is [`mc1702.md`](mc1702.md).

## Method

Each chip's **type is known from ПЭ3**, so every pin read off the scan is
cross-checked against that part's standard pinout — this corrects scan/OCR
ambiguity (e.g. a smudged `IOWC 11` that could look like `14` is fixed by the
КР1810ВГ88 datasheet). A pin is only recorded once it agrees with the datasheet
or is explicitly flagged as uncertain.

**The scans are strips of one drawing.** «Э3 л.1 / л.2 / л.3» are three
*overlapping* scans of a single large Э3 sheet (the same D18, D19, D20, D25,
D27 appear on both л.1 and л.2 at the overlap) — not three independent sheets.
So a `/N` suffix on a net is **not** a sheet number. It fits **the number of
pins on the net**: `1/27` on the multiplexed DRAM address = 26 КР565РУ7 A0
inputs + the D45 output; `8/3` on AD01 = D10 + D11 + D19; `22/6` on data D0 =
transceiver + PROM + video RAM + DRAM + СОКОО + buffer. (The `/2` on nets 23–27
is the one anomaly — noted.) Net numbers are **local to a bus group** (the
local CPU bus, the memory data bus and the СОКОО check bits all reuse 18–27),
so the machine-readable netlist [`mc1702-netlist.csv`](mc1702-netlist.csv)
prefixes them: `LB.`, `LA.`, `MD.`, `MA.`, `CK.`, `SEL.`.

### The local bus (nets 1–27) — shared by D10 (CPU) and D11 (FPU)

The 8086 and 8087 sit on one multiplexed Address/Data/Status/Control bus:

| Net | Signal | Net | Signal |
|-----|--------|-----|--------|
| 1 | S0 | 15 | AD08 |
| 2 | S1 | 16 | AD09 |
| 3 | S2 | 17 | AD10 |
| 4 | LOCK | 18 | AD11 |
| 5 | QS0 | 19 | AD12 |
| 6 | QS1 | 20 | AD13 |
| 7 | AD00 | 21 | AD14 |
| 8 | AD01 | 22 | AD15 |
| 9 | AD02 | 23 | A16/S3 |
| 10 | AD03 | 24 | A17/S4 |
| 11 | AD04 | 25 | A18/S5 |
| 12 | AD05 | 26 | A19/S6 |
| 13 | AD06 | 27 | BHE/S7 |
| 14 | AD07 | | |

Nets 7–27 carry a `/3` continuation reference (toward the address latches
D19–D23); nets 23–27 carry `/2`, status nets 1–3 `/2` (see the `/N` note above).

---

## Sheet 1

### Block 1 — Processor core (D4, D10, D11, D18)

**D4 — КР1810ГФ84** (synchronizer, marked `ГН/GN`):

| Pin | Name | Net |
|-----|------|-----|
| 16 | X1 | BQ1 / R1 |
| 17 | X2 | BQ1 / R1, and R2 → 0В |
| 13 | F/C | (mode select) |
| 1 | CSYN | 0В |
| 3 | AEN1 | ← addr-decoder D47 (DRAM area qualifier) |
| 4 | RDY1 | ← DRAM controller ready |
| 7 | AEN2 | ← (I/O + ЗУ area qualifier) |
| 6 | RDY2 | ← D81 |
| 11 | RES (RES3) | ← system reset (from interface) |
| 8 | CLK (out) | **CLK** → D10.19, D11.19, D18.2 |
| 5 | READY (out) | **READY** → D10.22, D11.22 |
| 10 | RESET (out) | **RESET** → D10.21, D11.21 |
| 12 | OSC (out) | → DRAM-refresh timing chain |

BQ1 = резонатор РК169МА-6АН-15М7 (15 MHz); R1 across BQ1, R2 = R1 = 510 Ω.

> **S1, S2, S3** — drawn as contact pairs `—o o—` in series with the CLK, READY
> and RESET lines between D4 and the CPU pair: these are three of the five
> switches S1…S5 of ПЭ3 ("см. п.4"), i.e. configuration/test jumpers on the
> processor clock, ready and reset (S4/S5 sit in the interface, see the Sheet 2
> notes). An earlier read of this doc treated them as net labels — corrected.

**D10 — КМ1810ВМ86** (CPU/8086, max mode, block `CPU`):

| Pin | Name | Net | | Pin | Name | Net |
|-----|------|-----|-|-----|------|-----|
| 19 | CLK | CLK | | 26 | S0 | 1 |
| 22 | RDY | READY | | 27 | S1 | 2 |
| 21 | RES | RESET | | 28 | S2 | 3 |
| 17 | NMI | ← interface | | 29 | LOCK | 4 |
| 18 | INTR | ← interface | | 25 | QS0 | 5 |
| 23 | TEST | ← D11.BUSY | | 24 | QS1 | 6 |
| 30 | RQ/GT1 | (unused) | | 16..2,39..34 | AD00..AD15, A16/S3..BHE/S7 | 7..27 |
| 31 | RQ/GT0 | ↔ D11.31 | | 33 | MN/MX | 0В (max mode) |
| | | | | 1 | VDD | +5В |

**D11 — КМ1810ВМ87** (FPU/8087, block `CPU` #②, shares the local bus):

| Pin | Name | Net |
|-----|------|-----|
| 19 | CLK | CLK |
| 22 | RDY | READY |
| 21 | RES | RESET |
| 17 | NC | — |
| 18 | NC | — |
| 23 | BUSY | → D10.TEST(23) |
| 30 | NC | — |
| 31 | RQ/GT0 | ↔ D10.31 |
| 26..24 | S0,S1,S2,LOCK,QS0,QS1 | 1..6 (shared) |
| 16..2,39..34 | AD00..BHE/S7 | 7..27 (shared) |

R3, R4, R5 (pull resistors, bottom-left) tie some of the D11 unused/control
inputs — exact nets TBD.

**D18 — КР1810ВГ88** (bus controller, block `CO`) — verified against the 8288
pinout:

| Pin | Name | Net | | Pin | Name | Net |
|-----|------|-----|-|-----|------|-----|
| 2 | CLK | CLK | | 7 | MRDC | → ЗУ read |
| 19 | S0 | 1 | | 9 | MWTC | → ЗУ write |
| 3 | S1 | 2 | | 8 | AMWC | (advanced MWTC) |
| 18 | S2 | 3 | | 13 | IORC | → УВВ read |
| 1 | IOB | 0В | | 11 | IOWC | → УВВ write |
| 6 | AEN | 0В | | 12 | AIOWC | (advanced IOWC) |
| 15 | CEN | R6 → +5В | | 14 | INTA | ← interrupt ack |
| | | | | 16 | DEN | → D26/D27 enable |
| | | | | 4 | DT/R | → D26/D27 dir |
| | | | | 17 | MCE/PDEN | |
| | | | | 5 | ALE | → D19–D21 latch |

Small glue gates near D18: **Э25.1**, **Э25.4**, **Э3.2** (2-input, combine the
command strobes for the address-decoder / register-latch timing) — pins TBD when
that corner is read.

### Block 2 — Address latches D19–D23 (КР580ИР82)

Octal transparent latches (`RG`) that de-multiplex the local AD bus into a
stable 20-bit address, strobed by the bus controller. Pin convention **as drawn**
(the drawing is authoritative for this board): DI on pins 1–8 (bits 0–7), DO on
pins 19–12 (bits 0–7), **EØ (OE̅) on pin 9, C (STB) on pin 11**. On every one seen
so far **EØ(9) → 0В** (outputs permanently enabled).

| Latch | DI ← local bus | Function |
|-------|----------------|----------|
| D19 | nets 8–15 (AD01…AD08) | low address byte latch |
| D20 | nets 16–23 (AD09…A16/S3) | mid address byte latch |
| D21 | (nets 24–27 + AD00/BHE) — *to confirm* | high address bits A17–A19, BHE |
| D22, D23 | A0–A15 | the "address to ПВК" readback latches (decoder output **Y2**, ТО Табл.2) |

- **STB (C, pin 11)** ← the latch strobe. On the local-bus latches this is
  **ALE** from D18.5 (possibly gated through Э3.2 / Э25); the exact gating is a
  cross-sheet trace — flagged.
- **DO (pins 19–12)** → the latched address; the output nets carry `/5`/`/6`
  continuation refs toward the PROM D37/D38 and the DRAM array. Exact output-net
  numbers are resolved in the cross-reference pass — flagged.

Adjacent blocks labelled `F` with A0–7 / B0–7 sides, `T` (pin 11 = direction)
and `EØ` (pin 9 = enable), right of D19/D20, are **D26, D27 — КР580ВА86** octal
bus transceivers: the local data bus buffering of ТО §5.1.1.4, with T ← D18.DT/R
and EØ ← D18.DEN. (An earlier read labelled them D25/D27 and guessed "address
mux" — corrected: the address mux is D45/D46, see the л.2 notes.)

> Caveat: the exact КР580ИР82 pin numbers are taken from the drawing, not an
> Intel-8282 datasheet (the Soviet part's OE sits on pin 9, STB on pin 11 here);
> confirm against the КР580ИР82 datasheet before the KiCad symbol is bound.
### Block 3 — Address decode, program PROM & video RAM

The 8086 memory map (ТО §5.1.1.6) is realised by a bipolar decode PROM plus glue
gates that drive the chip-selects of the program EPROM, the video RAM and the
DRAM. The **memory chips use the JEDEC 28-pin 8K×8 layout** (verified against the
2764/6264 pinout): address A0–A12 on pins 10,9,8,7,6,5,4,3,25,24,21,23,2; data
O0–O7 on pins 11,12,13,15,16,17,18,19.

**D47 — КР556РТ4А** (address decoder, bipolar PROM): inputs A12–A19, output the
area code per ТО §5.1.1.6 — `00000–7FFFF → D` (DRAM), `B8000–BBFFF → B` (video),
`FC000–FFFFF → 6` (ROM), reserved → 7. (The decode-PROM block itself sits among
the glue logic; its exact pins are read together with the DRAM in Block 4.)

**D37, D38 — К573РФ4А** (program EPROM, 8K×8, 2764 pinout → 16 KB at FC000):

| Pins | Signal | Net |
|------|--------|-----|
| 10,9,8,7,6,5,4,3,25,24,21,23,2 | A0–A12 | latched address, nets 1–13 |
| 11,12,13,15,16,17,18,19 | O0–O7 | data nets 22–29 (D37) / 30–37 (D38) |
| 20 | CE̅ | ← decode select (net 47) |
| 22 | OE̅ | ← decode / MRDC (net 40) |
| 27 | PGM̅ | +5В (read) |
| 1 | Vpp | +5В |

**D33, D34 — static RAM 8K×8** (video RAM at B8000, 6264 pinout → 16 KB):

| Pins | Signal | Net |
|------|--------|-----|
| 10…2 | A0–A12 | latched address, nets 1–13 |
| 11…19 | D0–D7 | data nets 22–29 / 30–37 |
| 20 | CS1̅ | ← decode "B" select = **net 46** (D47.Q2) |
| 22 | OE̅ | ← **net 40** (MRDC) |
| 27 | WE̅ | ← **net 39** (MWTC) |
| 26 | CS2 | ← nets 20/21 gating (D34) / +5В |

**Decode / ready glue** (gate sections, chips identified from ПЭ3):

| Ref | Chip | Type | Note |
|-----|------|------|------|
| Э3.2 | D3 | **К155ЛН1** | hex inverter (sections 1→2, 3→4, 5→6, 9→8, 11→10, 13→12 as drawn); the ПЭ3 transcription reads "ЛА1" — the schematic symbol (single-input `1`) wins, the ПЭ3 line is an OCR conflict to re-check |
| Э24.1/.2/.4 | D24 | К155ЛИ1 | 2-in AND sections |
| Э25.1/.4 | D25 | К155ЛИ1 | 2-in AND sections |
| Э32.1/.2/.3 | D32 | К155ЛЛ1 | 2-in OR sections |
| Э81 | D81 | К155ЛА2 | **8-in NAND** (in 1,2,3,4,5,6,11,12 → out 8); its output feeds **D4.RDY2(6)** — the wait/ready generator per ТО |
| Э80.x | D80 | **К155ЛН1** | hex inverter (drawn single-input `1`: 1→2, 3→4, 5→6, 9→8, 11→10, 13→12); ПЭ3 transcription said ЛИ1 — symbol wins, OCR conflict |
| Э24.x | D24 | К155ЛЛ1? | 2-in sections drawn `1` (OR-type), not `&` — ПЭ3 says ЛИ1 (AND): to settle by function |

These gates combine the D47 area codes with MRDC/MWTC/IORC/IOWC into the per-chip
CE/OE/WE strobes; the decode-output nets appear as the label row `50/2`, `44/2`,
`42`, `39`, `45`, `40`, `46`, `43/3`, `53`, `50` at the top of the memory array.
Exact gate-input nets are resolved with Block 4.
### Block 4 — DRAM subsystem (array + mux + control + СОКОО)

**DRAM array — КР565РУ7** (256K×1 dynamic RAM, `RAMD`). Each chip carries **one
data bit**; they are wired in parallel (common muxed address, common RAS/CAS/WE),
so the netlist is one instance × N. Per-chip pinout **as drawn** (Д49, Д51, Д56…):

| Pins | Signal | Net |
|------|--------|-----|
| 5,7,6,12,11,10,13,9,1 | A0–A8 (multiplexed row/col) | nets 1–9 |
| 2 | DI (data in) | net 2 (write data) |
| 14 | DO (data out) | one bit each: Д49→22 (D0), Д51→23 (D1), Д56→24 (D2), … |
| 3 | WE̅ | net 10 |
| 4 | RAS̅ (drawn `RD`) | net 11 |
| 15 | CAS̅ (drawn `CS`) | net 12 |

Organisation (ТО §5.1.1.10): 2 data banks × 256 KB + 2 check banks × (256K×5) →
**512 KB with Hamming SECDED**; the `КР565РУ7Г` count in ПЭ3 (≈26) fits 16 data
bits + ~10 check bits × 256K. Exact per-chip bit→bank map is filled when the full
array is instantiated (repetitive — deferred).

**Address multiplexer** — the octal latch/register blocks `F` (A0–7 → out, `T`
strobe pin 11, `EØ` OE pin 9) right of D19–D21 fold the latched 18-bit address
into the 9-bit muxed A0–A8. Designators/type to confirm (candidates: К555ИР22
D45/D46 per ТО §5.1.1.9, or the ИР82 readback pair D22/D23) — **flagged**, the
scan label was ambiguous.

**DRAM controller** (ТО §5.1.1.9): control PROM **D44 (КР556РТ5)** + register
**D54 (К555ИР23)** sequence RAS/CAS/WE; two-stage 18→9-bit address transfer;
refresh every **10 µs** from monostable **D29.2 (К155АГ3)** with R13, C2.

**RAS/CAS/WE distribution gates** (read in the array-control corner, chips from
ПЭ3): D16 (К155ЛА3), D55 (К155ЛИ1), D80 (К155ЛИ1), plus pull-ups R28–R31 → +5В
on the control lines. These fan the sequencer outputs to the banks (selected by
the decode nets 47/45/46).

**СОКОО (Hamming SECDED) — D78, D79 (К555ВЖ1)**: generate the 5 check bits on a
write and check/correct on a read, sitting between the data bus (nets 22–29) and
the check-bit DRAM banks (ТО §5.1.1.10). Pins deferred to a dedicated ECC tile.
### Block 5 — System-bus interface (XS1, buffers, command decoder)

This block is the host↔board protocol in hardware — it produces the **Y0–Y7**
command functions of ТО Табл.2 (and [`mc1702.md`](mc1702.md) §2).

**XS1** — the ПВК system-bus edge connector (contact → signal, board pin `Бnn`/`Ann`):

| Ct | Signal | Ct | Signal | Ct | Signal |
|----|--------|----|--------|----|--------|
| 1–8 | AD00–AD07 | 9–16 | AD08–AD15 | 17 | МОБМ (bus cmd strobe) |
| 18 | МD | 19 | МАЦВ | 20 | МDЧТ (read) |
| 21 | ВАД | 22 | ПМВ | 23 | МУСТ (reset) |
| 24 | 30А (IRQ) | 25 | 30Б (IRQ) | 26 | ОПВ |

Power/ground on separate contacts (Б2, Б18, Б30, …). AD00–AD15 are the ПВК's
multiplexed address/data; the rest are the bus control/handshake lines named in
ТО §5.1.2.

**D6, D7 — КР559ИП13** (octal bus buffers, block `F`): buffer AD00–AD07 (D6) and
AD08–AD15 (D7) between XS1 and the internal interface bus; enables E1(9)/E2(11).
**D13 — КР559ИП13**: the read-back data buffer (internal → XS1 on a ПВК read).
**D9 — К555АП3**: buffers the control strobes (МОБМ, МD, МАЦВ, МDЧТ, ВАД, ПМВ),
ТО §5.1.2.1.

**D8 — К155ТМ5** (command register, block `T`): on the **МОБМ** strobe it latches
AD01, AD02 and МDЧТ (ТО §5.1.2.2). Inputs B1(1),B2(2),B3,B4(5); clocks C1(3),
C2(12); outputs Q1(14),Q2(13),Q3,Q4 → the command decoder.

**D15 — К555ИД7** (3-to-8 decoder `DC`) — **the command decoder Y0–Y7**:

| Pin | Name | | Pin | Out |
|-----|------|-|-----|-----|
| 1 | A ← D8 (МDЧТ) | | 15 | **Y0** (test-ROM read + inc) |
| 2 | B ← D8 (МАD01) | | 14 | **Y1** (status word D36) |
| 3 | C ← D8 (МАD02) | | 13 | **Y2** (address to ПВК, D22/D23) |
| 4 | G0̅ | | 12 | **Y3** (data in, enable D12/D13) |
| 5 | G1̅ | ← МD/ПМВ gating | 11 | **Y4** (NMI) |
| 6 | G2 | | 10 | **Y5** (reset test-ROM ctr) |
| | | | 9 | **Y6** (interrupt vector → D14) |
| | | | 7 | **Y7** (data → MP bus) |

**D14 — КР580ИР82** (interrupt-vector register, `RG`): latches the vector from the
ПВК on **Y6**, presented to the 8086 on INTA (ТО §5.1.2.3).

Glue in this corner: D3, D16, D24 sections (gate the decoder enables and the МОБМ
timing), R1/R7 pull-ups.

> **S1–S5 switches** (ПЭ3 "см. п.4"): S1–S3 are the jumpers on CLK/READY/RESET
> (Block 1); **S4, S5** are located at the bottom of the л.2 strip, on the
> interface strobe/enable path next to the WAIT/HALT triggers D35 — their exact
> contact function (slot select vs. test mode) is still to be read — flagged.

---

**Sheet 1 complete** (blocks 1–5). Deferred to the cross-reference / sheets 2–3
pass: exact `/N` net semantics, the DRAM array per-bit instantiation and the
address-mux type, the СОКОО pin-level wiring, and the S1–S5 slot-address switches.

---

## Sheet 2

Sheet 2 carries the memory-side detail: the address decoder, the DRAM address
multiplexer, the DRAM controller (PROM + register + sequencer + refresh) and the
second data bank. It resolves several Sheet-1 flags.

### D47 — address decoder (КР556РТ4А, `PROM`) — pins confirmed

| Pin | Name | Net |
|-----|------|-----|
| 5,6,7,4,3,2,1,15 | A0–A7 | nets 12–19 = **A12–A19** (from latch D21) |
| 13 | E1̅ | enable |
| 14 | E2̅ | ← net 41 |
| 12 | Q0 | → net 47 (via R28 pull-up) — PROM CE̅ |
| 11 | Q1 | → net 45 (R29) |
| 10 | Q2 | → net 46 (R30) — bank / video select |
| 9 | Q3 | → D80.4 (inverter) (R31) |

Open-collector outputs pulled to +5В by R28–R31; the codes are the ТО §5.1.1.6
area codes (D/7/B/6).

### D45, D46 — DRAM address multiplexer (К555ИР22, `RG`) — **flag resolved**

The blocks drawn `RG` with `E`(1)/`C`(11) here are the ИР22 octal registers that
fold the latched address into the multiplexed A0–A8 of the array (ТО §5.1.1.9),
*not* the ИР82 read-back latches:

| Chip | DI (pins 3,4,7,8,13,14,17,18) | DO (2,5,6,9,12,15,16,19) |
|------|------------------------------|---------------------------|
| D45 | latched address nets 1–8 | muxed address `1/27 … 8/27` → array A0–A7 |
| D46 | latched address nets 10–17 | muxed address (row/col second stage) |

E̅(1) = output enable, C(11) = strobe from the sequencer; the two-stage 18→9-bit
transfer of ТО is these two registers alternately enabled.

### DRAM controller — D44 + D54 + sequencer

**D44 — КР556РТ5** (512×8 control PROM, `PROM`): address inputs A0–A8 on pins
8,7,6,5,4,3,2,1,23 ← the state/request nets 1–9; chip selects S1–S4 (18,19,20,21)
strapped via R14 to +5В / 0В; outputs Q0–Q7 (9,10,11,13,14,15,16,17) with pull-ups
**R20–R27** → **D54 — К555ИР23** DI0–7 (3,4,7,8,13,14,17,18), R̅(1) → 0В, C(11) ←
the sequencer clock (from D39.1, ТО §5.1.1.9). D54's outputs are the RAS/CAS/WE
and mux-strobe lines.

**Sequencer / arbiter** (К155ТМ2 D-flip-flops): D39.1, D43.1, D43.2, D48.1 (S/R/C/D
with outputs 5/6, 8/9) — the request/refresh arbiter; **D2.2 (К555ИЕ19, CT2)**
counter with R32 pull-up; **D29.2 (К155АГ3, `G`)** monostable, R12/R13/C2 — the
**10 µs refresh** request generator; glue D25.2, D32.4, D40.x, D42.x, D80.5/.6
(К155ЛН1 inverters); pull-ups R17–R19.

### DRAM array — complete bit map (л.2 + л.3 strips)

All **26 КР565РУ7** of ПЭ3 (D49–D52, D56–D77) located, four rows of the grid,
common muxed address A0–A8 (nets 1–9) and WE/RAS/CAS (nets 10/11/12):

| Row | Chips (DO pin 14 → net) | Function |
|-----|-------------------------|----------|
| 1 | D49→22, D51→23, D56→24, D58→25, D62→26, D66→27, D70→28, D74→29 | **data D0–D7** (low byte) |
| 2 | D50→30, D52→31, D57→32, D59→33, D63→34, D67→35, D71→36, D75→37 | **data D8–D15** (high byte) |
| 3 | D60, D64, D68, D72, D76 (DI ← nets 19–23) | **check bank A** (5 bits) |
| 4 | D61, D65, D69, D73, D77 (DI ← nets 23–27) | **check bank B** (5 bits) |

= 2 data banks + 2 check banks of 256K×5, exactly ТО §5.1.1.10. The check-bit
DI/DO nets are the СОКОО (D78/D79) syndrome/check lines.

The л.3 strip bottom carries the ГОСТ **«1. Цепи питания»** table (power pins per
designator group: D8; D1…D3, D16, D17, D24, D25, D28, D3x…, D48, D53, D55, D80,
D81; D15, D29, D47; D49…D52, D56…D77) — the power-net source for KiCad.

### Interface-side detail on the л.2 strip

- **D21** inputs confirmed: nets 24–27 (A17/S4…BHE/S7) + net 7 → the high
  address latch (Block 2 flag resolved).
- **D28.1/.2/.3 — К155ЛА4** (3-in NAND) decode glue on nets 28–30 (the cycle
  status S-lines) → memory/I-O qualifiers.
- **D22, D23 — КР580ИР82** read-back latches ("address to ПВК", Y2): DI ← nets
  1–7 / 8–15, EØ(9)/C(11) from the interface strobes.
- **WAIT/HALT detector** (ТО §5.1.2.3): **D29.1 — К155АГ3** monostable (A(1)/B(2)/
  R(3), C(14)/RC(15) with R8, R9, C1) feeding **D35.1, D35.2 — К155ТМ2** D-triggers
  (R/D/C/S, Q on 5 and 9) with R10 pull-up; their outputs become status-word bits
  ADL03 (WAIT) / ADL05 (HALT) via D3.4 / D24.3 inverters.
- **S4, S5** — the two remaining switches (contact pairs at the strip bottom).
- **D3.5, D3.6, D24.1** — inverters on the D34 chip-select path.

### Status register D36 and the state triggers (л.1/л.2 lower-right)

**D36 — К555ИР23** (`RG`, the status word of ТО Табл.3): DI0–7 on pins 1–8 ←
the state flags; DO0–7 on pins 19,18,17,16,15,14,13,12 → nets 8,7,6,5,4,3,2,1 =
**ADL07…ADL00** toward the read-back buffer (D13) on command **Y1**; EØ(9) ←
Y1 enable, C(11) ← latch clock.

State flags feeding it (К155ТМ2 D-triggers, R/D/C/S with Q on 5/9): **D30.1**,
**D35.1, D35.2** (WAIT/HALT, clocked from the D29.1 monostable), **D40.2**,
**D39.1/D39.2**, **D43.x**, **D48.1** — the RES/INTR/WAIT/HALT/video-write/IO
conditions of ADL01–ADL07; glue D3.3/D3.4, D16.3, D24.2/D24.3, D31.1/.3/.4
(К155ЛА3), D41.3, D42.2; pull-ups R12 (D29.2), R17–R19, R32.

**S4, S5** — contact pairs wired into the D36 input path from the D35 outputs:
test/force jumpers for status bits (or the slot-address enable) — exact
function still to be settled from the ТО п.4 reference — flagged.

### Test ROM D5 with its address counter (л.1, above XS1)

**D5 — К573РФ5** (2K×8 EPROM, 2716 pinout): A0–A10 ← the counter chain; Vpp(21)
= +5В; CS̅(20) and OE̅(18, drawn `E4`) ← **Y0** gating (via inverter D3.1);
O0–O7 (9,10,11,13,14,15,16,17) → **D12 — КР559ИП13** (`F`, E1(9)/E2(11)) → the
XS1 AD00–AD07 lines. This is ТО §5.1.2.4: the identification/test ROM the ПВК
reads with **Y0** (one byte per read, address auto-incremented) after **Y5**.

**Counter chain — К555ИЕ19** (dual 4-bit binary, `CT2`): **D1.1** (C(2)? R(2),
outputs 3,4,5,6 → A0–A3), **D1.2** (outputs 11,10,9,8 → A4–A7, R(12)), **D2.1**
(outputs 3,4,5 → A8–A10, R(2)); all R̅ inputs common ← **Y5** (reset), the first
stage clock ← **Y0** (each read advances the address); ripple C ← previous
stage's MSB.

### Video RAM / PROM select detail (л.2 overlap, confirmed)

- **D34** (video RAM upper byte): CS1̅(20) ← net 46, OE̅(22) ← net 40 (MRDC),
  WE̅(27) ← net 39 (MWTC), **CS2(26) ← D3.5/D3.6** (К155ЛН1) from nets 20/21 —
  the A0/BHE **byte-lane select** (ТО §5.1.1.8: "CS2 по инвертированным A0 и
  BHE"). D33 is the lower byte.
- **D38** (PROM upper byte): CS1̅(20) ← net 47, OE̅(22) ← net 40, PGM̅(27), CS2(1)
  ← +5В via **R11**.

### СОКОО — Hamming SECDED, D78/D79 (л.3 far right)

**D78, D79 — К555ВЖ1** (`COM`, 16-bit EDC chip used 8 bits wide, one per data
byte):

| Pin(s) | Name | D78 (low byte) | D79 (high byte) |
|--------|------|----------------|-----------------|
| 2,3,4,5,6,7,8,9 | DI0–DI7 | data nets 22–29 | data nets 30–37 |
| 10,11,12,13,15,16,17,18 | DI8–DI15 | 0В (unused) | 0В (unused) |
| 24,23,22,21,20 | DCO0–DCO4 (check bits) | nets 18,19,20,21,22 | nets 23,24,25,26,27 |
| 19 | DCO5 | (n/c) | (n/c) |
| 25, 26 | mode/control (`Ф`, `1`) | ← sequencer | ← sequencer |
| 1, 27 | error flags (single / multiple) | → nets 15/14 | → nets 15/14 |

The five check bits of each byte go straight to the check-bit DRAM banks (rows
3/4 of the array: D60…D76 DI ← 19–23, D61…D77 DI ← 23–27) — the "2 контрольных
банка 256K×5" of ТО §5.1.1.10 — and the error flags feed the RDY/NMI path.

**Every designator of ПЭ3 (D1…D81, BQ1, R1…R32, C1…C66, S1…S5, XS1) has now
been located on the Э3 strips.**

## Cross-reference pass — the interface data bus (IB)

Read from full-resolution tiles of the bottom-left group of л.1 after ERC on
the generated schematic listed every net with only one recorded end.

**Net groups.** The bottom-left group has its own local numbering: nets
**1–16 = AD00–AD15 behind the bus transceivers** (prefixed `IB.` in the CSV),
17–19 = its control nets. The latched address bus is **`LA.k` = A(k)**, k = 1…19
(D19 → 1–8, D20 → 9–16, D21 → 17–19), plus **LA.20 = BHE̅ latched, LA.21 = A0
latched** (D21 DO3/DO4, pins 16/15, both `/3`). The memory chips therefore see
A1…A13 on their A0…A12 — the 8086 word address; A0/BHE select the byte via
D3.5/D3.6 (LA.20/LA.21 → D34/D33 CS2 per Block 3).

| Part | Pins as drawn | Connection |
|------|---------------|------------|
| D6 КР559ИП13 | A 19…12, B 1…8, E1 9, E2 11 | A ← XS1 1…8 (AD00–07); B → IB.1…8 |
| D7 КР559ИП13 | same | A ← XS1 9…16 (AD08–15); B → IB.9…16 |
| D12 КР559ИП13 | A 1…8, B 19…12 | A ← IB.1…8; B → MD.22…29 (memory data low byte) |
| D13 КР559ИП13 | same | A ← IB.9…16; B → MD.30…37 |
| D22 КР580ИР82 | DI 1…8, DO 19…12 | DI ← LA.21, LA.1…7 (A0…A7); DO → IB.1…8; C(11) ← +5В via R1 (transparent); EØ(9) common with D23 |
| D23 КР580ИР82 | same | DI ← LA.8…15; DO → IB.9…16 |
| D14 КР580ИР82 (vector) | same | DI ← IB.9…16 (AD08–15 from the ПВК); DO → MD.22…29 (read by the 8086 on INTA) |
| D36 К555ИР23 (status) | DO 19…12 | → IB.8…1, i.e. **ADL07…ADL00 = IB.8…1** |
| D5 К573РФ5 (test ROM) | DO 9,10,11,13,14,15,16,17 | → IB.1…8 |
| D8 К155ТМ5 (= 7477: D 1,2,5,6; C 3+12; Q 14,13,9,8) | | D1 ← IB.2 (AD01), D2 ← IB.3 (AD02), D3 ← wire from below (not followed), D4 unconnected; C ← wire from below |
| D9 К555АП3 (= 74LS240 pinout) | A 2,4,6,8,11,13,15,17; Y 18,16,14,12,9,7,5,3; E 1,19 → 0В | A0…A5 ← XS1 17…22 (МОБМ, МD, МАЦВ, МDЧТ, ВАД, ПМВ); A6/A7 and the Y side not followed |
| D26 / D27 КР580ВА86 | A 1…8, B 19…12, EØ 9, T 11 | A ← LB.7…14 / LB.15…22 (AD0–15); B → MD.22…29 / MD.30…37 |

**Test-ROM read path.** Two nets tie the test ROM and the transceivers
together: net **A** = {D3.1 in, D5.20 (CS̅), D1.1 C, D6.9, D7.9} and net **B** =
{D5.18 (OE̅), D6.11, D7.11}; both come from below XS1 and their sources are not
followed yet (`TROM_SEL` / `TROM_OE` in the CSV; Y0 is the candidate for A —
ТО: "read test ROM + increment"). D3.1's output is the group's net 17. The
counter chain is D1.1 (Q → TA0…3, Q3 → D1.2 C) → D1.2 (TA4…7, Q3 → D2.1 C) →
D2.1 (TA8…10); all R ← Y5.

**D12/D13 enables.** E1 (pin 9) ← net 19 = D41.3 (OR: SEL.42 + wire from
below); E2 (pin 11) ← net 18 = **SEL.51** = D24.2 (OR: SEL.44 + wire from
below). The two labels 18 and 51 sit on one net, which is how a net crossing
bus groups is drawn.

**Drawing `/N` inconsistencies.** IB.1 is drawn `/4` but has five recorded
pins (D6, D12, D22, D36, D5); IB.9…16 are `/3` with four (D7, D13, D23, D14).
The count marks may predate the vector/test-ROM additions; the connections
themselves are unambiguous on the drawing.

**D28 К155ЛА4** (3-input NANDs beside D21): D28.1 ← labels 28, 29, 30; D28.3 ←
28, 29 + ?; D28.2 ← ?, ?, 30. Labels 28–30 belong to the local-bus group's
numbering beyond the 27 CPU-bus nets and are not yet identified.

## Cross-reference pass — interface glue, read by the wire tracer

Reading unlabelled point-to-point wires across the whole lower half of л.1 by
eye gave ±1-pin results, so the rest of that cluster was read mechanically:
`tools/mc1702_trace.py` (boxes and pin rows for this strip in
`mc1702-trace-boxes-l1-lower.json`) binarises the scan, keeps the thin
orthogonal ink runs as wire segments, and joins them by rules that were tuned
until every net it produced that *could* be checked against a drawn label
matched the label (test-ROM address bus 10/10, D8→D15 3/3, Y1→D36.EØ,
Y5→counter resets, D12/D13 E1/E2 = 19/18):

- thick runs (median half-width > 2.6 px) are frame / bus-box lines, not
  wires; wires are severed at them and a bridge across one is reported as a
  `frame` event for the reader to decide by the labels;
- an L corner connects only when the ink of both wires ends there; a wire
  ending on a passing wire is a T and connects only through a junction dot
  (round blob, thicker than a line, verified at 3× on the scan);
- a wire broken by a digit is bridged (≤ 40 px) unless something else ends in
  the gap or another line passes through it (the bus verticals, where labels
  on both sides happen to share a row — the source of every false join).

Nets that contradicted labels (bus-row alignments: IB.1↔net 19, D12.B↔D22.DI,
D7.B↔D14.DI) were rejected; everything below is what survived, with the
tracer's event list checked at 3× where a net looked doubtful.

**Test ROM.** D5.20 (CS̅) ← **Y0**; D5.18 (OE̅) ← D16.1 out; D3.1 out (net 17)
→ D1.1 clock (the counter steps on the trailing edge of the read); D3.1 in
joins the Y0 wire (by eye). D6/D7: E1 (9) ← D16.2 out, E2 (11) ← D9.3.

**D9 (74LS240) as the control-signal inverter.** A0–A5 ← XS1 17–22 (МОБМ, МD,
МАЦВ, МDЧТ, ВАД, ПМВ); two nested loops feed its own outputs back: Y0 (18,
~МОБМ) → A6 (15) and Y2 (14, ~МАЦВ) → A7 (17), so **Y6 (5) = МОБМ and Y7 (3)
= МАЦВ re-buffered**. Y6 → D8 C1/C2 (command strobe); Y7 → D6/D7 E2; Y1
(~МD) → D15 G2, D17.3.4, D36.C; Y3 (~МDЧТ) → D16.2.5; Y4 (~ВАД) → D8.D3;
Y5 (~ПМВ) → D16.1.2, D16.2.4, D17.3.5; Y0 → D16.1.1.

**Command decoder D15.** A/B/C ← D8 Q1–Q3 (D1 ← IB.2, D2 ← IB.3, D3 ← ~ВАД,
D4 unconnected); G0+G1 tied ← the ПМВ wire; G2 ← ~МD. Outputs: Y0 → D5.20;
Y1 → D36.9; Y2 → (toward D22/D23 EØ, not closed); Y3 → D24.2.5; Y4 → D3.3.5;
Y5 → counter resets + D16.3.9; **Y6 → D39.1.C, D40.2.C, D42.1**; Y7 →
D41.3.10. So D12/D13 E2 = SEL.44 ∨ Y3 ("data in") and E1 = SEL.42 ∨ Y7 ("data
out") — the direction logic of ТО §5.

**Status triggers (D30 = К155ТМ2; labels re-read at 4×: D30.1 and D30.2,
not «D39.1/D40.2»).** D30.1: R ↔ D30.2.S ↔ D35.2.D (one net, source not yet
found), D ← bus label 4 (?), C ← Y6, S ↔ D31.4.12, Q → D36.7 (DI6). D30.2:
C ← Y6, R ← D31.4 out, D ← bus-box bottom label (6?), Q → D36.4 (DI3).
D31.1 out → D14.C; D31.1.2 ← the inverter after Y6 (its label is smudged; an
inverter must be a ЛН1 unit — D80.1 assumed); D14.EØ ↔ D31.4.13. D36.C ← ~МD.

**Bottom bus line = XS1 23–26.** The thick line along the sheet bottom carries
the connector's last four contacts (labels 23–26 read at 3×): МУСТ (23) →
D31.3.10; 30А (24) → D36.1 (DI0); ОПВ (26) ← D17.3 out (К155ЛА13 is the
open-collector NAND — the board's driver onto the bus); 30Б (25) → a vertical
next to the D36.1 wire, source not identified; D36.2 ends on the line without
a readable label.

## Cross-reference pass — decode / ready glue, read by the wire tracer

Second tracer run: the top of л.1 (`mc1702-trace-boxes-l1-upper.json`) and,
because seven wires leave strip 1 at its right edge, the same cluster on
strip 2 (`mc1702-trace-boxes-l2-upper.json`, where D47 and the D25.3/D55/
D16.4/D80.4 group sit).

**D18 command outputs.** MRDC (7) → D32.2.5 (and net 40 at the memories);
MWTC (9) → D32.1.1 (net 39); IORC (13) → D25.4.12; IOWC (11) → D25.4.13 and
D25.1.1; DEN (16) → D3.2; DT/R (4), ALE (5) and D3.2 out go down through the
bus-box border to D26/D27 T, the latch strobes and D26/D27 EØ. **AMWC, AIOWC
and MCE are unconnected**; INTA (14) runs down to the bus-box border
(label not read); CEN ← R6 → +5В; IOB and AEN → 0В.

**Gate chain.** D32.1 out → D25.1.2 (D25.1 out = net 44); D32.2 out = **net 43**
→ D81.6, D16.4.12, D55.1.13 (both bottom labels read «43» at 2.5×, which is
the `/3`); D81: pins 3/4/5/12 bridged (net 53), 11 ← net 50, **1 ← D47 Q0**,
8 → RDY2 (D4); D32.3 ← 45/46 → D80.2 → D41.1.5 + D41.2.2; D41.2.1 ← net 18
(= SEL.51, the D24.2 "data-in" enable), D41.1.6 → 48, D41.2.3 → 49.

**D47 outputs, corrected at 2×.** Q0 (12) has no bus number: R28 pull-up →
D81.1 and D55.3.10; **Q1 (11) = net 46** (→ D80.4.11 and the label), **Q2
(10) = net 45**, **Q3 (9) = net 47** (PROM select). The earlier read-out had
Q0 = 47 / Q1 = 45 / Q2 = 46 and "Q3 → D80.4" — corrected in the CSV. E1 (13)
and E2 (14) are tied (junction dot) and go to net 41.

**Strip-2 ready group.** D80.4 (← 46) → D16.4.13; D16.4 (13 ← ~46, 12 ← 43)
→ D32.2.4 and D55.1.12; D55.1 (12, 13 ← 43) → D55.2.4; D25.3 (10, 9 ← two
long lines from the left, not closed) → D55.3.9; D55.3 (10 ← D47 Q0) →
D55.2.5; D55.2 out → a long line to the left toward the CPU/D18 area (not
closed — READY candidate). D24.4: 12 → 0В, 13 ← a top run, out → a long line
to the left (both not closed).

## Cross-reference pass — DRAM sequencer (strips 2 and 3)

Third tracer region: `mc1702-trace-boxes-l2-mid.json` on strip 2 (binarisation
offset lowered to 5 for the fainter middle strip) and, because the same group
is drawn more clearly on strip 3, `mc1702-trace-boxes-l3-seq.json` as an
independent second read. Where the two disagreed the strip-3 labels won:
the trigger with pins S 4 / C 3 / D 2 / R 1 / Q 5 is **D48.1**, the one with
R 1 / D 2 / C 3 / S 4 / Q̄ 6 is **D43.1**, the lower one (R 13, D 12, C 11,
S 10, Q 9, Q̄ 8) is **D48.2**. The thick vertical bus right of the group has
its own numbers (4, 5, 6, 10, 16, 17/2 …), prefixed `SQ.` until the DRAM
controller on strip 3 ties them to `MA.n`.

- **Clock tree.** D39.1 (R and S tied) Q̄ → C of D43.1, D48.1 and D43.2;
  D39.1 C comes from the long top runs, D from below (D36 area).
- **D43.1**: R = D43.2 R (common reset, source open), D ← a top run, Q̄ →
  D40.4.12 and D48.2.S.
- **D43.2**: S ← D29.2 Q (the АГ3 monostable: A̅ = 0В, B ← a long line from
  the left — D35.2 Q is the candidate, R̅ ← R12, C ← C2, RC ← R13 → +5В),
  D = D40.3.10 net, **Q̄ = `SQ.5`** → D42.1.1.
- **D48.1**: D = D42.3.9 net (driver not found), S ← a wire from the left,
  R short (R17–R19 pull-ups sit above it), Q up.
- **D48.2**: R ← `SQ.10`, D = D80.3.5 net (`SQ_A`), C ← D42.4 (D42.3 → D42.4
  with inputs tied), Q → D40.4.13 and up, **Q̄ = `SQ.6`** → D42.1.2 and a
  D41 gate. D40.4 → D80.5. D80.3 → `SQ.17` (drawn 17/2).
- The gate with pins 5,4→6 next to the bus is labelled D41.2 although those
  pins are already used by the decode-cluster gate; recorded as D41 12/13→11
  (ЛЛ1 unit 4) with the drafting error noted. That gate, D40.3.9, D43.1.2
  and D39.1.3 go into the long top runs — the same open item as the READY
  paths.
**DRAM controller (strip 3 left, read at 1.6×).** The thick vertical bus of
the sequencer is the controller's **state bus**: its numbers 1–9 address the
microcode PROM **D44** (КР556РТ5: A0…A8 = pins 8,7,6,5,4,3,2,1,23 ← labels
1,2,3,4,5,9,6,8,7; S1/S2 tied ← R14, S3/S4 → 0В), so `SQ.5` = D43.2 Q̄ and
`SQ.6` = D48.2 Q̄ are state bits. D44 Q0…Q7 (9,10,11,13,14,15,16,17) →
R20–R27 pull-ups → **D54** (К555ИР23, DI 3,4,7,8,13,14,17,18; OE → 0В; C ←
a long line from the left) → DO 2,5,6,9,12,15,16 = **MA.11 (RAS, /26),
MA.12 and MA.13 (CAS, /13 each — the л.3 grid shows D58 CS ← 12 and D59
CS ← 13, i.e. **CAS is split by byte: MA.12 = low byte + check bank A,
MA.13 = high byte + check bank B**; the generator assigns it per chip), MA.10
(WE, /26), MA.14, MA.15, MA.16 (/2 — also the bus
label 16), DO7 (19) up along the bus**. `SQ.17` = MA.17 = D80.3 out →
D41.4.12; D41.4.13 and its output are long lines. ТО Таблица 1 (the 256 …
512 PROM codes) is what turns this into a working machine — still to
transcribe.

**Step counter and the A8 multiplexer (strip 2, left of the sequencer).**
State-bus bits 1–3 are the outputs of **D2.2** (the second К555ИЕ19 counter:
11, 10, 9 → labels 1, 2, 3, pull-ups R17–R19), reset by D48.1 Q and clocked
from below (the sequencer clock, not closed) — the microcode step counter
addressing D44. D48.1 S ← net 43. **MA.9 (A8 of the array, drawn 9/26) is a
wired-AND of two open-collector NANDs**: D17.1 (2 ← LA.9 = A9, 1 ← a select
line) and D17.4 (13 ← net 49 = D41.2 out, 12 ← a second line; A18 expected)
— the ninth address bit is multiplexed outside D45/D46, whose OE lines are
the natural pair 48/49. D25.2 = MWTC · MRDC (39, 40), D32.4 ← 45.

**Microcode.** ТО Таблица 1 (page 15) is the full 512-byte dump of D44 —
transcribed to `mc1702-d44-prom.txt`. Its structure matches the wiring: the
16-entry rows step with the D2.2 counter (A0–A2), the 0x80 and 0x100 address
bits change only output bits 1 and 2 (80 → 82 → 84 …) — exactly the CAS A /
CAS B outputs of D54 — so PROM inputs A7 and A8 (state-bus labels 8 and 7) are
the byte selects A0/BHE named in the ТО text; the 0x180 block (both selects
off) is all FF = no cycle; the 0x60/0xE0 rows (EF = bit 4 = MA.14 low) are the
refresh mode; the 0x70 rows (E9 68 E8 EE …) the refresh RAS/CAS sequence.

**Passives, jumpers, crystal (ПЭ3 values + drawing).** R17–R19 → state bus
1–3, R20–R27 → D44 outputs, R28–R31 → D47 outputs, R12/R13/C2 → the АГ3
refresh timer, R14 → D44 S1/S2, R15 → D45/D46 C, R6 → CEN, R1 → D22/D23 C
(the label at D23 reads R1 while the ПЭ3 puts R1/R2 = 510 Ω in the
synchronizer — to re-check), C3/C4 electrolytic + C5…C66 decoupling, BQ1
15 MHz on D4 X1/X2. **S1…S5 are the slot-address switches** (ПЭ3: «адрес
гнезда»): S1–S3 sit in the CLK/READY/RESET lines between D4 and D10 as
drawn, S4/S5 in the D35.2/D36 area (their wires not followed). R11, R32, C1
have one end open.

- D35.1 S and D35.2 R/C/S come from the left (the S4/S5 jumper area);
  D35.2 Q goes right and down toward the D36 inputs; D36.3/5/6 rise into
  the S4/S5 area — not closed.

**Technical requirements on the sheet (л.3 bottom, partly cut).** Note 2:
D10 is mounted in a socket (РП-15-1 …), D5 in a РС-24-7 socket, D47 in a
РС-16-1 socket (АГО.364.003 ТУ); note 4 / 6: the jumpers S1…S5 may be
replaced by … (text cut at the strip edge). Decoupling: C3, C4 (electrolytic)
and C5…C30 (26 pcs, marked 2б) + C31…C66 (36 pcs, 3б) between +5В and 0В.

## Cross-reference pass — the long top runs (strips 1 and 2)

Tracer runs over the whole upper band of strip 1 (`…-l1-top.json`: D4, D10,
D18 and the decode cluster) and strip 2 (`…-l2-top.json`: the ready group and
the sequencer tops), matched across the strip edge by wire height (the middle
strip sits 10–25 px lower than the left one), plus `…-l1-osc.json` for the D4
wires that leave downwards.

- **Sequencer clock = D4 OSC.** The OSC output (pin 12) runs down and right
  along y ≈ 2129 on strip 1; the D39.1 C wire reaches the left edge of strip 2
  at y = 2150 — the same line. D39.1 D and the D2.2 (step counter) clock share
  a net whose source is still open; D43.1 D ← D32.4 out.
- **Ready chain.** D25.4 (IORC·IOWC) → D25.3.10, D25.3.9 ← INTA (by height),
  D25.3 → D55.3.9, D55.3.10 ← D47 Q0, D55.3 → D55.2.5, D55.2.4 ← D55.1 out
  (D16.4 ∧ net 43), **D55.2 out → D4 AEN2** (matched across the edge, dy 12);
  D81 (nets 43, 50, 53, D47 Q0) → **D4 RDY2**. D4 F/C and CSYNC → 0В; AEN1
  ends at (2304,364) on strip 1, RDY1 and RES̅ run right along y ≈ 2210/2235
  into strip 2 — not closed.
- D10: S0/S1/S2 → D18 (traced); CLK/READY/RESET arrive from the S1–S3
  jumper side; NMI, INTR and TEST wires go downwards (D3.3 out / D11 BUSY
  expected).

## Progress

- [x] Sheet 1, Block 1 — processor core (D4, D10, D11, D18)
- [x] Sheet 1, Block 2 — address latches (D19–D23), inputs + convention read; output nets + STB gating deferred to the cross-sheet pass
- [x] Sheet 1, Block 3 — address decode (D47), program PROM D37/D38, video RAM D33/D34, decode/ready glue (D3/D24/D25/D32/D81); memory pinout verified vs 2764/6264
- [x] Sheet 1, Block 4 — DRAM subsystem (КР565РУ7 array pinout + structure, controller D44/D54, refresh D29, RAS/CAS/WE gates, СОКОО D78/D79); array instantiation + mux type + ECC pins deferred
- [x] Sheet 1, Block 5 — interface XS1, buffers D6/D7/D9/D13, command register D8 + decoder D15 (Y0-Y7), interrupt-vector reg D14
- [x] Sheet 2 strip — D47 pins, D45/D46 mux (flag resolved), D44/D54 controller, sequencer/refresh, bank bit map, WAIT/HALT triggers, S4/S5 found; scans identified as overlapping strips of one drawing
- [x] Sheet 3 strip — full DRAM grid (26 chips, bit map), СОКОО D78/D79 pin map, power table
- [x] All 81 ICs located and assigned to a function block
- [x] `/N` semantics settled (pin count); bus-local net numbering handled by prefixes in `mc1702-netlist.csv` (started: CPU core, latches, memories, DRAM, СОКОО, decoder, interface, XS1)
- [~] Cross-reference pass, decode/ready cluster: gate pin maps + partial net endpoints in the CSV (D81 bridged inputs = net 53, D32.3←45/46 → D80.2 → D41 pair → nets 48/49, D24.4 as buffer, D3.2 = DEN inverter for the D26/D27 enables); remaining '?' wires + the sequencer cluster still to trace
- [ ] Cross-reference pass: the exact
      gate-input nets of the decode/sequencer glue (D3, D16, D17, D24, D25,
      D28, D30–D32, D39–D43, D48, D53, D55, D80, D81); settle S4/S5 function
- [~] Datasheet binding: КР580ИР82 = Intel 8282 (DI 1–8, DO 19–12, OE̅ 9, STB
      11, GND 10, Vcc 20) and КР580ВА86 = 8286 (A 1–8, B 19–12, OE̅ 9, T 11)
      match the drawing exactly; КР559ИП13 (drawn A 19–12, B 1–8, E1 9, E2 11 —
      the 8286/8287 footprint pattern) and К555ВЖ1 (74LS636 class: 8 data + 5
      check bits, 20 pins) keep the drawn pin numbers until their datasheets
      are at hand
- [x] Assemble the KiCad project from the netlist (`tools/mc1702_kicad.py` →
      `hw/mc1702/`); verified in KiCad 10.0.5 via `kicad-cli`: loads, ERC has
      no errors, KiCad's own netlist export equals the CSV (200 nets / 654 pins)
- [x] Interface bus closed: D6/D7, D12/D13, D22/D23, D14, D26/D27, D19–D21
      outputs, D36/D5 → IB, counter chain, D9 pinout (65 parts / 896 pins /
      254 nets in the CSV, KiCad netlist identical)
- [x] Interface glue on л.1 lower half read with `tools/mc1702_trace.py`:
      test-ROM enables, D6/D7 and D12/D13 enables, D8/D15 enables and all
      D15 outputs, D9 loops, D16/D17 gates, D39.1/D40.2/D31/D42 status
      logic, D36 DI3/DI6/C (72 parts / 943 pins / 266 nets, KiCad identical)
- [x] Decode/ready glue read with the tracer on л.1 top and strip 2 top:
      D18 commands, D25/D32/D81/D41/D80 chain, D47 output map corrected,
      strip-2 D25.3/D55/D16.4/D80.4 group
- [x] Sequencer transcribed from strips 2 and 3 (D39.1 clock tree, D43.1/D43.2,
      D48.1/D48.2, D29.2, D40.3/D40.4, D42.1/3/4, D41 gate, D80.3/5;
      state-bus labels as `SQ.n`; refs settled on strip 3); D44/D54 controller
      pinned (state bus -> PROM -> register -> MA.10-17); CAS split by byte
      read off the grid; D45/D46 pinned (row A1-A8 / column A10-A17 -> MA.1-8,
      C <- R15, OE lines open); MA.9 = D17.1/D17.4 wired-AND; step counter
      D2.2 -> SQ.1-3
- [x] Long top runs traced on strips 1–2 (sequencer clock = D4 OSC, ready
      chain D25/D55/D81 → D4 AEN2/RDY2, D43.1 D, D39.1 D = D2.2 clock)
- [x] ТО Таблица 1 (D44 microcode, 512 bytes) transcribed to `mc1702-d44-prom.txt`;
      structure cross-checked against the CAS/byte-select wiring
- [x] Passives (R1–R32, C1–C66, BQ1) and jumpers S1–S5 in the netlist with ПЭ3
      values; S1–S5 = slot-address switches per ПЭ3
- [ ] Still open after the tracer passes: sources of D39.1 D / D2.2 clock,
      D4 AEN1/RDY1/RES̅ far ends, D45/D46 OE (48/49 expected), state-bus bits
      4/7/8/9/10, D54 C, D29.2 B, D35.2 and the S4/S5 area (D36.3/5/6), Y2,
      XS1.25, D24.4 in/out, D40.2/D40.3/D32.4 inputs from the top runs
