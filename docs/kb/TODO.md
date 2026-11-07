# TODO

Tracked work items that are NOT bugs.  Issues with concrete defects
and/or workarounds live in `KNOWN_ISSUES.md`; this file collects
deferred improvements, refactoring ideas and experiments.

## Hardware-accurate MS7004 keyboard emulation

### Why

`core/src/ms7004.c` is a high-level state machine that approximates
what the real MS7004 keyboard microcontroller does — matrix scan,
debounce, autorepeat, modifier tracking, scancode emission over
UART.  It is byte-accurate at the OS-visible interface (verified
against MAME 2026-04-26 — see the Mihin entry in `KNOWN_ISSUES.md`)
but duplicates logic that already exists in well-known firmware.

When MAME's `ms0515` ROM set was located (2026-04-26) it became
clear that there is a public dump of the **authentic MS7004
firmware** we could just run inside an emulated CPU instead:

- `mc7004_keyboard_original.rom`, 2 KB
- CRC32 `69fcab53`
- SHA1 `2d7cc7cd182f2ee09ecf2c539e33db3c2195f778`

MAME pairs it with an emulated **Intel 8035** at 4.608 MHz plus an
**Intel 8243** port expander for column scanning.

### Goal

Replace the state-machine `ms7004.c` with a faithful hardware
emulation that runs the real firmware.

Outcomes:

1. Any future scancode question is answered by looking at the
   firmware itself, not by reverse-engineering our model.
2. Subtle behaviours we currently approximate (auto-repeat timing,
   ALL-UP semantics, ФКС latch) come for free, exactly as the real
   keyboard does them.
3. We lose the few high-level deviations we made for cap-spec
   correctness (e.g. local ФКС handling, shift-immune ШЩЭЧ
   wrapping) — those would have to be re-evaluated against
   firmware behaviour and either kept as host-side overrides or
   reconciled.

### Cost / scope (rough sketch)

1. Add an Intel 8035 / 8048-family CPU core to `core/src/`.
   Existing public 8048 emulators (e.g. MAME's `i8039` device,
   small standalone implementations on GitHub) are good
   references; the instruction set is small (~96 opcodes).
   Pure C implementation, ~1500 lines.
2. Add an Intel 8243 port expander emulator.  Trivial state
   device, ~100 lines.
3. Wire host keypress events to the matrix-scan inputs instead
   of feeding scancodes into the existing high-level model.
4. Wire the firmware's UART output line into our existing
   `kbd_push_scancode` path, so the rest of the system sees
   bytes coming from a real-firmware source rather than from
   the state machine.
5. Bundle `mc7004_keyboard_original.rom` as an asset in
   `src/assets/rom/`.  The licensing is the same kind of
   gray-zone preservation as the system ROMs we already ship.
6. Update tests:
   - `src/core/tests/test_ms7004.cpp` — current unit tests assert specific
     byte sequences for each key event; many will need to be
     reframed in terms of expected firmware output (which may
     differ from our state-machine output in fine timing).
   - `src/lib/tests/test_keyboard_emulated.cpp` — should keep passing
     unchanged, since OS-visible behaviour is the contract we
     already verify.

### Risk

Getting the firmware to drive UART output at the right rate, parity
and framing requires careful clock plumbing.  MAME's
`src/mame/shared/ms7004.cpp` is the reference for how everything is
hooked up (`tx_handler`, `rts_handler`, the i8035-to-i8243 PROG
line, etc.).

### Priority

Low.  Our current model is correct against real hardware at the
OS-visible level; this would be a quality-of-emulation improvement,
not a bug fix.  Worth doing when there is appetite for a
self-contained subsystem rewrite.

## The MS7004's auto-repeat code is 0xB4, not the key's scancode (done)

The model sends 0264 on every repeat since 2026-11-07 (`ms7004.c`,
`SC_REPEAT`), as the firmware's `L_2B0` does.  The other half the item
asked for was checked first: both ROMs look for 264 in their keyboard
handlers - ROM-B at 175616 (`CMP R2,#264`, then `BICB #3,(R1)`), ROM-A
at 175710 - and repeat the last key themselves, so typing through the
console is unchanged and a program reading the USART sees the key's
code once and then 264 at the period, as on the machine.  A core test
pins the sequence; the lib's keyboard and typist tests pass as they
were.  The firmware's other findings in that disassembly (the release
code 0xB3 from the three register keys alone, the command set without
division modes or a repeat-rate setting) stand in `../hardware/
keyboard.md`; the model's 0x99 "game mode" repeat-rate heuristic is
still its own.

## The keyboard USART's clock is the timer's counter 0 (done)

Modelled in `keyboard.c` and `board.c` (2026-11-07): every rise of
counter 0's output clocks the receiver (`kbd_clock_edge`), a character
from the MS 7004 is 2.29 ms on the line at the keyboard's own 4800 baud,
and the receiver takes it only if its clock gave the character's worth
of edges meanwhile, within 5 % - `docs/hardware/keyboard.md`, "The
clock".  A counter 0 stopped or reloaded with EX.SYS's page number loses
every character with a framing error, as the board showed.  The
transmitter stays unclocked (nothing depends on its delay); the serial
USART stays as it is.  Four core tests with a timer of their own; the
lib's keyboard tests pass with their frame counts.  A state saved before
format 4 with counters 0 and 1 unprogrammed gets the ROM's words when it
loads (the one design decision of the item, taken with the format bump
the turning diskette brought).

## The diskette turns (done)

Modelled in `floppy.c` (2026-11-07): each drive's angle on the turn,
the System 34 layout's sector positions, a Type II search by distance,
READ ADDRESS ending at the next ID, WRITE TRACK at the index, READY at
the third index, the index bit in a Type I status - `docs/hardware/
floppy.md`, "Timing: the turning diskette".  Behind a switch
(`Emulator::setDisketteTurning`): on in the GUI, the browser page and
the CLI's `--realtime`, off for `ms0515-run`, scripted runs and the
tests, whose budgets were the fast controller's.  Nine core tests and
`test_turning.cpp`, which times Saboteur 2's load against the board's
measurement.  What the board measured is in ms0515-fpga's
`docs/baseline.md`, item 9: the game's 23 ms between blocks miss the
next block's ID by 2 ms, so the machine took its 20 s too.

## The processor waits for the video's reads of the DRAM

### What the board does

The video reads the dynamic RAM for every character cell it draws, and
while it does the processor's clock is held: D24.1, clocked by ПВАС H
with ВУ L on D and reset by the video's request ТРВЗУ H, holds ПТС H
through D23.4 until the video has had the memory.  Measured on the FPGA
rebuild at a system's prompt (ms0515-fpga `docs/baseline.md`, open list
item 10): in 20 ms the clock is held 6098 times for 165 752 master
ticks at the typical delays - 6.9 % of the time, 27 ticks (0.23 us) a
hold, every 3.3 us - and 11.0 % at the slow corner; READY slips add
0.6 %.  The same 20 ms stepped in the emulator from its own state agree
with the board to the T at 113 of the 114 addresses the prompt's loop
visits once the holds are taken out: the instruction times are right,
the memory's waits are what is missing.  So a program that runs from
the DRAM runs some 7 % faster here than on the machine; code in the
ROM, which the video does not read, runs at the machine's pace.

### What the emulator does

`cpu.c` charges each instruction its microcycle table's cycles and
nothing for where its operands are; `memory.c` answers a DRAM access
like a ROM one.  There is no video timing to collide with: the frame is
a count of cycles, the picture is drawn from the memory's contents when
the host asks.

### What a model takes

- *The video's cadence.*  A read of one word every character cell of
  the active lines: 260 lines a frame at 60 Hz (64 us a line), 200 of
  them active, 40 or 80 cells a line by the mode - the board's own
  numbers are in ms0515-fpga `docs/timing.md`.  A function of the
  frame's cycle position saying whether the video holds the memory now,
  and for how many more cycles.
- *The processor's accesses.*  A hold costs the processor only when it
  touches the DRAM while the video does: a hook on `memory.c`'s DRAM
  reads and writes (the bank's RAM, not the ROM, not the I/O) that adds
  the remaining hold to a per-instruction wait, which `board_step_frame`
  adds to the instruction's cycles.  The T-11's microcycle is 0.4 us
  and a hold releases it at the next 15 MHz edge or two; the board's
  average of 0.23 us a hold with 7 % of the time is the figure to hit,
  not the exact edge.
- *What it changes.*  Every program in RAM slows by about 7 %: the
  boot budgets of the tests, the keyboard tests' frame counts and
  `ms0515-run`'s unthrottled builds all move a little; a game that paces
  itself by the processor (the Spectrum ports) plays as on the machine.
- *Tests.*  A core test that runs a known loop from RAM and from ROM
  and sees the RAM one take 7 % more cycles a frame; the honest test
  itself - the board's 20 ms against the emulator's steps - could be
  made a lib test if the board's log were a fixture, which it is not.
- *Size.*  Medium: 60 lines for the cadence and the hook, the tests, a
  day of re-budgeting whatever counted frames too tightly.

### Priority

Medium-low.  It is the last of the board's timings the emulator lacks
and the one with the widest reach - everything in RAM - but 7 % is
below what a person notices, and nothing known depends on it.

## The repeat's delay after the top key is let go

### What the firmware does

The MS 7004 keeps one counter for the delay before auto-repeat (27H,
set to 0BH when a key is pressed, decremented as the last-pressed key
is scanned, L_1EF) and does not reset it when a key is released.  So
when the top key of two is let go and the one under it becomes the
last pressed (the stack shift at L_237..L_27D), its repeat - the
metronome code 264 - begins from wherever the counter stood, usually
at once.

### What the model does

`ms7004.c` restarts the full delay on the fall-back
(`repeat_next_ms = now + repeat_delay_ms` in the release path): the
first 264 for the key still held comes a delay later than the
keyboard's.  Seen in Saboteur 2's jump (`docs/hardware/keyboard.md`,
264): the kick after landing starts a quarter of a second later here
than it would on the machine.

### What it takes

Keep the counter the firmware keeps - the delay runs from the last
press, not from the fall-back - and a core test: press A, press B,
release B after half the delay, and the first 264 comes when A's delay
would have, not a whole delay after B's release.  Twenty lines.

### Priority

Low.  A quarter of a second in one corner of one game's handling, but
it is a known difference from the firmware, and a small one to close.
