"""build_format.py - DEC's FORMAT with the MS 0515's module in it.

FORMAT is a root and a module for each device, put together at link
time, so the root is DEC's as it is and two files change:

  FMTDZ.MAC   DEC's is a stub for the Professional 350.  Ours is this
              directory's FMTDZ.MAC behind the macros every module opens
              with, which are taken from DEC's file: all of it that
              stands before its routine.
  FMTDEV.MAC  the table of devices gets MZ beside DZ.  DV needs no
              entry and cannot have one: it answers .DSTATUS with
              DZ's code and is found as DZ; the module tells them
              apart by the volume's size.

Both are made in a temporary directory and handed to ../../kit/build_util.py
over the kit's own, which then follows DEC's FORMAT.COM line for line.

    python build_format.py OUTDIR
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
KIT = HERE.parent.parent / "kit"
sys.path.insert(0, str(KIT))
import build_util as bu  # noqa: E402

ROUTINE = ".SBTTL\tRX50 FORMATTING ROUTINE"       # where DEC's own code begins
DZ_ENTRY = "DEVIC$  DZ,52,Y"
# The code the machine's MZ handler answers .DSTATUS with.
OTHERS = "\tDEVIC$\tMZ,55,Y\t\t\t;MS 0515, both sides as one volume\r\n"


def module(src: Path, out: Path) -> Path:
    dec = (src / "FMTDZ.MAC").read_bytes().decode("latin-1")
    at = dec.index(ROUTINE)
    ours = (HERE / "FMTDZ.MAC").read_text(encoding="ascii")
    ours = ours.replace("\r\n", "\n").replace("\n", "\r\n")
    target = out / "FMTDZ.MAC"
    target.write_bytes((dec[:at] + ours).encode("latin-1"))
    return target


def devices(src: Path, out: Path) -> Path:
    dec = (src / "FMTDEV.MAC").read_bytes().decode("latin-1")
    line = next(l for l in dec.splitlines(keepends=True) if DZ_ENTRY in l)
    target = out / "FMTDEV.MAC"
    target.write_bytes(dec.replace(line, line + OTHERS, 1).encode("latin-1"))
    return target


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    src = bu.dec_sources()
    with tempfile.TemporaryDirectory(prefix="format_") as tmp:
        given = [module(src, Path(tmp)), devices(src, Path(tmp))]
        return subprocess.run(
            [sys.executable, str(KIT / "build_util.py"), "--with",
             ",".join(str(g) for g in given), "FORMAT", sys.argv[1]]).returncode


if __name__ == "__main__":
    sys.exit(main())
