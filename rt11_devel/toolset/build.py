"""
build — universal driver for MS-0515 / RT-11 projects.

Reads a ``build.toml`` manifest in the project directory, runs the
project through the standard pipeline:

  1. (optional) pre_build hook            — host-side, e.g. code generator
  2. make a work folder and stage into it the sources, the extra object
     libraries and the language's toolchain out of the software collection
  3. run the recipe's commands, each one a run of ``ms0515-run`` in that
     folder: the program is RT-11's own (MACRO, LINK, PAS1 ...), run on the
     machine inside the tool, with the folder as its disk.  No disk is
     composed, no system booted; a program that fails stops the build with
     what it printed.
  4. outputs are host files the programs wrote in the work folder — copy
     them to the project directory
  5. (optional) post_build hook           — host-side, e.g. packaging

Manifest schema (TOML)
----------------------
::

    [project]
    name       = "MYPROG"           # required, matches source basename
    language   = "macro11"          # macro11 | pascal | fortran | basic
    sources    = ["MYPROG.MAC"]     # optional, default = [name + ext-for-lang]
    outputs    = ["MYPROG.SAV"]     # optional, default = [name + ".SAV"]
    pre_build  = "gen.py"           # optional, relative to manifest dir
    post_build = "pack.py"          # optional, relative to manifest dir

    [build]
    libs     = ["EXTRA.OBJ"]        # optional, extra files staged + linked:
                                    # the project's own, else the collection's
                                    # (software/development and its folders)
    commands = ["MACRO {name},{name}={name}"]   # optional, overrides the recipe

A command is a program and its command line, ``PROGRAM line``, in the
program's own syntax - not the monitor's: ``ms0515-run`` hands the line to
the program as ``RUN PROGRAM line`` would, so ``MACRO X`` names an input
alone and writes nothing, and ``MACRO X=X`` makes the object
(``outputs=inputs``; LINK's second output is the map).

Usage
-----
::

    python rt11_devel/toolset/build.py [path/to/build.toml]

If the path is omitted the manifest is taken from the current directory.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
import tomllib
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent

sys.path.insert(0, str(HERE))
import decsys                               # noqa: E402
from rt11 import RT11CommandError           # noqa: E402

# ms0515-run, the emulator's tool that runs one RT-11 program from its
# folder (src/tools/run): what a command of a recipe is run with.
RUNNER = [str(ROOT / "package" / "ms0515-run.exe")]

# The longest a single program may take, in seconds of the host's time.  A
# program that ends runs unthrottled; one still there after this has stopped
# for a reason the build cannot answer.
COMMAND_TIMEOUT = 600


# ── Language recipes ─────────────────────────────────────────────────────────
#
# Each recipe names the programs it runs (`compilers`), the libraries they
# read (`libs`), the folders of the collection those are taken from, in the
# order searched (`folders`, under software/development), and the commands,
# with ``{name}`` substituted with the project's base name at expand time.
# Manifests can override `commands` for projects with non-standard linking
# (overlays, a map, several sources, ...).
#
# DEC's tools and libraries are in the root folder; MACRO there is the FODOS
# kit's, the one tool DEC left no source for.  A kit's toolchain - Pascal,
# FORTRAN - has a folder of its own and links against the kit's system
# library, which with its macro library is in fodos/: that folder is
# searched before the root, so the kit's SYSLIB wins over DEC's there.
#
# The system libraries are looked for on SY: by the programs themselves
# (MACRO opens SY:SYSMAC.SML, LINK SY:SYSLIB.OBJ); ms0515-run gives a
# program the file it asks for by name out of the folder, so they are staged
# beside everything else.

RECIPES = {
    "macro11": {
        "extension": "MAC",
        "folders":   [""],
        "compilers": ["MACRO.SAV", "LINK.SAV"],
        "libs":      ["SYSMAC.SML", "SYSLIB.OBJ"],
        "commands":  ["MACRO {name}={name}",
                      "LINK {name}={name}"],
    },
    "pascal": {
        "extension": "PAS",
        "folders":   ["pascal", "fodos", ""],
        "compilers": ["PAS1.SAV", "MACRO.SAV", "LINK.SAV"],
        "libs":      ["SYSMAC.SML", "SYSLIB.OBJ", "PASLIB.OBJ", "PAS1.OBJ"],
        "commands":  ["PAS1 {name}={name}",
                      "MACRO {name}={name}",
                      "LINK {name}={name},PASLIB,PAS1"],
    },
    "fortran": {
        "extension": "FOR",
        "folders":   ["fortran", "fodos", ""],
        "compilers": ["FORTRA.SAV", "LINK.SAV"],
        "libs":      ["SYSLIB.OBJ", "FORLIB.OBJ"],
        "commands":  ["FORTRA {name}={name}",
                      "LINK {name}={name},FORLIB"],
    },
    "basic": {
        # BASIC is interpreter-only here — interactive sessions aren't a
        # build artifact, so this entry only stages the interpreter beside
        # the program: `ms0515-run BASICO` in the work folder runs it.
        "extension": "BAS",
        "folders":   ["basic"],
        "compilers": ["BASICO.SAV"],
        "libs":      [],
        "commands":  [],
    },
}

DEVELOPMENT = Path("software") / "development"


# ── Manifest -> resolved build plan ───────────────────────────────────────────

class BuildPlan:
    """Everything ``run()`` needs, in one validated bundle."""

    def __init__(self, manifest: dict, manifest_path: Path) -> None:
        if "project" not in manifest:
            raise ValueError("manifest is missing the [project] table")
        proj = manifest["project"]
        for required in ("name", "language"):
            if required not in proj:
                raise ValueError(f"[project] is missing {required!r}")
        if proj["language"] not in RECIPES:
            raise ValueError(
                f"unknown language {proj['language']!r}; "
                f"choose from {sorted(RECIPES)}"
            )
        self.manifest_dir = manifest_path.parent
        self.name      = proj["name"]
        self.language  = proj["language"]
        self.pre_hook  = proj.get("pre_build")
        self.post_hook = proj.get("post_build")

        recipe = RECIPES[self.language]
        self.sources  = proj.get("sources",  [f"{self.name}.{recipe['extension']}"])
        self.outputs  = proj.get("outputs",  [f"{self.name}.SAV"])

        for stray in ("commands", "libs"):
            if stray in proj:
                raise ValueError(
                    f"[project] carries {stray!r}: it belongs in [build], and "
                    "was being ignored - the project built with the language's "
                    "default recipe instead"
                )

        build_cfg = manifest.get("build", {})
        self.extra_libs = build_cfg.get("libs", [])
        commands_tmpl   = build_cfg.get("commands", recipe["commands"])
        self.commands   = [c.format(name=self.name) for c in commands_tmpl]
        self.folders    = recipe["folders"]
        self.compilers  = recipe["compilers"]
        self.recipe_libs = recipe["libs"]

    def dk_files(self) -> list[Path]:
        """The project's part of the work folder: the sources and the extra
        object libraries to link against - the project's own, else the
        collection's development files."""
        files = [self.manifest_dir / s for s in self.sources]
        for lib in self.extra_libs:
            own = self.manifest_dir / lib
            files.append(own if own.is_file() else collection_lib(lib))
        return files

    def tool_files(self) -> list[Path]:
        """The toolchain's part: the recipe's programs and libraries, each
        from the first of the recipe's folders that has it."""
        root = decsys.collection() / DEVELOPMENT
        files = []
        for name in [*self.compilers, *self.recipe_libs]:
            for folder in self.folders:
                if (root / folder / name).is_file():
                    files.append(root / folder / name)
                    break
            else:
                raise SystemExit(
                    f"the collection has no {name} for {self.language} "
                    f"(looked in {[str(root / f) for f in self.folders]})")
        return files


def collection_lib(name: str) -> Path:
    """The collection's copy of a library to link against: the development
    software's folders - DEC's system libraries, the linker and librarian
    in the root, Pascal's and FORTRAN's in theirs, the kits' own system
    libraries in fodos/."""
    root = decsys.collection() / DEVELOPMENT
    for folder in ("", "pascal", "fortran", "fodos"):
        if (root / folder / name).is_file():
            return root / folder / name
    return root / name      # a name it has not: reported as missing


def load_manifest(path: Path) -> BuildPlan:
    with path.open("rb") as f:
        manifest = tomllib.load(f)
    return BuildPlan(manifest, path)


# ── Build runner ─────────────────────────────────────────────────────────────

def run_command(command: str, work: Path) -> str:
    """One command of a recipe - ``PROGRAM line`` - run in `work` with
    ms0515-run; returns what the program printed.  Raises RT11CommandError
    when the program failed: the tool's exit status, which is the monitor's
    own account of the program, or an error or fatal diagnostic
    (``?XXX-E-``, ``?XXX-F-``) in what it printed.  A warning (``-W-``, e.g.
    LINK's undefined globals) is left to the reader."""
    program, _, line = command.partition(" ")
    try:
        r = subprocess.run([*RUNNER, program, *line.split()], cwd=work,
                           stdin=subprocess.DEVNULL, capture_output=True,
                           timeout=COMMAND_TIMEOUT)
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b"").decode("utf-8", errors="replace")
        raise RT11CommandError(command, f"still running after {COMMAND_TIMEOUT} s", out)
    out = r.stdout.decode("utf-8", errors="replace").replace("\r\n", "\n")
    out += r.stderr.decode("utf-8", errors="replace").replace("\r\n", "\n")
    diag = re.search(r"\?[A-Z]{2,6}-[FEU]-[^\r\n]*", out)
    if diag:
        raise RT11CommandError(command, diag.group(0).strip(), out)
    if r.returncode != 0:
        last = out.strip().splitlines()[-1] if out.strip() else "nothing printed"
        raise RT11CommandError(command, f"status {r.returncode}: {last}", out)
    return out


def run(plan: BuildPlan, *, build_root: Path | None = None) -> None:
    if plan.pre_hook:
        print(f"[1/4] pre_build -> {plan.pre_hook}")
        subprocess.run([sys.executable, str(plan.manifest_dir / plan.pre_hook)],
                       check=True)

    # One work folder: the sources, the extra libraries and the toolchain
    # side by side.  It is the disk of every program run in it (DK:), and
    # what a program writes is a host file there.
    if build_root is None:
        build_root = Path(tempfile.gettempdir()) / f"{plan.name.lower()}_build"
    shutil.rmtree(build_root, ignore_errors=True)
    work = build_root / "work"
    work.mkdir(parents=True)

    dk_files = plan.dk_files()
    tools = plan.tool_files()
    print(f"[2/4] stage {work}: {len(dk_files)} file(s) of the project, "
          f"{len(tools)} of the toolchain")
    for f in [*tools, *dk_files]:           # the project's own win a name
        shutil.copy(f, work / f.name)

    print(f"[3/4] build")
    for command in plan.commands:
        print(f"      {command}", flush=True)
        try:
            out = run_command(command, work)
        except RT11CommandError as e:
            print(e.full_output, flush=True)
            raise
        for text in out.strip().splitlines():
            print(f"        {text}")

    # Outputs are host files the programs wrote in the work folder (under
    # lowercased names).  Pick them up case-insensitively.
    print(f"[4/4] collect {plan.outputs}")
    byLower = {p.name.lower(): p for p in work.iterdir() if p.is_file()}
    missing = []
    for out in plan.outputs:
        src = byLower.get(out.lower())
        if src is None:
            missing.append(out)
            continue
        shutil.copy(src, plan.manifest_dir / out)
        print(f"  {src.name} -> {out} ({src.stat().st_size} B)")
    if missing:
        raise SystemExit(f"build produced no {missing} in {work}")

    if plan.post_hook:
        print(f"[+] post_build -> {plan.post_hook}")
        subprocess.run([sys.executable, str(plan.manifest_dir / plan.post_hook)],
                       check=True)

    print(f"done -- outputs in {plan.manifest_dir}")


def main(argv: list[str] | None = None) -> int:
    argv = argv if argv is not None else sys.argv
    if len(argv) >= 2:
        manifest_path = Path(argv[1]).resolve()
    else:
        manifest_path = Path.cwd() / "build.toml"
    if not manifest_path.exists():
        print(f"manifest not found: {manifest_path}", file=sys.stderr)
        return 1
    plan = load_manifest(manifest_path)
    run(plan)
    return 0


if __name__ == "__main__":
    sys.exit(main())
