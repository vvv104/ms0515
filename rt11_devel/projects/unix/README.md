# UNIX for the MS 0515

An own Unix-like system for the machine, written from scratch in C and
PDP-11 assembler: LSX and Mini-Unix in reach - multitasking without memory
protection, a file system with directories, a shell with pipes.
[`SPEC.md`](SPEC.md) is the specification and the plan of stages.

Stages 1-3 are done: the native ROM-B boots the system's diskette, the
boot block loads the kernel, /boot/kernel, and the kernel mounts the file
system and runs /etc/init by exec.  init asks for a name at `login:` and
takes commands a line at a time - `ls`, `cat`, `run` a program from /bin,
`t` the time - through the system calls; the keyboard comes by its
interrupt, the clock by the frame's.

## Building and booting

```
cmake -S rt11_devel/projects/unix -B rt11_devel/projects/unix/build -G Ninja
cmake --build rt11_devel/projects/unix/build
ms0515 --rom src/assets/rom/ms0515-romb.rom --disk0 rt11_devel/projects/unix/build/unix.dsk
```

The compiler is `$MS0515_GCC/bin/pdp11-aout-gcc`, or on the PATH; on
Windows the build runs in WSL.  The image is a double-sided diskette,
819200 bytes; `--disk0` puts both its sides in the first drive.

## What is there

- `boot/boot.s` - block 0.  The ROM reads it to address 0 and jumps there;
  it moves itself up to 070000, out of the kernel's way, reads the
  superblock, and when it carries the system's magic, the kernel's blocks it
  names to the address it names, through the ROM's 160010; then it jumps to
  the kernel.  Anything amiss ends in HALT, the ROM's console monitor.
- `kernel/start.s` - the kernel's first instructions at 01000: the stack in
  bank 0, the bss cleared, `main()`.
- `kernel/rom.s`, `rom.h` - the kernel's one door into the ROM: a table of
  the ROM's entries, the native ROM-B's now, an own ROM's later; every call
  at priority 7 on a stack of its own.
- `kernel/trap.s` - the vectors' entries: TRAP saves the process's
  registers on its stack and goes over to the kernel's; the interrupts;
  the priority.
- `kernel/main.c` - the vectors set, the clock started, init loaded into
  slot A at 040000 from the blocks the superblock names, and entered.
- `kernel/clock.c` - the frame interrupt: 50, 60 or 72 a second by the
  jumpers, the seconds since the boot, the cursor's blink.
- `kernel/tty.c` - the console: the line edited at the keyboard's
  interrupt with echo and BS, the lines queued for read(); a bare letter is
  small, Latin as Russian.
- `kernel/fs.h` - the file system on the disk: the superblock, the 32-byte
  inode with five extents, the 16-byte directory entry.
- `kernel/bio.c` - the buffer cache: four blocks in bank 0 for now, the
  least recently used given up.
- `kernel/fs.c` - the superblock read, inodes, a file's blocks by its
  extents, `readi`, paths.
- `kernel/exec.c` - a program into the slot: an a.out of the 0407 kind
  linked at 040000, its arguments on the stack as V6 laid them.
- `kernel/sys.c` - the system calls by V6's numbers: exit, read, write,
  open, close, exec, time; the open files.  When the process exits, init
  runs again.
- `user/` - a process's start (`crt0.s`, `main(argc, argv)`), the calls as
  C functions (`sys.s`, `unistd.h`), a little library (`lib.c`), `init.c`,
  and the programs of /bin: `hello.c`, `echo.c`.
- `root/` - the files the volume carries as they are: /etc/motd,
  /etc/numbers (a file of several blocks, for the tests).
- `tools/mkfs.py` - the diskette and its file system from PATH=FILE pairs:
  the boot block in block 0, the superblock naming /boot/kernel's blocks
  for it, the bitmap, the inode table, each file laid down whole in one
  extent; it refuses a kernel whose bss passes 040000.
- `tests/` - the system booted through the emulator's library on ROM-B, the
  screen read as text, the keys typed with the library's Typist: the boot to
  `login:`, a superblock without the magic refused, block 0 where the ROM
  takes it; a name echoed and greeted, the case of bare and shifted letters,
  a Russian name, BS, the clock, an unknown command; the volume read on the
  host by a second reading of the format (`Volume.hpp`), and on the machine
  `cat`, `ls`, a file across blocks line by line, a file not there, `run`
  with arguments and init back after.  Skipped when the image is not
  built; `--unix-dsk=<file>` names another.

The programs are linked freestanding at their own addresses and put down
flat with `objcopy`, no RT-11 `.SAV` about them.  Note for the GNU
assembler: a plain number is decimal (no trailing dot as in MACRO-11), and
`ld -Ttext` reads hex - the boot block's 070000 is `0x7000`.  And the
project builds Release, -O2, unless told otherwise: without a build type
the code is -O0's, where GCC 15's pdp11 backend loads a long through a
pointer into the register pair whose first register is the pointer, and
reads the second word from wherever the first one points (`mov 010(r0),r0`
then `mov 012(r0),r1`).  -O1, -O2 and -Os write no such code in this tree
or the toolset's.

Two things of ROM-B's keyboard the console turns round.  A bare Latin key
gives the capital - the ROM's choice, RT-11 only takes it - and the kernel
swaps the case of Latin letters, so a bare key is small as a Russian one
is.  And bit 2 of 157760, RUS/LAT's state, is the console's too: set, the
ROM draws the codes 0100..0177 with Cyrillic glyphs, KOI-7's way; the
kernel's console is eight-bit KOI-8, so its character out takes the bit
off for the character and puts it back for the keyboard.
