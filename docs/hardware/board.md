# Board — MS0515 System Module Integration

## Overview

The board module (board.c) integrates all hardware components of the
Elektronika MS 0515 system module (NS4) and implements the I/O bus, timing,
and interrupt routing.

## System Architecture

```
  ┌──────────┐      ┌──────────┐      ┌──────────┐
  │   CPU    │──────│  Memory  │──────│  Video   │
  │KR1807VM1 │      │ 128K RAM │      │Controller│
  │ 7.5 MHz  │      │ 16K ROM  │      │ 16K VRAM │
  └────┬─────┘      │ 16K VRAM │      └──────────┘
       │            └──────────┘
       │  Bus (16-bit multiplexed address/data)
  ─────┼──────────────────────────────────────────
       │        │        │        │         │
  ┌────┴───┐ ┌──┴──┐ ┌──┴──┐ ┌──┴───┐ ┌───┴──┐
  │ System │ │Timer│ │ Kbd │ │Serial│ │ FDC  │
  │  Regs  │ │8253 │ │8251 │ │ 8251 │ │ 1793 │
  │ (8255) │ │     │ │     │ │      │ │      │
  └────────┘ └─────┘ └─────┘ └──────┘ └──────┘
```

## I/O Register Map

All addresses in octal.  Offsets are relative to 177400 base, in decimal hex.
The addresses are the technical description's; how the board really
decodes them - every register also at its address + 020, bit 3 the
module's - is the next section.

| Address range     | Offset | Device                          |
|-------------------|--------|---------------------------------|
| 177400 – 177437   | 0x00   | Memory Dispatcher register      |
| 177440            | 0x20   | Keyboard USART data: RX (read), TX (write) |
| 177442            | 0x22   | Keyboard USART status (read) / mode and command (write) |
| 177460, 177462    | 0x30   | The same two registers, write only (the T-11's read before the write finds no chip here) |
| 177500 – 177506   | 0x40   | Timer: ch 0–2, control (read and write) |
| 177510 – 177516   | 0x48   | RAM disk PPI, read side (when fitted - [ramdisk.md](ramdisk.md)) |
| 177520 – 177526   | 0x50   | Timer, write only (the ROM programs the speaker's counter 2 here, the baud clocks at 177500-506) |
| 177530 – 177536   | 0x58   | RAM disk PPI, write side        |
| 177540 – 177546   | 0x60   | MS7007 PPI: A rows latch, B the joystick (in), C in |
| 177550, 177570    | 0x68, 0x78 | RAM disk data port (and the mirror EX.SYS uses) |
| 177600            | 0x80   | System Register A (write)       |
| 177602            | 0x82   | System Register B (read)        |
| 177604            | 0x84   | System Register C (write)       |
| 177606            | 0x86   | PPI control word                |
| 177640 – 177646   | 0xA0   | FDC registers                   |
| 177700            | 0xC0   | Serial USART data: RX (read), TX (write) |
| 177702            | 0xC2   | Serial USART status (read) / mode and command (write) |
| 177720            | 0xD0   | Serial TX data, write only; HDCSR while the paravirtual HD is enabled ([hd.md](hd.md)) |
| 177722            | 0xD2   | Serial command, write only; HDDAT while the HD is enabled |
| 177770            | 0xF8   | Halt/timer service address      |

## The register decoding

The technical description lists two addresses for most registers - the
timer's counters at 177500-506 "for reading" and 177520-526 "for
writing", the USARTs' data at 177440 and 177460, 177700 and 177720.  The
schematic (3.858.420 Э3, sheet 2) shows what that is.  The register
decoder D80 (К555ИД7) takes address bits 5-7 and nothing else: it picks a
peripheral's 040-byte block (177440 the keyboard USART, 177500 the timer,
177540 the MS 7007 PPI, 177600 the system registers' PPI, 177640 the FDC,
177700 the serial USART), and every register of the block is selected at
its address and at that address with bit 4 set.  The strobes do the rest:

- **WR** on every chip is the bus's write strobe ДЗП.  A write at 177506
  is a write at 177526.  The ROM relies on it: it programs the keyboard's
  and the serial port's baud clocks, counters 0 and 1, through 177506,
  177500 and 177502 (ROM-B at 160070), and the speaker's counter 2 through
  177526 and 177524.
- **RD** on the two ВВ51s and the ВИ53 is ЧТ51, the read strobe ДЧТ gated
  by АД04 (D79.4, К555ЛЛ1): those chips are read with bit 4 *clear* only.
  The T-11 reads an address before it writes it (a MOV to 177526 reads
  177526 first), and at the "+020" half that read finds no chip - a write
  there cannot pop the USART's receive buffer or step the timer's
  LSB/MSB sequence.  That is why the technical description keeps the
  writes at +020.
- **RD** on the two ВВ55s and the ВГ93 is ДЧТ itself: they are read and
  written at both halves (177620 is Register A, 177660 the FDC status).

The owner's machine - the one the emulator models, memory-expansion
module and all - had a factory-fitted wire from АД03 (D92.9) into D80's
G2B in place of the select (the FPGA rebuild, `docs/findings.md`, "The
green wire on the owner's board"): with it an address with bit 3 set
selects none of the board's chips.  The board loses nothing (every
register it has sits at a bit-3-clear address) and the module gets that
half of the space: its PPI at 177510-516 and 177530-536, its data port at
177550 and 177570 ([ramdisk.md](ramdisk.md)).  Without the wire counter
0, the keyboard's baud clock, takes EX.SYS's writes to the module's PPI
at 177530-536 and the keyboard dies at the prompt - which the rebuilt
board without the wire does.

The emulator decodes as the wired machine (`io_decode` in `board.c`):
bit 4 is ignored for every write and for the PPIs' and the FDC's reads,
the USARTs and the timer are read with bit 4 clear only, and an address
with bit 3 set is left to the module.  `core/tests/test_io_decode.cpp`
pins each of these.  (It used to take the technical description's split
literally - the timer written at 177520-526 only and read at 177500-506
only - and dropped the ROM's programming of counters 0 and 1.)

## System Register A (177600) — Output

```
   7    6    5    4    3    2    1    0
 ┌────┬────┬────┬────┬────┬────┬────┬────┐
 │EROM│CASS│VD16│VD9 │SIDE│MOTR│ DS1│ DS0│
 └────┴────┴────┴────┴────┴────┴────┴────┘
```

| Bit | Name | Description                                       |
|-----|------|---------------------------------------------------|
| 0-1 | DS   | Floppy drive select (0–3)                         |
| 2   | MOTR | Motor on (active low: 0 = on)                     |
| 3   | SIDE | Side select: 1 = side 0 (the lower), 0 = side 1 (the upper).  On the NS4 schematic (`3.858.420 Э3` sheet 5) the bit, ПОВЕР H off D76's PA3, goes through D8 (К155ЛП9, a non-inverting open-collector buffer) to ВБП, contact 32 of the drive connector XP4 - the drive's side select, which picks side 1, the upper head, when pulled low (the TEAC FD-55 specification; the МС 5305 is its 80-track twin). |
| 4   | VD9  | LED VD9 control                                   |
| 5   | VD16 | LED VD16 control                                  |
| 6   | CASS | Cassette output signal                            |
| 7   | EROM | Extended ROM (1 = full 16 KB at 140000–177377)    |

## System Register B (177602) — Input

```
   7    6    5    4    3    2    1    0
 ┌────┬────┬────┬────┬────┬────┬────┬────┐
 │CSIN│ —  │ —  │RVK1│RVK0│DRDY│ DRQ│INTR│
 └────┴────┴────┴────┴────┴────┴────┴────┘
```

| Bit | Name | Description                                       |
|-----|------|---------------------------------------------------|
| 0   | INTR | FDC INTRQ inverted (0 = ready for command)        |
| 1   | DRQ  | FDC DRQ (1 = data byte ready)                     |
| 2   | DRDY | Drive ready inverted (0 = ready)                  |
| 3-4 | RVK  | The jumpers E3/E4: frame rate (00=50, 01=72, 1x=60 Hz) |
| 7   | CSIN | Cassette input signal                             |

## System Register C (177604) — Output

```
   7    6    5    4    3    2    1    0
 ┌────┬────┬────┬────┬────┬────┬────┬────┐
 │GATE│SDEN│TONE│VD17│HRES│ G  │ R  │ B  │
 └────┴────┴────┴────┴────┴────┴────┴────┘
```

| Bit | Name | Description                                       |
|-----|------|---------------------------------------------------|
| 0   | B    | Border color — blue component                     |
| 1   | R    | Border color — red component                      |
| 2   | G    | Border color — green component                    |
| 3   | HRES | Video resolution (0 = 320x200, 1 = 640x200)      |
| 4   | VD17 | LED VD17 control                                  |
| 5   | TONE | Tone control (speaker direct drive)               |
| 6   | SDEN | Sound enable (master gate)                        |
| 7   | GATE | Timer gate input to channel 2                     |

## PPI Control Word (177606)

When bit 7 = 1: mode selection word (port direction, mode 0/1/2).
When bit 7 = 0: bit set/reset for port C (bits 3-1 select bit, bit 0 = value).

Boot configuration: code 202 (octal) = mode 0, port B input, ports A/C output.

## Timing

| Clock domain     | Frequency  | Period     |
|------------------|------------|------------|
| CPU              | 7.5 MHz    | 133 ns     |
| Timer (PIT)      | 2 MHz      | 500 ns     |
| Video pixel      | 15 MHz     | 66.7 ns    |
| VBlank (50 Hz)   | 50 Hz      | 20 ms      |
| VBlank (60 Hz)   | 60 Hz      | 16.7 ms    |
| VBlank (72 Hz)   | 72 Hz      | 13.9 ms    |

The timer runs on an exact quarter-cycle accumulator: 15 quarter-cycles a
tick against 4 quarter-cycles a CPU clock, an average of 3.75 cycles/tick
with no drift (the "4 CPU cycles" figure is a separate, rounded divisor
the emulator uses only to pace the keyboard USART, not the timer itself).
The frame rate is the jumpers E3 and E4 (NS4 TO Table 6: both fitted
50 Hz; E4 alone 60 Hz, 260 lines of 64 us; E3 alone 72 Hz), and a program
reads them back as РВК1/РВК0, Register B bits 4-3 (Table 10), to know
its timer tick.  The schematic's sheet 1 lists four builds, none with
both jumpers (3.858.420 and -02 with E4, 60 Hz; -01 and -03 with E3,
72 Hz), and the assembly drawing's standard list fits E4 without E3; a
reading of the jumpers off the one photo of the owner's board is not
possible at its resolution.  The machines themselves say 50 Hz: the
timer interrupt (vector 100) is the frame sync and nothing else - D60.1,
a К555ТМ2 with the enable bit on D and КСИ on its clock - so an RT-11
monitor's clock tick is the frame, and the kits of two bureaus (Omega's
and OSA's) are generated for a 50-cycle clock; the machines kept time
in banks, where a clock gaining 4.8 hours a day at 60 Hz would not have
lasted a week (the owner's own machine came from one).  So the bureaus
fitted both jumpers for the 50 Hz monitors they shipped, and the
emulator's board starts at **50 Hz, 150,000 CPU cycles per frame**
(`dip_refresh` 0 in `board.c`; it started at 60 Hz for a day in 2026-11
on the drawing's standard list, and a saved state keeps the rate it was
saved with).  The hosts pace the machine by `Emulator::frameMs()`, 20
ms a frame, never by a constant of their own.

What the rate does to the software: the timer interrupt is the monitors'
clock tick, and the RT-11 kits for this machine are generated for a
"50-cycle" clock and read nothing off Register B (ms0515-devel,
`projects/rt11/monitor/README.md`, "72 ticks a second"): their TIME is
right at 50 Hz and 20 % fast at 60.  `lib/tests/test_clock_time.cpp`
counts the monitor's minute in its 3000 ticks.  Tests that wait for a
boot or a command wait in frames: a budget of 720 frames is 14.4 s at
50 Hz (it was written as 12 s at 60 and is left as it is).

## Interrupt Routing

Only three sources are actually wired to an interrupt in the emulator -
Timer, Keyboard MS7004 and Monitor (VBlank):

| Source         | IRQ line | Vector | Priority | Gating                   |
|----------------|----------|--------|----------|--------------------------|
| Timer          | 11       | 0100   | 6        | Dispatcher bit 9, VBlank |
| Keyboard MS7004| 5        | 0130   | 5        | USART RxRDY + RxEN       |
| Monitor (VBlank)| 2       | 0064   | 4        | Dispatcher bit 8         |

When several are pending, the emulator takes them in the board's
encoder order but for the monitor, which comes first: monitor, keyboard,
timer (`kEncoderOrder` in `src/core/src/cpu.c`; why, in
[`../kb/KNOWN_ISSUES.md`](../kb/KNOWN_ISSUES.md), the 2026-09-19 entry).

Serial RX/TX and the MS7007 key matrix are in the NS4 tech description but
raise no interrupt in this emulator: the serial port is a stub that
accepts and discards, and no software at hand scans a matrix through the
MS7007 PPI (it is wired here only as the joystick input port).

## Boot Sequence

1. CPU reads mode register → start address 172000
2. CPU begins fetching instructions directly at 0172000; PSW starts at
   0340 (priority 7) - it is not loaded from a PC/PSW pair at 172000/172002
3. BIOS programs PPI (code 202), Reg C (border white, no sound, 320x200)
4. BIOS programs timer channels 0 and 1 (4800 baud)
5. BIOS initializes keyboard USART (3 zeros + reset + mode + command)
6. Self-tests: CPU, RAM, VRAM, keyboard, FDC, sound
7. Splash screen, then attempt boot from floppy (track 1, sector 1 → 000000)

## Sources

- NS4 technical description (3.858.420 TO), sections 4.1–4.10, Appendix 1
- MAME driver: https://github.com/mamedev/mame/blob/master/src/mame/ussr/ms0515.cpp
