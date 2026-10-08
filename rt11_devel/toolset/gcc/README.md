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
├── build-toolchain.sh   binutils + gcc for pdp11-aout from their sources
├── pdp11-rt11.cmake     the CMake toolchain file (Rt11Gcc.cmake names it)
├── aout2sav.py          the linked a.out as a .SAV: block 0, bss, stack
├── rt/                  the runtime, assembled with each project
│   ├── crt0.s           _start: main() then .EXIT; __main, exit
│   ├── arith.s          __mulhi3, __divhi3, __modhi3, __udivhi3, __umodhi3
│   └── emt.s            RT-11's requests as C functions
├── include/rt11.h       their declarations
├── examples/            HELLO, ARITH (the helpers checked), CALC (the benchmark)
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
  makes every `*`, `/` and `%` of an `int` a call into `rt/arith.s`, the
  `long` ones go to libgcc.  There is no floating point.
- The runtime is freestanding: no stdio yet, no malloc, no files.  What
  there is: `rt11.h` - `.TTYOUT`, `.TTYIN`, `.PRINT`, `.SETTOP`, `exit()`
  - and `main()`'s return value is the program's end.
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
print; without them it skips.  Build the examples first:

```
cmake -S rt11_devel/toolset/gcc/examples -B rt11_devel/toolset/gcc/examples/build -G Ninja
cmake --build rt11_devel/toolset/gcc/examples/build
```
