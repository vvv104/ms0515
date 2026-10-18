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

| Address range     | Offset | Device                          |
|-------------------|--------|---------------------------------|
| 177400 – 177437   | 0x00   | Memory Dispatcher register      |
| 177440            | 0x20   | Keyboard RX data (read)         |
| 177442            | 0x22   | Keyboard status/command         |
| 177460            | 0x30   | Keyboard TX data (write)        |
| 177462            | 0x32   | Keyboard command (write)        |
| 177500 – 177506   | 0x40   | Timer read (ch 0–2, control)    |
| 177520 – 177526   | 0x50   | Timer write (ch 0–2, control)   |
| 177540 – 177546   | 0x60   | MS7007 PPI: A rows latch, B the joystick (in), C in |
| 177600            | 0x80   | System Register A (write)       |
| 177602            | 0x82   | System Register B (read)        |
| 177604            | 0x84   | System Register C (write)       |
| 177606            | 0x86   | PPI control word                |
| 177640 – 177646   | 0xA0   | FDC registers                   |
| 177700            | 0xC0   | Serial RX data (read)           |
| 177702            | 0xC2   | Serial status/command           |
| 177720            | 0xD0   | Serial TX data (write)          |
| 177722            | 0xD2   | Serial command (write)          |
| 177770            | 0xF8   | Halt/timer service address      |

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
| 3   | SIDE | Side select: `0 = fdc_select side 1, 1 = fdc_select side 0` in `board.c`'s own reading of this bit ("active low"); the physical upper/lower labelling of side 0/1 is unresolved - `board.h`'s and `floppy.h`'s own header comments give it the *opposite* way round (`1 = upper`) from this table.  Needs the NS4 schematic (section 4.8) to settle. |
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
| 3-4 | RVK  | DIP switches: frame rate (00=50, 01=72, 10=60 Hz)|
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
At 50 Hz: 150,000 CPU cycles per frame.

## Interrupt Routing

Only three sources are actually wired to an interrupt in the emulator -
Timer, Keyboard MS7004 and Monitor (VBlank):

| Source         | IRQ line | Vector | Priority | Gating                   |
|----------------|----------|--------|----------|--------------------------|
| Timer          | 11       | 0100   | 6        | Dispatcher bit 9, VBlank |
| Keyboard MS7004| 5        | 0130   | 5        | USART RxRDY + RxEN       |
| Monitor (VBlank)| 2       | 0064   | 4        | Dispatcher bit 8         |

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
- MAME driver: https://github.com/mamedev/mame/blob/master/src/mame/drivers/ms0515.cpp
