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
OS-visible level, but for the auto-repeat code (below); this would be a quality-of-emulation improvement,
not a bug fix.  Worth doing when there is appetite for a
self-contained subsystem rewrite.

## The MS7004's auto-repeat code is 0xB4, not the key's scancode

### What the firmware does

Alex's disassembly of the MS7004 firmware (his annotated 8035 listing,
kept in the owner's archive, not in this repository) shows the auto-repeat path at `L_2B0`
sending the code **0xB4 (0o264)** to the host on every repeat - the
LK201's "metronome" code - and never the key's own scancode again.
The model in `core/src/ms7004.c` re-sends the key's scancode instead
(`SC_REPEAT` is defined as 0254 - not even the firmware's 0264 - and
unused).  Verified 2026-09-12 while working
out MANICM's key handling; the same listing shows the release code 0xB3
comes only from the three register keys (СУ, both ВР, `L_200`..`L_231`),
the full host command set is A1/23 sound, 99/1B/9F click, A7 bell,
89/8B data on/off, E1/D9/E3 auto-repeat, AB/FD/D3 probes, 11/13 LEDs -
no per-division modes and no repeat-rate setting, so a program cannot
ask for key-up codes or a faster repeat (the 0x99 "game mode" switch
in the model is our own heuristic; the firmware only turns the click
off).

### Why it matters

The three Spectrum ports (SABOT2's loader-side handling, FIST's
`KSCAN`, MANICM's `KEYS`) survive either way - to them a repeat is "a
code arrived" - but a program that reads the repeats as the key's own
code (any program that types text with the console driver, and our own
tests that press keys through the model) sees something a real machine
never sends.  The console ROM's keyboard routine must interpret 0xB4
as "the last key again" for auto-repeat to work at all on the iron;
check the ROM disassembly before changing the model, and add a core
test that pins the sequence: scancode, then 0xB4 every period.

### Priority

Low, but do it before anyone tunes a program's key handling on the
repeat codes.  Folds into the firmware-emulation item above if that
ever happens.

## The keyboard USART's clock is the timer's counter 0

### What the board does

On the schematic (3.858.420 Э3, sheet 1) the keyboard USART D22
(КР580ВВ51А) takes its TxC and RxC from the timer D9's OUT0 (`СИНХР1`):
counter 0 in mode 3 with a divisor of 26 from the 2 MHz clock, 76.9 kHz,
x16 in the USART, 4800 baud.  The serial USART D38 hangs on OUT1 the
same way.  A program that reprograms counter 0 - or writes into its
block by accident - kills the keyboard.  The FPGA rebuild found it with
EX.SYS's installation check, which writes the module's PPI at
177530-536, where a board without the owner's green wire has counter 0
(ms0515-fpga `docs/baseline.md`, open list item 1; `docs/findings.md`,
"The green wire"; the decoding in `../hardware/board.md`).

### What the emulator does

`keyboard.c` knows no clock: `kbd_tick` is called every
`KBD_TICK_DIVIDER * TIMER_DIVIDER` = 2048 CPU cycles (273 us) from
`board_step_frame`, and moves a byte from the FIFO into the receiver
after 8 such ticks - 2.2 ms a byte, close to the 11 bits at 4800 baud,
but tied to nothing a program can touch.  Counter 0 is programmed by the
ROM (since 2026-11-07, when writes at 177500-506 began to reach the
timer) and counts, and nobody listens to it.  The transmitter side
(`kbd_write`, reg 0) hands the byte to the MS 7004 model at once.

### How it could depend on counter 0

- *State.*  The USART gets a baud-clock input, `kbd_clock_edge()`,
  called on every rising edge of OUT0, and the receiver's and
  transmitter's bit timing counted in those edges: 16 edges a bit in the
  x16 mode the ROM sets (mode word bits 1:0 = 10), so a character with
  its start, 8 data and one stop bit is 176 edges, and `rx_delay`'s 8
  ticks become "176 edges since the last byte was taken".  The
  transmitter gets the same: TxRDY after a character's worth of edges,
  not at once.  The `while (board->timer_counter <= 0)` loop in
  `board_step_frame` already ticks the timer at 2 MHz; the board reads
  `timer_get_out(0)` after each tick and calls the USART on a 0->1
  change (one edge every 26 ticks with the ROM's divisor).  `kbd_counter`
  and `KBD_TICK_DIVIDER` go away.  No clock, no bytes: a counter 0 that
  is stopped, left in mode 0 at zero, or reloaded with a page number
  holds OUT0 still and the receiver never completes a character - the
  keyboard is dead, as on the board.  A wrong divisor gives a wrong baud:
  the MS 7004 sends at 4800 whatever the USART expects; a model that
  only counts edges would still take the bytes, a faithful one samples
  the line at the USART's rate and misreads them (framing error, status
  bit 5).  Counting edges and refusing a byte when the clock is off by
  more than a few per cent is enough for every known program.
- *The MS 7004 side.*  The keyboard's own transmitter runs at its own
  4800 baud; a byte it sends while the USART has no clock is lost (the
  receiver never samples it), so the FIFO's pop must be "the character's
  time has passed at the keyboard's rate" and the USART's acceptance a
  separate step: a byte that arrives with the clock dead is dropped, not
  held for a better time.
- *Interrupts.*  RxRDY, and with it the vector 130 request, follows the
  USART's own completion; nothing else changes in `board_step_frame`.
- *Tests.*  `core/tests/test_keyboard.cpp` gains: (1) with the ROM's
  programming (counter 0 mode 3, 26) a pushed scancode reaches RxRDY
  after about 176 x 26 timer ticks, 2.3 ms; (2) counter 0 stopped
  (control word 0x30, no count written) - the scancode never arrives
  and no interrupt is raised; (3) counter 0 reloaded with 1 (EX.SYS's
  page number, were the wire absent) - the same death; (4) the clock
  restored (the ROM's words again) - the next scancode arrives.  The
  lib's `test_keyboard_emulated.cpp` (the six ROM x disk configurations)
  and `test_keyclick_timing.cpp` must still pass with the same frame
  counts: a byte's latency barely changes (2.2 ms to 2.3 ms).
- *Old states.*  A snapshot saved before 2026-11-07 has counter 0
  unprogrammed (writes at 177500-506 were dropped), so a loaded old
  state would come up with a dead keyboard until something programs it:
  either `snapshot.c` gives such a state the ROM's words when it reads a
  format older than the change, or the change waits for the next format
  bump.  The one design decision in the item.
- *Size.*  Small: some 60 lines in `keyboard.c` / `keyboard.h` (the
  edge input, the bit counters, the framing check), 15 in `board.c`
  (the OUT0 edge detection in the timer loop, the old divider gone), the
  four core tests; the serial USART's stub stays as it is (it has no
  receiver to clock).  A day's work with the tests.

### Priority

Medium.  Nothing in the collection relies on the clock being
independent - the one program that touches counter 0 (EX.SYS, on a
board without the wire) is exactly the case the emulator should
reproduce, and a game that reprograms the timer's control word
carelessly would show the same death on the machine.  Worth doing
together with the serial port if that ever gets a receiver.

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
