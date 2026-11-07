# Timer — KR580VI53 (Intel 8253 Programmable Interval Timer)

## Overview

The MS0515 uses a KR580VI53 (КР580ВИ53), a Soviet clone of the Intel 8253
PIT.  It contains three independent 16-bit down-counters, clocked at 2 MHz.

## Channel Assignments

| Channel | Function                    | Mode | Initial value  |
|---------|-----------------------------|------|----------------|
| 0       | Keyboard baud rate (4800)   | 3    | 032 (26 dec)   |
| 1       | Printer baud rate (4800)    | 3    | 032 (26 dec)   |
| 2       | Speaker tone / timing       | 3    | (programmed)   |

The 2 MHz input clock divided by 26 yields ~76.9 kHz.  The i8251 USART
divides this by 16 (async mode 1/16) to produce the 4800 baud rate.

## Register Addresses

The technical description gives the timer a read address and a write
address for each register:

| Channel     | Read address | Write address |
|-------------|--------------|---------------|
| Channel 0   | 177500       | 177520        |
| Channel 1   | 177502       | 177522        |
| Channel 2   | 177504       | 177524        |
| Control word| 177506       | 177526        |

The chip itself has four registers.  The board's decoder ignores address
bit 4, and the chip's WR takes the bus's write strobe at any address, so
177500-506 are written exactly as 177520-526 are - the ROM programs
counters 0 and 1 through 177506/177500/177502 and counter 2 through
177526/177524.  Its RD is the read strobe gated by АД04, so the chip is
read at 177500-506 only: at the "+020" addresses the T-11's read before a
write finds no chip, and a write there cannot disturb a latched count or
the LSB/MSB sequence.  [board.md](board.md), "The register decoding".

## Control Word Format

```
   7    6    5    4    3    2    1    0
 ┌────┬────┬────┬────┬────┬────┬────┬────┐
 │ SC1│ SC0│ RW1│ RW0│ M2 │ M1 │ M0 │ BCD│
 └────┴────┴────┴────┴────┴────┴────┴────┘
```

| Bits | Name | Description                                      |
|------|------|--------------------------------------------------|
| 7-6  | SC   | Channel select: 00=ch0, 01=ch1, 10=ch2, 11=ignored (the 8254 read-back function; this is an 8253, and `timer_write` skips SC=11 as a no-op) |
| 5-4  | RW   | R/W mode: 00=latch, 01=LSB, 10=MSB, 11=LSB+MSB  |
| 3-1  | M    | Operating mode (0–5)                              |
| 0    | BCD  | 0=binary, 1=BCD counting                         |

## Operating Modes

| Mode | Name                        | Description                          |
|------|-----------------------------|--------------------------------------|
| 0    | Interrupt on terminal count | OUT low, goes high when count = 0    |
| 1    | Programmable one-shot       | Triggered by GATE rising edge        |
| 2    | Rate generator              | OUT low for 1 cycle, reloads         |
| 3    | Square wave generator       | decrements by 2; an odd reload is not exactly 50/50 - the high phase is `ceil(N/2)`, the low `floor(N/2)` (reloaded even on purpose - see `core/tests/test_timer.cpp`, "mode 3: an odd divisor gives both even and odd counts") |
| 4    | Software-triggered strobe   | OUT pulses low for 1 cycle at zero   |
| 5    | Hardware-triggered strobe   | Like mode 4, triggered by GATE       |

Modes 6 and 7 are aliases for modes 2 and 3 respectively.

## Timer Interrupt

The timer generates an interrupt at vector 0100, priority 6.  This interrupt
is **gated by bit 9** of the Memory Dispatcher register (177400) and
**strobed by VBlank**, so it fires once per video frame when enabled.

## Speaker Connection

Channel 2 output drives the speaker through System Register C:
- Bit 7: GATE input to channel 2
- Bit 6: Sound enable (master gate)
- Bit 5: Tone control ("changing this bit changes the speaker's tone" in
  the tech description) - not modelled: every program known to make sound
  is explained without it.  The Spectrum ports bit-bang the speaker by
  alternating 0x60 and 0x80 - bit 6 toggled with the gate closed (OUT held
  high), bit 5 going with it (see `board.c`, `core/tests/test_sound.cpp`)

The boot sequence plays ascending tones (E1, G#1, B1, E2) using channel 2
in mode 3 with programmed divisors.

## Boot Configuration

The ROM (ROM-B at 160070, ROM-A at 160060) programs the baud clocks
through the low half of the block:

```
177506 ← 026 (octal)  → Channel 0, LSB only, mode 3, binary
177500 ← 032 (octal)  → Channel 0 reload value = 26 decimal
177506 ← 126 (octal)  → Channel 1, LSB only, mode 3, binary
177502 ← 032 (octal)  → Channel 1 reload value = 26 decimal
```

and the speaker's channel 2 through 177526/177524 (at 174500).

## Sources

- Intel 8253/8254 datasheet (Order Number 231164-005)
- NS4 technical description (3.858.420 TO), section 4.9
- OSDev Wiki: Programmable Interval Timer
