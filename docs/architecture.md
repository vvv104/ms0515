# MS0515 Emulator — Architecture Overview

## The Elektronika MS 0515

The Elektronika MS 0515 (Электроника МС 0515) is a Soviet personal computer
manufactured by the "Processor" company in Voronezh, USSR.  It is based on
the KR1807VM1 processor (a clone of the DEC T-11) implementing a subset of
the PDP-11 instruction set.

## Emulator Architecture

The emulator is structured as three independent layers:

```
  ┌─────────────────────────────────────────┐
  │  Frontend (C++ / SDL2 / ImGui)          │
  │  - Window management, rendering         │
  │  - Input handling (keyboard, mouse)     │
  │  - Audio output (SDL2 audio)            │
  │  - Interactive debugger UI              │
  └──────────────┬──────────────────────────┘
                 │
  ┌──────────────┴──────────────────────────┐
  │  Lib (C++)                              │
  │  - Emulator wrapper (lifecycle, ROM)    │
  │  - Debugger (breakpoints, single-step)  │
  │  - Disassembler (PDP-11 mnemonics)      │
  │  - GDB RSP stub (remote debugging)      │
  └──────────────┬──────────────────────────┘
                 │
  ┌──────────────┴──────────────────────────┐
  │  Core (C11)                             │
  │  - CPU emulation (66 instructions)      │
  │  - Memory (128K RAM, bank switching)    │
  │  - Timer (8253 PIT, 3 channels)         │
  │  - Keyboard (MS7004 model + 8251 USART) │
  │  - Floppy (WD1793 FDC)                  │
  │  - Board (system integration, I/O bus)  │
  └─────────────────────────────────────────┘
```

### Core Layer (C11)

Pure emulation logic with zero OS dependencies.  Only uses `<stdint.h>`,
`<stdbool.h>`, `<string.h>`, and `<assert.h>`.  This layer is fully
portable and can be compiled for any platform.

Files:
- `emu/core/include/ms0515/` — public headers
- `emu/core/src/cpu.c`, `cpu_ops.c` — CPU core and instruction handlers
- `emu/core/src/memory.c` — address translation and bank switching
- `emu/core/src/timer.c` — Intel 8253 PIT emulation
- `emu/core/src/keyboard.c` — Intel 8251 USART for keyboard
- `emu/core/src/ms7004.c` — MS7004 keyboard microcontroller model
- `emu/core/src/floppy.c` — WD1793 floppy disk controller
- `emu/core/src/ramdisk.c` — 512 KB RAM disk expansion
- `emu/core/src/board.c` — system integration and I/O dispatch
- `emu/core/src/snapshot.c` — machine state snapshot serialization

### Lib Layer (C++)

C++ wrapper providing higher-level features:
- `Emulator` class — manages core lifecycle, ROM loading, frame stepping,
  save/load state (snapshots)
- `Debugger` — breakpoints, watchpoints, single-step, register inspection
- `Disassembler` — PDP-11 instruction decoding to human-readable text
- GDB RSP stub — allows remote debugging with standard GDB

### Frontend Layer (C++ / SDL2 / ImGui)

Desktop application with:
- Video display (320x200 color, 640x200 mono)
- On-screen keyboard (MS7004 virtual keyboard widget)
- Physical keyboard input mapping (host keyboard → MS7004 scancodes)
- Audio output (1-bit speaker via SDL2)
- ImGui-based debugger windows (registers, memory, disassembly, breakpoints)
- YAML config file for persistent settings (disk paths, window state)
- Platform abstraction (Windows/Linux/macOS) for file dialogs and fonts

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
