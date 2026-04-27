# MS7004 firmware-port journal

Multi-session work log for replacing the high-level state machine in
`emu/core/src/ms7004.c` with hardware-faithful emulation that runs the
real keyboard firmware (`mc7004_keyboard_original.rom`, 2 KB, CRC32
`69fcab53`, SHA1 `2d7cc7cd182f2ee09ecf2c539e33db3c2195f778`).

Architecture: Intel 8035 @ 4.608 MHz + Intel 8243 port expander.
External 2 KB program ROM, 64 bytes internal RAM, no internal mask
ROM. Reference: MAME `src/mame/shared/ms7004.cpp` for the wiring,
`src/devices/cpu/mcs48/` for the instruction set encoding (used as
documentation only — code is original, written from the Intel MCS-48
programmer's manual).

## Status

| Phase | Subject                                | Status      |
|-------|----------------------------------------|-------------|
|   1   | i8035 CPU emulator (TDD)               | in progress |
|   2   | i8243 port expander (TDD)              | pending     |
|   3   | Replace ms7004.c body                  | pending     |
|   4   | Reconcile tests vs firmware            | pending     |
|   5   | Frontend cleanup, docs                 | pending     |

## Decisions

- ROM is bundled as `emu/assets/rom/mc7004_keyboard_original.rom` (gray-zone
  preservation, same policy as `ms0515-roma.rom`).
- 8035 core lives next to other CPU code in `emu/core/src/i8035.c` —
  not under `core/src/kbd/` — because it might one day be reused for
  another peripheral. No suffix; the name `i8035` is unambiguous.
- 8243 ditto: `emu/core/src/i8243.c`.
- Reading MAME's `ms7004.cpp` for the keyboard matrix wiring (which P
  pin connects to which key) is allowed — that is data, not algorithm.
  The ms7004 reference is also documented in the original ms7004
  technical documentation (per user).

## Open questions

- Bus wiring details: BUS port (P0) usage, T0/T1 input lines on ms7004,
  exact PROG-line clocking. Will be answered while reading
  `mame/src/mame/shared/ms7004.cpp` during phase 3.
- Whether `repeat_*` fields in `ms7004_t` survive: probably not — the
  firmware does its own auto-repeat via the timer.

## Per-phase log

### Phase 1 — i8035 CPU emulator

Status: in progress.

#### Commit 1 (2026-04-27) — skeleton + control flow + data movement

Implemented opcodes (covered by tests):
- control flow:  JMP page0..7 (`0x?4`), CALL page0..7 (`0x?4` upper),
  RET (`0x83`), JZ (`0xC6`), JNZ (`0x96`), JC (`0xF6`), JNC (`0xE6`)
- data movement: MOV A,#imm (`0x23`); MOV Rn,#imm (`0xB8..BF`);
  MOV @Rn,#imm (`0xB0..B1`); MOV A,Rn (`0xF8..FF`); MOV Rn,A
  (`0xA8..AF`); MOV A,@Rn (`0xF0..F1`); MOV @Rn,A (`0xA0..A1`)
- simple ops: NOP, CLR A, CPL A, SWAP A, INC A, DEC A, INC Rn,
  DEC Rn, INC @Rn
- bank / PSW: SEL RB0/RB1, MOV A,PSW, MOV PSW,A

18 tests, 43 assertions. Total suite 203/203 green.

Boot test had to be taught to filter by `ms0515-` prefix so it
doesn't try to use the keyboard ROM as a system ROM.

#### Commit 2 (2026-04-27) — arithmetic + logic + rotates + flag ops

Implemented opcodes (covered by tests):
- arithmetic: ADD A,{#imm,Rn,@Rn} (`0x03`, `0x68..6F`, `0x60..61`);
  ADDC A,{#imm,Rn,@Rn} (`0x13`, `0x78..7F`, `0x70..71`); both update
  CY (carry-out from bit 7) and AC (carry-out from bit 3 to 4)
- DA A (`0x57`) — decimal adjust after BCD addition; low-nibble
  correction triggers on `(A & 0x0F) > 9 || AC`, high-nibble
  correction on `A > 0x9F || CY` and sets CY
- logic:    ANL/ORL/XRL A,{#imm,Rn,@Rn} (`0x53/43/D3` for #imm,
  `0x5x/4x/Dx` for Rn, `0x50-51/40-41/D0-D1` for @Rn).  CY and
  AC explicitly preserved by the test
- rotates:  RL A (`0xE7`), RR A (`0x77`), RLC A (`0xF7`),
  RRC A (`0x67`) — RLC/RRC route bits through CY; RL/RR don't
- flag ops: CLR/CPL C (`0x97`/`0xA7`), CLR/CPL F0 (`0x85`/`0x95`),
  CLR/CPL F1 (`0xA5`/`0xB5`)
- branches: JF0 (`0xB6`), JF1 (`0x76`)

19 new tests, 80 assertions total in i8035 suite. Full suite 222/222.

Test gotcha discovered while writing: `MOV @R0,#xx` with a pointer
≥ 0x40 wraps to a low RAM address (8035 RAM is only 64 bytes, so
indirect addresses are masked to 6 bits) and silently overwrites Rn
itself. Tests now use 0x30 — well inside the user RAM area at
0x20..0x3F.

#### Commit 3 (2026-04-27) — port IO + 8243 expander interface

Implemented opcodes (covered by tests):
- port IO:  INS A,BUS (`0x08`), IN A,P1 (`0x09`), IN A,P2 (`0x0A`);
  OUTL BUS,A (`0x02`), OUTL P1,A (`0x39`), OUTL P2,A (`0x3A`);
  ANL/ORL BUS|P1|P2,#imm (`0x88`/`0x89`/`0x8A` for OR;
  `0x98`/`0x99`/`0x9A` for AND).  P1/P2 latch in `cpu->p1_out` /
  `cpu->p2_out`; every change is mirrored to the host via the
  `port_write` callback so peripherals can react.
- 8243 expander: MOVD A,P4..P7 (`0x0C..0F`); MOVD P4..P7,A
  (`0x3C..3F`); ORLD P4..P7,A (`0x8C..8F`); ANLD P4..P7,A
  (`0x9C..9F`).  Each transaction drives a 2-bit command (00 read /
  01 write / 10 OR / 11 AND) plus a 2-bit port number on P2[3:0],
  strobes PROG low via the `prog` callback, exchanges the data
  nibble (read = inbound from P2, write/OR/AND = outbound), then
  raises PROG.

7 new tests, 22 assertions added; total i8035 suite is 44 cases /
102 assertions.  Full suite 229/229.

Test-fixture upgrade: Cpu now carries a programmable input bank
(`in_bus`/`in_p1`/`in_p2`/`pin_t0`/`pin_t1`/`pin_int`), records the
last byte written to each port and counts PROG transitions — enough
to assert wire-level behaviour of port-driving instructions without
a real expander attached.

#### Commit 4 — timer / counter + interrupts

Plan:
- MOV T,A; MOV A,T; STRT T; STRT CNT; STOP TCNT.
- Prescaler ticks per machine cycle; counter increments on T1
  falling edges (host signals via callback).
- TF flag, JTF.
- EN I, DIS I, EN TCNTI, DIS TCNTI, JNI.
- Interrupt vectoring: external → 0x003, timer → 0x007. CALL-style
  push (no PSW upper), in_irq blocks nesting, RETR clears in_irq.

#### Commit 5 — remaining opcodes

Plan:
- MOVP A,@A — page-local ROM read; MOVP3 A,@A — page-3 ROM read.
- JMPP @A — indirect jump within page.
- DJNZ Rn,addr.
- JBb (8 encodings) — jump on accumulator bit.
- XCH A,Rn / XCH A,@Rn / XCHD A,@Rn.
- MOVX A,@Rn / MOVX @Rn,A — external RAM (only relevant if
  firmware uses any; ms7004 might not).
