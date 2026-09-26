"""The DECUS C tools of the SIG tape (601, the "software tools", and the
programs of 602 the RT-11 kit shipped) built with the CC, AS and library
build.py made, twice: against CLIB as the compiler makes code for the T11,
and with /E against CLIBE - inline EIS for a machine that emulates it
(EM.SYS).  601's programs come with TTOOL.COM, a command file BUILD wrote
(RUN C:CC, RUN C:AS, R LINK, per program); 602's have none and are one
file each, so their lines are made here the same way.

    python build_tools.py [noeis] [eis] [--keep] [--only=NAME,NAME]
"""
from __future__ import annotations

import os
import re
import shutil
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "rt11" / "kit"))
sys.path.insert(0, str(HERE.parent.parent / "toolset"))
import build_util as bu  # noqa: E402
import decsys  # noqa: E402
import importlib.util
_spec = importlib.util.spec_from_file_location("decusc_build", HERE / "build.py")   # not the toolset's build.py
_build = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_build)
sources = _build.sources

TOOL_DIRS = ("601A", "601B", "602A", "602B", "602C")
# 602's programs of the RT-11 kit (MISC/DECUSC), one source file each
SINGLES = ["BANNER", "CALEND", "CRYPT", "DUAL", "E", "GRAB", "HACK", "KALEID", "NC", "PHBOOK", "PTR", "SH"]
STARTS = ["INSTALL HD", "LOAD HD", "ASSIGN HD DK", "ASSIGN HD SRC", "ASSIGN HD C", "SET EM SYSGEN", "SET EM ON"]
VARIANT = {"noeis": ("", "", ""), "eis": ("E", "", "E")}     # library suffix, CC switch, SAV suffix: CCE makes EIS by default


def tool_lines(src: Path, sfx: str, sw: str) -> list[tuple[str, list[str]]]:
    """(program, lines) for every program: TTOOL.COM's, then the singles.
    A variant's compile lines get the switch, its LINK lines the library."""
    sup, lib = ("SUPRT" + sfx, "CLIB" + sfx) if sfx else ("SUPORT", "CLIB")
    jobs: list[tuple[str, list[str]]] = []
    name, lines = "", []
    for raw in (src / "601B" / "TTOOL.COM").read_bytes().decode("latin-1").splitlines():
        line = raw.rstrip().replace("RUN C:CC", "RUN C:CC" + sfx)
        m = re.match(r"! \*\* Compile (\S+)", line)
        if m:
            if name:
                jobs.append((name, lines))
            name, lines = m.group(1).upper(), []
            continue
        if not line or line.startswith("!"):
            continue
        if re.match(r"(?i)(COPY src:|SET ERROR)", line):
            continue        # the headers are on DK: already, SRC: being the same volume
        if re.match(r"(?i)src:\S+\.C$", line):
            line += sw
        line = line.replace("C:SUPORT", "C:" + sup).replace("C:CLIB", "C:" + lib)
        lines.append(bu.ABORT if line == "^C" else line)
    if name:
        jobs.append((name, lines))
    for n in SINGLES:
        jobs.append((n, ["RUN C:CC" + sfx, f"src:{n}.C{sw}", "RUN C:AS", f"{n}.S/d", "R LINK",
                         f"{n}={n}.OBJ,C:{sup},C:{lib}/B:2000", bu.ABORT, f"DELETE/NOQ {n}.OBJ"]))
    return jobs


def main(argv: list[str]) -> int:
    variants = [v for v in argv[1:] if v in VARIANT] or list(VARIANT)
    only = [a.split("=", 1)[1].upper().split(",") for a in argv[1:] if a.startswith("--only=")]
    only = only[0] if only else None                # --only=ARCH,DIFF: those programs alone
    src = sources()
    build = Path(os.environ.get("MS0515_DECUSC_BUILD", tempfile.gettempdir()))
    made = build / "decusc_build" / "out"          # what build.py made
    work = build / "decusc_tools"
    if "--keep" not in argv:
        shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True, exist_ok=True)
    image = work / "work.hd"
    if "--keep" not in argv:
        stage = work / "stage"
        stage.mkdir()
        for d in TOOL_DIRS:
            for f in (src / d).iterdir():
                if f.suffix.upper() in (".C", ".H") and f.is_file():
                    shutil.copy(f, stage / f.name.upper())
        # the headers: the tape's 501C has most, the RT-11 kit on it (MISC/DECUSC,
        # a folder beside the tape's) the rest, CTYPE.H among them
        for d in ("501C", "DECUSC"):
            for f in (src / d).glob("*.H"):
                shutil.copy(f, stage / f.name.upper())
        for n in ("CC.SAV", "AS.SAV", "CCE.SAV", "CLIB.OBJ", "SUPORT.OBJ", "DTOA.OBJ", "ATOF.OBJ",
                  "CLIBE.OBJ", "SUPRTE.OBJ", "DTOAE.OBJ", "ATOFE.OBJ"):
            shutil.copy(made / n, stage / n)
        files = sorted(stage.iterdir())
        bu.disk("create", image, "--hd", "--blocks", 12000)
        bu.disk("init", image, "--hd", "--segments", 16)
        for i in range(0, len(files), 100):
            bu.disk("put", image, "--hd", *files[i:i + 100])
        print(f"{len(files)} files staged", flush=True)
        decsys.compose(work / "boot", add=["em"], startup=STARTS)
    bu.PROMPT = r"(?:CC>|AS>|[.*]|\?)\s*$"      # CC and AS prompt with their names
    os.environ.setdefault("DECUTIL_QUIET", "600")
    os.environ.setdefault("DECUTIL_CAP", "1200")
    report: dict[str, dict[str, list]] = {}
    for variant in variants:
        sfx, sw, out_sfx = VARIANT[variant]
        emu = bu.EmulatorDriver([bu.CLI, "--no-config", "--disk0-side0", work / "boot" / decsys.DESCRIPTOR,
                                 "--hd", str(image)])
        emu.start()
        report[variant] = {}
        t0 = time.time()
        try:
            bu.RT11Session(emu).boot(timeout=120)
            jobs = [j for j in tool_lines(src, sfx, sw) if only is None or j[0] in only]
            for n, (name, lines) in enumerate(jobs):
                bad = []
                for line in lines:
                    what = "^C^C" if line == bu.ABORT else line
                    try:
                        err = bu.complaint(bu.step(emu, line), what)
                    except TimeoutError as e:
                        err = str(e)
                        bu.recover(emu)
                    if err:
                        bad.append((what, err))
                report[variant][name] = bad
                print(f"{variant} {n + 1}/{len(jobs)} {name}: {'ok' if not bad else bad[0]}  {time.time() - t0:.0f}s", flush=True)
            # the variant's programs off the volume under their own names
            out = work / variant
            out.mkdir(exist_ok=True)
            names = [name for name, _ in jobs]
            for name in names:
                bu.step(emu, f"RENAME {name}.SAV {name}.SV{out_sfx or 'N'}")
        finally:
            emu.dump(work / f"session-{variant}.log")
            emu.kill()
        bu.disk("get", image, "--hd", "--out", out, *[f"{name}.SV{out_sfx or 'N'}" for name in names])
        for p in list(out.iterdir()):
            if p.suffix.upper() != ".SAV":
                p.replace(out / (p.stem + ".SAV"))     # over an earlier build's
        print(f"{variant}: {len(list(out.iterdir()))} programs in {out}", flush=True)
    failed = {v: {n: b for n, b in r.items() if b} for v, r in report.items()}
    for v, f in failed.items():
        print(f"{v}: {'all clean' if not f else 'FAILED ' + ', '.join(f'{n} ({b[0][1][:50]})' for n, b in f.items())}")
    return 1 if any(failed.values()) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
