# UNIX for the MS 0515: the boot block, the kernel and the diskette image.
#
#     cmake -S rt11_devel/projects/unix -B rt11_devel/projects/unix/build -G Ninja
#     cmake --build rt11_devel/projects/unix/build
#
# The compiler is GCC for pdp11-aout (rt11_devel/toolset/gcc), found under
# $MS0515_GCC/bin or on the PATH.  The programs are no RT-11 programs: they
# are linked freestanding at their own addresses and put down flat - the
# boot block at 070000, where it runs, the kernel at 01000 - and
# tools/mkimage.py lays them on a diskette as unix.dsk in the build folder,
# which tests/ boots.

cmake_minimum_required(VERSION 3.21)
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../toolset/gcc/pdp11-rt11.cmake")
project(unix LANGUAGES C ASM)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
string(REGEX REPLACE "gcc$" "objcopy" PDP11_OBJCOPY "${PDP11_GCC}")

add_executable(boot boot/boot.s)
target_link_options(boot PRIVATE -Wl,-Ttext,0x7000)          # ld reads hex: 070000
set_target_properties(boot PROPERTIES SUFFIX ".out")

add_executable(kernel kernel/start.s kernel/rom.s kernel/main.c)
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

add_custom_command(OUTPUT unix.dsk
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/mkimage.py"
            boot.bin kernel.bin unix.dsk
    DEPENDS boot.bin kernel.bin "${CMAKE_CURRENT_SOURCE_DIR}/tools/mkimage.py"
    COMMENT "unix.dsk")
add_custom_target(image ALL DEPENDS unix.dsk)
