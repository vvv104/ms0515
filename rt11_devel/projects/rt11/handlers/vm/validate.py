"""OS-oracle: VM.SYS serving the memory disk.

Byte for byte the kits' handler is one proof; this is the other, and the
only one for a build the kits never had (ERL$G): on a dec diskette the
machine loads VM, initialises VM:, copies a file onto it and copies it
back to a diskette on the other drive - through seven banks' worth of
the ROM's bank routines both ways - and the host tool reads that copy out
of the image, which has to be the file that went in.

    python validate.py KIT VM.SYS [SYSTEM-FILE ...]

KIT is the folder of the dec kit's programs (PIP, DUP).  Each SYSTEM-FILE
replaces the file of its name on the diskette, for a set built with other
conditionals than the collection's monitor (see ../dz/validate.py).
"""
from __future__ import annotations

import shutil
import sys
import tempfile
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "dz"))
import validate as dz  # noqa: E402  (the diskette maker and the session)

bu = dz.bu


def main() -> int:
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    kit, vm = Path(sys.argv[1]), Path(sys.argv[2])
    system = [Path(a) for a in sys.argv[3:]]
    probe = kit / "DUP.SAV"                  # 49 blocks: over three banks
    work = Path(tempfile.mkdtemp(prefix="vm_oracle_"))
    bad = 0
    try:
        # dz's maker puts DZ.SYS of its own folder on; VM and PIP ride along.
        boot, _, target = dz.make_disks(work, kit, [kit / "PIP.SAV", probe, vm], system)
        emu = dz.session(boot, target)
        try:
            for line in ("INSTALL VM", "LOAD VM", "INIT/NOQUERY VM:",
                         f"COPY DZ0:{probe.name} VM:{probe.name}",
                         f"COPY VM:{probe.name} DZ1:BACK.SAV"):
                try:
                    said = bu.complaint(bu.step(emu, line), line)
                except TimeoutError as e:
                    said = str(e)
                print(f"  {line}: {said or 'ok'}")
                if said:
                    bad += 1
                    bu.recover(emu)
        finally:
            emu.kill()
        back = work / "back"
        back.mkdir()
        dz.disk("get", target, "--out", back, "BACK.SAV")
        a = probe.read_bytes()
        got = back / "BACK.SAV"
        b = got.read_bytes() if got.is_file() else b""
        same = bool(b) and a == b[:len(a)] and not any(b[len(a):])
        bad += not same
        print(f"  {probe.name} through VM: and back: "
              f"{'identical' if same else 'DIFFERENT or missing'}")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("PASS" if not bad else f"FAIL: {bad} check(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
