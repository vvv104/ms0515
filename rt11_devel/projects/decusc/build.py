"""DECUS C for the MS 0515, built from the SIG tape's sources on the dec
system with the machine's own MACRO, LINK and LIBR - by DEC's command
files of the tape, typed at the machine, twice over: once with RT11.MAC
(C$$EIS = 0: the T11 has no EIS) and once with RT11.EIS, for a machine
that emulates the instructions (EM.SYS).

  TMAKCC.COM   TCOMLB.OBJ (the compiler's own library) and CC.SAV
  TMAKAS.COM   AS.SAV, the assembler for what CC writes
  TCLBAS.COM + TILBAS.COM, then TMAKLB's tail   CLIB, SUPORT, DTOA, ATOF
  TCLEIS.COM + TILEIS.COM, the same tail        the EIS library

The EIS build's files carry an E: CCE, TCOMLE, CLIBE, SUPRTE, DTOAE, ATOFE
(AS comes out the same either way: the assembler has no EIS in it, and CC
differs in one byte, the default of its -E toggle).  Then HELLO.C compiled
by each CC and run against its library.
The tape is $MS0515_DECUSC_SOURCES (the folder holding 501C, 503A, 503B,
504, 505), else sources/decus-c of the software collection.

    python build.py [stage ...]
    stages: cc as clib cc-e clib-e check
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

TAPE_DIRS = ("501C", "503A", "503B", "504", "505")
STAGES = ("cc", "as", "clib", "cc-e", "clib-e", "check")
STARTS = ["INSTALL HD", "LOAD HD", "ASSIGN HD DK", "ASSIGN HD SR", "ASSIGN HD OB", "ASSIGN HD OU",
          "ASSIGN HD MP", "ASSIGN NL LS", "ASSIGN HD C", "SET EM SYSGEN", "SET EM ON"]
HELLO = (b'#include <stdio.h>\r\n\r\nmain()\r\n{\r\n\tint i;\r\n\tlong f;\r\n'
         b'\tprintf("HELLO FROM DECUS C ON THE MS 0515\\n");\r\n'
         b'\tfor (i = 1; i <= 5; i++)\r\n\t\tprintf("%d SQUARED IS %d, DIV %d\\n", i, i * i, 1000 / i);\r\n'
         b'\tf = 1;\r\n\tfor (i = 1; i <= 12; i++)\r\n\t\tf *= i;\r\n\tprintf("12! = %ld\\n", f);\r\n}\r\n')
EXPECT = ["HELLO FROM DECUS C ON THE MS 0515", "5 SQUARED IS 25, DIV 200", "12! = 479001600"]
# the outputs of an EIS build, named apart from the other's - in the DEL
# lines too, or the EIS build's TMAKCC deletes the other build's compiler
EIS_NAMES = {"OU:CC,MP:CC=": "OU:CCE,MP:CCE=", "OU:AS,MP:AS=": "OU:ASE,MP:ASE=",
             "OU:CC.SAV": "OU:CCE.SAV", "MP:CC.MAP": "MP:CCE.MAP", "OU:AS.SAV": "OU:ASE.SAV",
             "MP:AS.MAP": "MP:ASE.MAP", "TCOMLB": "TCOMLE", "SR:RT11,": "SR:RT11.EIS,"}


def sources() -> Path:
    direct = os.environ.get("MS0515_DECUSC_SOURCES")
    src = Path(direct) if direct else decsys.collection() / "sources" / "decus-c"
    missing = [d for d in TAPE_DIRS if not (src / d).is_dir()]
    if missing:
        raise SystemExit(f"no DECUS C tape in {src}: {' '.join(missing)} missing "
                         "(set MS0515_DECUSC_SOURCES to the 11SP59 folder)")
    return src


def lines_of(com: Path, src: Path, eis: bool = False) -> list[str]:
    """The command file as the lines to type, with what the machine here
    has instead of the tape's: the logical names are assigned at boot, the
    listings go to NL:, DATE and TIME say nothing useful, an @file is that
    file's lines in place.  /C is CREF on a MACRO line (no CREF here) and
    "continue" on a LINK line, where it stays.  An EIS build reads RT11.EIS
    and names its outputs apart."""
    out: list[str] = []
    program = ""
    for raw in com.read_bytes().decode("latin-1").splitlines():
        line = raw.rstrip()
        if not line or line.startswith("!"):
            continue
        word = line.split()[0].upper()
        if word in ("ASSIGN", "DATE", "TIME"):
            continue
        if line.startswith("@"):
            name = re.sub(r"^@(SR:)?", "", line).split(".")[0].upper()
            out += lines_of(src / "503B" / (name + ".COM"), src, eis)
            continue
        if line == "^C":
            out.append(bu.ABORT)
            program = ""
            continue
        if word == "R" and len(line.split()) > 1:
            program = line.split()[1].upper()
        elif program == "MACRO":
            line = re.sub(r"/C(?![:A-Z0-9])", "", line)
        if eis:
            for old, new in EIS_NAMES.items():
                line = line.replace(old, new)
        out.append(line)
    return out


def flat(src: Path, stage: Path) -> list[Path]:
    """Every source of the tape's folders in one place: a later folder's
    file over an earlier one's (505's RT11.MAC is the library's)."""
    stage.mkdir(parents=True, exist_ok=True)
    for d in TAPE_DIRS:
        for f in sorted((src / d).iterdir()):
            if f.suffix.upper() in (".MAC", ".EIS", ".COM", ".H") and f.is_file():
                shutil.copy(f, stage / f.name.upper())
    (stage / "HELLO.C").write_bytes(HELLO)
    return sorted(stage.iterdir())


def run_lines(emu, label: str, lines: list[str]) -> list[tuple[str, str]]:
    bad: list[tuple[str, str]] = []
    t0 = time.time()
    for n, line in enumerate(lines):
        what = "^C^C" if line == bu.ABORT else line
        try:
            text = bu.step(emu, line)
        except TimeoutError as e:
            bad.append((what, str(e)))
            print(f"   {what}: {e}", flush=True)
            bu.recover(emu)
            continue
        err = bu.complaint(text, what)
        if err and what.split()[0].upper() in ("DEL", "DELETE"):
            # a file not there to delete is no failure; and MACRO says
            # nothing on success, so the screen would show this message
            # after every module that follows: scroll it away
            bu.recover(emu)
        elif err:
            bad.append((what, err))
            print(f"   {what}: {err}", flush=True)
        if n % 10 == 0:
            print(f"   {label} {n}/{len(lines)} {time.time() - t0:.0f}s", flush=True)
    print(f"{label}: {len(lines)} lines, {time.time() - t0:.0f}s, {len(bad)} complaints", flush=True)
    return bad


def library_tail(suffix: str) -> list[str]:
    """TMAKLB.COM's tail: SUPORT, DTOA and ATOF kept apart, the rest into
    CLIB by LIBR."""
    sup = "SUPRT" + suffix if suffix else "SUPORT"
    return [f"COPY/NOLOG OB:SUPORT.OB OU:{sup}.OBJ", f"COPY/NOLOG OB:DTOA.OB OU:DTOA{suffix}.OBJ",
            f"COPY/NOLOG OB:ATOF.OB OU:ATOF{suffix}.OBJ", "DELETE/NOQ OB:SUPORT.OB,OB:DTOA.OB,OB:ATOF.OB",
            "COPY/CONCAT/NOLOG OB:*.OB OB:OTS.TMP", "DELETE/NOQ OB:*.OB",
            "R LIBR", f"OU:CLIB{suffix}=OB:OTS.TMP", bu.ABORT, "DELETE/NOQ OB:OTS.TMP"]


def check(emu, suffix: str) -> str:
    """HELLO by the CC and AS of a build, against its library, run; what
    the screen showed after.  CC and AS prompt with their names, which the
    kit's prompt pattern does not know, and the C program asks Argv:."""
    sup = "SUPRT" + suffix if suffix else "SUPORT"
    steps = [(f"RUN DK:CC{suffix}", r"CC>\s*$"), ("HELLO", r"\.\s*$"), ("RUN DK:AS", r"AS>\s*$"),
             ("HELLO/D", r"\.\s*$"), ("R LINK", r"\*\s*$"),
             (f"HELLO{suffix}=HELLO,C:{sup},C:CLIB{suffix}/B:2000", r"\*\s*$"), (bu.ABORT, r"\.\s*$"),
             (f"RUN HELLO{suffix}", r"Argv:\s*$"), ("", r"12!.*\.\s*$|\?[A-Z]+-[FU]-.*\.\s*$")]
    text = ""
    for line, prompt in steps:
        mark = emu.buffer_len()
        emu.send(line + ("" if line == bu.ABORT else "\r"))
        try:
            emu.wait_for(prompt, line, timeout=300)
        except TimeoutError:
            pass
        time.sleep(0.5)
        with emu._buf_lock:
            text = emu._decode(bytes(emu._buf[mark:]))
    return " ".join(text.split())


def stage_lines(stage: str, src: Path) -> list[str]:
    eis = stage.endswith("-e")
    base = stage[:-2] if eis else stage
    if base == "cc":
        return lines_of(src / "503B" / "TMAKCC.COM", src, eis)     # TCOMLB and TCCBLD inside
    if base == "as":
        return lines_of(src / "503B" / "TMAKAS.COM", src, eis)
    if base == "clib":
        c, i = ("TCLEIS", "TILEIS") if eis else ("TCLBAS", "TILBAS")
        return (lines_of(src / "504" / (c + ".COM"), src) + lines_of(src / "505" / (i + ".COM"), src)
                + library_tail("E" if eis else ""))
    raise ValueError(stage)


def main(argv: list[str]) -> int:
    stages = [s for s in argv[1:] if s in STAGES] or list(STAGES)
    src = sources()
    build = Path(os.environ.get("MS0515_DECUSC_BUILD", tempfile.gettempdir())) / "decusc_build"
    keep = "--keep" in argv
    if not keep:
        shutil.rmtree(build, ignore_errors=True)
    image = build / "work.hd"
    if not keep:
        files = flat(src, build / "stage")
        bu.disk("create", image, "--hd", "--blocks", 12000)
        bu.disk("init", image, "--hd", "--segments", 16)
        for i in range(0, len(files), 100):
            bu.disk("put", image, "--hd", *files[i:i + 100])
        print(f"{len(files)} files staged on the work volume", flush=True)
        decsys.compose(build / "boot", add=["nl-rt11", "em"], startup=STARTS)
    boot = build / "boot"
    os.environ.setdefault("DECUTIL_QUIET", "900")     # a big module assembles in silence
    os.environ.setdefault("DECUTIL_CAP", "1800")
    emu = bu.EmulatorDriver([bu.CLI, "--no-config", "--disk0-side0", boot / decsys.DESCRIPTOR, "--hd", str(image)])
    emu.start()
    report: dict[str, list] = {}
    try:
        bu.RT11Session(emu).boot(timeout=120)
        for stage in stages:
            if stage == "check":
                for suffix in ("", "E"):
                    shown = check(emu, suffix)
                    # the mirrored screen loses blanks at its line ends
                    ok = all(e.replace(" ", "") in shown.replace(" ", "") for e in EXPECT)
                    report["check" + suffix] = [] if ok else [("RUN HELLO" + suffix, shown[-200:])]
                    print(f"HELLO by CC{suffix} with CLIB{suffix}: {'ok' if ok else 'WRONG: ' + shown[-160:]}", flush=True)
            else:
                report[stage] = run_lines(emu, stage, stage_lines(stage, src))
    finally:
        emu.dump(build / "session.log")
        emu.kill()
    out = build / "out"
    out.mkdir(exist_ok=True)
    bu.disk("get", image, "--hd", "--out", out, "TCOMLB.OBJ", "CC.SAV", "CC.MAP", "AS.SAV", "AS.MAP",
            "TCOMLE.OBJ", "CCE.SAV", "CCE.MAP",
            "CLIB.OBJ", "SUPORT.OBJ", "DTOA.OBJ", "ATOF.OBJ", "CLIBE.OBJ", "SUPRTE.OBJ", "DTOAE.OBJ", "ATOFE.OBJ",
            "HELLO.SAV", "HELLOE.SAV")
    print("out:", " ".join(f"{p.name}={p.stat().st_size}" for p in sorted(out.iterdir())), flush=True)
    failed = {k: v for k, v in report.items() if v}
    print("FAILED:" if failed else "all stages clean", *(f"{k}: {v[:3]}" for k, v in failed.items()), sep="\n")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
