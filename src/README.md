# Building the MS0515 Emulator

## Prerequisites

- **C/C++ compiler** with C11 and C++20 support
  - Windows: Visual Studio 2022+ Build Tools (MSVC 19.5+)
  - Linux: GCC 13+ or Clang 17+
  - macOS: Apple Clang 13+ (macOS 11 Big Sur and newer, Intel or Apple
    silicon — the Command Line Tools alone are enough, full Xcode is not)
- **[Conan 2](https://conan.io/)** package manager (`pip install conan`)
- **CMake** 3.21+ and **Ninja** on PATH.  A machine that has neither can
  take them from Conan too — add the `profiles/tools` add-on to the build
  command (see [Build](#build) below); the emulator itself then needs
  nothing but a compiler and Conan.

## Conan profile

Before the first build, create a default Conan profile:

```bash
conan profile detect
```

Make sure it uses C++20. Edit `~/.conan2/profiles/default` and set:

```ini
[settings]
compiler.cppstd=20
```

On Windows, also add Ninja as the CMake generator:

```ini
[conf]
tools.cmake.cmaketoolchain:generator=Ninja
```

## Build

```bash
cd src
conan build . --build=missing
```

If the machine has no cmake or ninja on PATH, let Conan supply those as
well:

```bash
conan build . -pr:h default -pr:h profiles/tools -pr:b default --build=missing
```

The add-on is a profile rather than part of the recipe on purpose:
dependencies are compiled in their own environment, which a recipe's
`build_requirements()` does not reach — without the profile, building
`fmt` from source stops at `cmake: command not found`.

This single command runs `conan install`, `cmake configure`, and
`cmake build`. Third-party dependencies (SDL2, Dear ImGui, fmt, toml++,
FTXUI, stb, doctest) are downloaded and built automatically on first run.

The build output goes to `build/Release/`.

## Package

A post-build step copies the executables and runtime assets into a
self-contained `package/` directory at the **repo root** (one level
above `src/`):

```
<repo>/package/
  ms0515(.exe)       the window
  ms0515-cli(.exe)   the text mode
  ms0515-disk(.exe)  the offline disk tool
  ms0515-run(.exe)   one RT-11 program run from its folder
  DISKS.txt          where the bootable diskettes come from
  assets/
    rom/          ROM images
    keyboard/     Keyboard layout data
    sounds/       Drive/keyboard sound recordings
```

## Project structure

```
conanfile.py      Conan recipe (dependencies + build steps)
CMakeLists.txt    Top-level CMake project
core/             Hardware emulation in pure C11 (no OS dependencies)
                  Includes snapshot serialization (snapshot.c) and the
                  trace ring (trace.c)
lib/              C++ wrapper (Emulator, Debugger, Disassembler, GDB stub)
libapp/           Shared host-app utilities (paths, YAML config, CLI parser,
                  disk-mount helpers, screen composition + PNG screenshots)
platform/         Host OS abstractions kept out of the binaries: cli/ (raw
                  stdin, signals, UTF-8 console), gui/ (file dialogs, fonts),
                  win7compat/ (the static MSVC runtime's shims for Windows 7)
cli/              ms0515-cli.exe - the text-mode binary
frontend/         SDL2 + Dear ImGui application
disk/             Offline RT-11 / MS-0515 disk-image library
files/            The RT-11 file manager (model + FTXUI panels)
tools/            The standalone binaries: disk/ = ms0515-disk, run/ = ms0515-run
web/              The Emscripten browser build (see src/web/README.md)
profiles/         Conan profiles: emscripten (the browser build's host),
                  tools (CMake and Ninja from Conan)
assets/           Runtime resources copied into package/
```

See `docs/architecture.md` and the repository's own `CLAUDE.md` for what
each layer does and how they depend on one another.

## CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `MS0515_BUILD_FRONTEND` | `ON` | Build the SDL2/ImGui frontend |
| `MS0515_BUILD_TESTS` | `OFF` | Build unit tests |

The defaults are a plain CMake configure's: `conan build` always turns
both on for a native build and both off for the browser one
(`conanfile.py`'s `build()`).

## Built inside another project

`src/` can be taken into another CMake project with `add_subdirectory`
(the RT-11 development repository builds its programs' harnesses that
way): the targets - `ms0515_lib`, `ms0515_disk`, `ms0515_run_core`,
`ms0515-run`, ... - are that project's to link, the emulator's folders are
named by `ms0515_SOURCE_DIR` / `ms0515_BINARY_DIR`, and nothing is copied
into `package/`, which is the emulator's own build's alone.  The other
project brings the same Conan packages (its recipe can take this one's
`requirements()`) and its own language standard.

Or directly with CMake after `conan install`:

```bash
cmake --preset conan-release -DMS0515_BUILD_TESTS=ON
cmake --build --preset conan-release
```

## Running

```bash
cd ../package
./ms0515 --disk0 path/to/disk.dsk
```

The options are listed in the [top-level README](../README.md#running);
`ms0515-cli --help` prints them all.

Disks can also be mounted at runtime via the **File** menu: Mount image
takes either size the same way, and once one side is in, Mount upper side
puts a second SS image on the drive.

Machine state can be saved and restored via **Machine → Save/Load State**.
Snapshots use the `.ms0515` extension and include CPU, RAM, VRAM, timer,
keyboard, FDC, and RAM disk state.  ROM and floppy disk images are not
included — the ROM is verified by CRC32, and disk image paths are
stored so they are re-mounted on load.
