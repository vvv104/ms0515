# UNIX for the MS 0515

An own Unix-like system for the machine, written from scratch in C and
PDP-11 assembler: LSX and Mini-Unix in reach - multitasking without memory
protection, a file system with directories, a shell with pipes.
[`SPEC.md`](SPEC.md) is the specification and the plan of stages.

Stage 1 is done: the native ROM-B boots the system's diskette, the boot
block loads the kernel, and the kernel prints `login:` through the ROM's
console.

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
  the ROM's entries, the native ROM-B's now, an own ROM's later.
- `kernel/main.c` - the kernel.
- `tools/mkimage.py` - the diskette: block 0 the boot block, block 1 the
  superblock (the magic, the version, the volume's blocks, the kernel's
  first block, its blocks, its load address, its entry), the kernel from
  block 139.
- `tests/` - the system booted through the emulator's library on ROM-B, the
  screen read as text: the boot to `login:`, a superblock without the magic
  refused, block 0 where the ROM takes it.  Skipped when the image is not
  built; `--unix-dsk=<file>` names another.

The programs are linked freestanding at their own addresses and put down
flat with `objcopy`, no RT-11 `.SAV` about them.  Note for the GNU
assembler: a plain number is decimal (no trailing dot as in MACRO-11), and
`ld -Ttext` reads hex - the boot block's 070000 is `0x7000`.
