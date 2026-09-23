"""OS-oracle: our DZ.SYS as the whole floppy system of a dec disk.

A dec diskette is composed from the software collection, its DZ.SYS
replaced with ours and its bootstrap written again - so the primary
driver on block 0 is ours too - and the machine has to:

  1. boot: the ROM enters our primary driver, which loads the monitor's
     bootstrap, which loads the monitor through our read routine;
  2. read a marked diskette on the other drive - every block of it full
     of its own number - through DZ1: and get blocks 0, 1 and 10 as 20,
     22 and 42: the 2:1 interleave, the skew of two sectors a track and
     cylinder 0 coming last, as the kits' handler reads it;
  3. copy files from drive 0 to an empty volume on drive 1, which the
     host tool - it has the geometry from the core, not from the handler
     - then reads back out of the image identical to the byte.

Build DZ.SYS first:
    python ../../kit/build_handler.py --source . DZ OUTDIR
and copy OUTDIR/DZ.SYS here.

    python validate.py KIT [SYSTEM-FILE ...]

KIT is the folder of the dec kit's programs (DUMP, PIP, DUP, built by
../../kit).  Each SYSTEM-FILE replaces the file of its name on the composed
diskette - a monitor refuses a handler whose sysgen word is not its own,
so a DZ.SYS built with ERL$G needs the monitor and TT.SYS built with it
too, and they are given here until the collection carries them.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
sys.path.insert(0, str(ROOT / "rt11_devel" / "toolset"))
sys.path.insert(0, str(HERE.parents[1] / "kit"))
from emu_driver import EmulatorDriver  # noqa: E402
import build_util as bu  # noqa: E402

DISK = ROOT / "package" / "ms0515-disk.exe"
MARKED = {"0": "000024", "1": "000026", "12": "000052"}   # block(8) -> word


def disk(*args) -> str:
    return subprocess.run([str(DISK), *map(str, args)], check=True,
                          capture_output=True, text=True).stdout


def collection() -> Path:
    base = os.environ.get("MS0515_SOFTWARE")
    root = Path(base) if base else ROOT.parent / "ms0515-software"
    if not (root / "disks.toml").is_file():
        raise SystemExit(f"no software collection in {root} (set MS0515_SOFTWARE)")
    return root


def make_disks(work: Path, kit: Path, files: list[Path],
               system: list[Path]) -> tuple[Path, Path, Path]:
    boot = work / "boot.dsk"
    disk("compose", "--repo", collection(), "--system", "dec", "--media", "ss", boot)
    # A DZ.SYS among the system files stands in for the one kept here.
    given = {f.name.upper() for f in system}
    ours = ([] if "DZ.SYS" in given else [HERE / "DZ.SYS"]) + system
    for f in ours:
        disk("unprotect", boot, f.name)
        disk("rm", boot, f.name)
    disk("squeeze", boot)                    # the monitor goes back in one piece
    disk("put", boot, *ours, kit / "DUMP.SAV", *files)
    disk("boot", boot)                       # block 0 becomes our primary driver

    marked = work / "marked.dsk"             # block n is full of the word n;
    with marked.open("wb") as f:             # both sides, as it was measured
        for n in range(1600):
            f.write(bytes([n & 0xFF, (n >> 8) & 0xFF]) * 256)

    target = work / "target.dsk"
    disk("create", target)
    disk("init", target)
    return boot, marked, target


def session(boot: Path, other: Path, how: str = "--disk1-side0"):
    emu = EmulatorDriver([bu.CLI, "--no-config", "--disk0-side0", str(boot), how, str(other)])
    emu.start()
    emu.wait_for(r"[.*]\s*$", "something on the screen", timeout=90)
    for _ in range(3):
        emu.send("\r")
    bu.step(emu, "DATE")
    with emu._buf_lock:
        whole = emu._decode(bytes(emu._buf))
    # The ROM's own screen ends in dots; only the monitor names itself.
    if not re.search(r"SJ[()A-Z]{0,3}V0?5\.", whole.replace(" ", ""), re.I):
        emu.kill()
        raise SystemExit("FAIL: the monitor never came up on our DZ")
    return emu


def check_reads(boot: Path, marked: Path) -> int:
    bad = 0
    emu = session(boot, marked, "--disk1")
    try:
        print("booted on our DZ, block 0 and all")
        for blk, want in MARKED.items():
            try:
                bu.step(emu, "R DUMP")
                bu.step(emu, f"TT:=DZ1:/O:{blk}")
            except TimeoutError:
                pass
            # The terminal mirrors the screen and runs the words together,
            # so the expected one is counted rather than parsed out.
            seen = " ".join(emu.tail(500).split()).count(want)
            ok = seen >= 3
            bad += not ok
            print(f"  DZ1: block {blk}(8) reads {want}: {'ok' if ok else 'WRONG'}")
            bu.recover(emu)
    finally:
        emu.kill()
    return bad


def check_writes(boot: Path, target: Path, files: list[Path], work: Path) -> int:
    bad = 0
    emu = session(boot, target)
    try:
        for f in files:
            line = f"COPY DZ0:{f.name} DZ1:{f.name}"
            try:
                said = bu.complaint(bu.step(emu, line), line)
            except TimeoutError as e:
                said = str(e)
            print(f"  {line}: {said or 'ok'}")
            if said:
                bad += 1
                bu.recover(emu)
    finally:
        emu.kill()
    back = work / "back"
    back.mkdir()
    disk("get", target, "--out", back, "*.SAV")
    for f in files:
        got = back / f.name
        a = f.read_bytes()
        b = got.read_bytes() if got.is_file() else b""
        same = bool(b) and a == b[:len(a)] and not any(b[len(a):])
        bad += not same
        print(f"  {f.name}: the host reads it back "
              f"{'identical' if same else 'DIFFERENT or missing'}")
    return bad


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    kit = Path(sys.argv[1])
    system = [Path(a) for a in sys.argv[2:]]
    if not (HERE / "DZ.SYS").is_file():
        raise SystemExit("DZ.SYS is not built - see the top of this file")
    files = [kit / "PIP.SAV", kit / "DUP.SAV"]
    work = Path(tempfile.mkdtemp(prefix="dz_oracle_"))
    try:
        boot, marked, target = make_disks(work, kit, files, system)
        bad = check_reads(boot, marked) + check_writes(boot, target, files, work)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("PASS" if not bad else f"FAIL: {bad} check(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
