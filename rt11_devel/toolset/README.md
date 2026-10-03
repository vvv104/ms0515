# MS-0515 / RT-11 build toolset

Reusable host-side helpers for building Soviet-era PDP-11 programs
inside the MS-0515 emulator.  Mix and match these modules to assemble
any MACRO-11, Pascal, FORTRAN or BASIC project that targets RT-11 SJ
V5.04 on this hardware.

## Layout

```
rt11_devel/toolset/
├── build.py         universal driver: read build.toml, run the recipe's
│                    programs with ms0515-run, collect the outputs
├── decsys.py        a whole `dec` system composed from the software
│                    collection, for the builders that need a booted
│                    monitor (see "Builds that need a system" below);
│                    build.py takes only the collection's location from it
├── emu_driver.py    generic stdio bridge to ms0515-cli (or any subprocess)
├── rt11.py          RT-11 monitor session (boot, dot prompt, command + errors)
├── system/          the vvv104 ОМЕГА as a bootable FOLDER (.rtfs device):
│                    what the games' tests (fist, manicm) boot to run them;
│                    no build runs on it
└── tests/           pytest tests for the Python modules
```

Projects that use this toolset live under `rt11_devel/projects/<name>/`
and declare a `build.toml` (see "Declarative builds" below).

**A build is the machine's own programs run one after another.**
`ms0515-run` (`src/tools/run`) runs one RT-11 program from a folder, with
that folder as the program's disk and nothing else to set up: no disk is
composed, no system booted, no screen read.  `build.py` stages the sources
and the toolchain into a work folder and runs the recipe there, a command
at a time:

```
ms0515-run MACRO MYPROG=MYPROG
ms0515-run LINK MYPROG=MYPROG
```

The programs are the collection's (`$MS0515_SOFTWARE`, else
`../ms0515-software` beside this repository), from
`software/development`: DEC's LINK, LIBR, SYSLIB, SYSMAC and ODT, and
MACRO, which is the FODOS kit's - the one tool the V5.4 source kit has no
source for.  A kit's toolchain (`pascal/`, `fortran/`) links against the
kit's own system library, in `fodos/`, and a recipe says which folders it
takes its files from, in order.  Nothing is kept here twice.

**The command lines are the programs' own, not the monitor's.**
`ms0515-run` hands the line to the program as `RUN PROGRAM line` does.
`MACRO X` at the monitor's prompt is a monitor command that the monitor
turns into `X=X`; here there is no monitor command, and `MACRO X` names an
input alone and writes nothing.  The form is `outputs=inputs`:

| monitor command | the program's own line |
|---|---|
| `MACRO X` | `MACRO X=X` |
| `MACRO X/LIST` | `MACRO X,X=X` (object, listing) |
| `MACRO A+B` | `MACRO A=A,B` |
| `LINK X` | `LINK X=X` |
| `LINK/MAP:X X,Y` | `LINK X,X=X,Y` (image, map) |
| `LINK X,PASLIB,PAS1` | `LINK X=X,PASLIB,PAS1` |

**What a program finds.**  The work folder is `DK:`.  A file a program
asks for on `SY:` - MACRO its `SYSMAC.SML`, LINK its `SYSLIB.OBJ` - is
given to it out of the same folder, so the system libraries are staged
beside everything else.  The date is 31-Dec-99.  A program that fails
ends the build: `ms0515-run` exits with 1 when the monitor's own account
of the program is an error, and `build.py` reads what was printed for an
`-E-` or `-F-` diagnostic as well.

## How `system/` was built

The seven base files were assembled inside the emulator with nothing but
documented RT-11 commands (originally `INITIALIZE` + seven `COPY`s +
`COPY/BOOT` onto a disk image, committed historically as `system.dsk` —
see git history), then extracted as host files; `boot.bin` was
materialized by one `COPY/BOOT DZ1:RT11SJ.SYS DZ1:` run against the
folder mounted as a `.rtfs` device.  Each build copies `system/` to a
temp `boot/` folder and stages onto the copy; the committed template is
never modified (enforced by a pytest invariant).

## Builds that need a system

Some builders cannot be a row of programs: DEC's command files run under
IND, a handler is checked by booting from it, the monitor is built by its
own SYSGEN.  Those boot a system in `ms0515-cli` and drive it:
`projects/rt11` (the monitor, the handlers, the kit), `projects/decusc`,
the `validate.py` oracles.  They stand on `decsys.compose()` - the `dec`
disk composed from the collection by `ms0515-disk`, as a folder device,
with the builder's commands as its `STARTS.COM` - and on the two modules
below.

When ms0515-cli is started with `--disk0 X.dsk --disk1 Y.dsk`, the
RT-11 monitor exposes the four floppy sides as:

| RT-11 | Physical                |
|-------|-------------------------|
| DZ0:  | drive 0 (`--disk0`) side 0 |
| DZ1:  | drive 1 (`--disk1`) side 0 |
| DZ2:  | drive 0 side 1          |
| DZ3:  | drive 1 side 1          |

Such a builder always passes `--no-config` so a GUI-saved `ms0515.yaml`
can never leak into a build.

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
handles host-side prep, stages the sources and the toolchain into a work
folder, runs the recipe there with `ms0515-run`, and copies the outputs
the programs wrote back into the project directory.

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
commands = ["MACRO {name},{name}={name}",
            "LINK {name}={name},MYLIB"]             # overrides recipe commands
```

Each language has a built-in recipe (`compilers`, `libs`, `folders`,
`commands`) that the driver applies unless `[build]` overrides it.
`{name}` in any command template is substituted with `project.name`.  A
command is `PROGRAM line`: the program by its name in the work folder,
then its command line.

### Language recipes (defaults)

| language | sources ext | programs staged   | libs staged                            | commands                                                                  |
|----------|-------------|-------------------|----------------------------------------|---------------------------------------------------------------------------|
| macro11  | `.MAC`      | MACRO, LINK       | SYSMAC.SML, SYSLIB                     | `MACRO {name}={name}` → `LINK {name}={name}`                              |
| pascal   | `.PAS`      | PAS1, MACRO, LINK | SYSMAC.SML, SYSLIB (the kit's), PASLIB, PAS1.OBJ | `PAS1 {name}={name}` → `MACRO {name}={name}` → `LINK {name}={name},PASLIB,PAS1` |
| fortran  | `.FOR`      | FORTRA, LINK      | SYSLIB (the kit's), FORLIB             | `FORTRA {name}={name}` → `LINK {name}={name},FORLIB` (FORTRA writes the object itself) |
| basic    | `.BAS`      | BASICO            | —                                      | (none — interactive only)                                                 |

### Custom build script (when the manifest isn't enough)

A one-off pipeline is the same thing by hand - a folder and a row of
runs:

```python
import shutil, subprocess, sys, tempfile
from pathlib import Path
sys.path.insert(0, "rt11_devel/toolset")
import build, decsys

work = Path(tempfile.mkdtemp())
dev = decsys.collection() / "software" / "development"
for f in ("MACRO.SAV", "LINK.SAV", "SYSMAC.SML", "SYSLIB.OBJ"):
    shutil.copy(dev / f, work / f)
shutil.copy("MYPROG.MAC", work / "MYPROG.MAC")

build.run_command("MACRO MYPROG,MYPROG=MYPROG", work)   # raises on an error
build.run_command("LINK MYPROG=MYPROG", work)

shutil.copy(work / "myprog.sav", "release/MYPROG.SAV")  # the program wrote it
```

A pipeline that needs a booted monitor is in "Builds that need a system".

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
| ``test_build.py``     | Recipe table sanity (the programs' own syntax), manifest → ``BuildPlan`` resolution, `{name}` substitution, the toolchain found in the collection's folders, a command run through a stand-in for ``ms0515-run`` (failure, diagnostic, warning), manifest-validation errors |

## `STARTS.COM` — for the builds that boot a system

The SJ monitor auto-runs `STARTS.COM` from SY: at boot, so a builder that
boots a system gives it its commands as the startup file:
`decsys.compose(folder, startup=[...])` writes them.  The `system/`
template carries **no** `STARTS.COM`.  Such a builder accepts the
Date/Time prompts, sends a type-ahead `DIR` whose "Free blocks" line marks
completion (it executes only after `STARTS.COM` finishes), and scans the
transcript for `?xxx-F-`/`-E-` diagnostics.

Direct boots that are not builds (`projects/rt11/handlers/hd/validate.py`, the demo disk)
stage the toolset's default `STARTS.COM` (`SET TT QUIET`) themselves so they
start cleanly.  See also `GOTCHAS.md`.

## Why these specific RT-11 binaries

They're the exact toolchain that originally produced VVV's disks
(MS-0515 RT-11 SJ V5.04, Soviet localisation, August 1989 build),
recovered from the floppies under `disk_recovery/` and shipped here
byte-for-byte.  Anything rebuilt with this toolset is link-compatible
with everything else that lived on those floppies.
