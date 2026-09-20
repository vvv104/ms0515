"""build_util.py - build utilities of DEC's RT-11 V5.4 from their own
sources, the way their own command files do it, with the real MACRO and
LINK inside the emulator.

Several utilities share one machine and one work volume: their sources are
all staged first, then each command file is typed in turn.  One that fails
is reported and left behind - the next utility is another program - so a
kit build says at the end which of them came out and which did not.

    python build_util.py [--sy FILE[,FILE...]] [--with FILE[,FILE...]]
                         UTIL [UTIL...] [OUTDIR]

--sy puts host files on the system volume over the ones the toolset
brings, which is how a tool built here takes over from the kit's: build
LIBR, then `--sy .../LIBR.SAV` and the next build uses it.  --with puts
them on the work volume instead, beside the sources - for what a command
file reads as OBJ: or SRC: and this kit has only as something built
earlier.

UTIL is the name of a build command file of the DEC kit (DUMP, DIR, ...);
the sources and the command file come from the software collection
($MS0515_SOFTWARE, else ../ms0515-software beside this repository), in
sources/rt11-v5.4.  The command file is followed line for line: its
`R PROG` runs the program, the lines after it are its CSI input, `^C`
ends it.  The devices it names are all the work volume, an HD image, so
every object, listing, map and .SAV lands in OUTDIR as a host file.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOLSET = HERE.parent.parent / "toolset"
ROOT = TOOLSET.parent.parent
sys.path.insert(0, str(TOOLSET))
from emu_driver import EmulatorDriver  # noqa: E402
from rt11 import RT11Session  # noqa: E402

CLI = ROOT / "package/ms0515-cli.exe"
DISKTOOL = ROOT / "package/ms0515-disk.exe"
ROM = ROOT / "package/assets/rom/ms0515-romb.rom"   # the toolset system: ROM-B
SYSTEM_DIR = TOOLSET / "system"
TOOLS = TOOLSET / "build_tools"
HD_SYS = HERE.parent / "hd" / "HD.SYS"
# DEC's own, built here from its sources.  Only MACRO comes from the kit:
# the V5.4 source distribution has no source for it.  This matters more
# than it looks - the toolset's SYSLIB is not DEC's, and a utility linked
# against it can come out broken (PIP, whose two overlay regions then fail
# at run time with ?MON-F-Overlay error).
DEC_TOOLS = HERE / "tools"
CTRL_C = "\x03"


def dec_sources() -> Path:
    base = os.environ.get("MS0515_SOFTWARE")
    root = Path(base) if base else ROOT.parent / "ms0515-software"
    src = root / "sources" / "rt11-v5.4"
    if not (src / "DUMP.COM").is_file():
        raise SystemExit(f"no DEC sources in {src} (set MS0515_SOFTWARE)")
    return src


def crlf(data: bytes) -> bytes:
    return data.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")


def disk(*args) -> None:
    subprocess.run([str(DISKTOOL), *map(str, args)], check=True, capture_output=True)


def recipe(com: Path) -> list[str]:
    """The command file's lines to type: its comments dropped, its ^C kept
    as the character itself.  Empty lines are typed as they stand - LINK
    ends a list of library modules with one, so dropping them would leave
    the program waiting for the rest of a list that never comes."""
    out: list[str] = []
    for line in com.read_bytes().decode("latin-1").splitlines():
        line = line.rstrip()
        if line.startswith("!"):
            continue
        # A command file meant for IND marks the lines it hands to the
        # monitor with a dollar; the monitor itself takes them without it.
        if line.startswith("$"):
            line = line[1:]
        # A cross-reference - /C on a CSI listing, /CROSSREFERENCE on a
        # command - is made by running CREF.SAV, a program of the kit we do
        # not have, and goes into a listing nobody reads here.
        # SYSLIB's two FORTRAN modules ask for threaded code, which is a
        # choice of code generation this machine's FORTRAN does not offer
        # ("?FORTRAN-F-Illegal value for /I switch").  The routines are the
        # same either way.
        line = re.sub(r"/I:THR", "", line)
        line = re.sub(r"/CRO[A-Z]*", "", line)
        line = re.sub(r"/C(?![:A-Z0-9])", "", line)
        out.append(CTRL_C if line == "^C" else shorten(line))
    return out


def shorten(line: str) -> str:
    """A command KMON reads is 80 characters; past that it takes what fits
    and refuses the rest as an invalid command.  IND's link line is 90.
    The names it spells out are all logical devices this build assigns to
    the one work volume, so dropping them says exactly the same thing in
    fewer characters."""
    if len(line) <= 80:
        return line
    # Only where a file specification starts - after the colon of a switch
    # or a separator.  `/MAP:MAP:IND` names the switch and then the device,
    # and taking the switch for a device leaves `/IND`, an invalid option.
    return re.sub(r"(?<=[:,= ])(?:DK|SRC|OBJ|BIN|LST|MAP):", "", line)


def sources_of(lines: list[str], src: Path) -> list[str]:
    """The files a recipe reads.  A CSI line is `outputs=inputs`, and the
    inputs are a comma-separated list where only the first may name its
    device (`SRC:DUPPRE,DUPSCN,DUPMRG`) and the last may end the command
    (`,ULBLIB//`).  A name is taken from the kit as its source, else as the
    object library it is; a word that names no file of the kit is not one
    of its files and is passed over, which is what keeps a command line's
    own switches out of the staging."""
    names: list[str] = []
    for line in lines:
        # A CSI line is `outputs=inputs`; a command line carries its files
        # in its switches (`LINK/LINK:OBJ:ULBLIB`), so all of it is read.
        rest = line.split("=", 1)[1] if "=" in line else (
            line if not re.match(r"^(R |\^C)", line) else "")
        for word in re.split(r"[,\s/]+", rest.replace("//", "")):
            # What is left of a word once its switch and device names are
            # off it: `/LINK:OBJ:ULBLIB` is the library ULBLIB.
            word = word.strip().rsplit(":", 1)[-1]
            if not re.fullmatch(r"[A-Z0-9$]+(\.[A-Z0-9]+)?", word or ""):
                continue
            stem = word.split(".")[0]
            for ext in (word[len(stem):], ".MAC", ".FOR", ".OBJ"):
                if ext and (src / (stem + ext)).is_file() \
                        and stem + ext not in names:
                    names.append(stem + ext)
                    break
    return names


def included_by(names: list[str], src: Path) -> list[str]:
    """The sources a source reads for itself.  A command file names only
    what it assembles; what that in turn pulls in with .INCLUDE or takes
    macros from with .LIBRARY has to be on the volume too, and may pull in
    more, so this follows the trail to its end."""
    out = list(names)
    seen = 0
    while seen < len(out):
        name = out[seen]
        seen += 1
        f = src / name
        if not f.is_file() or f.suffix.upper() != ".MAC":
            continue
        text = f.read_bytes().decode("latin-1")
        for m in re.finditer(r'\.(?:INCLUDE|LIBRARY)\s+"([^"]+)"', text, re.I):
            word = re.sub(r"^[A-Z]{2,3}:", "", m.group(1).strip().upper())
            if (src / word).is_file() and word not in out:
                out.append(word)
    return out


def stage(image: Path, files: Path, names: list[str], src: Path,
          extra: list[Path] | None = None,
          given: list[Path] | None = None) -> None:
    files.mkdir(parents=True)
    for n in names:
        shutil.copy(src / n, files / n)
    for f in given or []:                  # over the kit's, if it has one
        shutil.copy(f, files / f.name.upper())
    # The command files run the toolchain both ways: `R MACRO` takes it
    # from the system volume, `RUN LIBR` from DK: - which here is the work
    # volume.  So the programs go on it as well, the ones built here over
    # the kit's, as they were on the disk DEC built from.
    for f in ("MACRO.SAV", "LINK.SAV", "LIBR.SAV"):
        over = next((e for e in extra or [] if e.name.upper() == f), None)
        source = over or (TOOLS / f if f == "MACRO.SAV" else DEC_TOOLS / f)
        if source.is_file():
            shutil.copy(source, files / f)
    # A kit's worth of sources, objects, listings and maps at once: the
    # volume is as big as RT-11 lets a device be, and its directory has
    # segments enough for all of them (71 entries to a segment).
    disk("create", image, "--hd", "--blocks", 64000)
    disk("init", image, "--hd", "--segments", 24)
    disk("put", image, "--hd", *sorted(files.iterdir()))


def boot_volume(boot: Path, extra: list[Path] | None = None) -> None:
    shutil.copytree(SYSTEM_DIR, boot)
    # SYSLIB.OBJ too: LINK takes the system library from SY: by default.
    shutil.copy(TOOLS / "MACRO.SAV", boot / "MACRO.SAV")
    for f in ("LINK.SAV", "LIBR.SAV", "SYSMAC.SML", "SYSLIB.OBJ"):
        shutil.copy(DEC_TOOLS / f, boot / f)
    shutil.copy(HD_SYS, boot / "HD.SYS")
    for f in extra or []:
        shutil.copy(f, boot / f.name.upper())
    # HD: is every device the command files name.
    (boot / "STARTS.COM").write_bytes(crlf(b"""SET TT QUIET
INSTALL HD
LOAD HD
ASSIGN HD DK
ASSIGN HD SRC
ASSIGN HD OBJ
ASSIGN HD BIN
ASSIGN HD LST
ASSIGN HD MAP
"""))


PROMPT = r"(?:[.*]|\?)\s*$"


def step(emu, line: str) -> str:
    """Type one line of a recipe and wait for whatever prompts next; the
    text that came back is the answer to read for an error.

    The monitor prompts with the dot, a utility run with R with the CSI's
    asterisk (System Utilities Manual, "The Command-String Interpreter
    (CSI) Language"), and a utility can ask a question instead - LINK's /D
    ends the command line with "Duplicate symbol?" and reads names until an
    empty line.  These are 40-year old programs on a fast host: seconds,
    not minutes."""
    at_dot = line == CTRL_C
    label = "^C" if at_dot else repr(line)
    mark = emu.buffer_len()
    emu.send(line + ("" if at_dot else "\r"))
    wait_prompt(emu, f"after {label}")
    with emu._buf_lock:
        return emu._decode(bytes(emu._buf[mark:]))


def wait_prompt(emu, label: str) -> None:
    """Wait for a prompt by watching the machine, not the clock.

    A big assembly can take a minute and a small one a second, so a fixed
    timeout is either too short for the one or a minute wasted on the
    other.  What tells the two apart is the screen: while the machine is
    working it keeps writing, and a step that has gone quiet without
    prompting is a step that is waiting for something that will not come.
    DECUTIL_QUIET is how long that silence may last, DECUTIL_CAP the most
    any one step may take."""
    quiet = float(os.environ.get("DECUTIL_QUIET", 20))
    cap = float(os.environ.get("DECUTIL_CAP", 600))
    start = last_change = time.monotonic()
    seen = emu.buffer_len()
    while True:
        try:
            emu.wait_for(PROMPT, label, timeout=1.0)
            return
        except TimeoutError:
            pass
        now = time.monotonic()
        if emu.buffer_len() != seen:
            seen, last_change = emu.buffer_len(), now
        if now - last_change > quiet:
            raise TimeoutError(f"{label}: quiet for {quiet:.0f}s with no prompt")
        if now - start > cap:
            raise TimeoutError(f"{label}: still going after {cap:.0f}s")


def recover(emu) -> None:
    """Back to the monitor after a step went wrong, so the next utility
    starts from the same place the first one did, and with the screen
    scrolled clean: the terminal mirrors the machine's screen, so an error
    left standing on it would be read again as the next utility's."""
    for _ in range(3):
        try:
            if step(emu, CTRL_C).rstrip().endswith("."):
                break
        except TimeoutError:
            pass
    for _ in range(26):
        try:
            step(emu, "")
        except TimeoutError:
            return


def complaint(text: str, label: str) -> str:
    """The fatal error a step ran into, or the empty string.

    The terminal is a mirror of the machine's screen, so a scroll reprints
    lines that had long gone by and an old error comes back with them.
    Only what follows the echo of the line just typed is this step's."""
    squeezed = text.replace(" ", "")
    echo = squeezed.rfind(label.replace(" ", ""))
    fatal = re.search(r"\?[A-Z]{2,6}-[FU]-[^.\r\n]*",
                      squeezed[echo + 1 if echo >= 0 else 0:])
    return fatal.group(0) if fatal else ""


def run_job(emu, name: str, lines: list[str], verbose: bool) -> str:
    """Build one utility; the empty string when it came out, else what went
    wrong.  A failure never stops the kit - the next utility is another
    program and mostly another set of sources."""
    for line in lines:
        label = "^C" if line == CTRL_C else line
        try:
            text = step(emu, line)
        except TimeoutError as e:
            print(f"   {name}: stuck after {label!r}; the screen:\n"
                  f"{emu.tail(1200)}", flush=True)
            recover(emu)
            return str(e)
        if verbose:
            print(f"== {label}\n{text.strip()}", flush=True)
        fatal = complaint(text, label)
        if fatal:
            recover(emu)
            return fatal
    return ""


def jobs_of(utils: list[str], src: Path) -> tuple[list[tuple[str, list[str]]],
                                                  list[str]]:
    """Every utility's recipe, and the one set of sources they all read."""
    jobs: list[tuple[str, list[str]]] = []
    names: list[str] = []
    for u in utils:
        com = src / f"{u}.COM"
        if not com.is_file():
            raise SystemExit(f"no {com}")
        r = recipe(com)
        jobs.append((u, r))
        for n in sources_of(r, src):
            if n not in names:
                names.append(n)
    return jobs, names


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    args = [a for a in sys.argv[1:]]
    extra: list[Path] = []
    given: list[Path] = []
    while args and args[0] in ("--sy", "--with"):
        which = args.pop(0)
        target = extra if which == "--sy" else given
        target += [Path(f) for f in args.pop(0).split(",")]
    if not args:
        raise SystemExit(__doc__)
    out = Path(args.pop()) if len(args) > 1 and not (dec_sources() / (args[-1] + ".COM")).is_file() \
        else Path(tempfile.gettempdir()) / "dec_utils"
    verbose = bool(os.environ.get("DECUTIL_VERBOSE"))
    src = dec_sources()
    jobs, names = jobs_of([a.upper() for a in args], src)
    names = included_by(names, src)

    tmp = Path(tempfile.mkdtemp(prefix="dec_util_"))
    boot = tmp / "boot"
    boot_volume(boot, extra)
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    image = out / "work.hd"
    stage(image, tmp / "src", names, src, extra, given)

    emu = EmulatorDriver([CLI, "--no-config", "--rom", ROM,
                          "--disk0-side0", boot / "device.rtfs", "--hd", str(image)])
    emu.start()
    failed: dict[str, str] = {}
    try:
        rt = RT11Session(emu)
        rt.boot(timeout=90)
        for name, lines in jobs:
            started = time.time()
            why = run_job(emu, name, lines, verbose)
            took = time.time() - started
            print(f"{name:8s} {'ok' if not why else why}  ({took:.0f}s)", flush=True)
            if why:
                failed[name] = why
    finally:
        emu.dump(out / "session.log")
        emu.kill()
        shutil.rmtree(tmp, ignore_errors=True)
    # Not everything a utility builds is a .SAV: a foreground program is a
    # .REL, a handler a .SYS, HELP's text a .MLB.
    disk("get", image, "--hd", "--out", out,
         "*.SAV", "*.REL", "*.SYS", "*.MLB", "*.MAP", "*.OBJ")
    print("outputs in", out)
    if failed:
        print("failed:", ", ".join(sorted(failed)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
