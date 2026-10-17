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
# build type at all the code would be -O0's, slow and large.
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
    kernel/dev.c
    kernel/bio.c
    kernel/fs.c
    kernel/exec.c
    kernel/proc.c
    kernel/ramdisk.c
    kernel/sys.c
    kernel/mount.c
    kernel/ioctl.c
    kernel/own.c
    kernel/part.c
    "${ARITH}")
target_compile_options(kernel PRIVATE -Wall -Wextra -Werror)
# The kernel is held to 01000..040000 (mkfs.py reports it): -Os takes a
# quarter off its code; the loops that move memory and the RAM disk's pages
# keep -O2, coming last on the command line.
target_compile_options(kernel PRIVATE -Os)
set_source_files_properties(kernel/lib.c kernel/ramdisk.c PROPERTIES COMPILE_OPTIONS -O2)
# The code at 01000, the data at 140000 in bank 6 (kernel/high.h).
target_link_options(kernel PRIVATE -Wl,-Tdata,0xC000)

# flat(<target>): the program's text and data as they lie in memory.
function(flat target)
    add_custom_command(OUTPUT ${target}.bin
        COMMAND "${PDP11_OBJCOPY}" -O binary $<TARGET_FILE:${target}> ${target}.bin
        DEPENDS ${target}
        COMMENT "${target}.bin")
endfunction()
flat(boot)

# kernel.bin: the code, then the data's first values - start.s copies them
# up to bank 6, where they are linked.
add_custom_command(OUTPUT kernel.bin
    COMMAND "${PDP11_OBJCOPY}" -O binary -j .text $<TARGET_FILE:kernel> kernel.text
    COMMAND "${PDP11_OBJCOPY}" -O binary -j .data $<TARGET_FILE:kernel> kernel.data
    COMMAND "${Python3_EXECUTABLE}" -c
            "import sys; open(sys.argv[3], 'wb').write(open(sys.argv[1], 'rb').read() + open(sys.argv[2], 'rb').read())"
            kernel.text kernel.data kernel.bin
    DEPENDS kernel
    COMMENT "kernel.bin"
    VERBATIM)

# The diskettes' driver, /boot/fd: linked at 155000 in bank 6 (kernel.h
# DRV_BASE), put down flat, its header first.
add_executable(fd drivers/fd.s)
target_link_options(fd PRIVATE -Wl,-Ttext,0xDA00)
set_target_properties(fd PROPERTIES SUFFIX ".out")
flat(fd)

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
program(rm user/rm.c)
program(mkdir user/mkdir.c)
program(mkfs user/mkfs.c user/vol.c)
program(mount user/mount.c user/vol.c)
program(umount user/umount.c)
program(fdformat user/fdformat.c)
program(pwd user/pwd.c)
program(ln user/ln.c)
program(mv user/mv.c user/copy.c)
program(cp user/cp.c user/copy.c)
program(rtfs user/rtfs.c)
program(du user/du.c)
program(df user/df.c)
program(chmod user/chmod.c)

# own_program(<name> <sources>): a program that owns the machine
# (kernel/own.c, user/own/own.h): linked at 001000 with the ROM's little
# library, its a.out given magic 0401.
function(own_program name)
    add_executable(${name} user/own/crt0.s user/own/rom.s user/own/own.c user/own/files.c
                   ${ARGN} "${ARITH}")
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    add_custom_command(OUTPUT ${name}.x
        COMMAND "${PDP11_STRIP}" -o ${name}.a $<TARGET_FILE:${name}>
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/own_aout.py" ${name}.a ${name}.x
        DEPENDS ${name} "${CMAKE_CURRENT_SOURCE_DIR}/tools/own_aout.py"
        COMMENT "${name}.x")
    set(PROGRAMS ${PROGRAMS} ${name}.x PARENT_SCOPE)
endfunction()
own_program(bigmem user/own/bigmem.c)

# /bin/rt11 and the little RT-11 monitor it carries (user/rt11): the
# monitor linked flat at 140000, where RT-11's RMON is, its image taken
# into /bin/rt11 by monbin.s.
add_executable(rt11mon user/rt11/mon.s user/rt11/mon.c user/own/rom.s user/own/own.c
               user/own/files.c "${ARITH}")
target_compile_options(rt11mon PRIVATE -Wall -Wextra -Werror)
target_link_options(rt11mon PRIVATE -Wl,-Ttext,0xC000)
set_target_properties(rt11mon PROPERTIES SUFFIX ".out")
flat(rt11mon)
own_program(rt11 user/rt11/rt11.c user/rt11/monbin.s)
target_include_directories(rt11 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
set_source_files_properties(user/rt11/monbin.s PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/rt11mon.bin")

set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/root")
set(MKFS "${CMAKE_CURRENT_SOURCE_DIR}/tools/mkfs.py")
# The volume's files, for both images: the system and its programs.
set(VOLUME_FILES
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
    /bin/rm=rm.x
    /bin/mkdir=mkdir.x
    /bin/mkfs=mkfs.x
    /bin/mount=mount.x
    /bin/umount=umount.x
    /bin/fdformat=fdformat.x
    /bin/pwd=pwd.x
    /bin/ln=ln.x
    /bin/mv=mv.x
    /bin/cp=cp.x
    /bin/rtfs=rtfs.x
    /bin/du=du.x
    /bin/df=df.x
    /bin/chmod=chmod.x
    /bin/bigmem=bigmem.x
    /bin/rt11=rt11.x
    --dir /tmp
    --dir /home)
set(VOLUME_DEPENDS boot.bin kernel.bin fd.bin ${PROGRAMS} "${MKFS}" ${ROOT}/etc/motd ${ROOT}/etc/numbers)

# unix.dsk: the system as it boots - /etc/rc mounts the volumes and turns
# no more.  bare.dsk: the same without /etc/rc, init going straight to the
# shell, for the tests that mount by hand.
add_custom_command(OUTPUT unix.dsk
    COMMAND "${Python3_EXECUTABLE}" "${MKFS}"
            --boot boot.bin --nm "${PDP11_NM}" --kernel-out $<TARGET_FILE:kernel> unix.dsk
            /etc/rc=${ROOT}/etc/rc /etc/fstab=${ROOT}/etc/fstab /boot/fd=fd.bin ${VOLUME_FILES}
    DEPENDS ${VOLUME_DEPENDS} ${ROOT}/etc/rc ${ROOT}/etc/fstab
    COMMENT "unix.dsk")
# rt11test.dsk: the system with /rt11 of RT-11 programs, for the tests of
# /bin/rt11 alone - the GCC toolset's examples when they are built
# (toolset/gcc/examples/build/sav), SABOT2 when the recovery corpus holds
# it.  Looked for when the project is configured; neither goes into the
# system's images.
set(RT11_FILES)
set(RT11_DEPENDS)
set(GCC_SAV "${CMAKE_CURRENT_SOURCE_DIR}/../../toolset/gcc/examples/build/sav")
foreach(name HELLO ARITH INPUT)
    if(EXISTS "${GCC_SAV}/${name}.SAV")
        string(TOLOWER ${name} lower)
        list(APPEND RT11_FILES /rt11/${lower}.sav=${GCC_SAV}/${name}.SAV)
        list(APPEND RT11_DEPENDS "${GCC_SAV}/${name}.SAV")
    endif()
endforeach()
set(SABOT2 "${CMAKE_CURRENT_SOURCE_DIR}/../../../disk_recovery/work/corpus/export/data/src/extracted/System")
if(EXISTS "${SABOT2}/SABOT2.SAV" AND EXISTS "${SABOT2}/SABOT2.DAT")
    list(APPEND RT11_FILES /rt11/sabot2.sav=${SABOT2}/SABOT2.SAV /rt11/sabot2.dat=${SABOT2}/SABOT2.DAT)
    list(APPEND RT11_DEPENDS "${SABOT2}/SABOT2.SAV" "${SABOT2}/SABOT2.DAT")
endif()
add_custom_command(OUTPUT rt11test.dsk
    COMMAND "${Python3_EXECUTABLE}" "${MKFS}" --boot boot.bin --dir /rt11 rt11test.dsk
            /etc/rc=${ROOT}/etc/rc /etc/fstab=${ROOT}/etc/fstab /boot/fd=fd.bin ${RT11_FILES}
            ${VOLUME_FILES}
    DEPENDS ${VOLUME_DEPENDS} ${ROOT}/etc/rc ${ROOT}/etc/fstab ${RT11_DEPENDS}
    COMMENT "rt11test.dsk")
add_custom_command(OUTPUT bare.dsk
    COMMAND "${Python3_EXECUTABLE}" "${MKFS}" --boot boot.bin bare.dsk ${VOLUME_FILES}
    DEPENDS ${VOLUME_DEPENDS}
    COMMENT "bare.dsk")
# unix400.dsk: the system on one side, 400 KB - the image the other
# emulators take; a second one in the second drive is fd2.
add_custom_command(OUTPUT unix400.dsk
    COMMAND "${Python3_EXECUTABLE}" "${MKFS}" --sides 1 --boot boot.bin unix400.dsk
            /etc/rc=${ROOT}/etc/rc /etc/fstab=${ROOT}/etc/fstab /boot/fd=fd.bin ${VOLUME_FILES}
    DEPENDS ${VOLUME_DEPENDS} ${ROOT}/etc/rc ${ROOT}/etc/fstab
    COMMENT "unix400.dsk")
add_custom_target(image ALL DEPENDS unix.dsk bare.dsk unix400.dsk rt11test.dsk)
