# GCC for the MS 0515

Modern C for the machine: GCC built as a cross compiler for the target
`pdp11-aout`, with the processor's instruction set (`-m10`: the T-11's,
no EIS, no FPP), a runtime that makes its output an RT-11 program, and a
CMake module that makes a C program an ordinary CMake project.  The
machine takes no part in the build - everything is compiled on the host,
as `ms0515-disk` works - and only the finished `.SAV` runs on it, by
`ms0515-run` or from a diskette.

```
rt11_devel/toolset/gcc/
├── build-toolchain.sh   binutils + gcc for pdp11-aout from their sources, patched
├── gcc-pdp11-cmpsi.patch  the fix of the backend's comparison of longs
├── patch-gcc.py         what made the patch; remakes it for another GCC
├── pdp11-rt11.cmake     the CMake toolchain file (Rt11Gcc.cmake names it)
├── aout2sav.py          the linked a.out as a .SAV: block 0, bss, stack
├── rt/                  the runtime, assembled with each project
│   ├── crt0.s           _start: main() then .EXIT; __main, exit, rt11_memtop
│   ├── arith.s          the 16- and 32-bit multiply and divide, __xorhi3
│   └── emt.s            RT-11's requests as C functions
├── libc/                stdio over the console, string, ctype, stdlib, the heap
├── machine/             the screen, the clock, the keyboard, the joystick, the speaker
├── include/             rt11.h, ms0515.h, stdio.h, stdlib.h, string.h, ctype.h
├── examples/            HELLO, ARITH, LIBC and MACHINE (the runtime checked), CMPLONG
│                        (the compiler's comparison of longs), CALC (the benchmark)
└── tests/               the examples run by ms0515-run's machine (doctest)
```

## The compiler

```
rt11_devel/toolset/gcc/build-toolchain.sh [<prefix>]      default ~/pdp11-gcc
```

downloads binutils 2.44 and GCC 15.2.0, builds them for `pdp11-aout` (C
only) and installs `pdp11-aout-gcc`, `-as`, `-ld`, `-objdump` and the
rest under `<prefix>/bin`.  libgcc is compiled with `-m10` too, so that
nothing it brings in executes an instruction the processor lacks.  The
host needs gcc, make, bison, flex, texinfo and the GMP/MPFR/MPC
development packages; the build takes a quarter of an hour.  On Windows
this is a WSL job for now: the compiler, cmake and ninja run there, and
`ms0515-run.exe` is called from there as a Windows program.

Set `MS0515_GCC` to the prefix, or put `<prefix>/bin` on the PATH.

## A project

```cmake
cmake_minimum_required(VERSION 3.21)
include(<repository>/rt11_devel/toolset/cmake/Rt11Gcc.cmake)
project(myprog LANGUAGES C ASM)

rt11_c_program(NAME MYPROG SOURCES myprog.c helpers.s [STACK 2048])
```

The include stands before `project()`: it names the toolchain file, which
`project()` reads.  `cmake -S . -B build -G Ninja && cmake --build build`
leaves `build/sav/MYPROG.SAV`, and `ms0515-run MYPROG` there runs it.
`myprog` is the a.out target (`build/myprog.out`) for `pdp11-aout-objdump
-d` when the code is in question.

## What a program finds

- `int` and pointers are 16 bits, `long` 32, `long long` 64; `-m10`
  makes every `*`, `/` and `%` a call into `rt/arith.s` - the 16-bit
  ones and the 32-bit ones too, since libgcc has no 16-bit helpers for
  pdp11 and its 32-bit division is miscompiled (below).  There is no
  floating point.
- The C library (`libc/`, `include/`): `stdio.h` is the console -
  `putchar`, `getchar`, `puts`, `printf` with `d i u x X o c s %`, the
  flags `-` and `0`, a width, a precision for `s` and `l` for a long,
  `sprintf`, `snprintf`; `'\n'` goes out as CR LF and comes in as the
  LF after the CR.  `string.h` whole, `ctype.h` for ASCII, `stdlib.h`
  with `atoi`, `strtol`, `abs`, `rand`, `malloc` and company - the heap
  is the memory above the stack, taken from the monitor by `.SETTOP`.
  `rt11.h` has the monitor's requests themselves - `.TTYOUT`, `.TTYIN`,
  `.PRINT`, `.SETTOP`, `exit()` - and `main()`'s return value is the
  program's end.
- Files, a block of 512 bytes at a time, as RT-11 gives them (`rt11.h`,
  `rt/files.c`): a name in RAD50, `.LOOKUP` once at the start (it needs
  the USR), `.READW` during the run, `.READ` left to the disk's handler
  and `.WAIT`ed for, `.ENTER` and `.WRITW` for a file of the program's
  own, `.CLOSE`; the error's code from byte 052.  The monitor lies
  behind the VRAM window, so a program with the screen on closes the
  window round a request (`ms_window`).  No stdio over them yet:
  `fopen` and friends are not there, a game streams its data by block.
- The machine (`machine/`, `ms0515.h`): what a program does outside
  the monitor, as `docs/programming.md` says it and the ports do it -
  the screen in 320x200 colour through the VRAM window at 0100000 with
  the border, the attributes and the pixels; the frame interrupt as a
  clock; the keyboard taken off the ROM's vector into a ring, with the
  held-key timers the keyboard's lack of release codes calls for; the
  joystick port; the speaker bit.  Everything is C over the registers
  but the two interrupt handlers (RTI) and the PSW's two instructions.
  `examples/machine.c` uses all of it; STARS is built on it.
- The program lies from 01000: text, data, bss, then the stack's room
  (`STACK`, 1024 bytes when not said).  Above the high limit the memory
  is the program's to take by `rt11_settop()`.  The video window and the
  dispatcher are the program's own business, as in MACRO-11
  (`docs/programming.md`).
- The objects are a.out, not RT-11's: a program is linked whole by
  GCC's `ld`, there is no linking with MACRO-11 objects or SYSLIB.
  Assembler of the project's own goes in GNU as syntax beside the C
  (`examples/tick.s`): `$` for an immediate, `*$` for an absolute
  address (MACRO's `@#`), a leading `0` for octal, every C symbol with a
  leading underscore, the arguments on the stack at `2(sp)`, `4(sp)`...,
  the result in r0, r0 and r1 scratch, r2..r5 kept.

## A trap in the compiler

GCC 15.2's pdp11 backend compiles a **signed comparison of two longs**
wrongly when their high words are equal: it compares the high words,
and if they are the same, the low words - with the same signed branch,
so a low word with bit 15 set reads as negative.  `40000L > 5L` is
false, `32768L >= 0` too.  Comparisons whose high words differ, unsigned
comparisons and tests for equality are right.  libgcc's own 32-bit
division falls into this (its `__udivmodsi4` tests the divisor's sign),
which is why `rt/arith.s` carries the 32-bit divisions as well.

`build-toolchain.sh` applies a fix, `gcc-pdp11-cmpsi.patch` (made by
`patch-gcc.py`, which remakes it for another GCC): after the last
compare of a two- or four-word comparison, five words set N and V from
C, so that a signed branch reads the unsigned order of the low words
and an unsigned one sees what it saw.  `examples/cmplong.c` is the
proof, and its test fails on a compiler without the fix.

The same patch changes one more thing: a shift by a constant.  Without
`ASH` the backend writes a shift by up to 3 as single shifts and a
longer one as a loop - four words, three instructions a step - so `x >>
5` cost seventeen instructions; the patch writes shifts out up to 8
when speed is wanted (`-O2`), and keeps the loop from 4 under `-Os`.

`-m10` also means the PDP-11/10, which had no `XOR` and no `SOB`; the
T-11 has both, and GCC asks for a `__xorhi3` instead (in `rt/arith.s`).

## How fast

The CALC benchmark of 2026-09-25 (primes below 10000 by trial division
without multiply, divide or shift, timed by the machine's own 50 Hz
clock):

| MACRO-11 | GCC -O2 | DECUS C (`register`) | DECUS C | PAS1 | FORTRAN IV |
|---|---|---|---|---|---|
| 25.0 s | 29.3 s | 47.2 s | 76.1 s | 107.0 s | 135.2 s |

`-O3` is slower (41 s: the unrolling costs more than it saves), `-Os`
and `-O1` come to 33 s.

## Tests

`gcc_tests`, built with the emulator's tests (`rt11_devel/CMakeLists.txt`),
runs the examples' `.SAV`s from `examples/build/sav` - or
`--gcc-sav=<folder>` - on ms0515-run's machine and checks what they
print; without them it skips, unless the folder was named, when a
missing program fails.  CI (`.github/workflows/ci.yml`) builds the
compiler on one Linux job, cached by the script and the patch, builds
the examples and runs the suite that way.  Build the examples first:

```
cmake -S rt11_devel/toolset/gcc/examples -B rt11_devel/toolset/gcc/examples/build -G Ninja
cmake --build rt11_devel/toolset/gcc/examples/build
```
