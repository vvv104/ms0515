# MS0515 Emulator Project

**Read this first after compacting!**

## Architecture
Layered emulator for the Elektronika MS 0515 Soviet PDP-11 computer:
- **Core** (`src/core/`) — Pure C11, zero OS deps. Fully implemented and verified.
- **Lib** (`src/lib/`) — C++ wrapper: Emulator, Debugger, Disassembler, GDB RSP.
- **Libapp** (`src/libapp/`) — Shared host-side app utilities: filesystem paths, YAML config loader/writer, CLI argument parser, disk-mount helpers, screen composition (VRAM→RGBA) and PNG screenshots. Linked by both binaries, so both parse the same flags; the ones only the text mode acts on (`--no-config`, `--state`, `--save-state`) say so in its `--help`. Strictly host-app code — no emulation primitives, no core API.
- **Platform** (`src/platform/`) — Host abstractions kept out of binary sources. Split into two sublibs because needs barely overlap:
  - `platform/cli/` — raw stdin, signal handling, UTF-8 console setup.
  - `platform/gui/` — file dialogs, font discovery, GUI-subsystem console attach.
- **CLI** (`src/cli/`) — Text-mode binary (`ms0515-cli.exe`); stdio bridge over the lib layer. Ctrl+\ brings the two-panel commander (`src/files/`) up over the running machine, NC-style: the machine's prompt is the command line, Ctrl+O hides the panels, mounts made there go into the machine. `ms0515_cli_core` (mounts applied to the Emulator, the machine's screen as FTXUI rows) is unit-tested in `cli/tests/`.
- **Frontend** (`src/frontend/`) — C++ SDL2 + ImGui binary (`ms0515.exe`).
- **Disk** (`src/disk/`) — Offline RT-11 / MS-0515 disk-image library (lib `ms0515_disk`): LBN→byte geometry mirroring the emulator FDC, directory parse, file read, and volume create/init/put/rm/squeeze + per-entry protect/date metadata, and whole bootable diskettes composed from an exemplar and groups of files (`Compose`). Lib `ms0515_disk_manifest` reads the software collection's `disks.toml` (toml++). No emulator dependency.
- **Web** (`src/web/`) — The browser build: the core + lib compiled with Emscripten behind a flat C API (`ms0515_web.cpp`), a static page (`www/`) that runs the machine in the tab (its "Bug report" button saves the snapshot, the ROM and the mounted images as one .zip - `www/bugreport.js`; `?run=URL` opens it at a run card - one program started with no boot by `ms0515-run`'s machine, which the module carries too, the card's words under the screen in the visitor's language; the collection's Pages keep the cards), a Node smoke test. Configured only under the Emscripten toolchain (`src/profiles/emscripten`); no host layers.
- **Files** (`src/files/`) — The RT-11 file manager as libraries: `ms0515_files` (the model — a device's volume, the mounts, the panels, the operations between volumes and the host, the viewer; no terminal, unit-tested) and `ms0515_files_ui` (the two panels, dialogs and viewer drawn with FTXUI via Conan). Used by the commander inside `ms0515-cli`.
- **Tools** (`src/tools/`) — Standalone offline binaries over the libs. `tools/disk/` builds `ms0515-disk` (`create/init/put/rm/squeeze/protect/unprotect/setdate/get/dir/boot/system/split/merge/compose`). Heuristic recovery (consensus/donor) stays out — it is `recovery/` of the software collection (https://github.com/vvv104/ms0515-software). `tools/run/` builds `ms0515-run [switches] PROGRAM[.SAV] [its command line]`: one RT-11 program run from its folder, with ROM-B, the dec system diskette and a saved state compiled in (`bake/` makes them at build time of `assets/system.dsk`, the dec system diskette kept in the repository with the programs its tests run - their bytes from the software collection, taken anew by `tools/run_assets.py`, so the emulator builds from its own files alone). The monitor's own `RUN` starts the program (`src/Starter.hpp` says how); no assets, no settings. A program that prints and ends runs unthrottled with its text on stdout - MACRO, LINK and the compilers are run from the host this way; one a person sits at runs at the machine's pace, in a window once it draws. `ms0515-run-probe` (build tree only) reports such a run without a terminal or a window.

## What is not here
The programs for the machine and the toolset that builds them (MACRO/LINK
by ms0515-run, GCC for pdp11-aout, the games' ports, UBIX, the RT-11 kit
from sources) and the handbook for writing them (`docs/programming.md`)
live in ms0515-devel (https://github.com/vvv104/ms0515-devel), with their history; it uses this
repository (`add_subdirectory` of src/, `tools/emu_driver.py`, the
binaries in package/), never the other way round.

## Key rules
- **This repository is the emulator and nothing else.**  The emulator is an
  independent environment responsible only for emulating the machine: its
  sources, its tests and their fixtures, its own tools and the documentation
  of the machine as it emulates it.  Its artifacts (the libraries, the
  binaries, `tools/`) may be used by the other repositories - ms0515-devel
  (programs for the machine and the toolset that builds them) and
  ms0515-software (the software collection: the diskettes' files, their
  manuals, disk recovery) - never the other way round: nothing here reads
  from them at build or test time, and what concerns guest software, its
  development or the collection goes to its repository, not here.  A
  copy of what the emulator needs from them (`src/tools/run/assets/`) is
  kept here and refreshed by a script (`tools/run_assets.py`).
- All code, comments, and documentation must be in **English only**.
- Write original code based on architecture knowledge, do not copy from reference projects.
- **Never commit or push** without explicit user permission.  Commits to a
  feature branch are allowed without asking; commits to `main` and any push
  still require permission.
- **CI gates every merge and release — `main` must stay clean.**  The
  mandatory order: open the PR → wait for CI green on the PR (all four
  platform jobs; local MSVC green is NOT cross-platform green) → merge →
  wait for CI green on `main` → only then push the release tag.
  Fixes discovered along the way go through the feature branch, never
  directly onto `main`.
- **A release is finished, not started.**  Pushing the tag is the middle
  of the job, not the end: `.github/workflows/release.yml` deliberately
  uploads a **draft** with the archives and no notes, because the
  auto-generated "What's Changed" got confused by an unpublished draft.
  So the tail is always the same four steps, in order, and none of them
  is optional or someone else's:
  1. bump `src/VERSION` through a PR of its own, merge it, main green;
  2. push the `v*` tag and wait for the release build;
  3. **write the notes by hand on the draft** - what changed and why it
     matters to whoever runs the thing, in the register of the releases
     before it (`gh release view v1.6.0 --json body`), ending with the
     browser link;
  4. **publish it**
     (`gh release edit vX.Y.Z --notes-file ... --draft=false`).
  The web build needs no step of its own: the tag deploys Pages by itself.
  Do NOT `gh workflow run pages.yml` after tagging - both runs share the
  `pages` concurrency group, so the manual one cancels the tag's and
  deploys `main` instead of the tag.  Dispatch it by hand only to
  re-deploy between releases.
  Do not leave a draft standing and hand it back: unless the user says
  otherwise, "выкладывай" means through step 4.
- **Test-driven development**: after designing the interface, write unit tests first, then implement. Run tests at each stage.
- **Revert failed attempts**: always roll back changes from unsuccessful approaches to avoid accumulating dead code and clutter.
- **The language standard is declared once**, in `src/CMakeLists.txt`
  (`CMAKE_CXX_STANDARD` / `CMAKE_C_STANDARD`); every target — existing or
  added later — inherits it. Never repeat it with
  `target_compile_features(... cxx_std_NN)` per target: that is how the
  project ended up with the same standard spelled out in 15 files, one
  edit away from a target silently building against another one. A target
  that truly needs a different standard states it itself, with a comment
  saying why. The Conan profiles' `compiler.cppstd` must match, since it
  is what dependencies are built against.
- **Zero compiler warnings**: all code must compile without warnings. Use
  modern C++ idioms and features (C++20) in lib and frontend layers.
- **Never suppress warnings**: do not silence `/W4 /WX` (MSVC) or `-Werror` (gcc/clang) with `_CRT_SECURE_NO_WARNINGS`, `#pragma warning(disable: ...)`, or equivalents. Rewrite the offending call instead — replace deprecated CRT functions with their safe siblings (`_dupenv_s` over `getenv`, `fopen_s` over `fopen`, ...) or drop the call entirely (e.g. move runtime config from env vars to CLI flags).
- **No session links in the history**: commit messages and pull request
  bodies carry the `Co-Authored-By:` trailer and the "Generated with Claude
  Code" credit, and nothing else.  A `Claude-Session:` line points into the
  owner's private Claude account - it opens nothing for anyone else and puts
  a session identifier into a public history.  Whatever attribution an
  assistant session is configured with, this rule wins; strip the link
  before committing.
- **No vendored third-party sources**: never store external source files in the repo. All dependencies must be managed through Conan.
- **No machine-specific paths in tracked files**: never a user's home directory (`C:\Users\...`, `/home/...`), a drive letter or any absolute path of one machine - not as a default in code, not in docs, not in configs. External resources (the original game, tools) are located through an environment variable with a repository-relative fallback (see `projects/fist/source/wotef_dir.py` in ms0515-devel); examples in docs use `$PWD` or relative paths. Before every commit `git grep -i "users[.]voron"` (and the equivalent for the machine at hand) must return nothing.

## Project structure
```
src/                — emulator source code and build files
  core/src/         — cpu.c, cpu_ops.c, memory.c, timer.c, keyboard.c, ms7004.c, floppy.c, hd.c,
                      ramdisk.c, cassette.c, board.c, snapshot.c, trace.c
  core/include/     — ms0515/core/*.h headers
  core/tests/       — pure-core unit tests (link only against ms0515_core)
  lib/              — C++ wrapper (Emulator, Debugger, Disassembler, GdbStub, Terminal, VramMirror,
                      Typist, Audio, KeyboardLayout, Koi8)
  lib/tests/        — lib-level tests (Emulator/Terminal/KeyboardLayout/...) + disk fixtures
  libapp/           — shared host-app utilities (Paths, Config, Cli, Disks, Screen, Screenshot, Sounds)
  libapp/tests/     — libapp unit tests (paths/config/cli/disks)
  disk/             — offline RT-11 disk-image lib (Layout, Directory, Image, Build, Compose, Manifest,
                      Wizard, Rtfs/FolderVolume/SparseVolume for folder devices)
  disk/tests/       — disk lib unit tests
  tools/disk/       — ms0515-disk binary (offline disk utility) + tests/
  tools/run/        — ms0515-run: src/, bake/ (the saved machine), assets/ (system.dsk, DIR/PIP),
                      probe/ (ms0515-run-probe), tests/
  files/            — the RT-11 file manager libs: model (ms0515_files) + FTXUI panels (ms0515_files_ui) + tests/
  web/              — browser build: C API shim (src/), www/ page, test-disks/, the Node checks
                      (smoke, wizard, zip, diff, keys, encoding, run) and browser_check.mjs (Emscripten only)
  profiles/         — Conan profiles: emscripten (browser host), tools (CMake + Ninja from Conan)
  platform/cli/     — CLI host abstractions (Platform_unix.cpp / Platform_win32.cpp)
  platform/gui/     — GUI host abstractions (file dialogs, fonts)
  platform/win7compat/ — the static MSVC runtime's shims so the exes run on Windows 7
  cli/              — text-mode binary (main.cpp, StdioBridge, GuestScreen, Scrollback, MountSync,
                      CommanderHost) + tests/
  frontend/         — SDL2 + ImGui application
  frontend/tests/   — placeholder for future frontend tests
  assets/           — runtime resources (ROM files, keyboard firmware + layout, sounds, DISKS.txt) - no disks: they come
                      from the ms0515-software releases; the original disks the tests need live in
                      lib/tests/disks/originals/ as test_*.dsk
package/            — build output: ms0515.exe, ms0515-cli.exe, ms0515-disk.exe, ms0515-run.exe, DISKS.txt,
                      ms0515.yaml (once settings are saved), assets/
docs/               — architecture.md, folder-device.md and the subsystems
  hardware/        — the machine's parts as the emulator models them
  kb/              — knowledge base (references, verification, known issues, TODO)
  refs/            — the factory documentation transcribed (Russian, as the originals)
  images/          — the README's screenshots
tools/              — the emulator's Python utilities: emu_driver.py (drive ms0515-cli from a script;
                      ms0515-devel uses it too) and run_program.py on it (boot a disk headless, type,
                      keep the text and the screen PNG); bugreport.py (open a report the browser build
                      saved: unpack, summarise, place the images); dump_state.py (a state file's chunks
                      and the trace ring); pdp11_disasm.py (the PDP-11 disassembler); run_assets.py
                      (ms0515-run's assets taken anew from the software collection); tests/
```

The top-level `tests/` folder is intentionally gone — each layer owns its own
`tests/` subdir, which keeps the dependency direction enforced at link time.
