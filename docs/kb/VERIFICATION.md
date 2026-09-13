# Verification: NS4 Technical Description vs Implementation

Cross-referencing the NS4 technical description (3.858.420 TO) against the
emulator core implementation.  Checked 2026-04-07; the gap list and the
dispatcher bits re-checked against the code on 2026-09-13.

## Bugs found and fixed

### CRITICAL: I/O port offsets were wrong (board.c)

All I/O port offsets were computed incorrectly — octal digits were treated
as hex.  For example, 0177500 - 0177400 = 0100(oct) = 64(dec) = 0x40,
but code had 0x80.

**Affected ports:** Timer, System Registers, FDC, Serial — all shifted by
wrong amounts.  Only Keyboard (0x20, 0x22, 0x30, 0x32) and Memory
Dispatcher (0x00) happened to be correct because the octal and hex values
coincide for small numbers.

**Fixed:** All IO_* constants recalculated from octal addresses.

### CRITICAL: VBlank interrupt had wrong vector (board.c)

Per NS4 Table 4:
- Monitor/VBlank → vector 064, priority 4 (IRQ line "H H L H")
- Timer → vector 0100, priority 6 (IRQ line "L H L L")

Code had VBlank at vector 0100.  Fixed to vector 064.

### CRITICAL: Timer and VBlank interrupts not gated (board.c)

Per NS4 section 4.3 (dispatcher register bits 8-9):
- Bit 8: the monitor interrupt's request itself - "1 sets it, 0 resets
  it".  It is a level, not an enable: writing 1 raises the request,
  writing 0 withdraws it, and nothing else fires (a clear never does).
  First read as an enable that fired on any transition, which re-entered
  RT-11's terminal service per character - see the long-`TYPE` entry in
  `KNOWN_ISSUES.md` (fixed 2026-09-13, `core/tests/test_monitor_request.cpp`).
- Bit 9: Timer interrupt enable ("1" enables timer IRQ)

Both interrupts were firing unconditionally.  Fixed to check the
corresponding dispatcher bits before asserting.

### MINOR: Missing MS7007 keyboard vector (cpu.h)

NS4 Table 4 lists MS7007 parallel keyboard at vector 060, priority 4.
Added CPU_VEC_KBD7007 constant.

### MINOR: Missing halt/timer service address (board.c)

NS4 Appendix 1 lists address 177770 (offset 0xF8) for halt and system
timer service.  Added IO_HALT_TIMER constant (not yet handled).

## Verified correct

### CPU (cpu.h, cpu.c, cpu_ops.c)
- [x] KR1807VM1 @ 7.5 MHz, 400 ns base microcycle
- [x] 8 general-purpose registers (R0-R5, SP=R6, PC=R7)
- [x] PSW format: bits 0-4 (C,V,Z,N,T), bits 7-5 (priority), upper byte = 0
- [x] Mode register = 0xF2FF → start address 172000, restart 172004
- [x] 66 instructions, no MARK, no FIS (MUL/DIV/ASH/ASHC)
- [x] 8 addressing modes (4 direct + 4 indirect)
- [x] Vectored interrupts: bus error(004), reserved(010), BPT(014), IOT(020),
      EMT(030), TRAP(034)
- [x] HALT signal → push PSW+PC, load PC=172004, PSW=0340
- [x] Reset leaves PSW=0340 as well: the machine starts at priority 7 and
      the ROM's self-test runs masked (a key pressed during it hung the
      machine while reset left PSW=0 - fixed 2026-09-08, `test_cpu.cpp`)

### Memory (memory.h, memory.c)
- [x] 128 KB RAM = 16 banks × 8 KB
- [x] Dispatcher register at 177400, bits 0-6 select primary/extended
- [x] Bit 7 enables VRAM access through virtual window
- [x] Bits 10-11 select VRAM window position (00→000000, 01→040000, 1x→100000)
- [x] ROM: 16 KB, default visible at 160000-177377 (upper 8 KB)
- [x] Extended ROM: bit 7 of Reg A → full 16 KB at 140000-177377
- [x] I/O space: 177400-177776

### Timer (timer.h, timer.c)
- [x] KR580VI53 (i8253 clone), 3 channels, 2 MHz clock
- [x] 6 operating modes implemented correctly
- [x] Channel 0: keyboard baud rate (mode 3, value 032 octal = 26 → 4800 baud)
- [x] Channel 1: printer baud rate
- [x] Channel 2: speaker and timing intervals
- [x] Separate read (177500-177506) and write (177520-177526) addresses
- [x] Counter latch command, LSB/MSB/LSB+MSB access modes

### Keyboard (keyboard.h, keyboard.c)
- [x] KR580VV51 (i8251 clone) USART
- [x] Addresses: 177440 (RX data read), 177460 (TX data write),
      177442 (status read / command write)
- [x] Two-phase init: mode instruction → command instruction
- [x] Three-zero-byte reset sequence before real mode word
- [x] IRQ on vector 0130, priority 5
- [x] 4800 baud, 8-bit data, 2 stop bits, no parity (mode = 0xCE)

### Floppy (floppy.h, floppy.c)
- [x] KR1818VG93 (WD1793 clone)
- [x] Addresses: 177640 (status/cmd), 177642 (track), 177644 (sector), 177646 (data)
- [x] 80 tracks, 2 sides, 10 sectors/track, 512 bytes/sector
- [x] Type I/II/IV commands
- [x] DRQ/INTRQ signals readable via System Register B

### System Registers (board.h, board.c)
- [x] Reg A (177600): bits 1-0 drive select, bit 2 motor (active low),
      bit 3 side, bits 4-5 LEDs, bit 6 cassette, bit 7 extended ROM
- [x] Reg B (177602): bit 0 INTRQ inverted, bit 1 DRQ, bit 2 ready inverted,
      bits 4-3 DIP refresh, bit 7 cassette input
- [x] Reg C (177604): bits 2-0 border (GRB), bit 3 hires, bit 4 LED,
      bit 5 tone, bit 6 sound enable, bit 7 timer gate
- [x] PPI control (177606): bit 7=1 mode select, bit 7=0 bit set/reset
- [x] Boot programs PPI with code 202 (octal) = mode 0, port B input, A/C output

### Video controller
- [x] 320×200 with 8 colors (attribute mode, ZX Spectrum-like)
- [x] 640×200 monochrome
- [x] Color byte: bits 15(flash) 14(intensity) 13-11(bg GRB) 10-8(fg GRB)
- [x] Border color in Reg C bits 2-0

## Gaps, as of 2026-09-13

Done since the first check:

- MS7007 PPI at 177540-177546: port A latch, control word, port B read
  as the joystick lines (Kempston order, 2026-08-25 - `docs/hardware/keyboard.md`).
  The parallel keyboard matrix itself and vector 060 are not modelled:
  the machine has the serial MS7004.
- Cassette interface: `core/src/cassette.c`, the no-tape model - Reg A
  bit 6 is taken, Reg B bit 7 reads 0.  No tape playback.
- Video attributes: flash (phase every 30 frames) and the intensity bit
  are rendered in `libapp/src/Screen.cpp`.
- FDC Type I step rate honours the command's rate bits (`core/src/floppy.c`).

Still open:

- Serial port / printer (i8251 at 177700-177722, vectors 110/114): a
  stub - TX bytes go to a host callback, the status and command words are
  accepted and dropped, nothing is received.  While a hard-disk image is
  mounted the paravirtual HD: shadows these addresses (`docs/hardware/hd.md`).
- Register 177770 (halt/timer service): named in `board.c`, not handled.
