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
|   1   | i8035 CPU emulator (TDD)               | done        |
|   2   | i8243 port expander (TDD)              | done        |
|  3a   | Wiring research, dasm, ENT0 CLK fix    | done        |
|  3b   | Firmware-driven backend (skeleton)     | next        |
|  3c   | Wire matrix, T1 latch, UART RX/TX      | pending     |
|  3d   | Switch ms7004 facade to fw backend     | pending     |
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

#### Commit 4 (2026-04-27) — timer / counter + interrupts

Implemented opcodes (covered by tests):
- timer/counter:  MOV T,A (`0x62`), MOV A,T (`0x42`), STRT T (`0x55`),
  STRT CNT (`0x45`), STOP TCNT (`0x65`), JTF (`0x16`)
- interrupts:     EN I (`0x05`), DIS I (`0x15`), EN TCNTI (`0x25`),
  DIS TCNTI (`0x35`), JNI (`0x86`), RETR (`0x93`)

Timer mode: every machine cycle accumulates into a 5-bit prescaler;
overflow (32 cycles) bumps T.  Counter mode samples T1 once per
`step` and bumps T on a high→low transition.  Either way, T rolling
FF→00 latches TF.

Interrupt model: at the top of `step`, before the fetch, check pending
sources (external INT pin > timer TF, MCS-48 priority order); if `ie`
or `tie` enables the corresponding source and `in_irq` is false,
push the return slot CALL-style and jump to vector 003 or 007.
`in_irq` blocks nested entry until RETR clears it.  Acknowledging
the timer interrupt also clears TF (matches the standard MCS-48
behaviour).

10 new tests, 27 assertions; total i8035 suite is 54 cases / 129
assertions.  Full suite 239/239.

Test gotcha discovered while writing the RETR test: an ISR that does
`MOV PSW,A` clobbers the SP bits in PSW and the subsequent RETR
pops from the wrong stack slot.  ISRs must use bit-level PSW ops
(CLR/CPL C, F0) or save/restore PSW manually.  Our test now uses
`CLR C` to flip the flag without touching SP.

#### Commit 5 (2026-04-27) — remaining opcodes; phase 1 complete

Implemented opcodes (covered by tests):
- exchanges:    XCH A,Rn (`0x28..2F`); XCH A,@Rn (`0x20..21`);
  XCHD A,@Rn (`0x30..31`) — only swaps low nibbles
- DJNZ Rn,addr (`0xE8..EF`) — decrement-and-branch loop primitive
- JBb addr (`0x12,32,52,72,92,B2,D2,F2`) — branch on accumulator
  bit b (b extracted from bits 7..5 of opcode)
- pin tests:    JT0 (`0x36`), JNT0 (`0x26`), JT1 (`0x56`),
  JNT1 (`0x46`)
- ROM probes:   MOVP A,@A (`0xA3`); MOVP3 A,@A (`0xE3`); JMPP @A
  (`0xB3`)
- external mem: MOVX A,@Rn (`0x80..81`); MOVX @Rn,A (`0x90..91`) —
  routed through the BUS port_read / port_write callbacks so a host
  can model XRAM if needed (ms7004 doesn't, but kept generic)
- bank select:  SEL MB0 (`0xE5`), SEL MB1 (`0xF5`) — flag-only on
  our 2 KB image

15 new tests, 22 assertions; total i8035 suite is 69 cases / 151
assertions.  Full suite 254/254.

The unimplemented set is now down to ENT0 CLK (`0x75`, output T0
as clock — peripheral feature not used by ms7004) plus a handful of
documented-undefined slots.  Phase 1 is closed.

### Phase 2 (2026-04-27) — i8243 port expander

Status: done in a single commit.

`core/include/ms0515/i8243.h` + `core/src/i8243.c` (~70 LOC of
implementation) plus `tests/test_i8243.cpp` (9 cases, 21 assertions).

API:
- `i8243_p2_write(low_nibble)` — host-CPU side: stash whatever the
  CPU drove on P2[3:0]
- `i8243_prog(level)` — falling edge latches command + port from
  the stashed nibble; rising edge latches data into the addressed
  port for WRITE/ORLD/ANLD; READ command opens a drive window
- `i8243_p2_read()` — what the expander currently presents on
  P2[3:0]; `latch & input` during a READ window, `0xF` otherwise
- `i8243_get_port(p)` / `i8243_set_input(p, v)` — host-side
  inspection of the latch and injection of an external pull-down

Quasi-bidirectional model: `latch & input` for reads matches real
silicon — the host can pull lines low against a high latch but
cannot raise a line the latch holds low.

Wiring with i8035 happens in phase 3.

### Phase 3a (2026-04-27) — wiring research before coding

Status: done.

Outputs:
- `docs/kb/MS7004_WIRING.md` — full pin / matrix / serial spec
  derived from MAME `src/mame/shared/ms7004.cpp` (data only, not code).
- `tools/dasm8048.py` — original MCS-48 disassembler, ~250 LOC, used
  to read the firmware ROM and confirm the wiring assumptions.
- `i8035.c` — added ENT0 CLK (0x75) as a documented no-op + regression
  test.  Firmware issues it once at PC=134H during init; without this
  the CPU would trip the unimplemented-opcode assert before the main
  loop ever starts.

Firmware roadmap (addresses confirmed by disassembly):
- `000H`: reset → `JMP 133H`
- `003H`: external INT vector → `JMP 400H` (host-command receiver)
- `007H`: timer ISR — saves A at RAM[3AH], decrements R3, reloads R6
- `0C2H`: 2-instruction delay loop (`DJNZ R4 / DJNZ R5`) — used by
  every TX bit, every RX bit, and the speaker beep
- `0DDH`/`0E4H`: speaker beep — toggles P1[3] at ~2.5 kHz × 254
- `0F6H`: bit-mask helper — sets bits in [22H] to mirror P2 latch
- `115H`: 8-bit RX assembly (sample INT, rotate into R7)
- `133H`: main entry — `EN TCNTI`, init P1/P2 latches, then big poll loop
- `400H`: host-command ISR — branches on R7 against 0x23/0x11/0x13/
  0x1B/0xA1/0x99/0xA7 etc., matching exactly the table our existing
  hand-rolled `ms7004_host_byte` switch uses.  Confirms our
  command-set is authentic.

Data-table region: 380H..3F1H — disassembler reports several "DB"
bytes there; cluster is too tight to be code, almost certainly a
scancode lookup table read via MOVP/MOVP3.

T0 pin: never sampled by firmware (no `JT0`/`JNT0` anywhere), so
ENT0 CLK at 134H is functionally a no-op for ms7004.

### Phase 3b — firmware backend skeleton (next)

Build a parallel `ms7004_fw` translation unit that does NOT replace
the existing `ms7004.c` yet.  Goal: prove the firmware boots and
reaches a quiescent poll loop without crashing.

Steps:
- New `core/include/ms0515/ms7004_fw.h` + `core/src/ms7004_fw.c`.
- `ms7004_fw_t` carries `i8035_t cpu`, `i8243_t exp`, `uint8_t matrix[16]`,
  a TX-bit reassembler (sample P1[7] every 64 cyc when start bit seen),
  an RX-bit driver (push host bytes by holding INT low for 64 cyc/bit),
  and the firmware ROM blob loaded by `ms7004_assets.c`.
- Stub callbacks: port_read returns whatever P1/P2 latch holds, T1
  returns the cached keylatch (initially 0 = no key), INT mirrors the
  RX bit driver.
- `ms7004_fw_step(cycles)` runs the CPU forward.
- Test: load ROM, step ~10 000 instructions (≈ 30 ms wall), assert PC
  ends up inside the main loop region (133H..200H) and no assert fired.

### Phase 3c — matrix + UART wiring

Steps:
- Implement `ms7004_fw_press(col, row, down)` that sets `matrix[col]`
  bits and recomputes the cached keylatch on every `i8243` write.
- Map `ms7004_key_t` → (col, row) via a static table built from
  `MS7004_WIRING.md`.
- TX reassembler: detect start bit on P1[7], sample 8 bits at 64-cyc
  intervals, push to `kbd_push_scancode`.
- RX driver: take a queue of host→keyboard bytes; for each byte hold
  INT low for one bit, then drive each data bit on INT, then stop.
- Test: press F1 at boot, step long enough for one TX byte to assemble,
  assert it equals scancode 0o127 (F1 LK201 code).

### Phase 3d — switch facade

Replace the contents of `ms7004.c` with a thin wrapper that delegates
to `ms7004_fw`, keeping the existing `ms7004_*` public API.  Phase 4
then reconciles `test_keyboard_emulated` expectations against whatever
the real firmware produces (our state machine had OSK/case-toggle
deviations that the firmware does not implement — those move into the
frontend per the existing `[DEVIATE n]` notes in ms7004.c).
