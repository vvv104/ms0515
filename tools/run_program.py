"""run_program.py - boot a disk in the headless emulator, type at it, keep what it showed.

The way every program of the disk collection was probed: ms0515-cli boots
the image with its terminal mirrored to stdio, this script waits for the
monitor prompt (answering a startup file's date/time questions on the
way), types the lines given, lets the guest settle after each, then quits
through the CLI's hotkey so the CLI writes the real 640x400 screen as a
PNG on the way out.  The terminal text of the whole session is printed
(or saved) as well - graphics programs show nothing there, hence the PNG.

    python tools/run_program.py --disk systems/omega.dsk \\
        --put MAYATN.BAS --type "R BASICO" --type "LOAD MAYATN" --type RUN \\
        --shot mayatn.png --text mayatn.txt

The image is never written: it is copied to a scratch file first, and the
`--put` files go onto the copy with ms0515-disk (onto the DV whole-disk
volume when the image is one, else onto side 0).  `--realtime` throttles
the emulator to the machine's 50 Hz for programs that time themselves.

The binaries are looked up in $MS0515_PACKAGE, else in the repository's
package/ directory.  Uses rt11_devel/toolset/emu_driver.py.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "rt11_devel" / "toolset"))
from emu_driver import EmulatorDriver  # noqa: E402

PACKAGE = Path(os.environ.get("MS0515_PACKAGE", ROOT / "package"))
CLI = PACKAGE / "ms0515-cli.exe"
DISKTOOL = PACKAGE / "ms0515-disk.exe"

ANSI = re.compile(r"\x1B\[[0-9;?]*[a-zA-Z]")
# The monitor prompt is a lone dot at the end of the screen (two when a
# second prompt stacked under the first); three dots are still the ROM's
# own "загрузка операционной системы ..." - it has not booted yet.  A
# startup file may ask for the date or time first.
BOOT_TALK = re.compile(r"(?<!\.)\.{1,2}\s*$|(?:Дата|Время|Date|Time)\s*\[[^\]]*\]\s*\?\s*$", re.I)
STARTUP_ASK = re.compile(r"(Дата|Время|Date|Time)\s*\[([^\]]*)\]\s*\?\s*$", re.I)
QUIT_HOTKEY = b"\x1d"          # Ctrl-] - the CLI's quit key; the screenshot is written on the way out


def clean(text: str) -> str:
    return ANSI.sub("", text).replace("\x00", "")


def screen_tail(emu, n: int = 16384) -> str:
    text = clean(emu.tail(n)).rstrip()
    return text[:-1].rstrip() if text.endswith("█") else text


def wait_quiet(emu, settle: float, timeout: float, until=None) -> str:
    """Wait until the cleaned tail stops changing for `settle` seconds (and
    matches `until`, when given).  The raw mirror never rests - the cursor
    blinks - so only the cleaned text counts."""
    start = time.monotonic()
    last, quiet_since = None, None
    while True:
        text = screen_tail(emu)
        if text[-200:] != last:
            last, quiet_since = text[-200:], None
        elif until is None or until.search(text):
            if quiet_since is None:
                quiet_since = time.monotonic()
            elif time.monotonic() - quiet_since >= settle:
                return text
        if time.monotonic() - start > timeout:
            return text
        time.sleep(0.2)


def is_dv(image: Path) -> bool:
    """An 800 KB image whose directory parses as one DV whole-disk volume."""
    if image.stat().st_size != 819200:
        return False
    res = subprocess.run([str(DISKTOOL), "dir", str(image), "--dv"], capture_output=True, text=True)
    return "permanent file(s)" in res.stdout


def prepare_image(image: Path, puts: list[Path], workdir: Path) -> tuple[Path, bool]:
    copy = workdir / image.name
    shutil.copyfile(image, copy)
    dv = is_dv(copy)
    if puts:
        cmd = [str(DISKTOOL), "put", str(copy)] + (["--dv"] if dv else []) + [str(p) for p in puts]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            raise SystemExit("ms0515-disk put failed:\n" + res.stdout + res.stderr)
    return copy, copy.stat().st_size == 819200


def answer_startup(emu, settle: float) -> None:
    """A startup file's questions: a format hint gets a value, a default a bare Enter."""
    for _ in range(4):
        text = wait_quiet(emu, settle, timeout=90, until=BOOT_TALK)
        ask = STARTUP_ASK.search(text)
        if not ask:
            return
        hint, is_date = ask.group(2).lower(), ask.group(1).upper()[:1] in "ДD"
        if "дд" in hint or "dd" in hint:
            reply = "01-01-99" if is_date else "00:00:00"
        elif "чч" in hint or "hh" in hint:
            reply = "00:00:00"
        else:
            reply = ""
        emu.send(reply + "\r")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--disk", type=Path, required=True, help="bootable image (400 KB side, 800 KB DV or DZ pair)")
    ap.add_argument("--put", type=Path, action="append", default=[], help="file to copy onto the (scratch) image first")
    ap.add_argument("--type", action="append", default=[], metavar="LINE", help="a line to type at the guest, in order")
    ap.add_argument("--settle", type=float, default=2.0, help="seconds of a still screen before the next line (default 2)")
    ap.add_argument("--timeout", type=float, default=60.0, help="seconds to wait for each line to settle")
    ap.add_argument("--shot", type=Path, help="PNG of the screen at the end")
    ap.add_argument("--text", type=Path, help="terminal text of the session (default: stdout)")
    ap.add_argument("--realtime", action="store_true", help="run at the machine's 50 Hz")
    ap.add_argument("--rom", type=Path, help="ROM image (Rodionov's system wants ms0515-roma.rom)")
    args = ap.parse_args()
    if not CLI.exists() or not DISKTOOL.exists():
        print("ms0515-cli / ms0515-disk not found in %s (set MS0515_PACKAGE)" % PACKAGE, file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix="ms0515-run-") as tmp:
        image, double = prepare_image(args.disk, args.put, Path(tmp))
        cmd = [str(CLI), "--no-config", "--disk0" if double else "--disk0-side0", str(image), "--frames", "6000000"]
        if args.shot:
            cmd += ["--screenshot", str(args.shot.resolve())]
        if args.realtime:
            cmd.append("--realtime")
        if args.rom:
            cmd += ["--rom", str(args.rom)]
        emu = EmulatorDriver(cmd, encoding="utf-8")
        emu.start()
        try:
            answer_startup(emu, args.settle)
            for line in args.type:
                emu.send(line + "\r")
                wait_quiet(emu, args.settle, args.timeout)
            time.sleep(1.0)
            transcript = screen_tail(emu)
            emu.send(QUIT_HOTKEY)
            try:
                emu._proc.wait(timeout=15)       # noqa: SLF001 - the driver has no public waiter
            except subprocess.TimeoutExpired:
                print("the guest ignored the quit key; no screenshot this time", file=sys.stderr)
        finally:
            emu.kill()
    if args.text:
        args.text.write_text(transcript + "\n", encoding="utf-8")
    else:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        print(transcript)
    if args.shot:
        print("screen: %s" % (args.shot if args.shot.exists() else "not written"), file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
