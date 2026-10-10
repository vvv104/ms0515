# pdp11-rt11.cmake - the CMake toolchain file for C programs on the MS 0515.
#
# GCC built for the target pdp11-aout (build-toolchain.sh) is the compiler;
# the processor is the T-11's instruction set, -m10: no EIS, no FPP.  A
# program links freestanding - no startup files or libraries of GCC's but
# libgcc - from 01000, text, data and bss one after another, which is what
# aout2sav.py turns into a .SAV.  Rt11Gcc.cmake names this file for a
# project; the compiler is found under $MS0515_GCC/bin, or on the PATH.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR pdp11)

find_program(PDP11_GCC pdp11-aout-gcc
    HINTS "$ENV{MS0515_GCC}/bin" "${MS0515_GCC}/bin"
    DOC "GCC for the target pdp11-aout (rt11_devel/toolset/gcc/build-toolchain.sh)")
if(NOT PDP11_GCC)
    message(FATAL_ERROR "pdp11-aout-gcc is not found: build it with "
                        "rt11_devel/toolset/gcc/build-toolchain.sh and set "
                        "MS0515_GCC to its prefix, or put its bin on the PATH")
endif()

set(CMAKE_C_COMPILER "${PDP11_GCC}")
set(CMAKE_ASM_COMPILER "${PDP11_GCC}")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS_INIT "-m10 -ffreestanding")
set(CMAKE_ASM_FLAGS_INIT "-m10")
# -O2 is the level the benchmark picked: -O3 unrolls and loses, -Os and -O1
# come out slower too (README.md).  Set in the cache, ahead of CMake's own
# "-O3 -DNDEBUG" for GNU compilers, which fills it only when empty.
set(CMAKE_C_FLAGS_RELEASE "-O2" CACHE STRING "Flags used by the C compiler during RELEASE builds")
# The other configurations an IDE may pick: the target has no debug
# output (-g only warns), so Debug is -O1 - quick to compile, still
# running at a pace - and the rest are what their names say.
set(CMAKE_C_FLAGS_DEBUG "-O1" CACHE STRING "Flags used by the C compiler during DEBUG builds")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2" CACHE STRING "Flags used by the C compiler during RELWITHDEBINFO builds")
set(CMAKE_C_FLAGS_MINSIZEREL "-Os" CACHE STRING "Flags used by the C compiler during MINSIZEREL builds")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-m10 -nostdlib -N -Wl,-Ttext,0x200")
set(CMAKE_C_STANDARD_LIBRARIES "-lgcc")
set(CMAKE_EXECUTABLE_SUFFIX_C ".out")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
