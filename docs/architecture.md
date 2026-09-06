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
- `--screenshot` saves the real 640x400 picture as a PNG, decoded by the
  same `libapp` `Screen` the GUI displays.  The terminal mirror only carries
  text, so this is how a graphical guest program is probed without a display.
- The commander over the running machine (`CommanderHost`, a person at a
  terminal only): Ctrl+\ - the one byte every terminal delivers as itself
  and RT-11 never uses - brings the panels of `src/files/` up in the
  alternate screen and takes them down.  The mirror stops writing to the
  terminal and its 80x25 shadow is drawn instead: two rows around the
  guest's cursor under the panels (NC's command line is the machine's own
  prompt), the whole screen when Ctrl+O hides the panels.  Keys follow the
  NC rule (`files/Routing`): typed text, Backspace, Ctrl+letters and an
  Enter after typing go to the machine; Tab, arrows, Insert, Home / End /
  PgUp / PgDn, Esc, the F-keys and an untyped Enter work the panels; a
  dialog takes everything.  F10 takes the panels down, Ctrl+] still quits
  the CLI.  A mount made in the panels is applied to the machine at once
  (`MountSync`: units FD0..FD3 and the HD follow the slots); the floppies
  are shared through the image file the FDC reads and writes per sector,
  the HD - kept in memory with write-through - is re-read after the panels
  write into it.  The bridge parses the terminal's bytes into keys
  (`files/HostKey`) once, for the panels and the guest alike; Windows
  synthesises the same ESC sequences from console records.


### Offline Disk Tooling — `ms0515-disk` (C++)

Separate from the emulator: an offline RT-11 / MS-0515 disk-image library and
tool that read and write images directly, without running the machine.

- Lib `ms0515_disk` (`src/disk/`) — `Layout` (LBN→byte geometry mirroring the
  FDC; the `Vol` kind names the addressing — a DZ floppy side, a linear
  HD/LD container, or a DV:/MZ: whole-disk volume of 1600 blocks whose
  formulas come from the handlers' own translate code: track = LBN/10,
  +2 for DV with wrap at 160, natural sides, no interleave), `Directory`
  (home block + segment chain + RAD50), `Image` (load a capture, read
  files, split/merge sides; `openVolume` opens an explicit kind and
  `detectVolumes` names, by content, every kind whose home block points
  at a directory that parses), `Build` (create blank media, init a volume byte-identical
  to the OS's `INIT`, put / remove a file like PIP, set the entry's
  protect/date metadata, undelete, grow a linear volume, write the
  bootstrap the way the OS's `COPY/BOOT` does - `writeBoot`, verified byte
  for byte against RT-11 on every kit shipped: LBN 0 from the volume's
  DZ.SYS at the offset its `.DRBOT` header names, LBN 2..5 the monitor's
  blocks 1..4 with the device and monitor names in RAD50).
- Binary `ms0515-disk` (`src/tools/disk/`) — `create / init / put / rm /
  squeeze / protect / unprotect / setdate / get / dir / boot / system /
  split / merge` (`system <target> --from <image> [extra]...`: the kit -
  the monitor, SWAP, DZ, TT, PIP, DUP, DIR, RESORC - copied from a
  bootable image and protected, the extras with it, the startup .COM the
  monitor names made anew, then the bootstrap).  The floppy geometry
  follows the image size; `--hd`, `--dv` and `--mz` pick the other volume
  kinds, and `dir` says when the content rather parses as another one.

### Terminal File Manager — `ms0515-files` (C++)

Two panels over the machine's disks - never the host's file system - in
the terminal, with the web commander's keys (`src/files/` as libraries, `src/tools/files/` the standalone binary):

- The panels show the RT-11 volumes of the mounted devices, named as the
  guest names them: `DZ0:`/`DZ2:` (drive A's sides), `DZ1:`/`DZ3:`, `HD0:`,
  or one `DV0:`/`MZ0:` when the image's content is a whole-diskette
  volume.  The mounts are the emulator's: libapp's flag parser and
  `ms0515.yaml`, so the tool starts on the disks the emulator had last and
  a mount made here is what the emulator mounts next.  Alt+F1 / Alt+F2
  (F4 for the panel in use) choose the left / right panel's disk: a
  mounted device, or another image - picked from a listing of a host
  directory that stands in that panel for the moment, the only time the
  host's files are on screen; the device then opens where the listing was.
  Otherwise the host enters only as a path typed at a prompt (Tab
  completes) - a file to bring in (F1), a directory to put files out to
  (F2).  F10 asks before quitting; Esc only closes dialogs.
- `Keys` decodes the function keys a terminal sends with a modifier
  (xterm's `ESC [ 1 ; 3 P` for Alt+F1), which FTXUI passes through
  unnamed.  A terminal never reports a modifier pressed on its own, so
  the key bar cannot relabel itself while Alt is held - only react to
  the key the modifier lands on.
- `ms0515_files` — `Location` (a device's volume through `ms0515_disk`,
  every change written back to the image at once), `Mounts` (slots →
  devices by content), `Panel` (cursor, marks, selection), `Ops` (copy /
  move / delete / rename / protect between volumes, import / export, with
  an explicit policy for overwriting and protected files), `Viewer` (text
  in ASCII / KOI-8R / KOI-7 / KOI-7 with ^N ^O / CP866, octal and hex
  dumps, search).  Unit-tested on scratch copies of the fixture disks.
- The front-end (`ms0515_files_ui`: `Commander.cpp` the page and the
  panels, `CommanderDialogs.cpp`, `CommanderActions.cpp`,
  `CommanderViewer.cpp`, behind `TuiImpl.hpp`) draws with FTXUI (Conan),
  sized to the terminal, the way Midnight Commander does: the menu bar
  (F9), two panels with the title on the top border and the free space on
  the bottom one, column rules, the marks summary on the rule above the
  current-file line, a hint line, the key bar; grey dialogs with a cyan
  input line, radio and check items, `[< OK >] [ Cancel ]`.  The keys are
  mc's - F1 help, F2 user menu, F3 view, F5 copy / F6 move to the device
  in the "to:" line (a bare name renames) with the per-file "File exists"
  question (Yes / No / All / None / Abort), F7 squeeze, F8 delete, Insert
  and + - * marks, Ctrl+U swap, Ctrl+R reread, sort order from the
  Left / Right menus; the viewer's F2 wrap, F4 hex, F5 goto, F7 search,
  F8 encoding, F9 octal.  All state stays in the model; the panels are
  unit-tested by rendering into an FTXUI screen (`test_commander`).

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
