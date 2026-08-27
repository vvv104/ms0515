# MS0515 Emulator — Architecture Overview

## The Elektronika MS 0515

The Elektronika MS 0515 (Электроника МС 0515) is a Soviet personal computer
manufactured by the "Processor" company in Voronezh, USSR.  It is based on
the KR1807VM1 processor (a clone of the DEC T-11) implementing a subset of
the PDP-11 instruction set.

## Emulator Architecture

The emulator is structured as a stack of libraries with two binaries on top:

```
  ┌──────────────────────────────┐  ┌──────────────────────────────┐
  │  Frontend  (ms0515.exe)      │  │  CLI  (ms0515-cli.exe)       │
  │  - SDL2 window, ImGui UI     │  │  - stdio bridge over lib     │
  │  - audio, on-screen keyboard │  │  - KOI-8 ↔ host encoding     │
  │  - interactive debugger UI   │  │  - text-mode session         │
  └────────────┬──────────┬──────┘  └───────┬────────────┬─────────┘
               │          │                 │            │
               ▼          ▼                 ▼            ▼
       ┌──────────────┐ ┌─────────────────────────┐ ┌──────────────┐
       │ platform/gui │ │  Libapp  (host-app)     │ │ platform/cli │
       │ file dialogs │ │  - paths, config (YAML) │ │ raw stdin    │
       │ fonts, attach│ │  - CLI arg parser       │ │ signals      │
       │ console      │ │  - disk mount helpers   │ │ UTF-8 console│
       └──────────────┘ └────────────┬────────────┘ └──────────────┘
                                     │
                ┌────────────────────┴────────────────────┐
                │  Lib (C++)                              │
                │  - Emulator wrapper (lifecycle, ROM)    │
                │  - Debugger (breakpoints, single-step)  │
                │  - Disassembler (PDP-11 mnemonics)      │
                │  - GDB RSP stub (remote debugging)      │
                └────────────────────┬────────────────────┘
                                     │
                ┌────────────────────┴────────────────────┐
                │  Core (C11)                             │
                │  - CPU emulation (66 instructions)      │
                │  - Memory (128K RAM, bank switching)    │
                │  - Timer (8253 PIT, 3 channels)         │
                │  - Keyboard (MS7004 model + 8251 USART) │
                │  - Floppy (WD1793 FDC)                  │
                │  - Board (system integration, I/O bus)  │
                └─────────────────────────────────────────┘
```

`libapp` is linked by both binaries, so any host-app feature added to one
(new CLI flag, new config field, new mount helper) becomes available in the
other automatically.  `platform/` is split into two sublibs because the
CLI's needs (raw stdin, signals, UTF-8 console) and the GUI's needs (file
dialogs, fonts, console-attach) have very little overlap — pulling SDL
into the CLI link would just be dead weight.

### Core Layer (C11)

Pure emulation logic with zero OS dependencies.  Only uses `<stdint.h>`,
`<stdbool.h>`, `<string.h>`, and `<assert.h>`.  This layer is fully
portable and can be compiled for any platform.

Files:
- `src/core/include/ms0515/` — public headers
- `src/core/src/cpu.c`, `cpu_ops.c` — CPU core and instruction handlers
- `src/core/src/memory.c` — address translation and bank switching
- `src/core/src/timer.c` — Intel 8253 PIT emulation
- `src/core/src/keyboard.c` — Intel 8251 USART for keyboard
- `src/core/src/ms7004.c` — MS7004 keyboard microcontroller model
- `src/core/src/floppy.c` — WD1793 floppy disk controller
- `src/core/src/ramdisk.c` — 512 KB RAM disk expansion
- `src/core/src/board.c` — system integration and I/O dispatch
- `src/core/src/snapshot.c` — machine state snapshot serialization

### Lib Layer (C++)

C++ wrapper providing higher-level features:
- `Emulator` class — manages core lifecycle, ROM loading, frame stepping,
  save/load state (snapshots)
- `Debugger` — breakpoints, watchpoints, single-step, register inspection
- `Disassembler` — PDP-11 instruction decoding to human-readable text
- GDB RSP stub — allows remote debugging with standard GDB

### Libapp Layer (C++)

Host-side application utilities shared by both binaries.  Strictly host-app
code — no emulation primitives, no `core` API:
- `Paths` — exe directory, asset/config search roots
- `Config` — YAML config file load/save (disk paths, window state, options)
- `Cli` — command-line argument parser (shared flag set across binaries)
- `Disks` — disk-mounting helpers operating on `ms0515::Emulator`

### Platform Layer (C++)

Host OS abstractions kept out of the binary sources so `cli/` and
`frontend/` proper do not pull `<windows.h>` / `<commdlg.h>` / `<termios.h>`
directly.  Two sublibs, one per binary:
- `platform/cli/` — raw stdin reading, signal handling, UTF-8 console setup
- `platform/gui/` — file dialogs, font discovery, GUI-subsystem console attach

### Frontend Binary — `ms0515.exe` (C++ / SDL2 / ImGui)

Desktop application with:
- Video display (320x200 color, 640x200 mono)
- On-screen keyboard (MS7004 virtual keyboard widget)
- Physical keyboard input mapping (host keyboard → MS7004 scancodes)
- Audio output (1-bit speaker via SDL2)
- ImGui-based debugger windows (registers, memory, disassembly, breakpoints)
- Persistent settings and file dialogs via `libapp` + `platform/gui`

### CLI Binary — `ms0515-cli.exe` (C++)

Headless text-mode session over the same emulator core:
- Stdio bridge — host `stdin`/`stdout` ↔ emulated terminal
- KOI-8R ↔ host encoding conversion
- Same argument parser and config loader as the frontend (via `libapp`)
- Console setup, signal handling via `platform/cli`

### Offline Disk Tooling — `ms0515-disk` (C++)

Separate from the emulator: an offline RT-11 / MS-0515 disk-image library and
tool that read and write images directly, without running the machine.

- Lib `ms0515_disk` (`src/disk/`) — `Layout` (LBN→byte geometry mirroring the
  FDC; the image size selects single- vs double-sided), `Directory` (home
  block + segment chain + RAD50), `Image` (load a capture, read files,
  split/merge sides), `Build` (create blank media, init a volume byte-identical
  to the OS's `INIT`, put / remove a file like PIP, set the entry's
  protect/date metadata, undelete, grow a linear volume, write the
  bootstrap the way the OS's `COPY/BOOT` does - `writeBoot`, verified byte
  for byte against RT-11 on every kit shipped: LBN 0 from the volume's
  DZ.SYS at the offset its `.DRBOT` header names, LBN 2..5 the monitor's
  blocks 1..4 with the device and monitor names in RAD50).
- Binary `ms0515-disk` (`src/tools/disk/`) — `create / init / put / rm /
  squeeze / protect / unprotect / setdate / get / dir / boot / system /
  split / merge` (`system <target> --from <image>`: the kit - every .SYS,
  PIP, DUP, DIR, RESORC - copied from a bootable image, then the
  bootstrap).  Geometry follows the image size; there is no layout flag.

The geometry source of truth is the FDC (`src/core/src/floppy.c`); the format
is documented in [filesystem.md](hardware/filesystem.md).  The tool is verified
against the real OS in the emulator (`src/lib/tests/test_dir_vs_os.cpp`).
Heuristic multi-source recovery (consensus, donor matching, confidence tiers)
is kept out of these primitives — its knowledge base lives in `disk_recovery/`.

## Hardware Summary

| Component      | Chip               | Clone of      |
|----------------|--------------------|---------------|
| CPU            | KR1807VM1          | DEC T-11      |
| Timer          | KR580VI53          | Intel 8253    |
| Keyboard UART  | KR580VV51          | Intel 8251    |
| Serial UART    | KR580VV51          | Intel 8251    |
| System PPI     | KR580VV55          | Intel 8255    |
| FDC            | KR1818VG93         | WD1793        |

## Module Documentation

- [board.md](hardware/board.md) — I/O register map, system registers, timing
- [cpu.md](hardware/cpu.md) — CPU architecture, instruction set, interrupt system
- [memory.md](hardware/memory.md) — Address space, bank switching, VRAM window
- [video.md](hardware/video.md) — Display modes, color attributes, VRAM layout
- [keyboard.md](hardware/keyboard.md) — MS7004 protocol, scancodes, auto-repeat
- [timer.md](hardware/timer.md) — PIT channels, operating modes, speaker connection
- [floppy.md](hardware/floppy.md) — FDC commands, disk geometry, image format
- [filesystem.md](hardware/filesystem.md) — RT-11 disk layout, sector interleave
- [ramdisk.md](hardware/ramdisk.md) — 512 KB RAM disk expansion board

## Key Sources

1. NS4 technical description (3.858.420 TO) — primary hardware reference
2. PDP-11 Architecture Handbook (DEC, EB-23657-18)
3. T-11 User's Guide (EK-DCT11-UG)
4. Intel 8253, 8251, 8255 datasheets
5. WD1793 datasheet
6. MAME driver: `src/mame/drivers/ms0515.cpp`
