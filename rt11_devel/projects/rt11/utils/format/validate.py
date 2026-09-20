"""OS-oracle: FORMAT with the MS 0515's module formats a diskette.

A diskette full of rubbish goes into the second drive and the machine is
told to FORMAT it.  The host then reads the image: every sector of what
was to be formatted has to be the formatter's filler, and nothing else
may have been touched.  After that the machine initialises the volume,
copies a file onto it, and the host reads that file back identical.

  DZ1:  one side - a single-sided image, all of it
  DZ3:  the other side of a double-sided image, side 0 left as it was
  DV1:, MZ1:  a double-sided image, both sides - for each of DV.SYS and
        MZ.SYS that is given

    python validate.py KIT FORMAT.SAV [DV.SYS] [MZ.SYS]

KIT is a directory of utilities built by ../../kit/build_util.py (PIP,
DUP, DIR, DUMP).
"""
from __future__ import annotations

import shutil
import sys
import tempfile
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent.parent / "handlers" / "dz"))
import validate as dz  # noqa: E402

bu = dz.bu

SIDE = 80 * 10 * 512
TRACK = 10 * 512
FILLER = 0xE5
RUBBISH = 0xAA


def say(emu, line: str) -> str:
    try:
        return bu.complaint(bu.step(emu, line), line)
    except TimeoutError as e:
        return str(e)


def sides_of(image: bytes) -> list[bytes]:
    """The sides of an image: a double-sided one is interleaved by track."""
    if len(image) == SIDE:
        return [image]
    return [b"".join(image[t * 2 * TRACK + s * TRACK:][:TRACK] for t in range(80))
            for s in (0, 1)]


def formatted(side: bytes) -> bool:
    return side == bytes([FILLER]) * SIDE


def run(boot: Path, target: Path, how: str, device: str, probe: Path,
        expect: list[bool]) -> int:
    """FORMAT `device`, check which sides changed, then use the volume."""
    bad = 0
    emu = dz.session(boot, target, how)
    try:
        if not device.startswith("DZ"):
            for line in (f"INSTALL {device[:2]}", f"LOAD {device[:2]}"):
                bad += bool(say(emu, line))
        said = say(emu, f"FORMAT {device}")
        said = said or say(emu, "Y")
        print(f"  FORMAT {device}: {said or 'ok'}")
        bad += bool(said)
        after = sides_of(target.read_bytes())
        for n, (side, want) in enumerate(zip(after, expect)):
            ok = formatted(side) if want else side == bytes([RUBBISH]) * SIDE
            print(f"  side {n}: {'formatted' if want else 'left alone'}: "
                  f"{'ok' if ok else 'NO'}")
            bad += not ok
        for line in (f"INIT/NOQUERY {device}",
                     f"COPY DZ0:{probe.name} {device}BACK.SAV"):
            said = say(emu, line)
            print(f"  {line}: {said or 'ok'}")
            bad += bool(said)
    finally:
        emu.kill()
    return bad


def main() -> int:
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    kit, fmt = Path(sys.argv[1]), Path(sys.argv[2])
    whole = [Path(a) for a in sys.argv[3:]]
    probe = kit / "DUP.SAV"
    work = Path(tempfile.mkdtemp(prefix="format_oracle_"))
    bad = 0
    try:
        files = [kit / "PIP.SAV", probe, fmt, *whole]
        boot, _, target = dz.make_disks(work, kit, files, [])
        cases = [("--disk1-side0", "DZ1:", 1, [True]),
                 ("--disk1", "DZ3:", 2, [False, True])]
        for handler in whole:
            cases.append(("--disk1", f"{handler.stem.upper()}1:", 2,
                          [True, True]))
        for how, device, nsides, expect in cases:
            print(f"{device}")
            target.write_bytes(bytes([RUBBISH]) * SIDE * nsides)
            bad += run(boot, target, how, device, probe, expect)
            if device == "DZ1:":                # the file, read by the host
                back = work / "back"
                back.mkdir(exist_ok=True)
                dz.disk("get", target, "--out", back, "BACK.SAV")
                got = back / "BACK.SAV"
                a = probe.read_bytes()
                b = got.read_bytes() if got.is_file() else b""
                same = bool(b) and a == b[:len(a)] and not any(b[len(a):])
                print(f"  {probe.name} on the new volume, read by the host: "
                      f"{'identical' if same else 'DIFFERENT or missing'}")
                bad += not same
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("PASS" if not bad else f"FAIL: {bad} check(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
