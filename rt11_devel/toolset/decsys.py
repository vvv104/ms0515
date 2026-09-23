"""decsys.py - the build system of every builder here: a `dec` disk composed
from the software collection, mounted as a folder.

DEC's RT-11 V5.4 as built for the machine (rt11_devel/projects/rt11) is the
system every build runs on - its monitor, its DZ/DV/HD handlers, DIR, PIP,
DUP, and DEC's own LINK, LIBR, SYSLIB, SYSMAC and ODT.  Only MACRO is not
DEC's: the V5.4 source kit has no source for it, and the collection's is
the FODOS kit's (macro-vvv).  The disk is composed by ms0515-disk from the
collection ($MS0515_SOFTWARE, else ../ms0515-software beside this
repository), so what a build stands on is what the collection ships, and
nothing is kept here twice.

    boot = decsys.compose(folder, startup=["ASSIGN DZ1 DK", ...],
                          add=["pascal"], over=[Path("X.SAV")])
    ... "--disk0-side0", boot / "device.rtfs" ...

`add` names more bundles of the collection (the Pascal or FORTRAN
toolchain, IND), `over` puts host files on the system over the composed
ones.  The system is a FOLDER device (docs/folder-device.md), a
single-sided diskette of 800 blocks that boots as DZ0: an image answers
at a diskette's pace - the seeks and the turns of the emulated drive -
and a build reads SYSMAC and the libraries a thousand times, so on an
image a kit takes the best part of an hour; the folder answers at once.
The boot blocks are read off an image of the same system (LBN 0, the
DZ handler's primary driver, and the monitor's blocks 1..4 as COPY/BOOT
writes them).  The CLI's default ROM boots it.
"""
from __future__ import annotations

import fnmatch
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
CLI = ROOT / "package" / "ms0515-cli.exe"
DISKTOOL = ROOT / "package" / "ms0515-disk.exe"

# What every build needs: the system's handlers for the drives it mounts,
# the three utilities of a working disk, and DEC's tools with the MACRO.
BASE = ["dz-rt11", "hd-rt11", "dir-rt11", "dup-rt11", "pip-rt11",
        "link-rt11", "libr-rt11", "sysmac-rt11", "odt-rt11", "macro-vvv"]
# The system library LINK takes from SY:.  DEC's is the one for DEC's
# programs; the Pascal kit's is what PAS1's output links against, and
# the pascal and fortran bundles bring it themselves (their `requires`).
# One volume holds one SYSLIB.OBJ, so it is DEC's unless a toolchain that
# needs the kit's is added.
DEC_SYSLIB = "syslib-rt11"
KIT_SYSLIB_TOOLCHAINS = {"pascal", "fortran"}

# A folder floppy: 800 blocks, less the home block and the directory.
FOLDER_BLOCKS = 800
DIR_BLOCKS = 1 + 2 * 4
DESCRIPTOR = "device.rtfs"
BOOT = "boot.bin"
OWN = {DESCRIPTOR.upper(), BOOT.upper()}

# The single-sided image's geometry (src/disk/src/Layout.cpp): where a
# logical block lies.  LBN 0 and 2..5 are the boot blocks.
_IL = [0, 2, 4, 6, 8, 1, 3, 5, 7, 9]


def _lbn_offset(lbn: int) -> int:
    track = (lbn // 10 + 1) % 80
    sector = (_IL[lbn % 10] + 2 * track - 2) % 10
    return track * 5120 + sector * 512


def collection() -> Path:
    """The software collection: $MS0515_SOFTWARE, else ../ms0515-software
    beside this repository."""
    base = os.environ.get("MS0515_SOFTWARE")
    root = Path(base) if base else ROOT.parent / "ms0515-software"
    if not (root / "disks.toml").is_file():
        raise SystemExit(f"no software collection at {root} (set MS0515_SOFTWARE)")
    return root


def disk(*args: object) -> str:
    """One ms0515-disk command; its output."""
    r = subprocess.run([str(DISKTOOL), *map(str, args)], check=True,
                       capture_output=True, encoding="utf-8", errors="replace")
    return r.stdout


def crlf(text: str) -> bytes:
    return "".join(line + "\r\n" for line in text.splitlines()).encode("ascii")


def _compose_image(image: Path, media: str, bundles: list[str]) -> None:
    disk("compose", "--repo", collection(), "--system", "dec", "--media", media,
         "--add", ",".join(bundles), image)


def _listing(image: Path, dv: bool) -> list[tuple[str, str, bool]]:
    """(name, date, protected) of the files on an image, in block order."""
    out = disk("dir", image, *(["--dv"] if dv else []))
    rows = re.findall(r"^\s+([A-Z0-9$]+\.[A-Z0-9$]{0,3})\s+blk=.*?date=\s*([0-9-]+|-)\s*(\[P\])?", out, re.M)
    return [(n, d, bool(p)) for n, d, p in rows]


def compose(folder: Path, *, startup: list[str], add: list[str] = (),
            over: list[Path] = (), quiet: bool = True) -> Path:
    """The dec system in `folder`: BASE and `add`, the startup file made of
    `startup` (SET TT QUIET ahead of it unless `quiet` is off), `over` put on
    top.  Returns `folder`; mount `folder / "device.rtfs"` as DZ0."""
    bundles = [*BASE, *add]
    if not KIT_SYSLIB_TOOLCHAINS.intersection(add):
        bundles.append(DEC_SYSLIB)
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True)
    tmp = Path(tempfile.mkdtemp(prefix="decsys_"))
    try:
        # The files, off a DV image (room for any toolchain); the boot
        # blocks, off a single-sided one of the base system: LBN 0 is the
        # DZ handler's primary driver with the monitor's, LBN 2..5 the
        # monitor's blocks 1..4 as COPY/BOOT patches them.
        full = tmp / "full.dsk"
        _compose_image(full, "dv", bundles)
        disk("get", full, "--dv", "--out", folder, "*.*")
        ss = tmp / "ss.dsk"
        _compose_image(ss, "ss", BASE)
        raw = ss.read_bytes()
        (folder / BOOT).write_bytes(b"".join(raw[_lbn_offset(n):_lbn_offset(n) + 512]
                                             for n in (0, 2, 3, 4, 5)))
        files = _listing(full, dv=True)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    for f in folder.iterdir():                     # the tool's names as RT-11 spells them
        if f.name != f.name.upper():
            f.rename(folder / f.name.upper())
    lines = (["SET TT QUIET"] if quiet else []) + list(startup)
    (folder / "STARTS.COM").write_bytes(crlf("\n".join(lines)))
    dates = {n: (d, p) for n, d, p in files}
    order = [n for n, _, _ in files if n != "STARTS.COM"]
    for f in over:
        name = f.name.upper()
        shutil.copyfile(f, folder / name)
        if name not in dates:
            order.append(name)
    order.append("STARTS.COM")
    _describe(folder, order, dates)
    return folder


def _describe(folder: Path, order: list[str], dates: dict[str, tuple[str, bool]]) -> None:
    """The descriptor: one line per file, in the image's block order, and
    a check that the diskette holds them all."""
    blocks = 0
    lines = ["# MS0515 folder-backed block device", "device: floppy",
             f"blocks: {FOLDER_BLOCKS}", f"boot: {BOOT}"]
    for name in order:
        size = (folder / name).stat().st_size
        blocks += (size + 511) // 512
        date, protected = dates.get(name, ("-", False))
        attrs = (f" date={date}" if date != "-" else "") + (" protected" if protected else "")
        lines.append(f"file: {name} | {name} |{attrs}")
    if blocks > FOLDER_BLOCKS - DIR_BLOCKS:
        raise SystemExit(f"the system does not fit a diskette: {blocks} blocks in {folder}")
    (folder / DESCRIPTOR).write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")


def names(folder: Path) -> set[str]:
    """The files of the system folder, by name."""
    return {f.name.upper() for f in folder.iterdir() if f.is_file()} - OWN


def take(folder: Path, out: Path, *patterns: str, but: set[str] = frozenset()) -> list[Path]:
    """Files matching `patterns` out of the system folder into `out`, the
    names in `but` left alone (the system's own).  The guest spells its
    new files in lower case; they come out upper.  Returns what was taken."""
    out.mkdir(parents=True, exist_ok=True)
    taken = []
    for f in folder.iterdir():
        name = f.name.upper()
        if name in OWN or name in but or not f.is_file():
            continue
        if any(fnmatch.fnmatch(name, p.upper()) for p in patterns):
            shutil.copyfile(f, out / name)
            taken.append(out / name)
    return taken
