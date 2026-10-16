# UNIX for the MS 0515

An own Unix-like system for the machine, written from scratch in C and
PDP-11 assembler: LSX and Mini-Unix in reach - multitasking without memory
protection, a file system with directories, a shell with pipes.
[`SPEC.md`](SPEC.md) is the specification and the plan of stages.

Stages 1-4 are done, swap on the RAM disk: the native ROM-B boots the
system's diskette, the boot block loads the kernel, /boot/kernel, and the
kernel mounts the file system, looks for the EX board's RAM disk and runs
/etc/init.  init asks for a name at `login:` and gives its place to the
shell, /bin/sh, which runs each command as a child - fork, exec, wait - and
comes back with its prompt; `a | b | c` is a pipeline, `cmd &` runs without
being waited for.  Two processes are resident, one in each slot; with the
RAM disk the others wait there, swapped out, and come in when they are to
run.  Without it there are two at most, and the shell says so.  Stage 5 has
begun: the file system is written - `cmd > file`, `cmd < file`, `rm`,
`mkdir` - each change going to the diskette at once.  /bin has `sh`, `ls`,
`cat`, `echo`, `rm`, `mkdir`, `hello`, `fail` and `pid`; the shell's own
commands are `t` (the time), `status` (the last command's) and `exit`, which
ends the session.  Next: mount, mkfs on the machine, the swap file, /etc/rc
and fstab (SPEC.md "Volumes, swap and fstab").

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
- `kernel/dev.c` - the block devices: 0 the diskette through the ROM's
  160010, reading and writing; 1 the RAM disk, 1024 blocks.
- `kernel/bio.c` - the buffer cache: four blocks in bank 6, the device's
  and the block's, the least recently used given up; a write goes through
  at once.
- `kernel/fs.c` - the superblock, the bitmap, inodes, a file's blocks by
  its extents - a file grows at its end, its last extent grown while the
  next block is free - `readi` and `writei`, directories, paths, creat,
  mkdir, unlink (a file goes with its last name, once nothing holds it).
- `kernel/exec.c` - a program into the slot: an a.out of the 0407 kind
  linked at 040000, its arguments on the stack as V6 laid them.
- `kernel/proc.c`, `proc.h` - the processes: the table, the two slots
  (banks 2-5 primary and extra, dispatcher bits 2-5), a kernel stack of
  each with a canary at its bottom, sleep and wakeup, the switch (`swtch`
  in trap.s), swap - a process's used pages to its 32 KB area on the RAM
  disk and back - fork into the free slot or straight to the RAM disk,
  exit, wait.
- `kernel/high.h` - the big tables in bank 6, 140000-157000, outside the
  kernel's image: the processes with their kernel stacks, the block
  buffers, fork's bounce buffer, the pipes.
- `kernel/ramdisk.c` - the EX board: found by two pages written and one
  read back (a bus error caught on vector 4), pages of 256 bytes through
  its PPI and data port, each with the interrupts masked.
- `kernel/sys.c` - the system calls by V6's numbers: exit, fork, read,
  write, open (to read, to write, both), close, wait, creat, unlink, exec,
  time, getpid, sync, dup, pipe, getppid, mkdir; the open files, the
  system's, shared by a fork's descriptors; the pipes, a buffer of 256
  bytes each.  When pid 1 exits, init runs again.
- `user/` - a process's start (`crt0.s`, `main(argc, argv)`), the calls as
  C functions (`sys.s`, `unistd.h`), a little library (`lib.c`), `init.c`,
  and the programs of /bin: `sh.c`, `ls.c`, `cat.c`, `echo.c`, `rm.c`,
  `mkdir.c`, `hello.c`, `fail.c` (exits with 3), `pid.c` (its pid and its
  parent's).
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
  `cat`, `ls`, a file across blocks line by line, a file not there, a
  program with its arguments; the shell back at its prompt after a command,
  a command by name and by path and one not there, the exit status, the
  children's pids, `exit` back to `login:`; with the RAM disk a pipe, a
  pipeline of three carrying a file of several blocks, the background, the
  shell's state across swaps, and without it a pipe refused; files made by
  `>` and written over, a file of several blocks in one extent, mkdir and
  rm, `<`, a file kept across a boot, the errors - each then read off the
  diskette on the host, its bitmap counted (`Machine::imageNow`).
  `Machine::command` types a line and waits for the prompt, so the next is
  not typed into the last one's output; `Machine(image, true)` has the RAM
  disk.  Skipped when the image is not
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

The build reports the kernel's memory and stops past its limits
(mkfs.py): the image's size, the low part from 01000 to `_end` against
040000, and the bank-6 tables (the kernel's `_high_size`) against the 7680
bytes up to 157000 - with writing: an image of 12326 bytes, 3288 bytes
free below 040000, 672 free in bank 6.  The compiler puts even zeroed
arrays in .data, so they cost the image; the big ones live in bank 6 for
that.  The kernel is built -Os - a quarter off its code - but for the loops
that move memory and the RAM disk's pages, which keep -O2.

Two things of ROM-B's keyboard the console turns round.  A bare Latin key
gives the capital - the ROM's choice, RT-11 only takes it - and the kernel
swaps the case of Latin letters, so a bare key is small as a Russian one
is.  And bit 2 of 157760, RUS/LAT's state, is the console's too: set, the
ROM draws the codes 0100..0177 with Cyrillic glyphs, KOI-7's way; the
kernel's console is eight-bit KOI-8, so its character out takes the bit
off for the character and puts it back for the keyboard.
