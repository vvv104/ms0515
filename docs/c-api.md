# The C API of the MS 0515

What a C program on the machine is made of: the headers of
`rt11_devel/toolset/gcc/include`, their functions, and the rules they
keep.  `docs/programming.md` is the handbook of the machine itself - the
processor, RT-11's memory, the dispatcher, the interrupts - and every
function here says which part of it it stands on.  How to build the
compiler and a project is `rt11_devel/toolset/gcc/README.md`; the
examples under `examples/` there use everything below, and
`rt11_devel/projects/stars` is a whole program on it.

## The program

A program is GCC's output for the target `pdp11-aout` with `-m10`, the
T-11's instruction set: no EIS, no FPP.  `int` and pointers are 16 bits,
`long` 32, `long long` 64; there is no floating point.  Every `*`, `/` and
`%` is a call into the runtime's `arith.s`.  The program is linked whole
from 01000 - text, data, bss, then the stack's room - and wrapped into a
`.SAV` by `aout2sav.py`; RT-11's `RUN` loads it and jumps to `_start`,
which calls `main()` and leaves by `.EXIT` with its return value as the
status.  The memory above the stack is the heap, `malloc` takes it from
the monitor by `.SETTOP`.

```cmake
cmake_minimum_required(VERSION 3.21)
include(<repository>/rt11_devel/toolset/cmake/Rt11Gcc.cmake)
project(myprog LANGUAGES C ASM)
rt11_c_library(NAME world SOURCES world.c sectors.c INCLUDE include)
rt11_c_program(NAME MYPROG SOURCES myprog.c [helpers.s] LIBRARIES world [STACK 2048])
```

`SOURCES` is what is compiled and linked into the program, `LIBRARIES`
the project's own libraries (`rt11_c_library`: sources and the folders
of their headers; the linker takes the objects a program refers to); the
runtime and libgcc come by themselves.  `<build>/sav/MYPROG.SAV` comes
out; `ms0515-run MYPROG` there runs it.
Assembler of the program's own is GNU as syntax: `$` for an immediate,
`*$` for an absolute address (MACRO's `@#`), a leading `0` for octal,
every C symbol with a leading underscore, the arguments on the stack at
`2(sp)`, `4(sp)`..., the result in r0, r0 and r1 scratch, r2..r5 kept.

What the machine costs: an instruction is some three microseconds, so a
frame of 20 ms holds about 7000; a 16-bit division is some 150 of them, a
32-bit one 400; a constant shift up to 8 is that many single shifts,
longer ones a loop.  A program that draws every frame keeps its
arithmetic off the frame (STARS shows how).

## rt11.h - the monitor

RT-11's programmed requests as functions (`rt/emt.s`, `rt/files.c`): an
EMT with its arguments in r0 or in a block r0 points at.  **The monitor
lies behind the VRAM window**: a program that has the window open
(`ms_screen_begin`) closes it round a request with `ms_window(0)` and
opens it again with `ms_window(1)`.

### The console and memory

| function | request | what |
|---|---|---|
| `void rt11_ttyout(int c)` | `.TTYOUT` | the character out, waiting for room in the ring |
| `int rt11_ttyin(void)` | `.TTYIN` | the next character typed, waited for (the low 7 bits); the monitor hands a line over once it is typed, its own editing done, and in the case it was typed - the program's job status word says so (`aout2sav.py`), where the monitor folds to upper case by default |
| `void rt11_print(const char *s)` | `.PRINT` | the string up to a 0, and a new line; up to a 0200 without one |
| `void *rt11_settop(void *top)` | `.SETTOP` | the program's memory up to `top`, or as much as there is; the new high limit back |
| `void exit(int status)` | `.EXIT` | back to the monitor (also `stdlib.h`) |
| `extern char *rt11_memtop` | | the first address above the program's stack, where `main()` started with SP: the heap's beginning |

### Files, a block of 512 bytes at a time

A file is opened on a channel (0..15) by name once at the start - `.LOOKUP`
needs the USR - and read or written by block during the run.  Every
request gives -1 on an error and `rt11_error()` tells its code.

| function | request | what |
|---|---|---|
| `int rt11_filespec(unsigned spec[4], const char *name)` | | `"DK:NAME.EXT"`, or `"NAME.EXT"` on DK, as the four RAD50 words the monitor takes; 0 |
| `int rt11_lookup(int channel, const unsigned spec[4])` | `.LOOKUP` | the file opened for reading; its length in blocks (errors: 0 the channel in use, 1 no such file, 2 no such device) |
| `int rt11_enter(int channel, const unsigned spec[4], int blocks)` | `.ENTER` | a new file of so many blocks (0: half the largest free space) opened for writing; its length.  `rt11_close` makes it permanent |
| `int rt11_readw(int channel, unsigned block, void *buffer, unsigned words)` | `.READW` | `words` words from the file's block into the buffer, waited for; the words read, fewer at the end of the file (errors: 0 past the end, 1 the device, 2 the channel not open) |
| `int rt11_read(int channel, unsigned block, void *buffer, unsigned words)` | `.READ` | the same begun and left to the disk's handler, which fills the buffer on its interrupts while the program goes on; the words asked for |
| `int rt11_wait(int channel)` | `.WAIT` | until the channel's transfer is over; -1 when it failed |
| `int rt11_writew(int channel, unsigned block, const void *buffer, unsigned words)` | `.WRITW` | `words` words from the buffer into the file's block, waited for; the words written |
| `int rt11_close(int channel)` | `.CLOSE` | the channel freed; a file being written made permanent |
| `int rt11_error(void)` | | the last error's code, byte 052 of the system communication area |
| `int rt11_request(void *area)` | EMT 375 | a request block of the program's own; what r0 brings back, -1 on the carry |
| `int rt11_request374(unsigned code_and_channel)` | EMT 374 | the code in the high byte, the channel in the low |

`.LOOKUP` before the heap grows: the USR swaps in above the job's high
limit.  The disk's handler takes vector 100 for its wait and gives it
back, so `ms_frames` misses the ticks of a transfer.

## ms0515.h - the machine

What a program does outside the monitor, as `docs/programming.md` says it
and the ports (FIST, MANICM) do it (`machine/`).  Everything is C over the
registers but the interrupt handlers, which need `RTI`, and the PSW's two
instructions.  A program takes what it needs with `ms_*_begin()` and
gives it back with `ms_*_end()` before it returns to the monitor.

### The registers

`MS_DISPATCHER` (0177400), `MS_REGISTER_C` (0177604, write-only: the
library keeps its shadow), `MS_KEY_DATA` and `MS_KEY_STATUS` (0177440,
0177442), `MS_JOYSTICK` (0177542), as `volatile` pointers.

### The screen

Video memory seen through the window at `MS_VRAM` (0100000): `MS_ROWS`
(200) rows of `MS_STRIDE` (80) bytes, 40 words a row, a word the
attribute byte over the pixel byte, bit 7 the leftmost pixel of the
eight; `ms_row(y)` is a row's first byte.  The attribute is the
Spectrum's: `MS_INK(c) | MS_PAPER(c) | MS_BRIGHT | MS_FLASH`, colours 0
black, 1 blue, 2 red, 3 magenta, 4 green, 5 cyan, 6 yellow, 7 white.

| function | what |
|---|---|
| `void ms_screen_begin(unsigned char attribute)` | 320x200 colour, the border black, the window open at 0100000, the screen cleared to the attribute with no pixels.  RT-11 is behind the window from here: no monitor call until the window is closed |
| `void ms_screen_end(void)` | the screen cleared (in 640x200 the attributes would show as stripes), the window closed, the console's 640x200 back |
| `void ms_window(int open)` | the window closed (0) or opened (1) while the screen is on: round a file request |
| `void ms_border(unsigned char colour)` | the border, 0..7 in GRB order |
| `void ms_screen_clear(unsigned char attribute)` | every word of the screen: the attribute, no pixels |
| `void ms_plot(int x, int y)`, `void ms_unplot(int x, int y)` | the pixel at (x, y) lit or put out, 0 <= x < 320, 0 <= y < 200 |
| `void ms_attribute(int c, int y, unsigned char attribute)` | the cell (8 pixels wide, a row high) at column c (0..39) of row y |

A program that draws a lot writes the window itself: `ms_row(y)` and the
bit, as STARS does with its tables.

### The clock

RT-11 has no periodic clock on this machine; the frame interrupt, 50 Hz
on vector 100, fires while dispatcher bit 9 is set.

| function | what |
|---|---|
| `extern volatile unsigned ms_frames` | frames since `ms_clock_begin()` |
| `void ms_clock_begin(void)`, `void ms_clock_end(void)` | the vector taken with a counting handler and the interrupt on; both put back |
| `void ms_wait_frames(unsigned n)` | until `ms_frames` has moved on by n |

A paced loop keeps its own `last` and waits while `ms_frames - last <
PACE`, so that a slow pass does not pile up.

### The keyboard

The MS7004's bytes taken off vector 130 into a ring of 16 - the ROM's
handler would hand them to RT-11 - and 231 sent to the keyboard first,
keyclick off, which the firmware takes as "a game runs" and repeats keys
sooner.  **No key-up codes**: a key sends its code when pressed and again
while held, never a release; so a held key is a timer refreshed by its
code.

| function | what |
|---|---|
| `void ms_keys_begin(void)`, `void ms_keys_end(void)` | the vector taken and given back |
| `int ms_key(void)` | the next code that came, or -1 |
| `void ms_keys_flush(void)` | the codes that came and were not read, dropped: at a scene's start, so that a key let go a second ago does not act in it |
| `struct ms_held { unsigned char code, frames; }` | a key watched: its code, its timer |
| `void ms_held_tick(struct ms_held *keys, int n)` | a frame gone: every timer down by one |
| `int ms_held_code(struct ms_held *keys, int n, int code)` | a code came: the key it is wound up - `MS_HELD_FIRST` (9) frames by the first code, which has to outlast the keyboard's silence before the repeats, `MS_HELD_NEXT` (4) by a repeat; 1 when it was one of them |
| `ms_is_held(key)` | whether its timer is running |

The codes: `MS_KEY_UP`, `MS_KEY_DOWN`, `MS_KEY_LEFT`, `MS_KEY_RIGHT`,
`MS_KEY_SPACE`, `MS_KEY_ENTER`, `MS_KEY_SHIFT`, `MS_KEY_ALL_UP`,
`MS_KEY_A`..`MS_KEY_Z`, `MS_KEY_0`..`MS_KEY_9` (octal, from
`src/core/src/ms7004.c`).  A pass that polls:

```c
static struct ms_held keys[] = {{MS_KEY_LEFT, 0}, {MS_KEY_RIGHT, 0}};
int c;
ms_held_tick(keys, 2);
while ((c = ms_key()) >= 0)
	if (c == MS_KEY_Q) quit = 1;
	else ms_held_code(keys, 2, c);
if (ms_is_held(keys[0])) turn_left();
```

### The memory banks

56 KB in seven primary banks of 8 KB (bank n at n * 020000) and a
second, extended, bank behind each, which RT-11 never sees: a program's
own room, 8 KB a bank.  A bank switched to extended hides its primary
half - never the bank the code, the stack or the vectors are in.

| function | what |
|---|---|
| `void ms_bank(int n, int extended)` | bank n (0..6) switched to its extended half (1) or back to the primary (0) |

### The joystick, the speaker, the processor

| function | what |
|---|---|
| `unsigned ms_joystick(void)` | the lines pressed: `MS_JOY_RIGHT` 1, `MS_JOY_LEFT` 2, `MS_JOY_DOWN` 4, `MS_JOY_UP` 8, `MS_JOY_FIRE` 16 (port B of the MS7007 PPI, the Kempston order) |
| `void ms_speaker_flip(void)` | the speaker line flipped: a click, a square wave by flipping in time (register C bit 6, the Spectrum's `OUT`) |
| `unsigned ms_interrupts_off(void)`, `void ms_interrupts_restore(unsigned psw)` | the priority to 7 and back, round the few instructions an interrupt must not split |

## fx.h - fixed point, cut to size

What a 3D picture is made of on a processor without multiply or divide
(`rt/fx.s`, `rt/fx.c`): the shapes a projection or a ray needs, written
out, and the tables a turn needs.  8.8 fixed point where it is said: 256
is one.

| function | what |
|---|---|
| `int fx_div8(int num, int den)` | `num / den`, truncated toward zero, for a quotient that fits eight bits (`|num| < 256 * den`, `den > 0`): eight steps written out, half a general division.  A projection: `160 + fx_div8(x << 7, z)` for a point in view, since `x * 128 < 160 * z` there |
| `int fx_mul(int a, int b)` | `a * b / 256`: the product of two 8.8 numbers, or of a number and an 8.8 factor, as 8.8, the full 32-bit product shifted; the result must fit an int |
| `int fx_sin(int angle)`, `int fx_cos(int angle)` | the sine and the cosine of an angle in 256ths of a turn, as 8.8 (`fx_sin(64)` is 256), from a table of a quarter turn |

STARS projects with `fx_div8`; a raycaster steps its rays with `fx_sin`
and `fx_cos` and scales with `fx_mul`.

## The C library

`stdio.h` is the console: `putchar`, `getchar`, `puts`, `printf`,
`vprintf`, `sprintf`, `snprintf`, `vsnprintf`.  `printf` does `d i u x X o
c s %`, the flags `-` and `0`, a width, a precision for `s`, `l` for a
long; no floating point.  `'\n'` goes out as CR LF, since the console
moves the cursor down for an LF and left for a CR; a line typed comes in
through `getchar` with the CR dropped, the LF as `'\n'`.  There are no
`FILE`s: files are `rt11.h`'s, by block.

`string.h` whole (`memcpy`, `memmove`, `memset`, `memcmp`, `memchr`,
`strlen`, `strcpy`, `strncpy`, `strcat`, `strncat`, `strcmp`, `strncmp`,
`strchr`, `strrchr`, `strstr`), `ctype.h` for ASCII (the console's KOI-8
letters, bit 7 set, are none of its classes), `stdlib.h`: `abs`, `labs`,
`atoi`, `atol`, `strtol`, `strtoul`, `malloc`, `calloc`, `realloc`,
`free`, `rand` (`RAND_MAX` 32767), `srand`, `exit`, `abort`.  GCC's own
`stdint.h`, `stddef.h`, `stdarg.h`, `stdbool.h`, `limits.h` are there.

## What is not there

Floating point; `FILE` and `fopen`; threads; the time of day (RT-11 keeps
no clock here); the ROM's console routines while the screen is in
colour - the ROM opens the VRAM window at 040000 on the caller's stack,
so a program under it that calls them loses its stack (`programming.md`,
"The dispatcher, the VRAM window").
