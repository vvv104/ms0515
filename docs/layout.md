# Where every file lives

The repository's folders (moved here from `CLAUDE.md` on 2026-11-08).

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
  platform/serial/  — host serial port (Serial_win32.cpp / Serial_unix.cpp) + tests/
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
docs/               — architecture.md, folder-device.md, layout.md, release.md and the subsystems
  hardware/        — the machine's parts as the emulator models them
  kb/              — knowledge base (references, verification, known issues, TODO)
                      Not here, with their history: the factory documentation transcribed is the
                      software collection's refs/, the plant's story its history/, the floppy
                      handlers taken apart ms0515-devel's projects/rt11/handlers/dz_handler.md
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
