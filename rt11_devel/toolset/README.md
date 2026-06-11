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
├── system.dsk       bootable RT-11 SJ V5 build disk (committed binary)
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

For host-side disk-image operations (`create`, `init`, `put`, `get`,
`dir`, `split`, `merge`) call the `ms0515-disk` binary directly — it
already covers everything a build script needs and there is no point
wrapping it.

## How `system.dsk` was built

A one-time recipe executed inside the emulator using nothing but
documented RT-11 commands (HELP and 0515-OSA.md §2.7.3):

```
INITIALIZE/NOQUERY DZ1:                ! format side 0 of the target
INITIALIZE/NOQUERY DZ3:                ! format side 1 of the target
COPY DZ0:RT11SJ.SYS DZ1:               ! seven copies, one per system file
COPY DZ0:SWAP.SYS   DZ1:
COPY DZ0:DZ.SYS     DZ1:
COPY DZ0:TT.SYS     DZ1:
COPY DZ0:PIP.SAV    DZ1:
COPY DZ0:DUP.SAV    DZ1:
COPY DZ0:DIR.SAV    DZ1:
COPY/BOOT DZ1:RT11SJ.SYS DZ1:          ! install bootstrap from monitor
```

The result is committed as `system.dsk`; the build pipeline copies that
image to a working location each run and mutates the copy.

## Device-letter cheat sheet for `ms0515-cli`

When ms0515-cli is started with `--disk0 X.dsk --disk1 Y.dsk`, the
RT-11 monitor exposes the four floppy sides as:

| RT-11 | Physical                |
|-------|-------------------------|
| DZ0:  | drive 0 (`--disk0`) side 0 |
| DZ1:  | drive 1 (`--disk1`) side 0 |
| DZ2:  | drive 0 side 1          |
| DZ3:  | drive 1 side 1          |

So for the common "boot from system.dsk, work on its side 1" pattern
only `--disk0` is needed: side 0 = DZ0 (boot), side 1 = DZ2 (work
surface).

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
handles host-side prep, mounts a fresh copy of `system.dsk`, stages the
right toolchain on side 1, drives RT-11 through the language recipe,
and pulls the outputs back into the project directory.

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
commands = ["RUN DZ2:MACRO {name}/LIST",
            "RUN DZ2:LINK {name},MYLIB"]            # overrides recipe commands
```

Each language has a built-in recipe (`compilers`, `libs`, `commands`)
that the driver applies unless `[build]` overrides it.  `{name}` in any
command template is substituted with `project.name` before it is sent
to the monitor.

### Language recipes (defaults)

| language | sources ext | compilers staged                  | libs staged                              | monitor commands                                           |
|----------|-------------|-----------------------------------|------------------------------------------|------------------------------------------------------------|
| macro11  | `.MAC`      | MACRO, LINK                       | SYSMAC.SML, SYSLIB                       | `RUN DZ2:MACRO {name}` → `RUN DZ2:LINK {name}`            |
| pascal   | `.PAS`      | PAS1, MACRO, LINK                 | SYSMAC.SML, SYSLIB, PASLIB, PAS1.OBJ    | `PAS1 {name}={name}` → `MACRO {name}` → `LINK {name},PASLIB,PAS1` |
| fortran  | `.FOR`      | FORTRA, MACRO, LINK               | SYSMAC.SML, SYSLIB, FORLIB              | `FORTRA {name}` → `MACRO {name}` → `LINK {name},FORLIB`   |
| basic    | `.BAS`      | BASICO                            | —                                        | (none — interactive only)                                  |

### Custom build script (when the manifest isn't enough)

For one-off pipelines that don't fit the recipe model, drop down to
the two underlying modules — the driver itself is the canonical
example (~180 lines):

```python
import shutil, subprocess, sys, tempfile
from pathlib import Path
sys.path.insert(0, "rt11_devel/toolset")
from emu_driver import EmulatorDriver
from rt11 import RT11Session

DISK = "src/build/Release/tools/disk/ms0515-disk.exe"
CLI  = "src/build/Release/cli/ms0515-cli.exe"
ROM  = "src/assets/rom/ms0515-roma.rom"

work = Path(tempfile.gettempdir()) / "build.dsk"
shutil.copy("rt11_devel/toolset/system.dsk", work)
subprocess.run([DISK, "put", str(work), "--side", "1",
                "MYPROG.MAC",
                "rt11_devel/toolset/build_tools/MACRO.SAV",
                "rt11_devel/toolset/build_tools/LINK.SAV",
                "rt11_devel/toolset/build_tools/SYSMAC.SML",
                "rt11_devel/toolset/build_tools/SYSLIB.OBJ"], check=True)

emu = EmulatorDriver([CLI, "--rom", ROM, "--disk0", str(work)])
emu.start()
try:
    rt = RT11Session(emu); rt.boot()
    rt.command("ASSIGN DZ2 DK")
    rt.command("RUN DZ2:MACRO MYPROG", timeout=300)
    rt.command("RUN DZ2:LINK MYPROG",  timeout=300)
finally:
    emu.kill()

subprocess.run([DISK, "get", str(work), "--side", "1",
                "--out", "release/", "MYPROG.SAV"], check=True)
```

`R MYPROG` only loads from SY:, so when the compiler lives on side 1
the explicit-device form (`RUN DZ2:MACRO MYPROG`) is required.

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

## `STARTS.COM` — boot-time startup file

`STARTS.COM` is a toolset asset (not a per-project file): the SJ monitor
auto-runs it from SY: at boot.  Today it just carries `SET TT QUIET`;
`build.py` stages it on side 0 of every work disk.  It is the **seam** for
a planned refactor — the build recipe (`ASSIGN` + compile/link) will move
into a generated `STARTS.COM` so the monitor runs the build itself at boot,
replacing the per-command driving in `run()`.  See also `GOTCHAS.md`.

## Why these specific RT-11 binaries

They're the exact toolchain that originally produced VVV's disks
(MS-0515 RT-11 SJ V5.04, Soviet localisation, August 1989 build),
recovered from the floppies under `disk_recovery/` and shipped here
byte-for-byte.  Anything rebuilt with this toolset is link-compatible
with everything else that lived on those floppies.
