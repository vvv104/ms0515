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
    kernel/sys.c
    kernel/mount.c
    kernel/ioctl.c
    kernel/own.c
    kernel/part.c
    "${ARITH}")
target_compile_options(kernel PRIVATE -Wall -Wextra -Werror)
# The kernel is held to 01000..040000 (mkfs.py reports it): -Os takes a
# quarter off its code; the loops that move memory keep -O2, coming last on
# the command line.
target_compile_options(kernel PRIVATE -Os)
set_source_files_properties(kernel/lib.c PROPERTIES COMPILE_OPTIONS -O2)
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

# The drivers, put down flat in bank 6, their headers first (kernel.h):
# /boot/fd, the diskettes', linked at 155000 (DRV_BASE); /boot/rd, the RAM
# disk's, at 156300 (RD_BASE).
add_executable(fd drivers/fd.s)
target_link_options(fd PRIVATE -Wl,-Ttext,0xDA00)
set_target_properties(fd PROPERTIES SUFFIX ".out")
flat(fd)
add_executable(rd drivers/rd.s)
target_link_options(rd PRIVATE -Wl,-Ttext,0xDCC0)
set_target_properties(rd PROPERTIES SUFFIX ".out")
flat(rd)

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
program(sh user/sh.c user/cwd.c)
program(ls user/ls.c user/when.c)
program(cat user/cat.c)
program(echo user/echo.c)
program(hello user/hello.c)
program(fail user/fail.c)
program(pid user/pid.c)
program(rm user/rm.c)
program(mkdir user/mkdir.c)
program(mkfs user/mkfs.c user/vol.c)
program(mount user/mount.c user/vol.c user/mtab.c)
program(umount user/umount.c user/mtab.c)
program(fdformat user/fdformat.c)
program(pwd user/pwd.c user/cwd.c)
program(ln user/ln.c)
program(mv user/mv.c user/copy.c)
program(cp user/cp.c user/copy.c)
program(rtfs user/rtfs.c)
program(du user/du.c)
program(df user/df.c)
program(chmod user/chmod.c)
program(sleep user/sleep.c)
program(rmdir user/rmdir.c)
program(wc user/wc.c)
program(head user/head.c)
program(tail user/tail.c)
program(grep user/grep.c user/re.c)
program(ed user/ed.c user/re.c)
program(sort user/sort.c)
program(uniq user/uniq.c)
program(tr user/tr.c)
program(cmp user/cmp.c)
program(od user/od.c)
program(find user/find.c)
program(sync user/sync.c)
program(ps user/ps.c)
program(kill user/kill.c)
program(more user/more.c)
program(date user/date.c user/when.c)
program(clear user/clear.c)
program(uname user/uname.c)
program(ramroot user/ramroot.c user/clone.c user/mtab.c)
program(mkboot user/mkboot.c user/clone.c)

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
# The system's files, the release images' (ubix.dsk, ubix01.dsk); the
# tests' images have the tests' programs and data besides.
set(SYSTEM_FILES
    /boot/kernel=kernel.bin
    /etc/init=init.x
    /etc/motd=${ROOT}/etc/motd
    /bin/sh=sh.x
    /bin/ls=ls.x
    /bin/cat=cat.x
    /bin/echo=echo.x
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
    /bin/sleep=sleep.x
    /bin/rmdir=rmdir.x
    /bin/wc=wc.x
    /bin/head=head.x
    /bin/tail=tail.x
    /bin/grep=grep.x
    /bin/ed=ed.x
    /bin/sort=sort.x
    /bin/uniq=uniq.x
    /bin/tr=tr.x
    /bin/cmp=cmp.x
    /bin/od=od.x
    /bin/find=find.x
    /bin/sync=sync.x
    /bin/ps=ps.x
    /bin/kill=kill.x
    /bin/more=more.x
    /bin/date=date.x
    /bin/clear=clear.x
    /bin/uname=uname.x
    /bin/ramroot=ramroot.x
    /bin/mkboot=mkboot.x
    /bin/rt11=rt11.x
    --dir /tmp
    --dir /home
    --dir /mnt/floppy1
    --dir /mnt/floppy2)
set(VOLUME_FILES
    /etc/numbers=${ROOT}/etc/numbers
    /bin/hello=hello.x
    /bin/fail=fail.x
    /bin/pid=pid.x
    /bin/bigmem=bigmem.x
    /boot/rd=rd.bin
    ${SYSTEM_FILES})
set(VOLUME_DEPENDS boot.bin kernel.bin fd.bin rd.bin ${PROGRAMS} "${MKFS}" ${ROOT}/etc/motd ${ROOT}/etc/numbers)

# The tests' /etc/rc: the system's without its clear, so that what the
# boot said stays on the screen for them to read; the release images keep
# the clear.
file(READ ${ROOT}/etc/rc RC_TEXT)
string(REPLACE "\nclear\n" "\n" RC_TEST "${RC_TEXT}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/rc.test "${RC_TEST}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ROOT}/etc/rc)
set(RC_TEST ${CMAKE_CURRENT_BINARY_DIR}/rc.test)

# unix.dsk: the system as it boots - /etc/rc mounts the volumes and turns
# no more.  bare.dsk: the same without /etc/rc, init going straight to the
# shell, for the tests that mount by hand.
add_custom_command(OUTPUT unix.dsk
    COMMAND "${Python3_EXECUTABLE}" "${MKFS}"
            --boot boot.bin --nm "${PDP11_NM}" --kernel-out $<TARGET_FILE:kernel> unix.dsk
            /etc/rc=${RC_TEST} /etc/fstab=${ROOT}/etc/fstab /boot/fd=fd.bin ${VOLUME_FILES}
    DEPENDS ${VOLUME_DEPENDS} ${RC_TEST} ${ROOT}/etc/fstab
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
            /etc/rc=${RC_TEST} /etc/fstab=${ROOT}/etc/fstab /boot/fd=fd.bin ${RT11_FILES}
            ${VOLUME_FILES}
    DEPENDS ${VOLUME_DEPENDS} ${RC_TEST} ${ROOT}/etc/fstab ${RT11_DEPENDS}
    COMMENT "rt11test.dsk")
add_custom_command(OUTPUT bare.dsk
    COMMAND "${Python3_EXECUTABLE}" "${MKFS}" --boot boot.bin bare.dsk ${VOLUME_FILES}
    DEPENDS ${VOLUME_DEPENDS}
    COMMENT "bare.dsk")
# unix400.dsk: the system on one side, 400 KB - the image the other
# emulators take; a second one in the second drive is fd2.
add_custom_command(OUTPUT unix400.dsk
    COMMAND "${Python3_EXECUTABLE}" "${MKFS}" --sides 1 --boot boot.bin unix400.dsk
            /etc/rc=${RC_TEST} /etc/fstab=${ROOT}/etc/fstab /boot/fd=fd.bin ${VOLUME_FILES}
    DEPENDS ${VOLUME_DEPENDS} ${RC_TEST} ${ROOT}/etc/fstab
    COMMENT "unix400.dsk")
# ubix.dsk and ubix01.dsk: UBIX as it is given out, the system alone.
# ubix.dsk, both sides, for this emulator and the machine, with a RAM disk
# or without; ubix01.dsk, one side, for the other emulators, which take
# no other image and have no RAM disk: without /boot/rd, so nothing looks
# for the board - MS0515BTL halts at the monitor on its port, where the
# T-11 traps to 4.
add_custom_command(OUTPUT ubix.dsk
    COMMAND "${Python3_EXECUTABLE}" "${MKFS}" --boot boot.bin ubix.dsk
            /etc/rc=${ROOT}/etc/rc /etc/fstab=${ROOT}/etc/fstab /boot/fd=fd.bin /boot/rd=rd.bin ${SYSTEM_FILES}
    DEPENDS ${VOLUME_DEPENDS} ${ROOT}/etc/rc ${ROOT}/etc/fstab
    COMMENT "ubix.dsk")
add_custom_command(OUTPUT ubix01.dsk
    COMMAND "${Python3_EXECUTABLE}" "${MKFS}" --sides 1 --boot boot.bin ubix01.dsk
            /etc/rc=${ROOT}/etc/rc /etc/fstab=${ROOT}/etc/fstab /boot/fd=fd.bin ${SYSTEM_FILES}
    DEPENDS ${VOLUME_DEPENDS} ${ROOT}/etc/rc ${ROOT}/etc/fstab
    COMMENT "ubix01.dsk")
add_custom_target(image ALL DEPENDS unix.dsk bare.dsk unix400.dsk rt11test.dsk ubix.dsk ubix01.dsk)
