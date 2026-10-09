# STARS - a flight through a starfield, in C for GCC
# (rt11_devel/toolset/gcc): compiled on the host, run by ms0515-run.
#
#     cmake -S rt11_devel/projects/stars -B <build folder> -G Ninja
#     cmake --build <build folder>
#
# The program comes out as <build folder>/sav/STARS.SAV; `ms0515-run STARS`
# there flies it.  The compiler: $MS0515_GCC/bin/pdp11-aout-gcc, or on the
# PATH (rt11_devel/toolset/gcc/README.md).

cmake_minimum_required(VERSION 3.21)
include(../../toolset/cmake/Rt11Gcc.cmake)
project(stars LANGUAGES C ASM)

rt11_c_program(NAME STARS SOURCES stars.c machine.s proj.s)
