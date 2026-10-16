# UNIX for the MS 0515

An own Unix-like system for the machine, written from scratch in C and
PDP-11 assembler: LSX and Mini-Unix in reach - multitasking without memory
protection, a file system with directories, a shell with pipes.
[`SPEC.md`](SPEC.md) is the specification and the plan of stages.

Stages 1-5 are done.  The native ROM-B boots the system's diskette, the
boot block loads the kernel, /boot/kernel, and the kernel mounts the root,
looks for the EX board's RAM disk and runs /etc/init.  init gives its place
to the shell on /etc/rc: `mount -a` makes the RAM disk a volume at /tmp and
turns swap on by /etc/fstab - /tmp/swap, made there, else the diskette's
/swap - and getty asks for a name at `login:` and starts /bin/sh, which runs
each command as a child - fork, exec, wait - and comes back with its prompt;
`a | b | c` is a pipeline, `cmd &` runs without being waited for, `> file`
and `< file` redirect.  Two processes are resident, one in each slot; with
swap on the others wait in the swap file and come in when they are to run.
The file system is written, each change going to the disk at once.  /bin has
`sh`, `ls`, `cat`, `echo`, `rm`, `mkdir`, `mkfs`, `mount`, `umount`,
`swapon`, `hello`, `fail` and `pid`; the shell's own commands are `t` (the
time), `status` (the last command's), `exec` and `exit`, which ends the
session and brings getty back.

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
  160010 - fd0..fd3 a drive's side, 800 blocks, fdx0 and fdx1 a drive's
  both sides, 1600 - and the RAM disk, rd0, 1024 blocks.
- `drivers/fd.s` - /boot/fd, the diskettes' driver as a file: put down
  flat at 155000 in bank 6, its header first (magic, version, size, init,
  rw).  It drives the VG93 itself, as our RT-11 DZ handler does, asking
  where the head is only when the drive changes, the motor starts or a
  try fails; a sector's bytes go at priority 7, the seeks and the waits
  for the sector with the interrupts let in.  The kernel loads it once
  the root is mounted and sends the diskettes' blocks to it; with no
  /boot/fd, or one that is no driver, the ROM's 160010 stays.  A file
  copied takes half the time it took through the ROM.
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
  in trap.s), swap - a file in one extent cut into 32 KB areas, a
  process's used blocks out to one and back - fork into the free slot or
  straight to swap, exit, wait.
- `kernel/high.h` - the big tables in bank 6, 140000-155000, outside the
  kernel's image: the mount table, the processes with their kernel stacks,
  the block buffers and the shadow bank's index, the pipes, the inodes, the
  open files, the console's line and queue, exec's arguments.  The ROM is
  called on a stack of 157000-157400, under its cells.
- `kernel/ramdisk.c` - the EX board: found by two pages written and one
  read back (a bus error caught on vector 4), pages of 256 bytes through
  its PPI and data port, each with the interrupts masked.
- `kernel/sys.c` - the system calls by V6's numbers: exit, fork, read,
  write, open (to read, to write, both), close, wait, creat, unlink, exec,
  time, getpid, sync, dup, pipe, getppid, mkdir; the open files, the
  system's, shared by a fork's descriptors; the pipes, a buffer of 256
  bytes each.  When pid 1 exits, init runs again.
- `user/` - a process's start (`crt0.s`, `main(argc, argv)`), the calls as
  C functions (`sys.s`, `unistd.h`), a little library (`lib.c`), volumes
  and swap files (`vol.c`: mkfs.py's layout made on the machine), `init.c`,
  `getty.c`, and the programs of /bin: `sh.c` (scripts too), `ls.c`,
  `cat.c`, `echo.c`, `rm.c`, `mkdir.c`, `mkfs.c`, `mount.c` (`mount -a`:
  /etc/fstab's volumes and swap, in one process), `umount.c`, `swapon.c`,
  `hello.c`, `fail.c` (exits with 3), `pid.c` (its pid and its parent's).
- `root/etc/` - `rc`, `fstab`, `motd`, `numbers`.
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
  diskette on the host, its bitmap counted (`Machine::imageNow`); /dev, mkfs
  and mount on the RAM disk read on the host from the board's memory
  (`Machine::ramDisk`), `..` out of a volume, umount; swap on the RAM
  disk's file and the diskette's, the swap file held; /etc/rc with the
  board and without, getty again after a session, a mounted device
  refused for mkfs, a script; the single-sided system booted, a diskette
  in the second drive made a volume and read on the host, the double-sided
  root's sides busy, an empty drive; /boot/fd taking the diskettes over,
  not a block through the ROM after, a file copied in half the frames the
  ROM takes, the ROM's way without it, a broken one refused.  `bare.dsk`, the image without
  /etc/rc, is for the tests that mount and swap by hand.
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
(mkfs.py): the image's size, the low part from 01000 to `_end` against
040000, and the bank-6 tables (the kernel's `_high_size`) against the 6656
bytes up to 155000, the driver's place above - with the kernel stacks at
320 bytes (242 the deepest measured, tests/test_kstack.cpp keeping a
margin): an image of 14512 bytes, 1358 bytes free below 040000, 332 free
in bank 6.  The compiler puts even zeroed
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
