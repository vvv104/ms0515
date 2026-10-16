# UNIX for the MS 0515: the boot block, the kernel, the programs and the
# diskette image.
#
#     cmake -S rt11_devel/projects/unix -B rt11_devel/projects/unix/build -G Ninja
#     cmake --build rt11_devel/projects/unix/build
#
# The compiler is GCC for pdp11-aout (rt11_devel/toolset/gcc), found under
# $MS0515_GCC/bin or on the PATH.  Nothing here is an RT-11 program: the
# boot block is linked at 070000, where it runs, and the kernel at 01000,
# both put down flat; the programs are a.out files of the 0407 kind linked
# at 040000, the process's slot, their symbols stripped.  tools/mkfs.py
# builds the file system with them and root/ on a diskette, unix.dsk in the
# build folder, which tests/ boots.

cmake_minimum_required(VERSION 3.21)
# Release, -O2, unless asked otherwise, as Rt11Gcc.cmake has it: with no
# build type at all the code is -O0's, and GCC 15's pdp11 backend there
# loads a long through a pointer into the pair whose first register is the
# pointer, the second word read from wherever the first word points.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Release: -O2 (the toolchain file)")
endif()
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../toolset/gcc/pdp11-rt11.cmake")
project(unix LANGUAGES C ASM)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
foreach(tool objcopy nm strip)
    string(TOUPPER ${tool} TOOL)
    string(REGEX REPLACE "gcc$" "${tool}" PDP11_${TOOL} "${PDP11_GCC}")
endforeach()
set(ARITH "${CMAKE_CURRENT_SOURCE_DIR}/../../toolset/gcc/rt/arith.s")   # *, /, % of -m10

# ld reads -Ttext in hex: 070000 is 0x7000, 040000 0x4000.
add_executable(boot boot/boot.s)
target_link_options(boot PRIVATE -Wl,-Ttext,0x7000)
set_target_properties(boot PROPERTIES SUFFIX ".out")

add_executable(kernel
    kernel/start.s
    kernel/trap.s
    kernel/rom.s
    kernel/main.c
    kernel/lib.c
    kernel/clock.c
    kernel/tty.c
    kernel/bio.c
    kernel/fs.c
    kernel/exec.c
    kernel/proc.c
    kernel/sys.c
    "${ARITH}")
target_compile_options(kernel PRIVATE -Wall -Wextra -Werror)

# flat(<target>): the program's text and data as they lie in memory.
function(flat target)
    add_custom_command(OUTPUT ${target}.bin
        COMMAND "${PDP11_OBJCOPY}" -O binary $<TARGET_FILE:${target}> ${target}.bin
        DEPENDS ${target}
        COMMENT "${target}.bin")
endfunction()
flat(boot)
flat(kernel)

# program(<name> <sources>): a program of the system, <name>.x its a.out
# without symbols, as it goes on the volume.
set(PROGRAMS)
function(program name)
    add_executable(${name} user/crt0.s user/sys.s user/lib.c ${ARGN} "${ARITH}")
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    target_link_options(${name} PRIVATE -Wl,-Ttext,0x4000)
    add_custom_command(OUTPUT ${name}.x
        COMMAND "${PDP11_STRIP}" -o ${name}.x $<TARGET_FILE:${name}>
        DEPENDS ${name}
        COMMENT "${name}.x")
    set(PROGRAMS ${PROGRAMS} ${name}.x PARENT_SCOPE)
endfunction()
program(init user/init.c)
program(sh user/sh.c)
program(ls user/ls.c)
program(cat user/cat.c)
program(echo user/echo.c)
program(hello user/hello.c)
program(fail user/fail.c)
program(pid user/pid.c)

set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/root")
add_custom_command(OUTPUT unix.dsk
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/mkfs.py"
            --boot boot.bin --nm "${PDP11_NM}" --kernel-out $<TARGET_FILE:kernel> unix.dsk
            /boot/kernel=kernel.bin
            /etc/init=init.x
            /etc/motd=${ROOT}/etc/motd
            /etc/numbers=${ROOT}/etc/numbers
            /bin/sh=sh.x
            /bin/ls=ls.x
            /bin/cat=cat.x
            /bin/echo=echo.x
            /bin/hello=hello.x
            /bin/fail=fail.x
            /bin/pid=pid.x
    DEPENDS boot.bin kernel.bin ${PROGRAMS} "${CMAKE_CURRENT_SOURCE_DIR}/tools/mkfs.py"
            ${ROOT}/etc/motd ${ROOT}/etc/numbers
    COMMENT "unix.dsk")
add_custom_target(image ALL DEPENDS unix.dsk)
