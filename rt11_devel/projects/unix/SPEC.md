# UNIX for the Elektronika MS 0515 - specification

The working copy of this specification is the owner's shared document; this
file is its English version in the repository, kept in step with it.

## Goals and scope

The goal is an own Unix-like operating system for the MS 0515, written from
scratch in C and PDP-11 assembler.  It is not Linux: the Linux kernel needs a
32-bit processor and megabytes of memory.

The target in features is LSX and Mini-Unix of the mid-1970s: multitasking
without memory protection, a file system with directories, a shell with pipes,
a set of utilities.

- Target platforms: this emulator and the real machine.
- Tools: GCC 15 for `pdp11-aout` with `-m10` from `rt11_devel/toolset/gcc`.
- Out of scope: binary compatibility with V6/V7, memory protection, a
  multi-user system.

## The hardware

The machine dictates the model of the system: there is no MMU and no
user/kernel mode, so processes are not protected from one another, and memory
is shared out by banks.

| Part | What matters for the system |
| --- | --- |
| KR1807VM1 (T-11), 7.5 MHz | No MMU, no user/kernel modes, no MUL/DIV/ASH (EIS), no FPP; multiplication and division in software (`arith.s`). The PSW is 8 bits; four interrupt levels, 4-7. |
| RAM 128 KB | 16 physical banks of 8 KB.  Banks 0-6 switch one by one between a primary and an extra copy by bits 0-6 of the dispatcher 177400 (1 = primary). |
| ROM 16 KB (2 x K573RF4B) | The upper 8 KB is always seen at 160000-177377.  All 16 KB at 140000-177377 with bit 7 of register A, and then RAM bank 6 is gone.  Reset executes at 172000 (a JMP there), HALT goes to 172004, both with PSW 340. |
| VRAM 16 KB | Through a 16 KB window, dispatcher bit 7; its place by bits 11:10 - 000000, 040000 or 100000. |
| Timer KR580VI53 | Interrupt 0100 (priority 6) once a frame: 50/60/72 Hz by jumpers, enabled by dispatcher bit 9.  Channel 2 is the speaker. |
| Keyboard MS 7004 on a KR580VV51 | Interrupt 0130, priority 5; data 177440, status 177442. |
| Floppy, KR1818VG93 | No DMA and no vector: DRQ (bit 1), INTRQ (bit 0, inverted) and ready (bit 2) are polled in register B (177602).  Drive, motor and side by bits 0-3 of register A.  80 tracks x 10 sectors x 512 bytes a side; the 2:1 interleave and the skew between tracks are the driver's, not the controller's. |
| RAM disk 512 KB (optional) | Not in the CPU's address space.  A byte port 177550 with auto-increment, pages of 256 bytes, the page in PPI port A (177530).  Not on every machine. |
| Serial port VV51 | Vectors 0110/0114, priority 6, by the documentation.  In the emulator the port is a stub without interrupts, and its transmit registers 177720/177722 are taken by the paravirtual HD. |

What the hardware dictates besides:

- **The dispatcher 177400 and register A 177600 are write-only.**  The ROM
  keeps their copies at 157700 and 157720; programs read and change the copy,
  then write the register.  The new system does the same.
- **An interrupt's priority comes from its request line, not from the PSW in
  the vector:** the timer 6, the keyboard 5, the "monitor" (vector 064,
  dispatcher bit 8) 4.  Masked requests merge into one: a long masked stretch
  loses ticks.
- **The VRAM window hides the RAM under it:** vectors, a stack and handlers in
  it cannot be reached while the window is open.
- **The keyboard sends no key-up** and repeats only the last key.

Sources in the repository: `docs/hardware/*.md`, `docs/programming.md` (the
handbook of the machine), the emulator's core `src/core`, the annotated
listings of ROM-A and ROM-B.

## Architecture

At first the system runs on the native ROM-B, with no new firmware: the ROM
loads block 0 of the diskette, our boot block in it reads the kernel, and the
kernel takes the console, the keyboard, a disk block and the font from the
ROM.  The kernel is an ordinary file on the diskette.  All the kernel's calls
into the ROM are gathered in one jump table, so an own RT-11 compatible ROM can
later take the native one's place without changing the kernel.

```
  +--------------------------------------+
  | Processes - slots A and B            |
  | banks 2-5, 32 KB each, switched      |      +---------------------------+
  | through 177400; init, sh, utilities  |      | Storage                   |
  +------------------+-------------------+      |                           |
                     | TRAP - system calls      |  Diskette - own FS        |
  +------------------v-------------------+      |  800 blocks a side, or    |
  | Kernel - the file /boot/kernel       |<---->|  1600 on both; /boot, /bin|
  | banks 0, 1, 6; up to 22 KB           |  FS  |  swap 3 x 32 KB when      |
  | processes, FS, swap, system calls    | swap |  there is no RAM disk     |
  | buffer cache in shadow bank 1        |      |                           |
  +------------------+-------------------+      |  RAM disk 512 KB (option) |
                     | one jump table           |  swap 3 x 32 KB, /tmp     |
  +------------------v-------------------+      |  /tmp                     |
  | ROM: native ROM-B first, own later   |----->|                           |
  | console, keyboard, disk block, font  | boot +---------------------------+
  +--------------------------------------+
```

Processes enter the kernel through `TRAP`; the kernel reaches the console and
the disk through the ROM's services (native ROM-B first, then the own ROM),
the rest of the hardware by itself.  Swap goes to the RAM disk when the board
is there, else to the diskette.

## Memory map

The kernel has banks 0, 1 and 6 (24 KB), a process banks 2-5 (32 KB) in two
physical copies.  The extended ROM is off while the system runs, else bank 6
is gone.

| Addresses (octal) | Bank | Use |
| --- | --- | --- |
| 000000-017777 | 0 | Vectors, kernel data, the kernel stack.  Not switched: the vectors are here. |
| 020000-037777 | 1 | Kernel code.  The shadow copy is the buffer cache. |
| 040000-137777 | 2-5 | The process's space, 32 KB.  The primary copies are slot A, the extra ones slot B. |
| 140000-157777 | 6 | Kernel data and code: the tables (high.h) up to 151600, the kernel's second part /boot/hi at 151600-155000 (kernel/hi.h: the calls the boot does not need - mount, umount, ioctl, a program that owns the machine - linked there against the kernel's symbols, loaded once the root is mounted), the diskettes' driver /boot/fd at 155000-157000, the stack the kernel calls the ROM on at 157000-157400.  The top of the primary copy, 157614-157777, is the ROM's own stack and variables: the kernel leaves it alone.  The shadow copy is free: the kernel's stacks are in the primary one, so a copy into it would want a bounce in bank 0. |
| 160000-177377 | - | ROM (the upper 8 KB), always seen. |
| 177400-177777 | - | I/O registers. |

- **Process slots.**  Switching slot A <-> B is one write of bits 2-5 to
  177400.  Two processes are resident at once.
- **The shadow copies of banks 1 and 6** are switched on by the kernel only
  from code that itself lies in bank 0 or the ROM: code switching its own bank
  vanishes from under its feet.
- **The VRAM window** covers the process's space.  The native ROM-B opens the
  window at 040000-077777 for the output of a character, on the caller's
  stack, so the kernel calls the console from its own stack in bank 0, as our
  DEC monitor does.  The own ROM will run on a stack of its own and remove the
  trap.
- **The kernel's budget:** 24 KB less ~0.5 KB of vectors and the stack, ~0.2
  KB of the ROM's cells - about 20-22 KB for code and data.  Banks 0 and 1
  hold the code and the small data, loaded from the diskette; bank 6, up to
  155000, the big tables (the processes with their kernel stacks, the
  buffers, the pipes), cleared at the start, and the driver above them.
  Every build reports both.

## The ROM: native ROM-B first, own later

The first stages run on the native ROM-B: it already has the console, the
keyboard, a disk block and the font - the 4-5 KB a BIOS was to save.  The own
ROM is stage 9, once the kernel has settled.

**What the kernel takes from the native ROM-B.**  The same entry points RT-11
calls.

| Address | What it does | For the kernel |
| --- | --- | --- |
| 160000 | a character out of R0: VT52 commands (ESC A B C D H J K Y I T U), SO/SI | the console, 80 x 25 in 640 x 200 |
| 160004 | a key into R0, C = no key; F-keys as `CSI n ~`, Russian letters after RUS/LAT | from the vector 130 handler into a ring buffer |
| 160010 | a disk block by an RT-11 queue element (its pointer at 157730): interleave, skew, retries, motor; C = error; no formatting | reading and writing blocks through a small wrapper |
| 160014 | the cursor blink | from the timer interrupt |
| 160020 / 160024, 160030 / 160034 | a word / a byte into another bank and out of it | copying between slots and shadow banks |
| 167070, 170470 | the 8x8 font: Latin and Cyrillic, 1.3 KB | no font of its own in RAM |
| 172000 / 172004 | reset / HALT -> the ROM's ODT-like console monitor | debugging |

The ROM has cells of its own at the top of bank 6: 157700 the copy of the
dispatcher, 157720 the copy of register A with the motor flags, 157730 the
disk element, 157760 the console flags, the ROM's stack from 157614 down.
Programs read them too, so they are part of the interface.

**Calling the services.**  All the ROM calls of the kernel are gathered in one
module with a jump table: now it leads to the addresses of ROM-B, later to the
table of the own ROM.  The call goes from the kernel's stack in bank 0 (the
trap with the VRAM window, see the memory map).

**The own ROM (stage 9)** replaces the native one and stays compatible with
RT-11.  The model is ROM-B.  Rodionov's system (RT15SJ) is not supported: it
jumps into ROM-A's internals by their addresses.

| System | ROM-A | ROM-B | Own ROM | What it uses |
| --- | --- | --- | --- | --- |
| OMEGA, the vvv104 build | no | yes | yes | the console; 160014 the blink (ROM-A has a cassette loader there) |
| OMEGA 059 | yes | yes | yes | the console |
| OSA | yes | yes | yes | the console; the disk through 160010 |
| Mihin, OS-16SJ | yes | yes | yes | the console through its own table at 157400 |
| DEC RT-11 from the sources | yes | yes | yes | the console; the blink when it recognises ROM-B |
| Rodionov, RT15SJ | yes | no | no | ROM-A's internal addresses |

The compatibility contract - what the own ROM must keep:

1. The entry points of the table above at the same addresses and with the
   same behaviour; at 160014 a jump with the offset 3420, by which the DEC
   monitor recognises ROM-B.
2. The cells 157700, 157720, 157730, 157760 and the stack below 157614;
   nothing below 154000 touched after the boot.
3. The boot: the hardware set up as by ROM-B, the dispatcher 002177, the
   640 x 200 mode, block 0 to address 0 and `JMP @#0`, the drive and the side
   in 157720, no vectors set.
4. The font in the upper 8 KB with the same glyphs: programs find it by the
   pattern of the digit 0 and the Cyrillic capital A; the pseudographics at 157000 for the codes 200-277.
5. Not to repeat the VRAM window on the caller's stack: a stack of its own
   does not break compatibility.

**The layout of the own ROM.**  The lower half is switched on only together
with bank 6 going away, where the ROM's variables lie, so everything that runs
while a system runs stands in the upper half.

| Half | What is there | Size |
| --- | --- | --- |
| Upper 8 KB, always seen | the services shared by RT-11 and the new system at the old addresses: the VT52 console (~1.8 KB), the font (1.3), the keyboard (0.8), the disk (0.6), the banks, the start; the new system's table of entries and its services (RAM disk, block by number, formatting) | ~4.7 KB taken, ~3.3 KB for the new |
| Lower 8 KB, only before a system starts | the self-test, the ODT-like monitor, the boot menu: RT-11 (block 0) or the new system (the kernel by file name); the pseudographics at 157000 | up to 8 KB |

**Firmware.**  In the emulator this is a ROM image file.  On the real machine,
two 2764-compatible 8 KB chips.  An EEPROM (28C64) is handier, to spare the UV
erasing: the pinout to be checked against the schematic.

## The kernel

The kernel is loaded from the diskette, takes up to 20-22 KB and keeps two
processes resident, the others in swap.

**Processes and scheduling.**

- Two slots in banks 2-5 (A and B), switched by a write to the dispatcher
  177400.
- Four processes at most - the shell and a pipe of three, as the two pipes
  the kernel has: the machine is not for more.  The two outside the slots
  lie in swap.
- A quantum by the timer interrupt (vector 0100), that is 50 Hz.  Preemptive
  switching only between resident processes; swapping out and in on a block or
  after several quanta.
- No protection: a process can spoil the kernel.  This is an accepted limit.

**Swap.**

- Swap is a file on some volume, contiguous, in one extent: `swapon`
  checks that, and the kernel writes its blocks directly, past the file
  system.  The file cannot be removed while swap is on.
- With a RAM disk it is `/tmp/swap` there (see "Volumes, swap and
  fstab"): a 32 KB image moves in ~0.15-0.3 s.  Without one it is `/swap`
  on the diskette, ~1.5-2 s an image.  Each 32 KB of the file is a
  process's area, one kept for the exchange: three areas, 96 KB, for the
  two processes out; a smaller file allows fewer.  Only the image's used
  part moves.
- The file's bytes do not matter, only its inode and its blocks: `mkfs -s`
  lays it down with the volume, nothing written into it.
- Until swap is on there are no more processes than slots, two; whether
  the board is there is found at the start, as EX.SYS does: a pattern
  written and checked.

**System calls.**  Through `TRAP` (vector 034); the number in the instruction,
the arguments in registers.  The first set (~25): `fork`, `exec`, `exit`,
`wait`, `getpid`, `kill`, `signal`, `open`, `close`, `read`, `write`, `lseek`,
`creat`, `unlink`, `link`, `mkdir`, `chdir`, `stat`, `pipe`, `dup`, `mount`,
`sync`, `time`, `brk`; `ioctl` for what a device does besides its blocks -
a diskette's track written (`kernel/ioctl.h`).

**Drivers.**

| Device | Model |
| --- | --- |
| Console | The keyboard by interrupt 0130: the handler calls the ROM's 160004 and puts the byte into a ring buffer; output through 160000.  Line and raw modes.  Eight-bit KOI-8: a bare letter is small, Latin as Russian (ROM-B gives the Latin capital bare, the kernel swaps), and Latin is drawn Latin whatever RUS/LAT says (ROM-B's bit 2 of 157760 taken off for the character). |
| Floppy | A driver of its own as a file, /boot/fd, as RT-11's handlers are: put down flat at 155000 in bank 6, 1 KB at most, its header the magic, the version of the door (2), its size, init, rw (a drive's side as the ROM numbers it, a block 0..799) and format (a track 0..79 written from the image WRITE TRACK takes).  The kernel loads it through ROM-B's 160010 once the root is mounted; with none, or one that is no driver, 160010 stays.  The driver drives the VG93 itself (drivers/fd.s, after our RT-11 DZ handler) and asks where the head is only when the drive changes, the motor starts or a try fails; a file copied takes half the time it took through 160010.  DRQ polled: the CPU is busy while a sector is read. |
| RAM disk | A block device of 512 bytes; interrupts masked for a page of 256 bytes (~1 ms). |
| HD (paravirtual) | Only for development in the emulator: a big root FS without the 400 KB limit. |
| Serial port | Later: exchanging files with a PC. |

**The buffer cache** is in two levels: three buffers in bank 6 are the blocks
in hand, and a block given up goes to the shadow copy of bank 1 - 15 blocks,
the 16th fork's bounce between the slots - and comes back from there without
the device.  A block is in one level or the other; the shadow bank holds
clean blocks only.  The shadow bank is switched on only by `xcopy`, in bank 0,
at priority 7, with not a word pushed meanwhile.

**Building.**  GCC `-m10 -Os`, an own `crt0` and linker script.  The section
sizes (`size`, the map file) are a measure of every build: the ceiling is
22 KB.

## The file system and the diskette format

An own format, inode-based, with directories and names of up to 14 characters.
A volume lies on a side of a diskette, 800 blocks of 512 bytes, or on both
sides as one, 1600 blocks.

**The devices.**  A side is a device and a drive's two sides are one more,
as RT-11 has DZ for the sides and DV for both:

| Device | What | Blocks | The ROM's unit |
| --- | --- | --- | --- |
| `fd0`, `fd1` | the boot drive's sides 0 and 1 | 800 | 0, 2 |
| `fd2`, `fd3` | the second drive's sides 0 and 1 | 800 | 1, 3 |
| `fdx0`, `fdx1` | the boot drive, the second, both sides | 1600 | side 0's, then side 1's |

A single-sided image, 409600 bytes, is a side: the other emulators take no
other (ms0515btl has four drives of one side each, its drive by bits 0-1
of register A, the side bit not heard), so there the second diskette is
`fd2`.  The root is the boot drive's: the superblock, block 1, is on side
0 either way, and a volume larger than a side makes the root `fdx0`.  A
block of `fdx` is cached under its side's name, so the two never disagree;
a device that shares blocks with a mounted volume is busy.

**Geometry.**  A block = a sector of 512 bytes.  The driver turns an LBN into
the track, the side and the sector.  A block number is 16 bits.  A file takes
whole blocks of 512 bytes, but its length in the inode is in bytes, so a text
is not padded with nulls as in RT-11.  Nothing smaller than a block is shared
out: the VG93 writes only a whole sector, the tails of several files in one
block would tie them to one write and spoil them together, and the gain is
half a block a file, about 4% of the diskette.

**The volume layout (800 or 1600 blocks).**

| Blocks | What |
| --- | --- |
| 0 | The boot block: the ROM loads it to address 0 and jumps there |
| 1 | The superblock: the magic, the version, the volume size, the starts of the areas, the number of inodes, the label; the kernel's extents for the boot block |
| 2 | The bitmap of free blocks (a bit a block, 200 bytes at most) |
| 3-10 | The inode table: 8 blocks x 16 inodes = 128 files |
| 11-138 | Reserved: once the swap area; swap is a file now ("Volumes, swap and fstab") |
| 139-1599 | Data |

**The superblock (block 1)**, words: the magic 054125, the version, the
volume's blocks, /boot/kernel's first block and blocks with its load address
and entry (for the boot block, which needs no file system), the bitmap's
block, the inode table's first block and blocks, the swap area's first block
and blocks, the data's first block, the number of inodes, the volume's number
(a long), a label of 16 bytes.  A long is two words, the high word first, as
the compiler keeps one.

**Inode (32 bytes).**

```c
struct dinode {
    uint16_t mode;      /* type (file/directory/device) + permissions */
    uint8_t  nlink;
    uint8_t  nextents;  /* 0..5 */
    uint32_t size;      /* bytes */
    uint32_t mtime;     /* seconds since the epoch */
    struct { uint16_t start, len; } ext[5];  /* extents, in blocks */
};                      /* 2+1+1+4+4+20 = 32 */
```

Extents instead of a block list: the allocator lays a file down contiguously,
and an executable is read straight through, with no needless head moves.  Five
extents almost always suffice; when fragmented, a defragmenting utility (like
`squeeze`).

**A program** is an a.out of the 0407 kind, as V6's: a header of eight
words (the magic, the text's, the data's and the bss's bytes, the symbols',
the entry), the text and the data, linked at 040000.  exec puts them at
040000, clears the bss, and lays the arguments on the stack at the slot's
top as V6 did: the strings, the pointers to them ending in 0, their count
under SP.

**A directory** is a file of 16-byte entries: 2 bytes the inode number + 14
bytes the name (KOI-8, case kept).  32 entries a block.  The root is inode 1.

**The interleave** is the driver's numbering, not the format's: every track
holds sectors 1 to 10 in order (`fdformat`, as our RT-11 FORMAT's FMTDZ
does), and the driver puts block n in sector (ILV[n mod 10] + 2 * track - 2)
mod 10 + 1 - 2:1 and two sectors of skew a track, the machine's own
diskettes' layout.

## Volumes, swap and fstab

A volume is a file system on a block device: `/dev/fd0`..`/dev/fd3` and
`/dev/fdx0`, `/dev/fdx1` the diskettes ("The devices"), `/dev/rd0` the RAM
disk.  To the kernel the RAM disk is just another
device; what is mounted where, and where swap is, `/etc/fstab` says:

```
# device      where  what to do
fd0           /      root
rd0           /tmp   mkfs,swap=96,optional  # made at the boot with /swap in it; no board - skipped
/tmp/swap     swap   optional            # the RAM disk's swap file turned on
/swap         swap   optional            # the fallback: a file on the diskette
```

What the system has for it:

1. Block devices: the diskette - reading and writing through 160010, an
   own driver later; the RAM disk.  The block cache tells the device and
   the block.
2. Writing to the file system: `creat`, `write`, `unlink`, `mkdir`, blocks
   given out by the bitmap, `sync`.
3. `mount`/`umount`: several volumes in one tree.
4. `mkfs` on the machine itself - the format of `tools/mkfs.py`; the tests
   hold one against the other.  `mkfs -s KB` puts a swap file `/swap` of
   KB kilobytes in the new volume: its inode, one extent, its blocks
   taken.
5. `swapon` with a file's path: the file in one extent, its blocks written
   directly, not to be removed while swap is on.
6. `/etc/rc` and the reading of `fstab`: `mount -a` makes and mounts the
   volumes and then turns on the first swap line it can - a file under a
   volume that did not mount passed by - all in one process, since two are
   all there may be before swap is on.

The raw swap on the RAM disk of stage 4, a 32 KB area a process with no
file system, goes: a file on a mounted volume takes its place.  Swapping
processes out and in stays as it is; only where it writes changes.  Blocks
11-138 of the diskette, kept for swap, become a reserve.

## The boot sequence

From power-on to the shell's prompt about 2-3 seconds, of them ~1-2 s for
reading the kernel off the diskette.

1. The T-11 starts at 172000, PSW = 0340.  The native ROM-B sets up the
   hardware, runs its self-test, reads block 0 of the diskette to address 0
   and jumps there.  RT-11 boots the same way, so no separate compatibility
   mode is needed.  No diskette or an error - the ROM prints its load error
   message and stays in its console monitor.
2. Our boot block in block 0 (512 bytes) moves itself higher, into the
   process's space, reads the superblock and takes from it the extent of
   `/boot/kernel`, written there by `mkfs`.  It needs no file name: finding
   the kernel by name comes with the own ROM.
3. The kernel is loaded through 160010 into banks 0 and 1; the boot block
   jumps to its entry point.
4. The kernel: its own vectors, the bank-6 tables cleared, the RAM disk
   looked for, the root mounted from fd0 (fdx0 when it takes both
   sides), process 1 out of `/etc/init` in
   slot A.  No swap yet, two processes at most.
5. `init` does `exec sh /etc/rc`: pid 1 becomes the shell running the
   script, and the second slot is free.
6. `/etc/rc` by `fstab`: `mkfs rd0` in the second slot, `mount rd0 /tmp`,
   `swapon` - now there is swap, and more processes may be.
7. At its end `/etc/rc` does `exec /etc/getty`: it asks for `login:` and
   runs `sh`; when the session ends the kernel runs `getty` again.  The
   classic init, staying a process of its own and sleeping in swap, may
   come back later.

The own ROM will later add a menu: RT-11 or the new system, another kernel
file by name, "insert the system diskette" with waiting.

**The root diskette:**

```
/boot/kernel
/etc/init  /etc/rc
/bin/      sh, ls, cat, ed, ...
/tmp/      (the RAM disk mounted, when there is one)
/home/
```

## The user environment

The utilities come in levels; with the first level and `ed` the system is
already usable.  The sizes are estimates for GCC `-Os`.

| Level | Utilities | Estimate |
| --- | --- | --- |
| 1. Minimum | `init`, `sh` (pipes, `<` `>`, `&`, scripts), `ls`, `cat`, `cp`, `mv`, `rm`, `ln`, `mkdir`, `rmdir`, `pwd`, `echo`, `ps`, `kill`, `mount`, `umount`, `sync`, `df`, `date` | ~30-40 KB in all, `sh` ~6-8 KB |
| 2. Text | `ed`, `grep`, `wc`, `head`, `tail`, `tr`, `uniq`, `cmp`, `od`, `sort` (temporary files on the RAM disk), `more` | `ed` ~8 KB |
| 3. Screen editor | vi-like, 80 x 25 | - |
| 4. Languages | Forth, BASIC, `as` (a PDP-11 assembler), `dc`/`bc`; a C compiler on the machine later, multi-pass | Forth 4-8 KB |
| 5. MS 0515 specifics | `rt11` (ls/get/put on RT-11 diskettes), `fdformat`, `beep`/`play` (timer channel 2), `view` (`.SCR`), exchange with a PC over RS-232 (XMODEM/Kermit) | - |
| 6. Compatibility | Running RT-11 `.SAV` in userland, EMT trapped | Ambitious |

First: `sh`, `ls`, `cat`, `ed`.  Then `rt11` (the bridge to the software
collection) and Forth (interactive work with the hardware).

## Tools on the host

Everything is built on the host; the machine and the emulator only run the
result.

- **The toolchain** - the existing GCC 15 `pdp11-aout -m10`.  Own `crt0` and
  libc over the new system calls (based on the present libc of
  `rt11_devel/toolset/gcc`), a linker script for the kernel and one for the
  ROM.
- **`mkfs` and the diskette image builder** - after the model of
  `ms0515-disk`: make a volume, put files and directories in, write the kernel
  and the boot block into block 0 with the kernel's extents in the superblock
  (`installboot`), check the FS (`fsck`).
- **The ROM builder** - a 16 KB image, cut into two 8 KB chips, a check that
  the working part fits in the upper 8 KB.
- **The emulator** - the choice of the ROM image; knowledge of the kernel in
  the debugger: symbols from the map file, the process list, the state of the
  slots and the dispatcher.
- **Tests** - scenarios through `ms0515-run`/the CLI: boot to the prompt, run
  commands, compare the output.  As in the rest of the project - TDD.

## For the future: the kernel in ROM

Once the kernel has settled and fits in ~15.75 KB, it can move into the ROM
whole: a process then gets 40 KB instead of 32.

| | Kernel on the diskette (the main plan) | Kernel in ROM |
| --- | --- | --- |
| A process's space | 32 KB (banks 2-5) | 40 KB (banks 1-5) |
| The kernel in RAM | code + data, banks 0/1/6 | data only, bank 0 |
| Changing the kernel | copy a file | reflash |
| Booting | ~2-3 s | a fraction of a second |
| Bank 6 | the kernel's | gone (the extended ROM) |

Conditions for the move: the linker script ROM-ready already now (`.text` /
`.rodata` in ROM, `.data` copied into RAM, `.bss` cleared); no self-modifying
code and no changing data in instruction operands.

A fallback: the kernel in ROM looks at start for `/boot/kernel` on the
diskette and, if the file is there, loads it, switches the extended ROM off and
hands over.

## Open questions

- What the ROM-B console costs: a character out with the VRAM window switched,
  and 160004 called from the keyboard interrupt - to be measured, whether it is
  enough for the shell and a screen editor.  (The question "does RT-11 call the
  ROM" is closed: yes, its whole console goes through 160000/160004.)
- The real speed of the RAM disk on the T-11 (estimated 100-250 KB/s against
  ~43 KB/s for EX.SYS's formatting) - to be measured in the emulator and on the
  machine.
- Whether the floppy driver keeps up reading sectors 1:1 or needs a 2:1
  interleave.
- After the one-volume system: the machine has a single drive.  How to
  free it of the system diskette after the boot and work with a second
  diskette, changing them on the go: with a RAM disk the root copied there;
  without one a resident `sh` with built-in commands; a diskette changed by
  a command that flushes the cache, a volume number in the superblock to
  check it.  To be settled once the one-volume system is done.
- For stage 9: the pinout of the K573RF4 and the fitness of a 28C64 as a
  replacement - to be checked against the board's schematic.
- How the VG93 behaves when a DRQ is missed (a byte lost) - how the driver
  recovers.

## Stages

1. **Hello from an own kernel** (done).  The boot block in block 0 on the
   native ROM-B loads the kernel, the kernel prints `login:` through the
   ROM's console (160000).
2. **A minimal kernel** (done).  The timer, the keyboard, one process,
   `TRAP`, `read`/`write` of the console.
3. **FS** (done).  `mkfs` on the host, `open`/`read`/`exec`.
4. **Multitasking** (done).  Two slots, a kernel stack each,
   `fork`/`exit`/`wait`, `sh`, pipes and the background, swap on the RAM
   disk (raw for now).
5. **Volumes and the swap file** (done).  The block devices fd0 and rd0, writing
   to the file system, `mount`, `mkfs` on the machine, `swapon` with a
   file, `/etc/rc` and `fstab` ("Volumes, swap and fstab"); swap to the
   diskette is the file `/swap` by the same means.
6. **The floppy** (done).  An own VG93 driver instead of 160010, for speed:
   /boot/fd, a driver as a file.  Formatting is a program, `fdformat`, not
   the kernel's: whatever can live in a process does.  It builds each
   track's image and hands it over by `ioctl(fd, FD_WTRACK, ...)`; the
   kernel gives it to the driver's format - the device not in use - and
   forgets the track's blocks in the cache.
7. **Programs that own the machine** (the mechanism and RT-11 programs
   done).  The mode is the binary's own - magic 0401 in the a.out header -
   and such a program is run as any other, by its name.  It brings its own
   loader, the first bytes of its text: exec of one checks it, writes the
   cache out, reads the file's first block into the process's slot and
   enters the loader with the file's extents (kernel/hi/own.c).  The loader
   moves itself to 157000 and, through the ROM's 160010, lays the file over
   everything, the kernel too - its header at 000760, the text at 001000 -
   so the file is to be on a diskette.  The program has 001000-157000, some
   55 KB in one piece, and the extra copies of the banks; it uses the ROM
   directly through a small library (user/own/own.h) - the console, the
   keyboard, a block of the diskette, the system's files read by path;
   the command line is its main's.  There is no way back, the exit is a
   reset and the system booted again.

   **RT-11 programs** run so too, as they are: `rt11 PROGRAM.SAV`.  It puts
   a little monitor at 140000, where RT-11's RMON is, loads the .SAV under
   it as RUN does - the program has 001000-137776 - and serves the requests
   most programs and games make: .TTYIN (a line typed with its echo, or
   the special mode), .TTYOUT, .PRINT, .SETTOP, .LOOKUP, .READ/.READW,
   .CLOSE, .PURGE, .WAIT, .GTIM and .DATE (0), .EXIT (a key, then the
   reset); a trap to 4 or 10 is told as RT-11 tells it.  Every disk device
   a program names is the .SAV's own directory of the system's file
   system, the name in lower case.  Not there: writing files, USR, KMON,
   chaining, overlays, handlers, RMON's internals.  SABOT2 runs so from
   its own diskette's files.
8. **Utilities** of levels 1-2, then `rt11` (ls/get/put on RT-11
   diskettes) and Forth.

Stages 9-11 are put off for a long time: there is no real machine to try
them on.

9. **The own ROM.**  Compatible with RT-11 by the ROM-B contract (without
   Rodionov's system): the shared services and the new system's table in the
   upper 8 KB, the self-test, the monitor and the boot menu with the kernel
   found by name in the lower.  The kernel's jump table switches over to it.
10. **The real machine.**  Firmware, measurements, the interleave chosen.
11. **Optional:** the kernel in ROM.
