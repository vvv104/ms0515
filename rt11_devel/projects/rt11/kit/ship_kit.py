"""ship_kit.py - put what was built here into the software collection.

The collection (ms0515-software) keeps files, not builds; this copies the
ones the `dec` systems are made of from the directories the builders left
them in.  What goes where is the table below, and nothing else is copied:
a build directory holds objects, maps and tools beside the programs.

    python ship_kit.py COLLECTION BUILD-DIR [BUILD-DIR ...]

A file is looked for in the build directories in the order given, so a
later, better build is named first.  One that no directory has is
reported and the collection is left as it was.
"""
from __future__ import annotations

import shutil
import sys
from pathlib import Path

# The handlers: the machine's own, and DEC's as they are.  DEC's SL is
# not among them: its screen control does not fit the console (README).
HANDLERS = ["DZ.SYS", "DV.SYS", "MZ.SYS", "TT.SYS", "VM.SYS", "EX.SYS",
            "HD.SYS", "NL.SYS", "LD.SYS", "LP.SYS", "LS.SYS",
            "SP.SYS", "BA.SYS"]
# The utilities of a working system.
UTILS = ["DIR.SAV", "DUP.SAV", "PIP.SAV", "DUMP.SAV", "EDIT.SAV",
         "SRCCOM.SAV", "BINCOM.SAV", "SLP.SAV", "PAT.SAV", "SIPP.SAV",
         "STRIP.SAV", "SPLIT.SAV", "HELP.SAV", "HELP.MLB", "RESORC.SAV",
         "DATIME.SAV", "UCL.SAV", "LET.SAV", "FORMAT.SAV",
         "BATCH.SAV", "QUEMAN.SAV", "QUEUE.REL", "SPOOL.REL"]
# What programs are built with.  MACRO is not here: DEC's V5.4 kit has no
# source for it, and the collection has the kits' own.
DEVEL = ["LINK.SAV", "LIBR.SAV", "ODT.OBJ", "SYSMAC.SML", "SYSLIB.OBJ"]

PLACES = {"software/system/handlers/rt11": HANDLERS,
          "software/system/utils/rt11": UTILS,
          "software/development/rt11": DEVEL}


def main() -> int:
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    collection = Path(sys.argv[1])
    builds = [Path(a) for a in sys.argv[2:]]
    found: dict[tuple[str, str], Path] = {}
    missing: list[str] = []
    for place, names in PLACES.items():
        for name in names:
            source = next((b / name for b in builds if (b / name).is_file()), None)
            if source is None:
                missing.append(name)
            else:
                found[place, name] = source
    if missing:
        print("not built:", ", ".join(missing))
        return 1
    for (place, name), source in found.items():
        target = collection / place
        target.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target / name)
        print(f"{place}/{name}  <- {source.parent.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
