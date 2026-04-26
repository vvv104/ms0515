# Building the MS0515 Emulator

## Prerequisites

- **C/C++ compiler** with C11 and C++23 support
  - Windows: Visual Studio 2022+ Build Tools (MSVC 19.5+)
  - Linux: GCC 13+ or Clang 17+
  - macOS: Xcode 15+ / Apple Clang
- **[Conan 2](https://conan.io/)** package manager (`pip install conan`)
- **CMake** 3.16+ and **Ninja** (installed automatically by Conan if missing)

## Conan profile

Before the first build, create a default Conan profile:

```bash
conan profile detect
```

Make sure it uses C++23. Edit `~/.conan2/profiles/default` and set:

```ini
[settings]
compiler.cppstd=23
```

On Windows, also add Ninja as the CMake generator:

```ini
[conf]
tools.cmake.cmaketoolchain:generator=Ninja
```

## Build

```bash
cd emu
conan build . --build=missing
```

This single command runs `conan install`, `cmake configure`, and
`cmake build`. Third-party dependencies (SDL2, Dear ImGui) are
downloaded and built automatically on first run.

The build output goes to `build/Release/`.

## Package

A post-build step copies the executable and runtime assets into a
self-contained `package/` directory:

```
package/
  ms0515(.exe)
  assets/
    rom/          ROM images
    keyboard/     Keyboard layout data
```

## Project structure

```
conanfile.py      Conan recipe (dependencies + build steps)
CMakeLists.txt    Top-level CMake project
core/             Hardware emulation in pure C11 (no OS dependencies)
                  Includes snapshot serialization (snapshot.c)
lib/              C++ wrapper (Emulator, Debugger, Disassembler, GDB stub)
frontend/         SDL2 + Dear ImGui application
assets/           Runtime resources copied into package/
```

## CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `MS0515_BUILD_FRONTEND` | `ON` | Build the SDL2/ImGui frontend |
| `MS0515_BUILD_TESTS` | `OFF` | Build unit tests |

Pass via Conan:

```bash
conan build . --build=missing -o "&:MS0515_BUILD_TESTS=True"
```

Or directly with CMake after `conan install`:

```bash
cmake --preset conan-release -DMS0515_BUILD_TESTS=ON
cmake --build --preset conan-release
```

## Running

```bash
cd package
./ms0515 --disk0-side0 path/to/disk.dsk
```

| Option | Short | Description |
|--------|-------|-------------|
| `--rom <path>` | | ROM image (default: `assets/rom/ms0515-roma.rom`) |
| `--disk0 <path>` | `-d0` | Drive 0, both sides from one 819200-byte track-interleaved image |
| `--disk0-side0 <path>` | `-d0s0` | Drive 0, lower side (409600-byte SS image) |
| `--disk0-side1 <path>` | `-d0s1` | Drive 0, upper side |
| `--disk1 <path>` | `-d1` | Drive 1, both sides from a track-interleaved image |
| `--disk1-side0 <path>` | `-d1s0` | Drive 1, lower side |
| `--disk1-side1 <path>` | `-d1s1` | Drive 1, upper side |
| `--screen-dump <path>` | | Dump VRAM text (`stderr` / `stdout` accepted) |

`--diskN` and `--diskN-sideM` for the same N are mutually exclusive.

Disks can also be mounted at runtime via the **File** menu (single-
side only at the moment; the menu still accepts double-sided images
through `--disk0` / `--disk1` on the command line).

Machine state can be saved and restored via **Machine → Save/Load State**.
Snapshots use the `.ms0515` extension and include CPU, RAM, VRAM, timer,
keyboard, FDC, and RAM disk state.  ROM and floppy disk images are not
included — the ROM is verified by CRC32, and disk image paths are
stored so they are re-mounted on load.
