# MS0515 Emulator

An emulator for the **Elektronika MS 0515** — a Soviet PDP-11-compatible
personal computer built around the T-11 (KM1801VM2) CPU, produced in the
late 1980s.

## Features

- Cycle-accurate T-11 CPU emulation (PDP-11 instruction set)
- Full memory subsystem with bank switching and RAM disk
- WD1793-based floppy disk controller (2 drives × 2 sides, 400 KB per side)
- KM1801VP1-065 timer with programmable frequency
- MS7004 keyboard with on-screen virtual keyboard (OSK)
- Video output (512x256 monochrome framebuffer)
- Audio output (beeper via timer)
- Built-in debugger with disassembler and GDB RSP server
- Save/load state snapshots (Machine menu)
- YAML config file for persistent settings
- Boots RT-11 and runs period software

## Repository structure

```
docs/           Technical documentation (architecture, CPU, memory, etc.)
emu/            Emulator source code and build system
  core/         Hardware emulation in pure C11
  lib/          C++ wrapper (Emulator, Debugger, Disassembler, GDB stub)
  frontend/     SDL2 + Dear ImGui application
  assets/       Runtime resources (ROM images, keyboard layout)
reference/      External references and links
tools/          Utility scripts (PDP-11 disassembler, disk tools)
```

## Building

### Prerequisites

- Visual Studio 2022+ Build Tools (MSVC)
- [Conan 2](https://conan.io/) package manager
- CMake 3.16+ and Ninja (bundled with VS Build Tools)

### Build steps

```bash
cd emu
conan build . --build=missing
```

The build produces a self-contained `emu/package/` directory with the
executable and all required assets.

### Running

```bash
cd emu/package
ms0515.exe --fd0 path/to/disk.dsk
```

Command-line options:

| Option | Description |
|--------|-------------|
| `--rom <path>` | ROM image (default: `assets/rom/ms0515-roma.rom`) |
| `--fd0..--fd3 <path>` | Mount floppy disk image to drive 0-3 |
| `--trace <path>` | Write CPU execution trace to file |
| `--screen-dump <path>` | Dump VRAM text to file (`stderr`/`stdout` accepted) |

Disks can also be mounted at runtime via the File menu.

## Documentation

- [Architecture overview](docs/architecture.md)

### Hardware

- [Board](docs/hardware/board.md) — system integration, interrupts, I/O mapping
- [CPU](docs/hardware/cpu.md) — T-11 instruction set, addressing modes, traps
- [Memory](docs/hardware/memory.md) — bank switching, I/O page layout
- [Video](docs/hardware/video.md) — framebuffer, palette, scroll registers
- [Keyboard](docs/hardware/keyboard.md) — MS7004 protocol, scancodes, auto-repeat
- [Timer](docs/hardware/timer.md) — KM1801VP1-065 programmable timer
- [Floppy](docs/hardware/floppy.md) — WD1793 FDC, disk format, DMA
- [Filesystem](docs/hardware/filesystem.md) — RT-11 disk layout, sector interleave
- [RAM disk](docs/hardware/ramdisk.md) — 512 KB expansion board

## License

This project is currently unlicensed (all rights reserved).
