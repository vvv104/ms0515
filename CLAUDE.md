# MS0515 Emulator Project

**Read this first after compacting!**

## Architecture
Three-layer emulator for the Elektronika MS 0515 Soviet PDP-11 computer:
- **Core** (`emu/core/`) — Pure C11, zero OS deps. Fully implemented and verified.
- **Lib** (`emu/lib/`) — C++ wrapper: Emulator, Debugger, Disassembler, GDB RSP.
- **Frontend** (`emu/frontend/`) — C++ SDL2 + ImGui.

## Key rules
- All code, comments, and documentation must be in **English only**.
- Write original code based on architecture knowledge, do not copy from reference projects.
- **Never commit or push** without explicit user permission.
- **Test-driven development**: after designing the interface, write unit tests first, then implement. Run tests at each stage.
- **Revert failed attempts**: always roll back changes from unsuccessful approaches to avoid accumulating dead code and clutter.
- **Zero compiler warnings**: all code must compile without warnings. Use modern C++ idioms and features (C++20/23) in lib and frontend layers.
- **No vendored third-party sources**: never store external source files in the repo. All dependencies must be managed through Conan.

## Project structure
```
emu/                — emulator source code and build files
  core/src/         — cpu.c, cpu_ops.c, memory.c, timer.c, keyboard.c, floppy.c, board.c
  core/include/     — ms0515/*.h headers
  lib/              — C++ wrapper (Emulator, Debugger, Disassembler, GdbStub)
  frontend/         — SDL2 + ImGui application
  assets/           — runtime resources (ROM files, keyboard layout, disk images)
docs/               — architecture and subsystem documentation
  kb/              — knowledge base (references, verification, known issues)
tools/              — utility scripts (disassembler, disk tools)
```
