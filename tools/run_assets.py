"""run_assets.py - ms0515-run's assets taken anew from the software collection.

    python tools/run_assets.py            fetch them, write those that changed
    python tools/run_assets.py --check    say whether they are current; 1 if not

src/tools/run/assets/ holds what ms0515-run is made of and tested with,
kept in this repository so that the emulator builds from its own files
alone.  Their bytes come from the software collection; this script is
the one way they are brought up to date, and the change then goes in as
a pull request of its own:

    system.dsk  the diskette ms0515-run boots and bakes in: the collection's
                `dec` kit with OSA's EM.SYS, composed by this repository's
                ms0515-disk (`compose --system dec --media ss --add hd-rt11
                --add em`)
    DIR.SAV,    the programs its tests run (src/tools/run/tests,
    PIP.SAV     src/web/run_check.mjs): RT-11's DIR and PIP of the
                collection's `vvv` kit, as they are

The collection is $MS0515_SOFTWARE_DIR, else ../ms0515-software beside
this repository; ms0515-disk is the one in package/ (`conan build` in
src/ puts it there).
"""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "src" / "tools" / "run" / "assets"
SYSTEM = ["--system", "dec", "--media", "ss", "--add", "hd-rt11", "--add", "em"]
COPIED = {"DIR.SAV": "kits/vvv/utils/DIR.SAV", "PIP.SAV": "kits/vvv/utils/PIP.SAV"}


def collection() -> Path:
    base = os.environ.get("MS0515_SOFTWARE_DIR")
    root = Path(base) if base else ROOT.parent / "ms0515-software"
    if not (root / "disks.toml").is_file():
        sys.exit(f"no software collection at {root}: set MS0515_SOFTWARE_DIR")
    return root


def disktool() -> Path:
    for name in ("ms0515-disk.exe", "ms0515-disk"):
        if (ROOT / "package" / name).is_file():
            return ROOT / "package" / name
    sys.exit(f"no ms0515-disk in {ROOT / 'package'}: build the emulator first")


def fetch(repo: Path) -> dict[str, bytes]:
    """The assets as the collection makes them now, by name."""
    with tempfile.TemporaryDirectory() as tmp:
        made = Path(tmp) / "system.dsk"
        subprocess.run([str(disktool()), "compose", "--repo", str(repo), *SYSTEM, str(made)],
                       check=True, stdout=subprocess.DEVNULL)
        fresh = {"system.dsk": made.read_bytes()}
    for name, source in COPIED.items():
        fresh[name] = (repo / source).read_bytes()
    return fresh


def main() -> int:
    check = "--check" in sys.argv[1:]
    stale = 0
    for name, data in fetch(collection()).items():
        target = ASSETS / name
        if target.is_file() and target.read_bytes() == data:
            print(f"{name}: current")
            continue
        stale += 1
        if check:
            print(f"{name}: differs from the collection's")
        else:
            target.write_bytes(data)
            print(f"{name}: written anew")
    if stale and not check:
        print("commit the new assets by a pull request of their own")
    return 1 if stale and check else 0


if __name__ == "__main__":
    sys.exit(main())
