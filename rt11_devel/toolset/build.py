"""
build — universal driver for MS-0515 / RT-11 projects.

Reads a ``build.toml`` manifest in the project directory, runs the
project through the standard pipeline:

  1. (optional) pre_build hook            — host-side, e.g. code generator
  2. compose the system disk (decsys.py: the collection's dec system with
     the language's toolchain and the recipe as its STARTS.COM) and make an
     empty work/ folder, a folder-backed device (.rtfs)
  3. stage the sources + object libraries into work/ (= DZ1, ASSIGNed DK)
  4. boot ms0515-cli --no-config; the SJ monitor auto-runs STARTS.COM, so
     the build runs unattended.  Wait for it to finish (a type-ahead `DIR`
     probe), then scan the whole transcript for any ?xxx-F-/-E- diagnostic.
  5. outputs are host files the guest materialized in work/ — copy them to
     the project directory
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
    libs     = ["EXTRA.OBJ"]        # optional, extra files staged + linked:
                                    # the project's own, else the collection's
                                    # (kits/common/development)
    commands = ["MACRO {name}/LIST"]  # optional, overrides the language recipe

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

sys.path.insert(0, str(HERE))
import decsys                               # noqa: E402
from emu_driver import EmulatorDriver       # noqa: E402
from rt11 import RT11CommandError           # noqa: E402

CLI = decsys.CLI


# ── Language recipes ─────────────────────────────────────────────────────────
#
# Each recipe names the bundles of the collection that put its toolchain on
# the system disk (`bundles`, over decsys.BASE: DEC's LINK, LIBR, SYSLIB,
# SYSMAC and ODT, the kit's MACRO), the compilers and libraries that puts
# there (`compilers`, `libs` - what the commands may count on), and the
# commands, with ``{name}`` substituted with the project's base name at
# expand time.  Manifests can override `commands` for projects with
# non-standard linking (overlays, specific arg orders, ...).
#
# Commands use the CCL form (``MACRO foo`` not ``RUN DZ2:MACRO foo``): the
# compilers are on SY:, so KMON resolves them as commands and translates
# switches (needed for e.g. LINK/NOBITMAP/EXECUTE).  Sources and extra
# object libraries sit on DK: (the work folder, ASSIGNed from DZ1); the
# system library LINK takes from SY: - DEC's, or the Pascal kit's where
# the toolchain is that kit's (decsys.KIT_SYSLIB_TOOLCHAINS).

RECIPES = {
    "macro11": {
        "extension": "MAC",
        "bundles":   [],
        "compilers": ["MACRO.SAV", "LINK.SAV"],
        "libs":      ["SYSMAC.SML", "SYSLIB.OBJ"],
        "commands":  ["MACRO {name}",
                      "LINK {name}"],
    },
    "pascal": {
        "extension": "PAS",
        "bundles":   ["pascal"],
        "compilers": ["PAS1.SAV", "MACRO.SAV", "LINK.SAV"],
        "libs":      ["SYSMAC.SML", "SYSLIB.OBJ", "PASLIB.OBJ", "PAS1.OBJ"],
        "commands":  ["PAS1 {name}={name}",
                      "MACRO {name}",
                      "LINK {name},PASLIB,PAS1"],
    },
    "fortran": {
        "extension": "FOR",
        "bundles":   ["fortran"],
        "compilers": ["FORTRA.SAV", "MACRO.SAV", "LINK.SAV"],
        "libs":      ["SYSMAC.SML", "SYSLIB.OBJ", "FORLIB.OBJ"],
        "commands":  ["FORTRA {name}",
                      "MACRO {name}",
                      "LINK {name},FORLIB"],
    },
    "basic": {
        # BASIC is interpreter-only here — interactive sessions aren't a
        # build artifact, so we mostly use this entry for staging the
        # binary on a work disk and letting the user drive it manually.
        "extension": "BAS",
        "bundles":   ["basico"],
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
        self.bundles    = recipe["bundles"]
        self.compilers  = recipe["compilers"]
        self.recipe_libs = recipe["libs"]

    def dk_files(self) -> list[Path]:
        """Staged on DK: (the work folder): the sources and the extra object
        libraries to link against - the project's own, else the collection's
        development files.  The toolchain and its libraries are on SY:, the
        system disk."""
        files = [self.manifest_dir / s for s in self.sources]
        for lib in self.extra_libs:
            own = self.manifest_dir / lib
            files.append(own if own.is_file()
                         else decsys.collection() / "kits" / "common" / "development" / lib)
        return files


def load_manifest(path: Path) -> BuildPlan:
    with path.open("rb") as f:
        manifest = tomllib.load(f)
    return BuildPlan(manifest, path)


# ── Build runner ─────────────────────────────────────────────────────────────

def run(plan: BuildPlan, *, build_root: Path | None = None) -> None:
    if plan.pre_hook:
        print(f"[1/5] pre_build -> {plan.pre_hook}")
        subprocess.run([sys.executable, str(plan.manifest_dir / plan.pre_hook)],
                       check=True)

    # Two temp folder-backed devices (.rtfs):
    #   boot/  — the system composed from the collection (decsys): DEC's
    #            system and tools, the language's toolchain, and the build
    #            recipe as STARTS.COM (the SJ monitor runs it at boot);
    #            mounts as DZ0, the system.
    #   work/  — sources + extra object libraries; mounts as DZ1
    #            (ASSIGNed DK).
    # Outputs are simply host files the guest materializes in work/.
    if build_root is None:
        build_root = Path(tempfile.gettempdir()) / f"{plan.name.lower()}_build"
    shutil.rmtree(build_root, ignore_errors=True)
    boot = build_root / "boot"
    work = build_root / "work"
    work.mkdir(parents=True)

    recipe = ["ASSIGN DZ1 DK", *plan.commands]
    print(f"[2/5] the system -> {boot}: dec + {plan.bundles or 'DEC tools'}, "
          f"STARTS.COM:")
    for c in recipe:
        print(f"      {c}")
    decsys.compose(boot, startup=recipe, add=plan.bundles, quiet=False)

    dk_files = plan.dk_files()
    print(f"[3/5] stage work/: {len(dk_files)} file(s)")
    for f in dk_files:
        shutil.copy(f, work / f.name)
    (work / "device.rtfs").write_bytes(b"device: floppy\nblocks: 800\n")

    print(f"[4/5] boot + run the build (STARTS.COM)")
    emu = EmulatorDriver([CLI, "--no-config", "--disk0-side0", boot / decsys.DESCRIPTOR,
                          "--disk1-side0", work / "device.rtfs"])
    emu.start()
    try:
        # Accept the localized Date/Time prompts; STARTS.COM then auto-runs the
        # build.  The DIR probe is type-ahead — it executes only after the
        # startup file finishes, so its "Free blocks" line marks completion.
        time.sleep(2.0)
        for _ in range(3):
            emu.send("\r"); time.sleep(0.4)
        emu.send("DIR DZ1:\r")
        try:
            emu.wait_for(r"Free|Files,", "build complete", timeout=600)
        except TimeoutError:
            print("the build did not end; the screen:\n" + emu.tail(1500), flush=True)
            raise
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

    # Outputs are already host files in work/ — the guest materialized them
    # (under lowercased names).  Pick them up case-insensitively.
    print(f"[5/5] collect {plan.outputs}")
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
