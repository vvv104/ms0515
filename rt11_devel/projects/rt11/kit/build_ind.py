"""build_ind.py - run DEC's own IND command files on the machine, and keep
what they build.

    python build_ind.py [--sy FILE[,FILE...]] [--with FILE[,FILE...]]
                        COMFILE [COMFILE...] [OUTDIR]

Some of DEC's build files are not lists of commands but programs for IND -
K52.COM, KED.COM and the variants, which pull in ALLDEV.COM with its
variables, conditions and questions.  build_util.py follows a command file
line for line and has no IND in it; this one gives the machine IND itself
(the dec disk's; another one goes on the system volume with --sy), sets
`SET KMON IND` so that `@file` reaches it, types `@K52`,
and waits for the run to end rather than for a prompt: IND's own `$` lines
bring the monitor's dot back between steps, so the end is a dot the screen
has stayed on for a while.

The machine is DEC's own: the `dec` disk the collection's preset
composes - its monitor, its utilities, IND, the tools - with the builder's
startup file over the preset's - and under ROM-A, since under ROM-B the
monitor's cursor blink through the ROM wrecks IND's stack (below).

The whole source kit goes on the work volume: what an IND file reads is
decided as it runs, and the volume is big enough for all of it.  Every
logical device the files name - Src:, Obj:, Bin:, Lst:, Map: - is the work
volume, as in build_util.py.  What comes out is taken back the same way:
.SAV, .REL, .SYS, .MLB, .MAP, .OBJ and the .LOG a build writes for itself.
"""
from __future__ import annotations

import os
import shutil
import sys
import tempfile
import time
from pathlib import Path

import build_util as bu

# The dec disk boots under ROM-A.  Under ROM-B the monitor blinks the
# cursor through the ROM every sixteenth tick, and ROM-B's blink pushes on
# the interrupted program's stack with the VRAM window over 040000..077777
# - where IND keeps its stack.  The push lands in the video memory and
# the return goes astray (the project's README, "IND and ROM-B").
ROM_A = bu.ROOT / "package/assets/rom/ms0515-roma.rom"

STARTS = b"""SET TT NOQUIET
SET KMON IND
INSTALL HD
LOAD HD
ASSIGN HD DK
ASSIGN HD SRC
ASSIGN HD OBJ
ASSIGN HD BIN
ASSIGN HD LST
ASSIGN HD MAP
"""


def dec_boot_disk(image: Path, extra: list[Path]) -> None:
    """The dec preset's disk, with this builder's startup file: the echo on,
    for the run's record is the commands IND hands the monitor; @file to IND;
    HD: every device the build files name.  --sy files go over the preset's."""
    bu.disk("compose", "--repo", bu.collection(), "--preset", "dec", image)
    starts = image.parent / "STARTS.COM"
    starts.write_bytes(bu.crlf(STARTS))
    bu.disk("put", image, "--dv", starts, *extra)


def wait_done(emu, label: str, live: Path) -> list[str]:
    """The run is over when the screen ends in the monitor's dot and has not
    changed for DECUTIL_DONE seconds; DECUTIL_CAP bounds the whole run.

    A screen that has gone quiet on anything else is a question.  DEC's
    files ask theirs with a default and a timeout (`.AskS [::"ALL LNK":10.s]`)
    and go on by themselves; one that stands past that gets Enter, which
    takes the default too.  The questions answered are returned, for the
    run's record.

    Every five seconds `live` gets the screen's tail and how long it has
    stood still, so that a run can be looked at from outside while it
    goes: `cat live.txt`."""
    settle = float(os.environ.get("DECUTIL_DONE", 150))    # above DECUTIL_QUIET: a big module assembles in silence
    cap = float(os.environ.get("DECUTIL_CAP", 3600))
    start = last_change = time.monotonic()
    seen = emu.buffer_len()
    answered: list[str] = []
    tick = 0
    while True:
        time.sleep(1.0)
        now = time.monotonic()
        tick += 1
        if tick % 5 == 0:
            live.write_text(f"{label}: {now - start:.0f}s in, screen still for "
                            f"{now - last_change:.0f}s ({len(answered)} answered)\n"
                            f"{'-' * 60}\n{emu.tail(3000)}",
                            encoding="utf-8", newline="\n")
        if emu.buffer_len() != seen:
            seen, last_change = emu.buffer_len(), now
        elif now - last_change > settle:
            line = emu.tail(400).rstrip().rsplit("\n", 1)[-1].strip()
            if line.endswith("."):
                return answered
            answered.append(line)
            print(f"   answered with Enter: {line}", flush=True)
            emu.send("\r")
            last_change = now
        if now - start > cap:
            raise TimeoutError(f"{label}: still going after {cap:.0f}s")


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    args = list(sys.argv[1:])
    extra: list[Path] = []
    given: list[Path] = []
    while args and args[0] in ("--sy", "--with"):
        which = args.pop(0)
        target = extra if which == "--sy" else given
        target += [Path(f) for f in args.pop(0).split(",")]
    if not args:
        raise SystemExit(__doc__)
    src = bu.dec_sources()
    def is_com(a: str) -> bool:   # a name in DEC's kit, or a file of one's own
        return (src / (a.upper() + ".COM")).is_file() or a.upper().endswith(".COM") and Path(a).is_file()
    out = Path(args.pop()) if len(args) > 1 and not is_com(args[-1]) \
        else Path(tempfile.gettempdir()) / "dec_ind"
    coms: list[str] = []
    for a in args:
        if not is_com(a):
            raise SystemExit(f"no {src / (a.upper() + '.COM')}")
        if not (src / (a.upper() + ".COM")).is_file():
            given.append(Path(a))
        coms.append(Path(a).stem.upper())

    names = sorted(p.name for p in src.iterdir() if p.is_file())
    tmp = Path(tempfile.mkdtemp(prefix="dec_ind_"))
    boot = tmp / "boot.dsk"
    dec_boot_disk(boot, extra)
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    image = out / "work.hd"
    bu.stage(image, tmp / "src", names, src, extra, given)

    emu = bu.EmulatorDriver([bu.CLI, "--no-config", "--rom", ROM_A,
                             "--disk0", str(boot), "--hd", str(image)])
    emu.start()
    failed: dict[str, str] = {}
    try:
        rt = bu.RT11Session(emu)
        rt.boot(timeout=90)
        for c in coms:
            started = time.time()
            mark = emu.buffer_len()
            emu.send(f"@{c}\r")
            answered: list[str] = []
            try:
                answered = wait_done(emu, f"@{c}", out / "live.txt")
                why = ""
            except TimeoutError as e:
                why = str(e)
                bu.recover(emu)
            with emu._buf_lock:
                text = emu._decode(bytes(emu._buf[mark:]))
            head = "".join(f"[answered with Enter: {q}]\n" for q in answered)
            (out / f"{c}.run.txt").write_text(head + text, encoding="utf-8", newline="\n")
            print(f"@{c:8s} {'done' if not why else why}  ({time.time() - started:.0f}s)",
                  flush=True)
            if why:
                failed[c] = why
    finally:
        emu.dump(out / "session.log")
        emu.kill()
        shutil.rmtree(tmp, ignore_errors=True)
    bu.disk("get", image, "--hd", "--out", out,
            "*.SAV", "*.REL", "*.SYS", "*.MLB", "*.MAP", "*.OBJ", "*.LOG")
    print("outputs in", out)
    if failed:
        print("failed:", ", ".join(sorted(failed)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
