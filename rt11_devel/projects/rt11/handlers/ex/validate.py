"""OS-oracle: EX.SYS serving the electronic disk, and booting from it.

Byte for byte the kit's handler is one proof (EX=EXKIT,EX with --bitmap);
this is the proof of the build the kits never had - a sound board, all
1024 blocks - and of what the board was for:

  1. the machine installs EX (its installation check has to find the
     board), loads it, initialises EX:, copies a 49-block file onto it
     and back to a diskette, and the host reads that copy out of the
     image identical to the byte;
  2. the system is copied onto EX:, a bootstrap written there, and the
     machine booted from it - after which the floppy drive is free, SY:
     is the electronic disk, and a directory of it comes from there.

    python validate.py KIT EX.SYS [SYSTEM-FILE ...]
"""
from __future__ import annotations

import re
import shutil
import sys
import tempfile
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "dz"))
import validate as dz  # noqa: E402

bu = dz.bu


def say(emu, line: str) -> str:
    try:
        return bu.complaint(bu.step(emu, line), line)
    except TimeoutError as e:
        return str(e)


def main() -> int:
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    kit, ex = Path(sys.argv[1]), Path(sys.argv[2])
    system = [Path(a) for a in sys.argv[3:]]
    probe = kit / "DUP.SAV"
    work = Path(tempfile.mkdtemp(prefix="ex_oracle_"))
    bad = 0
    try:
        boot, _, target = dz.make_disks(
            work, kit, [kit / "PIP.SAV", probe, kit / "DIR.SAV", ex], system)
        emu = dz.session(boot, target)
        try:
            for line in ("INSTALL EX", "LOAD EX", "INIT/NOQUERY EX:",
                         f"COPY DZ0:{probe.name} EX:{probe.name}",
                         f"COPY EX:{probe.name} DZ1:BACK.SAV",
                         "COPY/SYSTEM DZ0:*.* EX:",
                         "COPY/BOOT EX:RT11SJ.SYS EX:"):
                said = say(emu, line)
                print(f"  {line}: {said or 'ok'}")
                if said:
                    bad += 1
                    bu.recover(emu)

            # From here the electronic disk is the system.
            mark = emu.buffer_len()
            emu.send("BOOT EX:\r")
            try:
                bu.wait_prompt(emu, "after BOOT EX:")
            except TimeoutError:
                pass
            for _ in range(3):
                emu.send("\r")
            bu.step(emu, "DATE")
            with emu._buf_lock:
                after = emu._decode(bytes(emu._buf[mark:]))
            up = bool(re.search(r"SJ[()A-Z]{0,3}V0?5\.", after.replace(" ", ""), re.I))
            print(f"  BOOT EX: the monitor came up again: {'ok' if up else 'NO'}")
            bad += not up
            if up:
                bu.step(emu, "DIR SY:EX.SYS")
                seen = "EX.SYS" in " ".join(emu.tail(600).split()).replace(" ", "")
                print(f"  DIR SY: answers from the electronic disk: "
                      f"{'ok' if seen else 'NO'}")
                bad += not seen
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
        print(f"  {probe.name} through EX: and back: "
              f"{'identical' if same else 'DIFFERENT or missing'}")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("PASS" if not bad else f"FAIL: {bad} check(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
