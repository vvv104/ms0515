"""build_handler.py - build device handlers of DEC's RT-11 V5.4 from their
own sources, with the real MACRO and LINK inside the emulator.

    python build_handler.py [--sy FILE[,FILE...]] [--answers FILE]
                            [--source DIR] DD [DD...] [OUTDIR]

--sy puts host files on the system volume over the ones the toolset
brings, the way build_util.py takes it: a handler belongs with the rest of
the kit, so it is assembled and linked with DEC's own tools and libraries.

--source names a folder looked in for <DD>.MAC before DEC's kit, which is
how this machine's own handlers are built with the same recipe and the
same tools as DEC's: `--source ../dz DZ`.  It comes first on purpose -
DEC's kit has a DZ.MAC too, and it is a terminal multiplexer.

DD is a handler's two-letter name (NL, LD, SL, ...) whose source is in the
software collection's sources/rt11-v5.4.  A handler has no command file of
its own - SYSGEN built them - so the recipe is the standard one: assemble
the source behind the system conditional file, then link it with no bitmap
into <DD>.SYS.  --answers names that conditional file; the default is the
dec profile's, so the handlers match the monitor they will run under.

The handlers that touch this machine's hardware (DZ, HD, VM, TT) are not
DEC's to build: DEC's DZ is a terminal multiplexer, not the MS 0515's
floppy controller.  What is worth taking from DEC is what needs no
hardware of its own - the null device, the logical disk, the single line
editor, the spooler, the batch and error-logging handlers.
"""
from __future__ import annotations

import os
import shutil
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from build_util import (CLI, CTRL_C, EmulatorDriver, ROM, RT11Session,  # noqa: E402
                        boot_volume, dec_sources, disk, run_job)

ANSWERS = HERE.parent / "omega" / "SYCDEC.MAC"


def recipe(dd: str, answers: str) -> list[str]:
    """What builds one handler: MACRO over the conditional file and the
    source, then LINK with no bitmap - a handler is not a program and must
    not carry one - into the .SYS file the monitor loads."""
    return [
        "R MACRO",
        f"OBJ:{dd},LST:{dd}={answers},{dd}",
        CTRL_C,
        f"LINK/NOBITMAP/EXECUTE:{dd}.SYS {dd}",
    ]


def stage(image: Path, files: Path, names: list[Path]) -> None:
    files.mkdir(parents=True)
    for n in names:
        shutil.copy(n, files / n.name.upper())
    disk("create", image, "--hd", "--blocks", 8000)
    disk("init", image, "--hd", "--segments", 8)
    disk("put", image, "--hd", *sorted(files.iterdir()))


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    args = list(sys.argv[1:])
    answers = ANSWERS
    extra: list[Path] = []
    own: list[Path] = []
    while args and args[0] in ("--answers", "--sy", "--source"):
        which = args.pop(0)
        if which == "--answers":
            answers = Path(args.pop(0))
        elif which == "--source":
            own.append(Path(args.pop(0)))
        else:
            extra += [Path(f) for f in args.pop(0).split(",")]
    if not args:
        raise SystemExit(__doc__)
    src = dec_sources()
    out = Path(args.pop()) if len(args) > 1 and len(args[-1]) > 3 \
        else Path(tempfile.gettempdir()) / "dec_handlers"
    handlers = [a.upper() for a in args]

    def source_of(dd: str) -> Path | None:
        for folder in own + [src]:
            if (folder / f"{dd}.MAC").is_file():
                return folder / f"{dd}.MAC"
        return None

    missing = [d for d in handlers if source_of(d) is None]
    if missing:
        raise SystemExit("no source for " + ", ".join(missing))

    tmp = Path(tempfile.mkdtemp(prefix="dec_hand_"))
    boot = tmp / "boot"
    boot_volume(boot, extra)
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    image = out / "work.hd"
    stage(image, tmp / "src",
          [answers] + [source_of(d) for d in handlers])

    emu = EmulatorDriver([CLI, "--no-config", "--rom", ROM,
                          "--disk0-side0", boot / "device.rtfs", "--hd", str(image)])
    emu.start()
    failed: dict[str, str] = {}
    try:
        rt = RT11Session(emu)
        rt.boot(timeout=90)
        for dd in handlers:
            started = time.time()
            why = run_job(emu, dd, recipe(dd, answers.stem.upper()),
                          bool(os.environ.get("DECUTIL_VERBOSE")))
            print(f"{dd:4s} {'ok' if not why else why}  "
                  f"({time.time() - started:.0f}s)", flush=True)
            if why:
                failed[dd] = why
    finally:
        emu.dump(out / "session.log")
        emu.kill()
        shutil.rmtree(tmp, ignore_errors=True)
    disk("get", image, "--hd", "--out", out, "*.SYS", "*.OBJ")
    print("handlers in", out)
    if failed:
        print("failed:", ", ".join(sorted(failed)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
