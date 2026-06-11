"""
build — universal driver for MS-0515 / RT-11 projects.

Reads a ``build.toml`` manifest in the project directory, runs the
project through the standard pipeline:

  1. (optional) pre_build hook            — host-side, e.g. code generator
  2. copy system.dsk to a working location
  3. stage the language recipe as STARTS.COM on SY: + source + toolchain on
     side 1 (system.dsk carries no STARTS.COM, so this is a plain put)
  4. boot ms0515-cli; the SJ monitor auto-runs STARTS.COM, so the build runs
     unattended.  Wait for it to finish (a type-ahead `DIR` probe), then scan
     the whole transcript for any ?xxx-F-/-E- diagnostic.
  5. extract output files back to the project directory
  6. (optional) post_build hook           — host-side, e.g. packaging

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
    libs     = ["EXTRA.OBJ"]        # optional, extra files staged + linked
    commands = ["RUN DZ2:..."]      # optional, overrides the language recipe

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
import time
import tomllib
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
SYSTEM_DISK = HERE / "system.dsk"
DEVEL       = HERE / "build_tools"
CLI         = ROOT / "package/ms0515-cli.exe"
ROM         = ROOT / "package/assets/rom/ms0515-roma.rom"
DISK_TOOL   = ROOT / "package/ms0515-disk.exe"

sys.path.insert(0, str(HERE))
from emu_driver import EmulatorDriver       # noqa: E402
from rt11 import RT11CommandError           # noqa: E402


# ── Language recipes ─────────────────────────────────────────────────────────
#
# Each recipe is a (compilers, libs, commands) triple plus the extension
# that names the canonical source file.  ``{name}`` is substituted with the
# project's base name at expand time.  Manifests can override `commands` for
# projects with non-standard linking (overlays, specific arg orders, ...).

RECIPES = {
    "macro11": {
        "extension": "MAC",
        "compilers": ["MACRO.SAV", "LINK.SAV"],
        "libs":      ["SYSMAC.SML", "SYSLIB.OBJ"],
        "commands":  ["RUN DZ2:MACRO {name}",
                      "RUN DZ2:LINK {name}"],
    },
    "pascal": {
        "extension": "PAS",
        "compilers": ["PAS1.SAV", "MACRO.SAV", "LINK.SAV"],
        "libs":      ["SYSMAC.SML", "SYSLIB.OBJ", "PASLIB.OBJ", "PAS1.OBJ"],
        "commands":  ["RUN DZ2:PAS1 {name}={name}",
                      "RUN DZ2:MACRO {name}",
                      "RUN DZ2:LINK {name},PASLIB,PAS1"],
    },
    "fortran": {
        "extension": "FOR",
        "compilers": ["FORTRA.SAV", "MACRO.SAV", "LINK.SAV"],
        "libs":      ["SYSMAC.SML", "SYSLIB.OBJ", "FORLIB.OBJ"],
        "commands":  ["RUN DZ2:FORTRA {name}",
                      "RUN DZ2:MACRO {name}",
                      "RUN DZ2:LINK {name},FORLIB"],
    },
    "basic": {
        # BASIC is interpreter-only here — interactive sessions aren't a
        # build artifact, so we mostly use this entry for staging the
        # binary on a work disk and letting the user drive it manually.
        "extension": "BAS",
        "compilers": ["BASICO.SAV"],
        "libs":      [],
        "commands":  [],
    },
}


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

        build_cfg = manifest.get("build", {})
        self.extra_libs = build_cfg.get("libs", [])
        commands_tmpl   = build_cfg.get("commands", recipe["commands"])
        self.commands   = [c.format(name=self.name) for c in commands_tmpl]
        self.compilers  = recipe["compilers"]
        self.recipe_libs = recipe["libs"]

    def staged_files(self) -> list[Path]:
        """Absolute paths of every file we put on side 1 before the build."""
        files = [self.manifest_dir / s for s in self.sources]
        files += [DEVEL / c for c in self.compilers]
        files += [DEVEL / l for l in self.recipe_libs]
        files += [DEVEL / l for l in self.extra_libs]
        return files


def load_manifest(path: Path) -> BuildPlan:
    with path.open("rb") as f:
        manifest = tomllib.load(f)
    return BuildPlan(manifest, path)


# ── Build runner ─────────────────────────────────────────────────────────────

def run(plan: BuildPlan, *, work_disk: Path | None = None) -> None:
    if not SYSTEM_DISK.exists():
        raise SystemExit(f"missing {SYSTEM_DISK}")

    if plan.pre_hook:
        print(f"[1/5] pre_build -> {plan.pre_hook}")
        subprocess.run([sys.executable, str(plan.manifest_dir / plan.pre_hook)],
                       check=True)

    if work_disk is None:
        work_disk = Path(tempfile.gettempdir()) / f"{plan.name.lower()}_build.dsk"
    print(f"[2/5] copy system.dsk -> {work_disk}")
    shutil.copy(SYSTEM_DISK, work_disk)

    # The build recipe IS the startup file: write the project's commands into
    # a STARTS.COM and stage it on SY: (side 0) like any other build file, so
    # the SJ monitor runs the whole build itself at boot.  (system.dsk carries
    # no STARTS.COM, so this is just a plain `put` — nothing to replace.)  We
    # then wait for it to finish instead of driving each command from the host.
    print(f"[3/5] stage build STARTS.COM + {len(plan.staged_files())} files")
    starts = Path(tempfile.mkdtemp()) / "STARTS.COM"
    recipe = ["ASSIGN DZ2 DK", *plan.commands]
    starts.write_bytes(("".join(c + "\r\n" for c in recipe)).encode("ascii"))
    subprocess.run([str(DISK_TOOL), "put", str(work_disk), "--side", "0",
                    str(starts)], check=True)
    subprocess.run([str(DISK_TOOL), "put", str(work_disk), "--side", "1",
                    *(str(f) for f in plan.staged_files())], check=True)
    for c in recipe:
        print(f"      {c}")

    print(f"[4/5] boot + run the build (STARTS.COM)")
    emu = EmulatorDriver([CLI, "--rom", ROM, "--disk0", work_disk])
    emu.start()
    try:
        # Accept the localized Date/Time prompts; STARTS.COM then auto-runs the
        # build.  The DIR probe is type-ahead — it executes only after the
        # startup file finishes, so its "Free blocks" line marks completion.
        time.sleep(2.0)
        for _ in range(3):
            emu.send("\r"); time.sleep(0.4)
        emu.send("DIR DZ2:\r")
        emu.wait_for(r"Free|Files,", "build complete", timeout=600)
        time.sleep(0.5)
        with emu._buf_lock:
            log = emu._decode(bytes(emu._buf))
    finally:
        emu.kill()

    # The build ran unattended, so scan its whole transcript for fatal (-F-)
    # or error (-E-, e.g. MACRO "Errors detected") diagnostics.
    diag = re.search(r"\?[A-Z]{2,5}-[FE]-[^\r\n]*", log)
    if diag:
        raise RT11CommandError("build (STARTS.COM)", diag.group(0).strip(), log)

    print(f"[5/5] extract {plan.outputs}")
    subprocess.run([str(DISK_TOOL), "get", str(work_disk), "--side", "1",
                    "--out", str(plan.manifest_dir), *plan.outputs], check=True)

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
