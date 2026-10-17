# UNIX for the MS 0515

An own Unix-like system for the machine, written from scratch in C and
PDP-11 assembler: LSX in reach - two processes at once without memory
protection, a file system with directories, a shell with pipelines.
[`SPEC.md`](SPEC.md) is the specification and the plan of stages.

Stages 1-5 are done.  The native ROM-B boots the system's diskette, the
boot block loads the kernel, /boot/kernel, and the kernel mounts the root,
loads its drivers - /boot/rd finds the EX board's RAM disk, /boot/fd takes
the diskettes - and runs /etc/init.  init gives its place
to the shell on /etc/rc: `mount -a` makes the RAM disk a volume at /tmp
by /etc/fstab, and gives its place to /bin/sh for the console, which runs
each command as a child - fork, exec, wait - and comes back with its prompt;
`a | b | c` is a pipeline, `cmd &` runs without being waited for, `> file`,
`>> file` and `< file` redirect.  A process has its own directory, its
children's the same; a path not beginning with / goes from it.  Two
processes run at once, one in each slot, the clock switching between them
every 4 frames; there is no swap, but a fork with both slots taken parks
the shell - its used part, some 5 KB - in the shadow copy of bank 6 until
a slot is free, so two commands of one's own run beside it: `sleep 10 &`,
then anything in front.  A pipeline's commands run one after another,
each one's output a file of /tmp the next one reads; `sh FILE` typed at
the shell runs the file in the shell itself; a pipeline is not put in the
background.
A file has one rwx, there being no users, kept in its inode: r to read
it, w to write it, empty it or remove it, x to run it.  The system's files,
/boot's and /bin's and every program, are r-x - neither removed nor written
over - the rest rw-; a new file is rw-, `chmod r-x FILE`
(or a digit, 5) changes it.  A directory's rights are not looked at.
The file system is written, each change going to the disk at once.  /bin has
`sh`, `ls` (`-l`, `-a`), `cat`, `echo`, `rm`, `mkdir`, `pwd`, `ln`, `mv`, `cp`, `du`, `df`, `chmod`, `mkfs`,
`mount`, `umount`,
`fdformat`, `sleep`, `hello`, `fail`, `pid`, `bigmem` (a program that owns
the machine), `rt11` (an RT-11 program run on the machine: `rt11
/rt11/game.sav`) and `rtfs` (an RT-11 diskette's side: `rtfs ls /dev/fd2`,
`rtfs get /dev/fd2 game.sav /rt11/game.sav`, `rtfs put DEVICE FILE [NAME]`); the shell's own commands are `cd` (alone: /home), `t` (the time),
`status` (the last command's), `exec` and `exit`, which ends the session:
a new shell takes its place.  The session's shell starts in /home, a
shell started by hand where it was started.  There is no login: one person at one
console, and no protection a password could stand on - any program can
write over the kernel.

## Building and booting

```
cmake -S rt11_devel/projects/unix -B rt11_devel/projects/unix/build -G Ninja
cmake --build rt11_devel/projects/unix/build
ms0515 --rom src/assets/rom/ms0515-romb.rom --disk0 rt11_devel/projects/unix/build/unix.dsk
```

The compiler is `$MS0515_GCC/bin/pdp11-aout-gcc`, or on the PATH; on
Windows the build runs in WSL.  `unix.dsk` is a double-sided diskette,
819200 bytes, its volume on both sides (`fdx0`); `--disk0` puts both its
sides in the first drive.  `unix400.dsk` is the same system on one side,
409600 bytes (`fd0`), for the emulators that take no other - in
ms0515btl, with ROM-B chosen in its menu:

```
MS0515BTL.exe /disk0:unix400.dsk /autostart
```

A second diskette there is the second drive, `/dev/fd2`: `mkfs /dev/fd2`
makes a volume on it, `mount /dev/fd2 /tmp` puts it in the tree.

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
- `kernel/dev.c` - the block devices: the diskettes through the ROM's
  160010 or /boot/fd - fd0..fd3 a drive's side, 800 blocks, fdx0 and fdx1
  a drive's both sides, 1600 - and the RAM disk, rd0, 1024 blocks through
  /boot/rd; the drivers loaded.
- `drivers/rd.s` - /boot/rd, the RAM disk's driver as a file: put down
  flat at 156300 in bank 6, 320 bytes at most, its header first (magic,
  version 1, size, init, rw).  init finds the EX board as EX.SYS does -
  two pages written, the first read back, a bus error caught on vector 4
  by a handler of its own - and tells the blocks, 0 with no board; rw
  moves a block's two pages through the board's PPI and data port, four
  bytes a step, each page with the interrupts masked.  With no /boot/rd,
  or one that is no driver, there is no RAM disk.
- `drivers/fd.s` - /boot/fd, the diskettes' driver as a file: put down
  flat at 155000 in bank 6, 704 bytes at most, its header first (magic,
  version 2, size, init, rw, format).  It drives the VG93 itself, as our RT-11 DZ handler does, asking
  where the head is only when the drive changes, the motor starts or a
  try fails; a sector's bytes go at priority 7, the seeks and the waits
  for the sector with the interrupts let in.  The kernel loads it once
  the root is mounted and sends the diskettes' blocks to it; with no
  /boot/fd, or one that is no driver, the ROM's 160010 stays.  A file
  copied takes half the time it took through the ROM.  format writes a
  track from the image fdformat built, by WRITE TRACK at priority 7.
- `kernel/sys.h` - what the calls' files share; `kernel/own.c` (a
  program that owns the machine: an a.out of magic 0401 from a diskette,
  the cache written out, its first block - the header and its own loader
  - read into the slot and the loader entered with the file's extents;
  the system is gone after), `kernel/ioctl.c` (a diskette's track
  written), `kernel/mount.c` (mount and umount), `kernel/part.c` (a
  driver read in).
- `kernel/ioctl.h` - what `ioctl` does, for the kernel and the programs:
  FD_WTRACK, a diskette's track written anew through /boot/fd.
- `kernel/bio.c` - the buffer cache in two levels: three blocks in hand in
  bank 6, the least recently used given up to the shadow copy of bank 1,
  15 blocks more, from where it comes back without the device; a write goes
  through at once, but for the bitmap's, which waits to the end of the
  call.
- `kernel/start.s` - the first instructions, and `xcopy`: words copied with
  the shadow bank or the other slot switched in, from bank 0, at priority
  7 - the cache's way into the shadow bank and fork's between the slots.
- `kernel/fs.c` - the superblock, the bitmap, inodes, a file's blocks by
  its extents - a file grows at its end, its last extent grown while the
  next block is free - `readi` and `writei`, directories, paths, creat,
  mkdir, unlink (a file goes with its last name, once nothing holds it);
  block devices - inodes of their own kind - and the volumes mounted on
  directories, a path going into them and out by "..".
- `kernel/exec.c` - a program into the slot: an a.out of the 0407 kind
  linked at 040000, its arguments on the stack as V6 laid them.
- `kernel/proc.c`, `proc.h` - the processes: the table, the two slots
  (banks 2-5 primary and extra, dispatcher bits 2-5), a kernel stack of 320 bytes
  each with a canary at its bottom, sleep and wakeup, the switch (`swtch`
  in trap.s) - by the clock every 4 frames too, at once in a process's
  own code, else at its system call's end - fork into the free slot, or,
  both taken, the parent parked in the shadow copy of bank 6 and the child
  in its slot, exit, which gives the slot back at once, a zombie keeping
  its place in the table until wait.
- `kernel/high.h` - the big tables in bank 6, a part of the kernel's
  bss there, all of its data being in bank 6: the mount table, the processes with their kernel stacks,
  the block buffers and the shadow bank's index, the inodes, the
  open files, the console's line and queue, exec's arguments.  The ROM is
  called on a stack of 157000-157400, under its cells.
- `kernel/sys.c` - the system calls by V6's numbers: exit, fork, read,
  write, open (to read, to write, both), close, wait, creat, unlink, exec,
  time, getpid, sync, dup, getppid, mkdir, chmod, stat, link and the
  rest; the open files, the system's, shared by a fork's descriptors.
  When pid 1 exits, a new shell runs in its place.
- `user/` - a process's start (`crt0.s`, `main(argc, argv)`), the calls as
  C functions (`sys.s`, `unistd.h`), a little library (`lib.c`), volumes
  (`vol.c`: mkfs.py's layout made on the machine), `init.c`,
  and the programs of /bin: `sh.c` (scripts too), `ls.c`,
  `cat.c`, `echo.c`, `rm.c`, `mkdir.c`, `mkfs.c`, `mount.c` (`mount -a`:
  /etc/fstab's volumes, in one process), `umount.c`,
  `fdformat.c` (a diskette's tracks built and written, a side or both),
  `hello.c`, `fail.c` (exits with 3), `pid.c` (its pid and its parent's).
- `user/own/` - the little library of a program that owns the machine:
  `crt0.s` (the loader, run first at 040020 and then from 157000, reading
  the file through the ROM's 160010 to 000760 and up, the command line
  to 000400; then the start: the vectors at an RTI, the bss,
  main(argc, argv), the end a reset), `rom.s` and `own.c` (the ROM's
  console, keyboard and diskette, `own.h`), `files.c` (the system's file
  system read by path on the program's own volume, no mounts), and
  `bigmem.c`, /bin/bigmem, its proof.  `tools/own_aout.py` gives the
  linked a.out its magic, 0401.
- `user/rt11/` - /bin/rt11 (`rt11.c`) and the little RT-11 monitor it
  carries (`mon.c`, `mon.s`, linked flat at 140000 where RT-11's RMON is,
  taken in by `monbin.s`): the .SAV loaded under it as RUN does, its
  vectors the monitor's, and the requests served - the console, .SETTOP,
  the .SAV's directory's files read, .EXIT a key and a reset.
- `root/etc/` - `rc`, `fstab`, `motd`, `numbers`.
- `root/` - the files the volume carries as they are: /etc/motd,
  /etc/numbers (a file of several blocks, for the tests).
- `tools/mkfs.py` - the diskette and its file system from PATH=FILE pairs:
  the boot block in block 0, the superblock naming /boot/kernel's blocks
  for it, the bitmap, the inode table, each file laid down whole in one
  extent; it refuses a kernel whose bss passes 040000.
- `tests/` - the system booted through the emulator's library on ROM-B, the
  screen read as text, the keys typed with the library's Typist: the boot to
  the shell's prompt, a superblock without the magic refused, block 0 where
  the ROM takes it; a line echoed and read, an empty one, the case of bare
  and shifted letters, a Russian word, BS, the clock, an unknown command; the volume read on the
  host by a second reading of the format (`Volume.hpp`), and on the machine
  `cat`, `ls`, a file across blocks line by line, a file not there, a
  program with its arguments; the shell back at its prompt after a command,
  a command by name and by path and one not there, the exit status, the
  children's pids, `exit` bringing a new shell; a pipe, a pipeline of
  three carrying a file of several blocks, its files in /tmp gone after,
  with the RAM disk and without, a pipeline's exit status, the background
  and a pipeline refused there, no swap file anywhere; files made by
  `>` and written over, a file of several blocks in one extent, mkdir and
  rm, `<`, a file kept across a boot, the errors - each then read off the
  diskette on the host, its bitmap counted (`Machine::imageNow`); /dev, mkfs
  and mount on the RAM disk read on the host from the board's memory
  (`Machine::ramDisk`), `..` out of a volume, umount; /etc/rc with the
  board and without, a new shell in / after a session, a mounted device
  refused for mkfs, a script; the single-sided system booted, a diskette
  in the second drive made a volume and read on the host, the double-sided
  root's sides busy, an empty drive; /boot/fd taking the diskettes over,
  not a block through the ROM after, a file copied in half the frames the
  ROM takes, the ROM's way without it, a broken one refused, one longer
  than its room refused; /boot/rd fitting its room, the kernel with no
  word of the board, a broken one leaving no RAM disk, a file through it
  read on the host from the board; fdformat writing a side or both of the second
  drive, read on the host - every sector E5, the rest untouched - then
  mkfs and mount on it, refused on a volume in use and without /boot/fd;
  bigmem owning the machine - its pattern over 001000..150000 read back,
  the root's superblock read through the ROM, a key and a reset, the
  system booted again - from the root and from side 1 of the second
  drive, refused from the RAM disk; its command line, and a file's last
  line read by the library's own reader; RT-11 programs by rt11 - the GCC
  toolset's HELLO, ARITH and INPUT, and SABOT2 to its game - from
  `rt11test.dsk`, the system with /rt11 of what the build found (the
  toolset's examples when they are built, SABOT2 when the recovery corpus
  holds it), a .SAV not there refused; the kernel one file, no /boot/hi.
  `bare.dsk`, the image without
  /etc/rc, is for the tests that mount by hand.
  `Machine::command` types a line and waits for the prompt, so the next is
  not typed into the last one's output; `Machine(image, true)` has the RAM
  disk.  Skipped when the image is not
  built; `--unix-dsk=<file>` names another.

The programs are linked freestanding at their own addresses and put down
flat with `objcopy`, no RT-11 `.SAV` about them.  Note for the GNU
assembler: a plain number is decimal (no trailing dot as in MACRO-11), and
`ld -Ttext` reads hex - the boot block's 070000 is `0x7000`.  And the
project builds Release, -O2, unless told otherwise (the kernel -Os).  The
compiler wants the toolset's patch as it is now (toolset/gcc/README.md, "A
trap in the compiler"): before it, a long read through a pointer into the
register pair the pointer was in came out wrong, and -Os walked the
kernel's tables by multiplying.

The build reports the kernel's memory and stops past its limits
(mkfs.py): the image's size, the code from 01000 to `_etext` against
040000, and the data from 140000 to `_end` against the 6656 bytes up to
the drivers' place - with four places in the process table, the two
slots', a parked one's and a zombie's, and their kernel stacks at 320
bytes (242 the deepest measured, tests/test_kstack.cpp keeping a margin):
an image of 14344 bytes, 1972 bytes free below 040000, 1848 free in
bank 6.  The kernel's data, the big tables with it, live in bank 6: the low
banks are its code's.  The kernel is built -Os - a quarter off its code - but for the loops
that move memory, which keep -O2.

Two things of ROM-B's keyboard the console turns round.  A bare Latin key
gives the capital - the ROM's choice, RT-11 only takes it - and the kernel
swaps the case of Latin letters, so a bare key is small as a Russian one
is.  And bit 2 of 157760, RUS/LAT's state, is the console's too: set, the
ROM draws the codes 0100..0177 with Cyrillic glyphs, KOI-7's way; the
kernel's console is eight-bit KOI-8, so its character out takes the bit
off for the character and puts it back for the keyboard.
