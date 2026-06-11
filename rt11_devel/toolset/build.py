"""
build — universal driver for MS-0515 / RT-11 projects.

Reads a ``build.toml`` manifest in the project directory, runs the
project through the standard pipeline:

  1. (optional) pre_build hook            — host-side, e.g. code generator
  2. copy the bootable system/ FOLDER template to a temp boot/ folder; make
     an empty work/ folder — both are folder-backed devices (.rtfs), no
     disk images and no ms0515-disk calls anywhere
  3. stage the recipe as boot/STARTS.COM + compilers into boot/ (= SY:),
     sources + object libraries into work/ (= DZ1, ASSIGNed DK)
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
SYSTEM_DIR  = HERE / "system"        # bootable folder template (.rtfs)
SYSTEM_DISK = HERE / "system.dsk"    # legacy image (oracle scripts only)
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
#
# Commands use the CCL form (``MACRO foo`` not ``RUN DZ2:MACRO foo``): build.py
# stages the compilers on SY: (side 0), so KMON resolves them as commands and
# translates switches (needed for e.g. LINK/NOBITMAP/EXECUTE).  Sources and
# object libraries sit on DK: (side 1, where DZ2 is ASSIGNed).

RECIPES = {
    "macro11": {
        "extension": "MAC",
        "compilers": ["MACRO.SAV", "LINK.SAV"],
        "libs":      ["SYSMAC.SML", "SYSLIB.OBJ"],
        "commands":  ["MACRO {name}",
                      "LINK {name}"],
    },
    "pascal": {
        "extension": "PAS",
        "compilers": ["PAS1.SAV", "MACRO.SAV", "LINK.SAV"],
        "libs":      ["SYSMAC.SML", "SYSLIB.OBJ", "PASLIB.OBJ", "PAS1.OBJ"],
        "commands":  ["PAS1 {name}={name}",
                      "MACRO {name}",
                      "LINK {name},PASLIB,PAS1"],
    },
    "fortran": {
        "extension": "FOR",
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

    # Files MACRO/LINK auto-search on the system device SY: (side 0): the
    # compilers (so the CCL command form resolves them) and SYSMAC.SML (the
    # macro library MACRO looks for on SY: when expanding .MCALL).
    SY_LIBS = frozenset({"SYSMAC.SML"})

    def sy_files(self) -> list[Path]:
        """Toolchain staged on SY: (side 0): compilers + the macro library."""
        files = [DEVEL / c for c in self.compilers]
        files += [DEVEL / l for l in self.recipe_libs if l in self.SY_LIBS]
        return files

    def dk_files(self) -> list[Path]:
        """Staged on DK: (side 1): sources + object libraries to link against."""
        files = [self.manifest_dir / s for s in self.sources]
        files += [DEVEL / l for l in self.recipe_libs if l not in self.SY_LIBS]
        files += [DEVEL / l for l in self.extra_libs]
        return files


def load_manifest(path: Path) -> BuildPlan:
    with path.open("rb") as f:
        manifest = tomllib.load(f)
    return BuildPlan(manifest, path)


# ── Build runner ─────────────────────────────────────────────────────────────

def run(plan: BuildPlan, *, build_root: Path | None = None) -> None:
    if not SYSTEM_DIR.is_dir():
        raise SystemExit(f"missing {SYSTEM_DIR}")

    if plan.pre_hook:
        print(f"[1/5] pre_build -> {plan.pre_hook}")
        subprocess.run([sys.executable, str(plan.manifest_dir / plan.pre_hook)],
                       check=True)

    # Everything runs on folder-backed devices (.rtfs) — no disk images, no
    # ms0515-disk calls.  Two temp folders, both copies (the committed
    # system/ template is never modified):
    #   boot/  — the bootable system + the compilers + the build recipe as
    #            STARTS.COM (the SJ monitor runs it at boot); mounts as DZ0.
    #   work/  — sources + object libraries; mounts as DZ1 (ASSIGNed DK).
    # Outputs are simply host files the guest materializes in work/.
    if build_root is None:
        build_root = Path(tempfile.gettempdir()) / f"{plan.name.lower()}_build"
    shutil.rmtree(build_root, ignore_errors=True)
    boot = build_root / "boot"
    work = build_root / "work"
    print(f"[2/5] system/ template -> {boot}")
    shutil.copytree(SYSTEM_DIR, boot)
    work.mkdir(parents=True)

    sy_files = plan.sy_files()
    dk_files = plan.dk_files()
    print(f"[3/5] stage boot/: STARTS.COM + {len(sy_files)} tool(s), "
          f"work/: {len(dk_files)} file(s)")
    recipe = ["ASSIGN DZ1 DK", *plan.commands]
    (boot / "STARTS.COM").write_bytes(
        ("".join(c + "\r\n" for c in recipe)).encode("ascii"))
    for f in sy_files:
        shutil.copy(f, boot / f.name)
    for f in dk_files:
        shutil.copy(f, work / f.name)
    (work / "device.rtfs").write_bytes(b"device: floppy\nblocks: 800\n")
    for c in recipe:
        print(f"      {c}")

    print(f"[4/5] boot + run the build (STARTS.COM)")
    emu = EmulatorDriver([CLI, "--no-config", "--rom", ROM,
                          "--disk0-side0", boot / "device.rtfs",
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
