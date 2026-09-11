# MS0515 Emulator Project

**Read this first after compacting!**

## Architecture
Layered emulator for the Elektronika MS 0515 Soviet PDP-11 computer:
- **Core** (`src/core/`) — Pure C11, zero OS deps. Fully implemented and verified.
- **Lib** (`src/lib/`) — C++ wrapper: Emulator, Debugger, Disassembler, GDB RSP.
- **Libapp** (`src/libapp/`) — Shared host-side app utilities: filesystem paths, YAML config loader/writer, CLI argument parser, disk-mount helpers, screen composition (VRAM→RGBA) and PNG screenshots. Linked by both binaries so any flag added to one is automatically supported by the other. Strictly host-app code — no emulation primitives, no core API.
- **Platform** (`src/platform/`) — Host abstractions kept out of binary sources. Split into two sublibs because needs barely overlap:
  - `platform/cli/` — raw stdin, signal handling, UTF-8 console setup.
  - `platform/gui/` — file dialogs, font discovery, GUI-subsystem console attach.
- **CLI** (`src/cli/`) — Text-mode binary (`ms0515-cli.exe`); stdio bridge over the lib layer. Ctrl+\ brings the two-panel commander (`src/files/`) up over the running machine, NC-style: the machine's prompt is the command line, Ctrl+O hides the panels, mounts made there go into the machine. `ms0515_cli_core` (mounts applied to the Emulator, the machine's screen as FTXUI rows) is unit-tested in `cli/tests/`.
- **Frontend** (`src/frontend/`) — C++ SDL2 + ImGui binary (`ms0515.exe`).
- **Disk** (`src/disk/`) — Offline RT-11 / MS-0515 disk-image library (lib `ms0515_disk`): LBN→byte geometry mirroring the emulator FDC, directory parse, file read, and volume create/init/put/rm/squeeze + per-entry protect/date metadata, and whole bootable diskettes composed from an exemplar and groups of files (`Compose`). Lib `ms0515_disk_manifest` reads the software collection's `disks.toml` (toml++). No emulator dependency.
- **Web** (`src/web/`) — The browser build: the core + lib compiled with Emscripten behind a flat C API (`ms0515_web.cpp`), a static page (`www/`) that runs the machine in the tab (its "Bug report" button saves the snapshot, the ROM and the mounted images as one .zip - `www/bugreport.js`), a Node smoke test. Configured only under the Emscripten toolchain (`src/profiles/emscripten`); no host layers.
- **Files** (`src/files/`) — The RT-11 file manager as libraries: `ms0515_files` (the model — a device's volume, the mounts, the panels, the operations between volumes and the host, the viewer; no terminal, unit-tested) and `ms0515_files_ui` (the two panels, dialogs and viewer drawn with FTXUI via Conan). Used by the commander inside `ms0515-cli`.
- **Tools** (`src/tools/`) — Standalone offline binaries over the libs. `tools/disk/` builds `ms0515-disk` (`create/init/put/rm/squeeze/protect/unprotect/setdate/get/dir/boot/system/split/merge/compose`). Heuristic recovery (consensus/donor) stays out — see `disk_recovery/`.

## Key rules
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
  added later, `rt11_devel/` included — inherits it. Never repeat it with
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
- **No machine-specific paths in tracked files**: never a user's home directory (`C:\Users\...`, `/home/...`), a drive letter or any absolute path of one machine - not as a default in code, not in docs, not in configs. External resources (the original game, tools) are located through an environment variable with a repository-relative fallback (see `rt11_devel/projects/fist/source/wotef_dir.py`); examples in docs use `$PWD` or relative paths. Before every commit `git grep -i "users[.]voron"` (and the equivalent for the machine at hand) must return nothing.

## Project structure
```
src/                — emulator source code and build files
  core/src/         — cpu.c, cpu_ops.c, memory.c, timer.c, keyboard.c, floppy.c, board.c
  core/include/     — ms0515/core/*.h headers
  core/tests/       — pure-core unit tests (link only against ms0515_core)
  lib/              — C++ wrapper (Emulator, Debugger, Disassembler, GdbStub)
  lib/tests/        — lib-level tests (Emulator/Terminal/KeyboardLayout/...) + disk fixtures
  libapp/           — shared host-app utilities (Paths, Config, Cli, Disks, Screen)
  libapp/tests/     — libapp unit tests (paths/config/cli/disks)
  disk/             — offline RT-11 disk-image lib (Layout, Directory, Image, Build)
  disk/tests/       — disk lib unit tests
  tools/disk/       — ms0515-disk binary (offline disk utility)
  files/            — the RT-11 file manager libs: model (ms0515_files) + FTXUI panels (ms0515_files_ui) + tests/
  web/              — browser build: C API shim, www/ page, smoke.mjs / zip_check.mjs / browser_check.mjs (Emscripten only)
  profiles/         — Conan host profiles (emscripten)
  platform/cli/     — CLI host abstractions (Platform_unix.cpp / Platform_win32.cpp)
  platform/gui/     — GUI host abstractions (file dialogs, fonts, console attach) + tests/
  cli/              — text-mode binary (main.cpp, StdioBridge, Koi8)
  frontend/         — SDL2 + ImGui application
  frontend/tests/   — placeholder for future frontend tests
  assets/           — runtime resources (ROM files, keyboard layout, disk images)
package/            — build output: ms0515.exe, ms0515-cli.exe, ms0515-disk.exe, ms0515.yaml, assets/
rt11_devel/         — RT-11 guest programs: toolset/ (build.py: MACRO/LINK inside the emulator),
                      projects/<name>/ (sources, generators, README) and projects/<name>/tests/
                      (the program's own doctest harness on ms0515_lib; pulled in by the emulator's
                      test build via rt11_devel/CMakeLists.txt - game tests never live under src/)
docs/               — architecture and subsystem documentation
  kb/              — knowledge base (references, verification, known issues)
disk_recovery/      — disk-recovery knowledge base + verified-image vault (no build inputs)
tools/              — misc Python utilities: pdp11 disassembler, Extended-CPC convert, state dump,
                      bugreport.py (open a report the browser build saved: unpack, summarise, place the images);
                      run_program.py (boot a disk headless, type, keep the text and the screen PNG),
                      scr2png.py (.SCR video-RAM dumps to PNG), sample2wav.py (the home-made
                      sampler's recordings to WAV)
hw/                 — hardware reconstructions: hw/mc1702/ = KiCad schematic of the МС 1702
                      coprocessor, GENERATED by tools/mc1702_kicad.py from docs/kb/mc1702/netlist.csv
                      (the netlist is the source of truth; never edit the .kicad_sch by hand)
```

The top-level `tests/` folder is intentionally gone — each layer owns its own
`tests/` subdir, which keeps the dependency direction enforced at link time.
