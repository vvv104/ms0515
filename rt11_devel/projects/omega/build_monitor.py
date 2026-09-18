"""build_monitor.py - build RT11SJ.SYS from DEC's RT-11 V5.4 sources, a set
of SYSGEN answers and the Omega modules, the way SYSGEN.COM's MONBLD does
it, with the real MACRO and LINK inside the emulator.

    python build_monitor.py [OUTDIR] [--profile omega|dec] [PART...]

The profile picks the answers: omega - Omega's monitor exactly (SYCND.MAC),
dec - DEC's RT-11 on the MS 0515 (SYCDEC.MAC); see OMEGA.MAC.  PARTs
(BTSJ, RMSJ, KMSJ, TBSJ) assemble just those, without the LINK.

The DEC sources come from the ms0515-software collection: $MS0515_SOFTWARE,
else ../ms0515-software beside this repository (sources/rt11-v5.4).  Files
of this folder with a DEC file's name take its place (the Omega changes).
The work volume is a folder mounted as HD:, so every object, listing, map
and the monitor itself land in OUTDIR (default: a temp folder) as host
files.
"""
from __future__ import annotations

import os
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
ROM = ROOT / "package/assets/rom/ms0515-roma.rom"
SYSTEM_DIR = TOOLSET / "system"
TOOLS = TOOLSET / "build_tools"
HD_SYS = HERE.parent / "hd" / "HD.SYS"

# The monitor's four parts, as MONBLD assembles them for SJ.
PREFIX = ["SJ", "SYCND", "EDTGBL", "OMEGA"]   # OMEGA: the Omega modules' macros
PARTS = {
    "BTSJ": ["BSTRAP"],
    "RMSJ": ["USR", "RMONSJ"],
    "KMSJ": ["KMON", "KMOVLY"],
    "TBSJ": ["DEVTBL"],
}


def dec_sources() -> Path:
    base = os.environ.get("MS0515_SOFTWARE")
    root = Path(base) if base else ROOT.parent / "ms0515-software"
    src = root / "sources" / "rt11-v5.4"
    if not (src / "RMONSJ.MAC").is_file():
        raise SystemExit(f"no DEC sources in {src} (set MS0515_SOFTWARE)")
    return src


def crlf(data: bytes) -> bytes:
    return data.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")


def lf(data: bytes) -> bytes:
    return data.replace(b"\r\n", b"\n")


def patched(names: list[str], src: Path, scratch: Path) -> Path:
    """DEC's files with Omega's changes: the patches of patches/series, one
    per architectural difference, applied in order.  $OMEGA_WORK instead
    names a folder of working copies (while the patches are being made)."""
    work = os.environ.get("OMEGA_WORK")
    if work:
        return Path(work)
    for n in names:
        if (src / n).is_file():
            (scratch / n).write_bytes(lf((src / n).read_bytes()))
    series = HERE / "patches" / "series"
    if series.is_file():
        for line in series.read_text().splitlines():
            line = line.split("#", 1)[0].strip()
            if line:
                subprocess.run(["patch", "--quiet", "--forward", "-p1", "-d", str(scratch),
                                "-i", str(HERE / "patches" / line)], check=True)
    return scratch


def source(name: str, tree: Path, src: Path) -> bytes:
    """A file of Omega's own (SYCND, DEVTBL), else the patched DEC one (a
    working-copy folder need not hold the files no patch touches)."""
    for p in (HERE / name, tree / name, src / name):
        if p.is_file():
            return p.read_bytes()
    raise SystemExit(f"no {name}")


def disk(*args) -> None:
    subprocess.run([str(DISKTOOL), *map(str, args)], check=True, capture_output=True)


ANSWERS = {"omega": "SYCND.MAC", "dec": "SYCDEC.MAC"}


def stage(image: Path, files: Path, profile: str) -> None:
    """A fresh HD image holding the sources.  An image, not a folder device:
    MACRO's tentative files would stay at their full allocation there."""
    src = dec_sources()
    names = set(PREFIX)
    for p in PARTS.values():
        names.update(p)
    files.mkdir()
    scratch = files.parent / "patching"
    scratch.mkdir()
    macs = [f"{n}.MAC" for n in sorted(names)]
    tree = patched(macs, src, scratch)
    for n in macs:
        data = (HERE / ANSWERS[profile]).read_bytes() if n == "SYCND.MAC" else source(n, tree, src)
        (files / n).write_bytes(crlf(data))
    for mod in sorted(HERE.glob("OM*.MAC")):       # Omega's modules (.INCLUDEd)
        (files / mod.name).write_bytes(crlf(mod.read_bytes()))
    disk("create", image, "--hd", "--blocks", 30000)
    disk("init", image, "--hd")
    disk("put", image, "--hd", *sorted(files.iterdir()))


def boot_volume(boot: Path) -> None:
    shutil.copytree(SYSTEM_DIR, boot)
    for f in ("MACRO.SAV", "LINK.SAV", "SYSMAC.SML"):
        shutil.copy(TOOLS / f, boot / f)
    shutil.copy(HD_SYS, boot / "HD.SYS")
    (boot / "STARTS.COM").write_bytes(b"SET TT QUIET\r\nASSIGN HD DK\r\nASSIGN HD SRC\r\n")


def link(emu: EmulatorDriver, rt: RT11Session) -> str:
    """LINK/BOUNDARY asks for the boundary section; /PROMPT for more input."""
    marker = emu.buffer_len()
    try:
        emu.send("LINK/EXE:RT11SJ.SYG/BOU:1000/PROMPT/MAP:RT11SJ BTSJ\r")
        emu.wait_for(r"\*\s*$", "LINK prompt", timeout=60)
        emu.send("RMSJ,KMSJ,TBSJ//\r")
        emu.wait_for(r"[Bb]oundary\s*section\?\s*$", "boundary question", timeout=120)
        emu.send("OVLY0\r")
        emu.wait_for(r"\.\s*$", "LINK done", timeout=300)
    except Exception:
        print("LINK stuck; the screen:\n" + emu.tail(1500), flush=True)
        raise
    with emu._buf_lock:
        return emu._decode(bytes(emu._buf[marker:]))


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")   # the guest's KOI-8
    args = sys.argv[1:]
    profile = "omega"
    if "--profile" in args:
        i = args.index("--profile")
        profile = args[i + 1]
        del args[i:i + 2]
    if profile not in ANSWERS:
        raise SystemExit(f"no profile {profile!r}: {', '.join(ANSWERS)}")
    out = Path(args[0]) if args else Path(tempfile.gettempdir()) / "omega_monitor"
    for need in (CLI, ROM, HD_SYS):
        if not need.exists():
            raise SystemExit(f"missing {need}")
    tmp = Path(tempfile.mkdtemp(prefix="omega_boot_"))
    boot = tmp / "boot"
    boot_volume(boot)
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    image = out / "work.hd"
    stage(image, tmp / "src", profile)

    emu = EmulatorDriver([CLI, "--no-config", "--rom", ROM,
                          "--disk0-side0", boot / "device.rtfs",
                          "--hd", str(image)])
    emu.start()
    log = []
    try:
        rt = RT11Session(emu)
        rt.boot(timeout=90)
        pre = "+".join(PREFIX)
        only = args[1:]              # build just these parts (no LINK)
        for obj, files in PARTS.items():
            if only and obj not in only:
                continue
            t0 = time.time()
            text = rt.command(f"MACRO/OBJECT:{obj}/LIST:{obj} {pre}+{'+'.join(files)}",
                              timeout=3600, ignore_errors=True)
            log.append(text)
            (out / f"{obj}.out").write_text(text, encoding="utf-8")
            print(f"== {obj}: {time.time() - t0:.0f} s\n{text.strip()}", flush=True)
        if not only:
            log.append(link(emu, rt))
            (out / "LINK.out").write_text(log[-1], encoding="utf-8")
            print("== LINK\n" + log[-1].strip(), flush=True)
    finally:
        emu.dump(out / "session.log")
        emu.kill()
        shutil.rmtree(tmp, ignore_errors=True)
    disk("get", image, "--hd", "--out", out, "*.OBJ", "*.LST", "*.MAP", "*.SYG")
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    print("outputs in", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
