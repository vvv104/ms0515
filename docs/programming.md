# Programming the MS-0515 under RT-11: the handbook

What a program running on this machine has to know, collected from the
ports (FIST, MANICM), the disk work and the traces that found each fact.
The hardware documents in `hardware/` describe the registers; this one
says what happens when a program uses them under RT-11, and what the
emulator's core does about it.  Everything here was verified in the
emulator against the original software or the machine's own documents; a
figure with no source named comes from the core's own model
(`src/core/`), which is the reference for the machine as far as this
project can check it.

`rt11_devel/toolset/GOTCHAS.md` covers the build pipeline (MACRO, LINK,
the staging of files); this covers the program.

## The processor

- **KR1807VM1 = DEC T-11**, 7.5 MHz, 16-bit bus mode.  A microcycle is 3
  clocks (400 ns).  Base PDP-11 instruction set: `SOB`, `XOR`, `SXT`,
  `MFPS`, `MTPS` are there.  **No EIS, no FIS**: `ASH`, `ASHC`, `MUL`,
  `DIV` trap to vector 10 (reserved instruction).  OSA's `EM.SYS` (`SET EM
  ON`) emulates them through that trap for programs that want it; a
  program of ours shifts by repeated `ASL`/`ASR` (see MANICM's `ASLN`/
  `ASRN` macros) and stays portable to the DVK, whose 1801VM1/VM2 lack the
  EIS as well.
- `JMP Rn` and `JSR Rn,Rn` (mode 0) are illegal: vector 10.
- `MTPS` changes the low byte of the PSW only (priority included).
- `MOVB` into a register sign-extends: mask with `BIC #177400` before
  using a byte as a number or an address.
- **Instruction times** (the core's tables, `src/core/src/cpu_ops.c`,
  pinned by `core/tests/test_cpu_timing.cpp`; Appendix B of the T-11
  spec; clocks): fetch 9; `MOV R,R` 12; `DEC R`, a branch (taken or not)
  12; `INC R` 12; `MOV #n,R` 18; `BIC #n,R`, `ADD #n,R`, `CMP R,#n` 18;
  `MOV (R)+,R` 18; `BISB (R)+,R` 18; `MOV R,(R)+` 21; `CLR (R)+` 21;
  `MOV (R)+,(R)+` 27; `MOVB (R)+,(R)` 27; `MOVB X(PC),R` 24; `MOV
  X(PC),R` 27; `MOVB R,X(PC)` 30; `MOVB R,@#n` 27; `SOB` 18; `NOP` 18;
  `JSR PC,X(PC)` 33; `RTS PC` 21; `MTPS #n` 30.  A word copy loop `MOV
  (R0)+,(R1)+ / SOB` costs 45 clocks a word: 4 KB in 12 ms.  When a loop
  must take a known time, ask the core - a scratch `TEST_CASE` over that
  test's `time_of()` prints the figure - rather than estimate.
- **Spectrum conversions**: one Z80 T-state at 3.5 MHz = 2.1429 clocks;
  a `DJNZ` turn (13 T) = 1.55 turns of `SOB Rn,.`; N turns for T
  T-states is `T * 0.119`.  `rt11_devel/projects/fist/source/timing.py`
  has the arithmetic; MANICM's `MS0515.MAC` has worked examples (the
  beeper loops).

## Memory and the monitor

- 56 KB of RAM in 7 primary banks of 8 KB (dispatcher bits 0-6, 1 =
  primary), 7 more extended behind them, 16 KB VRAM behind the ROM.  RT-11
  sees the primary banks only; the extended ones are a program's own
  (FIST keeps its game state there).
- **Where the monitor is** depends on the kit.  OSA, OMEGA, Mihin: RMON at
  the top, from about 0150000; the USR and KMON below it, swapped as
  usual.  **Rodionov's RT15SJ has parts from 072000**: a program that
  writes above 070000 kills it (FIST's loader did, once).  The safe rule:
  keep everything under 070000, or ask with `.SETTOP` and honour the
  answer.
- **`.SETTOP`** returns the highest address you may use; check it against
  what you need and say so if it is less.  `.LOOKUP` needs the USR; do it
  once at the start.  `.READW` does not need the USR: a game can read its
  data file a piece at a time during play.  The area for a `.READW` by
  hand: word 0 = the channel in the low byte and 010 in the high, then the
  block, the buffer, the word count, and a 0 (wait).  `.LOOKUP` takes an
  area of 3 words and a `.RAD50` name of 4 words (`DK `, two words of
  name, the extension).
- A `.SAV` is a memory image from 0 to its high limit: holes are stored
  on disk.  Put big buffers above the image and claim them with
  `.SETTOP` instead of `.BLKB`-ing them (MANICM: 20 blocks on disk for a
  program that uses 28 KB).  Block 0 of a `.SAV` is the RT-11 job header:
  040 the start, 042 the stack, 050 the high limit; for a handler `.SYS`
  052 is the handler's size, 060 its SYSGEN options, 062/064/066 its
  primary bootstrap.
- Everything below 01000 is RT-11's: the vectors and the system
  communication area.  The stack starts at 01000 and grows down.

## The dispatcher, the VRAM window, the interrupts

The dispatcher is the register at 177400 (`hardware/memory.md`).  What a
program needs of it:

| value | meaning |
|---|---|
| 002177 | as the ROM leaves it and RT-11 runs on it: banks primary, VRAM window off (its place bits say 040000), timer interrupt **off** |
| 003177 | the same with the timer interrupt on (bit 9) |
| 006377 / 007377 | VRAM window on at 100000..137777 (bit 11), without / with the timer interrupt |

- **The VRAM window** hides the RAM behind it while it is on.  Put it
  where your program is not: at 100000 for a program under 070000.  RT-11
  is behind it there, so make no monitor call while it is open.
  Interrupts may stay on while it is open **if their handlers and the
  vectors are visible** - our own handlers in low memory are; RT-11's
  clock service behind the window is not (FIST masked everything at
  priority 7 for that reason).  A VRAM row is 40 words = 80 bytes, 200
  rows; a word is the attribute over the pixels (`hardware/video.md`).
- **Do not mask interrupts across a long section** if you count them: an
  interrupt that arrives while masked merges with the next of its kind
  (the core keeps one pending flag), and a 35 ms copy with the frame
  interrupt masked loses a tick every pass.
- **RT-11 has no periodic clock on this machine.**  The timer interrupt
  (vector 100, 50 Hz, strobed by the frame) fires only while dispatcher
  bit 9 is set, and RT-11 sets it only inside the floppy handler while it
  waits for the controller, clearing it after; the 50 Hz seen in KMON's
  idle loop is the monitor interrupt (vector 064) triggered by software
  through bit 8.  So `.GTIM` never advances on OSA (it writes nothing at
  all), and a program that wants time keeps it itself: set bit 9, take
  vector 100 with a handler that counts, restore both on exit (MANICM's
  `FRISR`/`PACE`).  The floppy handler takes vector 100 for its wait and
  puts it back, so a `.READW` in the middle costs nothing but the ticks
  during it.  FIST kept time on timer channel 1 instead (the printer's
  baud generator, unused), read by polling.
- **Priorities**: the core takes an interrupt's priority from the PSW
  word stored at its vector, not from the device.  RT-11 puts high ones
  there, so raising the processor priority to 5 does not keep the
  keyboard's interrupt from RT-11's handler.  To own a device, own its
  vector.

## The keyboard

- The MS7004 talks through the i8251 USART at 177440 (data) / 177442
  (status, bit 1 = a byte waiting), 4800 baud - one byte per 2 ms
  (`hardware/keyboard.md`).  Its interrupt is vector 130.  **The ROM's
  handler on that vector takes the byte the moment it arrives** and hands
  it to RT-11; a program that polls 177440 sees nothing.  Two ways out:
  run at priority 7 with the keyboard polled (FIST, which also keeps its
  own clock), or take vector 130 with a handler that queues the bytes and
  give it back on exit (MANICM's `KBISR`).
- **No key-up codes.**  A key sends its code when pressed and repeats it
  while held (the core's game-mode repeat: after 125 ms, then every 50
  ms); modifiers send their own code, and ALL-UP (263) comes once
  everything is released after a modifier - never after a plain key.  So a
  "held" key is a timer refreshed by its code, and a chord is every timer
  still running when any code arrives (the keyboard repeats only the last
  key).  Both ports use the same model (`KSCAN` in FIST, `KEYS` in
  MANICM); its price is a key released just before another is pressed
  counting as held with it.
- Scancodes the ports use (octal): 1..0 = 300 305 313 320 325 333 340 345
  352 357; Q W E R T Y U I O P = 303 315 327 335 336 307 314 331 342 330;
  A S D F G H J K L = 322 316 354 302 341 366 301 321 347; Z X C V B N M
  = 360 343 306 362 350 334 323; SPACE 324, ENTER 275, keypad ENTER 225,
  SHIFT 256 (both), arrows up/down/left/right 252 251 247 250, keypad 1-9
  = 226 227 230 231 232 233 235 236 237.  The full table is
  `src/core/src/ms7004.c`.
- The console bridge of `ms0515-cli` types a host character as the key
  with SHIFT for an uppercase letter (SHIFT, the key, ALL-UP): a test can
  press SHIFT+letter that way but not SHIFT+SPACE.  For chords and holds,
  drive the emulator library (`keyPress`, `keyTick`) as the ports' test
  harnesses do.
- The joystick is port B of the MS7007 PPI, 177542: bits 0 right, 1 left,
  2 down, 3 up, 4 fire, low when pressed, the Kempston order.  SABOT2,
  FIST and MANICM read it.  The same register is the Covox-style DAC
  output of the VLAD & ALEX sampler when the PPI is switched to output
  (`ms0515-software/programs/covox`).

## The screen

- The console leaves 640x200 mode; a colour program clears register C
  bit 3 in its prologue (`BIC #17` then the border bits) and puts 010
  back on exit - the console draws garbage in the wrong mode.  Register C
  (177604) is write-only: keep a shadow.
- Clear VRAM once at the start: the console's text is still there.
- **The Spectrum's picture fits bit for bit.**  Its attribute byte (FLASH
  BRIGHT PAPER INK) is the MS-0515's attribute byte in the same order,
  its pixel byte the same (bit 7 leftmost); 256x192 sits centred in
  320x200 with 4 words to the left and 4 rows above.  Presenting a
  Spectrum display file costs a word per byte: the attribute row turned
  into words once per eight pixel rows, then `MOV (R0)+,R3 / BISB
  (R1)+,R3 / MOV R3,(R2)+` per byte, about 57 clocks - 4 KB in 31 ms.
  Present from the game's own buffer when the display file is only a
  copy of it (MANICM), and only the rows that changed.

## The speaker

- One bit: register C bit 6, with the timer gate (bit 7) off so the line
  is the program's; bit 5 set as well.  `MOVB` the shadow with bit 6
  flipped to 177604 and the speaker toggles - exactly the Spectrum's `OUT
  (254)`, so beeper routines port loop for loop with their delays
  converted (above).  BIRDS, SABOT2, FIST and MANICM all do this.  The
  timer's own square wave (channel 2, mode 3, gate on) is the other way
  and gives one voice only.
- Register C bits 0-2 are the border, in the Spectrum's GRB order; the
  original's `XOR 24` flipped border blue with the speaker, and the ports
  do the same for the flicker.

## MACRO-11 and LINK

- **Numbers are octal**: `11` is nine.  Write decimals with a dot (`11.`)
  or think in octal; a shift count or a table size written without the
  dot is the classic silent error.
- `MACRO A+B` stops at the first `.END`: to build from several sources
  assemble each and `LINK A,B`; globals resolve at link time.  A symbol
  is six characters; a local label (`1$`) lives between two global
  labels, so a routine's `9$` is not visible to the next routine.
- Branches reach 127 words either way; past that, `BNE 1$ / JMP far /
  1$:`.  MACRO says `A` on the line.
- A routine placed between a piece of code and the label it falls into
  breaks the fall-through silently (MANICM lost two hours to `RELOC` and
  `EXIT` put in such a gap): mark every fall-through and keep it a
  fall-through.
- `.REPT` works inside `.MACRO`; `.RAD50 /DK /` is a device.
- `LINK/MAP:NAME A,B` writes the map; only `.GLOBL` symbols appear in it.
  A test harness that reads a program's state by name gets the addresses
  from there (MANICM's tests).
- The rest - ASCII only, `SYSMAC.SML` on SY:, `STARTS.COM`, LINK's
  switches, `--no-config` - is in `rt11_devel/toolset/GOTCHAS.md`.

## RT-11 handlers

- A handler carries its SYSGEN options in word 060 of block 0; the
  monitor refuses one whose options differ ("Invalid device" at LOAD,
  "?KMON-F-Conflicting SYSGEN options" at INSTALL).  The word matters:
  `.DREND` puts pointer words at the handler's end that the monitor
  fills from the end by its own layout - `$INPTR`, `$FKPTR`, and
  before them `$TIMIT` on a monitor generated with time-outs (Mihin's
  OS-16SJ).  Flipping the bit alone makes the monitor overwrite the
  handler's last code word; `tools/timit_handler.py` inserts the word
  properly.  `ms0515-software/software/system/handlers/README.md` tells
  the story.
- The floppy handlers poll the controller; they use neither `.DRAST` nor
  `.FORK`.

## Tools of the trade

- `tools/run_program.py`: boot a disk headless, type, keep the text and
  the screen PNG (the PNG is 640x400: two image rows per screen row).
  `--put` copies files onto a scratch copy of the image first.
- `ms0515-cli --history-size N --save-state F` plus
  `--history-watch-addr A --history-watch-len L` (writes) and
  `--history-read-watch-addr` (reads): addresses in **decimal**; RAM only,
  not the I/O page.  `tools/dump_state.py F` prints the events: traps
  with their vector and PC (a reserved-instruction trap is logged with
  the faulting instruction's next PC), FDC commands, register A and
  dispatcher writes, PSW priority changes, the watched writes and reads.
  The ring floods with KMON's idle EMTs once a program has exited, so
  stop soon after the moment of interest.  Sampling the PC at every
  frame interrupt (the `vec=100` events) is a profiler.
- `tools/pdp11_disasm.py FILE BASE`: a `.SAV` disassembled at base 0
  has addresses equal to file offsets.
- SkoolKit's `trace.py --start A --stop B --stats snapshot out.z80`
  measures the original's loops in T-states and saves a state to continue
  from; `tap2sna.py` makes the snapshot from a tape.
- A game's tests: a doctest binary over `ms0515_lib` (the FIST and MANICM
  `tests/`), booting the built program from a folder device, feeding the
  date prompts through the serial callbacks, reading its state from RAM
  (primary banks are `mem.ram[address]`), pressing keys through the
  library.  It skips itself when the program is not built.

## Porting from the ZX Spectrum, the recipe

1. Take a SkoolKit disassembly; read all of it before writing.  Measure
   the main loop's pass in T-states with `trace.py`.
2. Keep the original's memory layout for its buffers, moved by a multiple
   of 2048 so its tricks on the high byte (`INC H`, `AND 7`) still hold,
   and remember the low byte wraps on its own (`L + 32` past 0xE0 stays a
   byte: MANICM's first guardian "collision").  Relocate the addresses the
   data carries by that constant when loading.
3. Data stays external: extract it from the tape at build time into a
   file the program reads, never into the repository.
4. Screen: present the display file as above.  Keys: the hold-timer
   model.  Sound: the beeper loops with converted delays, calibrated
   against the core's figures.  Pace: own the frame interrupt.
5. Write the game in one file and the machine in another, with the
   machine's interface at its top - the next PDP-11 is one file away.
