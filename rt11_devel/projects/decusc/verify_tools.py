"""The tools run: a few of each variant on the machine, the T11 build
without the instruction emulator and the EIS build with it, on a text of
known counts.  Prints what each said.

    python verify_tools.py [noeis] [eis]
"""
from __future__ import annotations

import os
import re
import shutil
import sys
import tempfile
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "rt11" / "kit"))
sys.path.insert(0, str(HERE.parent.parent / "toolset"))
import build_util as bu  # noqa: E402
import decsys  # noqa: E402

TEXT = b"the quick brown fox\r\njumps over the lazy dog\r\n\r\nfox again\r\n"
# (program, its command line, what its output must contain)
TRIALS = [("ECHO", "one two three", "one two three"),
          ("WC", "TEXT.TXT", "4"),                       # 4 lines
          ("GREP", "fox TEXT.TXT", "fox again"),
          ("UNIQ", "TEXT.TXT", "lazy dog"),
          ("DETAB", "TEXT.TXT", "quick brown"),
          ("BANNER", "MS", "*"),
          ("CALEND", "", "19")]
EM = {"noeis": [], "eis": ["SET EM SYSGEN", "SET EM ON"]}
PROMPT = re.compile(r"(Argv:|\.)\s*$")


def main(argv: list[str]) -> int:
    variants = [v for v in argv[1:] if v in EM] or list(EM)
    base = Path(os.environ.get("MS0515_DECUSC_BUILD", tempfile.gettempdir())) / "decusc_tools"
    bad = 0
    for variant in variants:
        work = base / f"verify-{variant}"
        shutil.rmtree(work, ignore_errors=True)
        work.mkdir(parents=True)
        (work / "TEXT.TXT").write_bytes(TEXT)
        (work / "device.rtfs").write_bytes(b"device: floppy\nblocks: 800\n")
        for name, _, _ in TRIALS:
            if (base / variant / f"{name}.SAV").exists():
                shutil.copy(base / variant / f"{name}.SAV", work / f"{name}.SAV")
        boot = decsys.compose(base / f"boot-{variant}", add=["em"] if variant == "eis" else [],
                              startup=["ASSIGN DZ1 DK", *EM[variant]], quiet=False)
        emu = bu.EmulatorDriver([bu.CLI, "--no-config", "--disk0-side0", boot / decsys.DESCRIPTOR,
                                 "--disk1-side0", work / "device.rtfs"])
        emu.start()
        try:
            bu.RT11Session(emu).boot(timeout=120)
            for name, args, expect in TRIALS:
                if not (work / f"{name}.SAV").exists():
                    print(f"{variant} {name}: not built")
                    bad += 1
                    continue
                for line in (f"RUN {name}", args):
                    mark = emu.buffer_len()
                    emu.send(line + "\r")
                    try:
                        emu.wait_for(PROMPT, line, timeout=60)
                    except TimeoutError:
                        emu.send("\x03\x03")
                    with emu._buf_lock:
                        text = " ".join(emu._decode(bytes(emu._buf[mark:])).split())
                ok = expect.replace(" ", "") in text.replace(" ", "")
                bad += not ok
                print(f"{variant} {name}: {'ok' if ok else 'WRONG'}  {text[-120:]}", flush=True)
        finally:
            emu.dump(work / "session.log")
            emu.kill()
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
