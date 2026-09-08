#!/usr/bin/env python3
"""Open a bug report saved by the browser build (src/web/www/bugreport.js).

The page packs the machine's snapshot, the ROM, the picture and every
mounted image into one .zip.  This unpacks it, says what is inside, and -
with --place - puts the images where the snapshot looks for them, so the
emulator's File / Load State finds the disks the report was taken with.

    python tools/bugreport.py report.zip                 # what is in it
    python tools/bugreport.py report.zip -o work/bug1    # unpacked there
    python tools/bugreport.py report.zip -o work/bug1 --place

A snapshot names its images by the path they had in the browser's own file
system ("/disks/osa.dsk"), which on Windows is that path on the current
drive.  --place copies them there (--root puts them elsewhere: a snapshot
opened with the disks absent simply loads with empty drives).
"""

import argparse
import json
import shutil
import sys
import zipfile
from pathlib import Path


def summary(report, names):
    """The report's own words, as a few lines."""
    emu, page, mach = report["emulator"], report["page"], report["machine"]
    out = [
        f"saved   {report['saved']}",
        f"note    {report.get('note') or '-'}",
        f"emulator v{emu['version']}, ROM {emu['rom'].upper()}",
        f"machine  frames {mach['frames']}, speed {mach['speedPct']}%, "
        f"{'HALTED' if mach.get('halted') else ('running' if mach.get('running') else 'stopped')}"
        f", reg C {mach.get('regC')}",
        f"status   {mach.get('status')}",
        f"page     {page['url']}",
        f"         {page['userAgent']}",
    ]
    for slot in report["mounts"]["fd"]:
        if slot:
            out.append(f"DZ{slot['unit']}:     {slot['name']} -> {slot['path']} "
                       f"({slot['size']} bytes, CRC-32 {slot['crc32']:#010x})")
    hd = report["mounts"]["hd"]
    if hd:
        out.append(f"HD:      {hd['name']} -> {hd['path']} ({hd['size']} bytes)")
    out.append("files    " + ", ".join(names))
    return "\n".join(out)


def images(report):
    """The mounted images: (the file in the archive, the path the snapshot names)."""
    slots = [s for s in report["mounts"]["fd"] if s] + ([report["mounts"]["hd"]] if report["mounts"]["hd"] else [])
    seen, out = set(), []
    for slot in slots:
        if slot["file"] and slot["file"] not in seen:
            seen.add(slot["file"])
            out.append((slot["file"], slot["path"]))
    return out


def place(unpacked, report, root):
    """The images where the snapshot looks for them."""
    for name, path in images(report):
        target = Path(root) / path.lstrip("/")
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(unpacked / name, target)
        print(f"placed  {target}")


def main():
    ap = argparse.ArgumentParser(description="open a bug report from the browser build")
    ap.add_argument("zip", help="the report")
    ap.add_argument("-o", "--out", help="unpack into this directory (default: the report's name beside it)")
    ap.add_argument("--place", action="store_true",
                    help="copy the images to the paths the snapshot names")
    ap.add_argument("--root", default=Path.cwd().anchor or "/",
                    help="where those paths start (default: the current drive's root)")
    args = ap.parse_args()

    src = Path(args.zip)
    out = Path(args.out) if args.out else src.with_suffix("")
    with zipfile.ZipFile(src) as z:
        bad = z.testzip()
        if bad:
            sys.exit(f"{src}: {bad} is corrupt")
        names = z.namelist()
        if "report.json" not in names:
            sys.exit(f"{src}: no report.json - is this a bug report?")
        z.extractall(out)
    report = json.loads((out / "report.json").read_text(encoding="utf-8"))
    if report.get("kind") != "ms0515-bug-report":
        sys.exit(f"{src}: not an MS-0515 bug report")

    print(summary(report, names))
    print(f"\nunpacked into {out}")
    if args.place:
        place(out, report, args.root)
    state = report["files"].get("state")
    if state:
        print(f"\nthe machine as it was: {out / state}"
              f"\n  ms0515.exe: File / Load State (the same ROM - {report['emulator']['rom'].upper()})"
              f"\n  python tools/dump_state.py \"{out / state}\" reads it without the emulator")


if __name__ == "__main__":
    main()
