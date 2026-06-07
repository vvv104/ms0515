# MS0515 Emulator Project

**Read this first after compacting!**

## Architecture
Layered emulator for the Elektronika MS 0515 Soviet PDP-11 computer:
- **Core** (`src/core/`) — Pure C11, zero OS deps. Fully implemented and verified.
- **Lib** (`src/lib/`) — C++ wrapper: Emulator, Debugger, Disassembler, GDB RSP.
- **Libapp** (`src/libapp/`) — Shared host-side app utilities: filesystem paths, YAML config loader/writer, CLI argument parser, disk-mount helpers. Linked by both binaries so any flag added to one is automatically supported by the other. Strictly host-app code — no emulation primitives, no core API.
- **Platform** (`src/platform/`) — Host abstractions kept out of binary sources. Split into two sublibs because needs barely overlap:
  - `platform/cli/` — raw stdin, signal handling, UTF-8 console setup.
  - `platform/gui/` — file dialogs, font discovery, GUI-subsystem console attach.
- **CLI** (`src/cli/`) — Text-mode binary (`ms0515-cli.exe`); stdio bridge over the lib layer.
- **Frontend** (`src/frontend/`) — C++ SDL2 + ImGui binary (`ms0515.exe`).
- **Disk** (`src/disk/`) — Offline RT-11 / MS-0515 disk-image library (lib `ms0515_disk`): LBN→byte geometry mirroring the emulator FDC, directory parse, file read, and volume create/init/put. No emulator dependency.
- **Tools** (`src/tools/`) — Standalone offline binaries over the libs. `tools/disk/` builds `ms0515-disk` (`create/init/put/get/dir/split/merge`). Heuristic recovery (consensus/donor) stays out — see `disk_recovery/`.

## Key rules
- All code, comments, and documentation must be in **English only**.
- Write original code based on architecture knowledge, do not copy from reference projects.
- **Never commit or push** without explicit user permission.
- **Test-driven development**: after designing the interface, write unit tests first, then implement. Run tests at each stage.
- **Revert failed attempts**: always roll back changes from unsuccessful approaches to avoid accumulating dead code and clutter.
- **Zero compiler warnings**: all code must compile without warnings. Use modern C++ idioms and features (C++20/23) in lib and frontend layers.
- **Never suppress warnings**: do not silence `/W4 /WX` (MSVC) or `-Werror` (gcc/clang) with `_CRT_SECURE_NO_WARNINGS`, `#pragma warning(disable: ...)`, or equivalents. Rewrite the offending call instead — replace deprecated CRT functions with their safe siblings (`_dupenv_s` over `getenv`, `fopen_s` over `fopen`, ...) or drop the call entirely (e.g. move runtime config from env vars to CLI flags).
- **No vendored third-party sources**: never store external source files in the repo. All dependencies must be managed through Conan.

## Project structure
```
src/                — emulator source code and build files
  core/src/         — cpu.c, cpu_ops.c, memory.c, timer.c, keyboard.c, floppy.c, board.c
  core/include/     — ms0515/core/*.h headers
  core/tests/       — pure-core unit tests (link only against ms0515_core)
  lib/              — C++ wrapper (Emulator, Debugger, Disassembler, GdbStub)
  lib/tests/        — lib-level tests (Emulator/Terminal/KeyboardLayout/...) + disk fixtures
  libapp/           — shared host-app utilities (Paths, Config, Cli, Disks)
  libapp/tests/     — libapp unit tests (paths/config/cli/disks)
  disk/             — offline RT-11 disk-image lib (Layout, Directory, Image, Build)
  disk/tests/       — disk lib unit tests
  tools/disk/       — ms0515-disk binary (offline disk utility)
  platform/cli/     — CLI host abstractions (Platform_unix.cpp / Platform_win32.cpp)
  platform/gui/     — GUI host abstractions (file dialogs, fonts, console attach) + tests/
  cli/              — text-mode binary (main.cpp, StdioBridge, Koi8)
  frontend/         — SDL2 + ImGui application
  frontend/tests/   — placeholder for future frontend tests
  assets/           — runtime resources (ROM files, keyboard layout, disk images)
package/            — build output: ms0515.exe, ms0515-cli.exe, ms0515-disk.exe, ms0515.yaml, assets/
docs/               — architecture and subsystem documentation
  kb/              — knowledge base (references, verification, known issues)
disk_recovery/      — disk-recovery knowledge base + verified-image vault (no build inputs)
tools/              — misc Python utilities (pdp11 disassembler, Extended-CPC convert, state dump)
```

The top-level `tests/` folder is intentionally gone — each layer owns its own
`tests/` subdir, which keeps the dependency direction enforced at link time.
