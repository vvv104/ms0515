# RAM Disk — Expansion Board (EX0:)

## Overview

Optional expansion board for the Elektronika MS 0515 that adds:
- **512 KB RAM disk** (1024 RT-11 blocks) — electronic disk via EX.SYS driver
- **RS-232C serial interface** — two channels, 110-19200 baud

The RAM disk is accessed as device `EX0:` in RT-11 via the EX.SYS device driver.
It is not a system resource — it's a fast data transfer medium.

## Hardware Components

| Chip | Function |
|------|----------|
| D2: КР580ВВ55А | 8255 PPI — address setup and control |
| D3: КР580ВИ53 | Timer — baud rate generator for serial |
| D14: К555ИЕ19 | 8-bit binary counter (MA00-MA07) |
| D19-D34: 16× КР565РУ7Г | 256Kx1 DRAM — total 512 KB |
| D4,D5: КР1102АП15 | RS-232 line drivers |
| D6: К555АП6 | Data bus buffer |

Sources: `reference/docs/ext/l1_1.doc.txt` (BOM), `l2-l5` (schematics).

## Memory Architecture

19-bit address space = 512 KB, organized as 2 banks × 256 KB.

```
Bit:  18    17:16    15:08       07:00
      |      |        |           |
    БАНК   MA17:16  Port A      Counter
    (PB2)  (PB1:0)  (MA15:08)   (MA07:00)
```

The 16 DRAM chips (D19-D34) are split into two groups:
- CAS0: D19,D21,D23,D25,D27,D29,D31,D33 (bank 0)
- CAS1: D20,D22,D24,D26,D28,D30,D32,D34 (bank 1)

Each group provides 8 data bits (ДД00-ДД07), giving byte-wide access.

## I/O Address Map

All addresses relative to the PDP-11 I/O space:

| Address (octal) | R/W | Function |
|-----------------|-----|----------|
| 0177510 | R | PPI Port A read (MA08-MA15) |
| 0177512 | R | PPI Port B read (control + MA16-MA18) |
| 0177514 | R | PPI Port C read (serial signals) |
| 0177516 | R | PPI control word read |
| 0177530 | W | PPI Port A write (MA08-MA15) |
| 0177532 | W | PPI Port B write (control + MA16-MA18) |
| 0177534 | W | PPI Port C write (serial signals) |
| 0177536 | W | PPI control word write |
| 0177550 | R/W | RAM data port (auto-increments counter) |
| 0177570 | R/W | RAM data port alias — mirror of 0177550 |

The on-board address decoder ignores address bit 4 (0x10) for the data port,
so `0177570` is a functional mirror of `0177550`.  This is important: the
EX.SYS write routine writes the **first byte** of every 256-byte page to
`0177570` and the remaining 255 bytes to `0177550`.  A decoder that does
not mirror these addresses will silently drop the first byte of every page
and corrupt all writes (format fails with the disk ending up full of the
initial DRAM noise at every offset 0 mod 256).

Note: PPI uses split read/write addresses, same as the main board timer.

## PPI Port B Bit Definitions

Port B is output-only for RAM disk operation:

| Bit | Name | Function |
|-----|------|----------|
| 0 | MA16 | Address bit 16 |
| 1 | MA17 | Address bit 17 |
| 2 | БАНК | Bank select (MA18) |
| 3 | ДОП ОЗУ | Additional RAM flag (board present indicator) |
| 4 | — | Not used |
| 5 | СБРОС | Reset: sets counter (MA00-MA07) to 0 |
| 6 | — | Not used |
| 7 | СТАРТ | Start: enables data transfer |

## Data Transfer Protocol

Derived from EX.SYS driver disassembly:

### Setup
1. Configure PPI: Port A = output, Port B = output
2. Write page address (MA08-MA15) to Port A at 0177530
3. Write `0xA0` (СТАРТ | СБРОС) to Port B at 0177532 — resets counter
4. Write `address_high | 0x80` to Port B — sets MA16-MA18 + СТАРТ

### Read Transfer (256 bytes per page)
```
MTPS  #340           ; disable interrupts
loop: MOVB @#177550, (R3)+  ; read byte, auto-increment counter
      SOB   R1, loop         ; repeat 256 times
MTPS  #000           ; re-enable interrupts
```

### Write/Format Transfer (256 bytes per page)
EX.SYS uses an asymmetric pattern for writes: the first byte is written to
the mirror address `0177570`, then 255 bytes to `0177550`.  The two
addresses behave identically (bit 4 is ignored by the decoder), but the
emulator must map both to the same handler.
```
MTPS  #340                 ; disable interrupts
      MOVB  (R3)+, 20(R5)  ; R5=177550 → first byte to 0177570
      DEC   R0
      BEQ   done
loop: MOVB  (R3)+, (R5)    ; remaining bytes to 0177550
      SOB   R1, loop       ; loop 255 times
done: MTPS  #000
```
Reads use `0177550` directly for all 256 bytes.

### Multi-page transfer
After 256 bytes, increment the page (Port A) and repeat.
For addresses crossing a 64 KB boundary, update Port B too.

## Timing Constraints

From the hardware documentation (`reference/docs/ext/l6_1.doc.txt`):

- Each bus access must complete within **100 μs** (DRAM refresh timing)
- The driver disables interrupts during page transfers to meet timing
- Format time: ~12 sec for full 512 KB
- Speed comparison: ~10× faster than floppy disk, ~1 sec additional
  for OS formatting overhead

## Detection

The EX.SYS driver detects the board by:
1. Writing a known pattern to the data port
2. Reading it back
3. Checking for non-uniform data (distinguishes real DRAM from bus float)

Bus float (no board present) returns 0xFF. Zero-filled memory (wrong
initialization) returns 0x00. Real DRAM powers on with random content.

## Implementation

- Header: `core/include/ms0515/ramdisk.h`
- Source: `core/src/ramdisk.c`
- Integration: `core/src/board.c` routes I/O to ramdisk module
