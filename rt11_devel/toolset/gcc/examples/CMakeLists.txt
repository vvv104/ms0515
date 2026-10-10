# The example programs of the GCC toolset, and what its tests run
# (../tests): HELLO; ARITH, LIBC, LIBC2, INPUT and FX for the runtime and
# the C library; CMPLONG for the compiler; MACHINE, HELD, PORTS, DRAW and
# STREAM for the machine library; FILES for the files; DEEP for STACK;
# the CALC benchmark.
#
#     cmake -S rt11_devel/toolset/gcc/examples -B <build folder> -G Ninja
#     cmake --build <build folder>
#
# The programs come out in <build folder>/sav; `ms0515-run HELLO` there
# runs one.  The compiler: $MS0515_GCC/bin/pdp11-aout-gcc, or on the PATH.

cmake_minimum_required(VERSION 3.21)
include(../../cmake/Rt11Gcc.cmake)
project(gcc_examples LANGUAGES C ASM)

rt11_c_program(NAME HELLO SOURCES hello.c)
rt11_c_program(NAME ARITH SOURCES arith.c)
rt11_c_program(NAME LIBC SOURCES libc.c)
rt11_c_program(NAME LIBC2 SOURCES libc2.c)
rt11_c_program(NAME INPUT SOURCES input.c)
rt11_c_program(NAME CMPLONG SOURCES cmplong.c)
rt11_c_program(NAME MACHINE SOURCES machine.c)
rt11_c_program(NAME HELD SOURCES held.c)
rt11_c_program(NAME DRAW SOURCES draw.c)
rt11_c_program(NAME TEX SOURCES tex.c)
rt11_c_program(NAME STREAM SOURCES stream.c)
rt11_c_program(NAME DEEP SOURCES deep.c STACK 4096)
rt11_c_program(NAME PORTS SOURCES ports.c)
rt11_c_program(NAME FX SOURCES fx.c)

# A library of the project's own, and a program that takes from it.
rt11_c_library(NAME examples SOURCES lib/checksum.c INCLUDE lib)
rt11_c_program(NAME FILES SOURCES files.c LIBRARIES examples)
rt11_c_program(NAME CALC SOURCES calc.c tick.s)
