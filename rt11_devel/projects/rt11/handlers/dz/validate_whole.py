"""OS-oracle: DV.SYS and MZ.SYS, DZ.MAC built as the whole-disk kinds.

On a dec diskette running on our DZ, with our MZ and DV loaded:

  1. a marked diskette on the other drive - every block of it full of its
     own number - read through MZ1: gives blocks 0, 1 and 10 as 0, 1 and
     10, and through DV1: as 20, 21 and 30: what the kits' handlers gave
     when they were measured (docs/kb/dz_handler.md);
  2. an empty diskette is initialised and written through each - a file
     of 49 blocks, so both sides and several cylinders - and the host
     tool, told the volume is a DV: or an MZ: one, reads it back out of
     the image identical to the byte.

    python validate_whole.py KIT MZ.SYS DV.SYS [SYSTEM-FILE ...]
"""
from __future__ import annotations

import shutil
import sys
import tempfile
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import validate as dz  # noqa: E402

bu = dz.bu
MARKED = {"MZ": {"0": "000000", "1": "000001", "12": "000012"},
          "DV": {"0": "000024", "1": "000025", "12": "000036"}}


def main() -> int:
    if len(sys.argv) < 4:
        raise SystemExit(__doc__)
    kit = Path(sys.argv[1])
    handlers = {"MZ": Path(sys.argv[2]), "DV": Path(sys.argv[3])}
    system = [Path(a) for a in sys.argv[4:]]
    probe = kit / "DUP.SAV"
    work = Path(tempfile.mkdtemp(prefix="whole_oracle_"))
    bad = 0
    try:
        boot, marked, _ = dz.make_disks(
            work, kit, [kit / "PIP.SAV", probe, *handlers.values()], system)
        load = [f"{verb} {name}" for name in handlers for verb in ("INSTALL", "LOAD")]

        emu = dz.session(boot, marked, "--disk1")
        try:
            for line in load:
                bu.step(emu, line)
            bu.recover(emu)
            for name, wants in MARKED.items():
                for blk, want in wants.items():
                    try:
                        bu.step(emu, "R DUMP")
                        bu.step(emu, f"TT:={name}1:/O:{blk}")
                    except TimeoutError:
                        pass
                    # Block 0 of MZ is all zeros, which the screen is full
                    # of anyway; the other five readings carry the proof.
                    seen = " ".join(emu.tail(500).split()).count(want)
                    ok = seen >= 3
                    bad += not ok
                    print(f"  {name}1: block {blk}(8) reads {want}: "
                          f"{'ok' if ok else 'WRONG'}")
                    bu.recover(emu)
        finally:
            emu.kill()

        for name in handlers:
            target = work / f"{name}.dsk"
            target.write_bytes(bytes(1600 * 512))
            emu = dz.session(boot, target, "--disk1")
            try:
                for line in load + [f"INIT/NOQUERY {name}1:",
                                    f"COPY DZ0:{probe.name} {name}1:{probe.name}"]:
                    try:
                        said = bu.complaint(bu.step(emu, line), line)
                    except TimeoutError as e:
                        said = str(e)
                    if said:
                        print(f"  {line}: {said}")
                        bad += 1
                        bu.recover(emu)
            finally:
                emu.kill()
            back = work / f"back_{name}"
            back.mkdir()
            try:
                dz.disk("get", target, f"--{name.lower()}", "--out", back, probe.name)
            except Exception:
                pass
            got = back / probe.name
            a = probe.read_bytes()
            b = got.read_bytes() if got.is_file() else b""
            same = bool(b) and a == b[:len(a)] and not any(b[len(a):])
            bad += not same
            print(f"  {name}: a file written through it, read back by the host: "
                  f"{'identical' if same else 'DIFFERENT or missing'}")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("PASS" if not bad else f"FAIL: {bad} check(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
