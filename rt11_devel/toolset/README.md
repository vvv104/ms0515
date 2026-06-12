# MS-0515 / RT-11 build toolset

Reusable host-side helpers for building Soviet-era PDP-11 programs
inside the MS-0515 emulator.  Mix and match these modules to assemble
any MACRO-11, Pascal, FORTRAN or BASIC project that targets RT-11 SJ
V5.04 on this hardware.

## Layout

```
rt11_devel/toolset/
├── build.py         universal driver: read build.toml, drive the pipeline
├── emu_driver.py    generic stdio bridge to ms0515-cli (or any subprocess)
├── rt11.py          RT-11 monitor session (boot, dot prompt, command + errors)
├── system/          bootable RT-11 SJ V5 FOLDER template (.rtfs device):
│                    the 7 base system files + boot.bin + device.rtfs
├── build_tools/     compilers and libraries
│   ├── MACRO.SAV    MACRO-11 assembler
│   ├── LINK.SAV     linker
│   ├── SYSMAC.SML   system macros for MACRO-11
│   ├── SYSLIB.OBJ   RT-11 system library
│   ├── PAS1.SAV     Pascal pass 1
│   ├── PAS1.OBJ     Pascal pass-1 object module
│   ├── PASLIB.OBJ   Pascal runtime library
│   ├── FORTRA.SAV   FORTRAN-IV compiler
│   ├── FORLIB.OBJ   FORTRAN runtime library
│   ├── BASICO.SAV   BASIC
│   └── GRAPH.P1U    Pascal graphics include
└── tests/           pytest tests for the Python modules
```

Projects that use this toolset live under `rt11_devel/projects/<name>/`
and declare a `build.toml` (see "Declarative builds" below).

The build pipeline itself runs entirely on **folder-backed devices**
(`.rtfs`, see `docs/folder-device.md`): staging is plain file copies into
two temp folders, outputs are host files the guest materializes — no
`ms0515-disk` calls anywhere.  The binary remains available for disk-image
work outside the pipeline (`create`, `init`, `put`, `get`, `dir`, `split`,
`merge`).

## How `system/` was built

The seven base files were assembled inside the emulator with nothing but
documented RT-11 commands (originally `INITIALIZE` + seven `COPY`s +
`COPY/BOOT` onto a disk image, committed historically as `system.dsk` —
see git history), then extracted as host files; `boot.bin` was
materialized by one `COPY/BOOT DZ1:RT11SJ.SYS DZ1:` run against the
folder mounted as a `.rtfs` device.  Each build copies `system/` to a
temp `boot/` folder and stages onto the copy; the committed template is
never modified (enforced by a pytest invariant).

## Device-letter cheat sheet for `ms0515-cli`

When ms0515-cli is started with `--disk0 X.dsk --disk1 Y.dsk`, the
RT-11 monitor exposes the four floppy sides as:

| RT-11 | Physical                |
|-------|-------------------------|
| DZ0:  | drive 0 (`--disk0`) side 0 |
| DZ1:  | drive 1 (`--disk1`) side 0 |
| DZ2:  | drive 0 side 1          |
| DZ3:  | drive 1 side 1          |

The build pipeline mounts two folder devices:
`--disk0-side0 boot/device.rtfs` (DZ0 = SY:, the bootable system + the
compilers + STARTS.COM) and `--disk1-side0 work/device.rtfs` (DZ1,
ASSIGNed `DK:` — sources in, outputs out).  It always passes
`--no-config` so a GUI-saved `ms0515.yaml` can never leak into a build.

## Module overview

### `emu_driver.EmulatorDriver`

Captures stdout into a rolling byte buffer on a reader thread, lets you
``send`` bytes, and (most importantly) ``wait_for`` regex patterns with
an *idle* requirement: returns only once the pattern matched **and** the
child has been silent for N ms.  Catches the "prompt echoed mid-print"
race that bites every naive auto-driver.

Works against anything — pass any command line.  Tests use a fake
Python child to exercise every code path without booting the emulator.

### `rt11.RT11Session`

Wraps an EmulatorDriver in RT-11 monitor semantics:

* ``boot(send_returns=3)`` accepts the localized Date/Time/Startup
  prompts and waits for the first dot prompt.
* ``command(line)`` sends a line, waits for the dot to come back,
  returns the new output only, raises ``RT11CommandError`` on any
  ``?xxx-F-...`` fatal diagnostic.
* Shortcuts: ``assign``, ``deassign``, ``run``, ``chain``.

## Declarative builds (`build.toml`)

Most projects don't need a custom Python script — they need a
manifest.  Drop a `build.toml` next to the sources and run:

```
python rt11_devel/toolset/build.py path/to/build.toml
```

(or just `build.py` from inside the project directory).  The driver
handles host-side prep, copies the `system/` folder template, stages the
right toolchain next to it, lets the monitor run the recipe at boot, and
copies the outputs the guest materialized back into the project
directory.

Minimal manifest:

```toml
[project]
name     = "MYPROG"
language = "macro11"
```

Full schema:

```toml
[project]
name       = "MYPROG"           # required; matches source basename
language   = "macro11"          # macro11 | pascal | fortran | basic
sources    = ["MYPROG.MAC"]     # optional; default = [<name>.<ext-for-lang>]
outputs    = ["MYPROG.SAV"]     # optional; default = ["<name>.SAV"]
pre_build  = "gen.py"           # optional host-side hook, e.g. code generator
post_build = "pack.py"          # optional host-side hook, e.g. packager

[build]
libs     = ["EXTRA.OBJ"]                            # extra files staged + linked
commands = ["MACRO {name}/LIST",
            "LINK {name},MYLIB"]                    # overrides recipe commands
```

Each language has a built-in recipe (`compilers`, `libs`, `commands`)
that the driver applies unless `[build]` overrides it.  `{name}` in any
command template is substituted with `project.name` before it is sent
to the monitor.

### Language recipes (defaults)

| language | sources ext | compilers staged                  | libs staged                              | monitor commands                                           |
|----------|-------------|-----------------------------------|------------------------------------------|------------------------------------------------------------|
| macro11  | `.MAC`      | MACRO, LINK                       | SYSMAC.SML, SYSLIB                       | `MACRO {name}` → `LINK {name}`            |
| pascal   | `.PAS`      | PAS1, MACRO, LINK                 | SYSMAC.SML, SYSLIB, PASLIB, PAS1.OBJ    | `PAS1 {name}={name}` → `MACRO {name}` → `LINK {name},PASLIB,PAS1` |
| fortran  | `.FOR`      | FORTRA, MACRO, LINK               | SYSMAC.SML, SYSLIB, FORLIB              | `FORTRA {name}` → `MACRO {name}` → `LINK {name},FORLIB`   |
| basic    | `.BAS`      | BASICO                            | —                                        | (none — interactive only)                                  |

### Custom build script (when the manifest isn't enough)

For one-off pipelines that don't fit the recipe model, drop down to
the two underlying modules — the driver itself is the canonical
example (~180 lines):

```python
import shutil, sys, tempfile
from pathlib import Path
sys.path.insert(0, "rt11_devel/toolset")
from emu_driver import EmulatorDriver
from rt11 import RT11Session

CLI  = "package/ms0515-cli.exe"
ROM  = "package/assets/rom/ms0515-roma.rom"

boot = Path(tempfile.gettempdir()) / "myboot"      # folder devices: just
work = Path(tempfile.gettempdir()) / "mywork"      # copy files around
shutil.copytree("rt11_devel/toolset/system", boot)
for tool in ("MACRO.SAV", "LINK.SAV", "SYSMAC.SML"):
    shutil.copy(f"rt11_devel/toolset/build_tools/{tool}", boot / tool)
(boot / "STARTS.COM").write_bytes(b"SET TT QUIET\r\n")
work.mkdir()
shutil.copy("MYPROG.MAC", work / "MYPROG.MAC")
shutil.copy("rt11_devel/toolset/build_tools/SYSLIB.OBJ", work / "SYSLIB.OBJ")
(work / "device.rtfs").write_bytes(b"device: floppy\nblocks: 800\n")

emu = EmulatorDriver([CLI, "--no-config", "--rom", ROM,
                      "--disk0-side0", boot / "device.rtfs",
                      "--disk1-side0", work / "device.rtfs"])
emu.start()
try:
    rt = RT11Session(emu); rt.boot()
    rt.command("ASSIGN DZ1 DK")
    rt.command("MACRO MYPROG", timeout=300)
    rt.command("LINK MYPROG",  timeout=300)
finally:
    emu.kill()

shutil.copy(work / "myprog.sav", "release/MYPROG.SAV")   # guest-made file
```

## Running the tests

```
python -m pytest rt11_devel/toolset/tests/ -v
```

Tests don't boot the real emulator: each module is exercised against a
small fake child process that mimics the relevant slice of monitor
behaviour.

Coverage:

| File                  | What it covers                                       |
|-----------------------|------------------------------------------------------|
| ``test_emu_driver.py``| Buffer capture, idle-aware ``wait_for``, ANSI strip in decoded output, lifecycle errors |
| ``test_rt11.py``      | ``boot`` reaches the prompt, ``command`` returns only new output, ``RT11CommandError`` on ``?xxx-F-``, ``chain`` ordering, ``DOT_PROMPT`` regex |
| ``test_build.py``     | Recipe table sanity, manifest → ``BuildPlan`` resolution, `{name}` substitution, default vs. override sources/outputs/commands, manifest-validation errors |

## `STARTS.COM` — the build runs from the startup file

The SJ monitor auto-runs `STARTS.COM` from SY: at boot, so the build recipe
*is* the startup file.  For each build, `build.py` writes the project's
commands (`ASSIGN DZ1 DK` + the language recipe) into `boot/STARTS.COM` —
the `system/` template carries **no** `STARTS.COM`, so there is nothing
to replace.  Then it boots: the
monitor runs the whole build itself; the host just accepts the Date/Time
prompts, sends a type-ahead `DIR` whose "Free blocks" line marks completion
(it executes only after `STARTS.COM` finishes), and scans the transcript for
`?xxx-F-`/`-E-` diagnostics.

Direct boots that are not builds (`projects/hd/validate.py`, the demo disk)
stage the toolset's default `STARTS.COM` (`SET TT QUIET`) themselves so they
start cleanly.  See also `GOTCHAS.md`.

## Why these specific RT-11 binaries

They're the exact toolchain that originally produced VVV's disks
(MS-0515 RT-11 SJ V5.04, Soviet localisation, August 1989 build),
recovered from the floppies under `disk_recovery/` and shipped here
byte-for-byte.  Anything rebuilt with this toolset is link-compatible
with everything else that lived on those floppies.
