"""verify_kit.py - the built kit used on a real dec system.

    python verify_kit.py [--system KEY] PROGRAM [PROGRAM...] [OUTDIR]

A bootable diskette is composed from the software collection, the programs
go on it, and each is used the way it is meant to be: a utility run with
`R`, a handler installed and loaded, `HELP` asked a question, `IND` given
an indirect file written for the occasion.  What the machine says back is
read for a `?xxx-F-` of its own.

Nothing here assumes: the ROM's own screen ends in dots, so the monitor
has to name itself before a single program is tried.  A test that takes a
dot for a prompt passes on a machine that never booted.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "rt11_devel" / "toolset"))
from emu_driver import EmulatorDriver  # noqa: E402
import build_util as bu  # noqa: E402

DISK = ROOT / "package" / "ms0515-disk.exe"

# An indirect file that uses what IND is for: a variable, a number, a test
# and a branch.  If IND runs it at all, it says so in its own words.
TRY_COM = b""".ENABLE SUBSTITUTION
.SETS NAME "MS0515"
.SETN N 40
.IF N GT 39 .GOTO BIG
.;IND TOOK THE WRONG BRANCH
.GOTO DONE
.BIG:
.;IND RAN 'NAME'
.DONE:
.EXIT
"""


def disk(*args) -> None:
    subprocess.run([str(DISK), *map(str, args)], check=True, capture_output=True)


def collection() -> Path:
    base = os.environ.get("MS0515_SOFTWARE")
    root = Path(base) if base else ROOT.parent / "ms0515-software"
    if not (root / "disks.toml").is_file():
        raise SystemExit(f"no software collection in {root} (set MS0515_SOFTWARE)")
    return root


def lines_for(p: Path) -> list[tuple[str, bool]]:
    """What to type for a program, and whether that step may end without a
    prompt: IND's own prompt is none of the three RT-11 has."""
    name = p.stem.upper()
    if p.suffix.upper() == ".SYS":
        # SHOW takes no device name here; what proves a handler is the
        # device working, so the null device is written to.
        use = [("COPY TRY.COM NL:", False)] if name == "NL" else []
        return [(f"INSTALL {name}", False), (f"LOAD {name}", False)] + use
    if name == "IND":
        return [("R IND", True), ("TRY", False)]
    if name == "HELP":
        return [("HELP DIR", False)]
    if name == "EDIT":
        # EDIT takes ^C for itself (.SCCA), so the two ^C that abort any
        # other program are just characters to it.  Its own commands end
        # with two ESC, and ^C among them is the one that leaves.
        return [("R EDIT", False), ("\x03\x1b\x1b", False)]
    return [(f"R {name}", False), (bu.ABORT, False)]


def make_disk(out: Path, system: str, programs: list[Path]) -> Path:
    floppy = out / "verify.dsk"
    disk("compose", "--repo", collection(), "--system", system,
         "--media", "dz", floppy)
    staged = out / "files"
    staged.mkdir(parents=True)
    for p in programs:
        shutil.copy(p, staged / p.name.upper())
    (staged / "TRY.COM").write_bytes(TRY_COM.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))
    disk("put", floppy, *sorted(staged.iterdir()))
    return floppy


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    args = list(sys.argv[1:])
    system = "dec"
    while args and args[0] == "--system":
        args.pop(0)
        system = args.pop(0)
    if not args:
        raise SystemExit(__doc__)
    out = Path(args.pop()) if len(args) > 1 and not Path(args[-1]).is_file() \
        else Path(tempfile.mkdtemp(prefix="dec_verify_"))
    programs = [Path(a) for a in args]
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    floppy = make_disk(out, system, programs)

    emu = EmulatorDriver([bu.CLI, "--no-config", "--disk0", str(floppy)])
    emu.start()
    failed: dict[str, str] = {}
    try:
        emu.wait_for(r"[.*]\s*$", "something on the screen", timeout=90)
        for _ in range(3):
            emu.send("\r")
        bu.step(emu, "DATE")
        with emu._buf_lock:
            whole = emu._decode(bytes(emu._buf))
        if not re.search(r"SJ[()A-Z]{0,3}V0?5\.", whole.replace(" ", ""), re.I):
            print("the monitor never came up; the screen:\n" + emu.tail(800))
            return 1
        print(f"booted: {system}")

        for p in programs:
            said = ""
            for line, may_hang in lines_for(p):
                try:
                    out_text = bu.step(emu, line)
                except TimeoutError as e:
                    if may_hang:
                        continue
                    said = str(e)
                    break
                said = bu.complaint(out_text, line)
                if said:
                    break
            print(f"{p.stem.upper():8s} {said or 'ok'}", flush=True)
            if said:
                failed[p.stem.upper()] = said
            bu.recover(emu)
    finally:
        emu.dump(out / "session.log")
        emu.kill()
    if failed:
        print("failed:", ", ".join(sorted(failed)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
