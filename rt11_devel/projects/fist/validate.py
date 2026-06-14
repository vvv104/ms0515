"""Runtime smoke test for FIST.SAV (the OS oracle, no pixels).

Boots RT-11 from a temp copy of the toolset's system/ folder template with
the freshly built FIST.SAV dropped in, runs it, injects a keypress, and
confirms a clean return to the monitor dot prompt (no trap, no -F- error).

This proves the program installs the video mode, runs SPSCR, polls the
keyboard and restores state cleanly.  Pixel-correctness of the 1:1 present
is checked separately by the C++ VRAM oracle.

    python rt11_devel/projects/fist/validate.py
"""
import re
import shutil
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "rt11_devel/toolset"))

from emu_driver import EmulatorDriver          # noqa: E402
from rt11 import RT11Session, DOT_PROMPT        # noqa: E402

CLI    = ROOT / "package/ms0515-cli.exe"
ROM    = ROOT / "package/assets/rom/ms0515-roma.rom"
SYSTEM = ROOT / "rt11_devel/toolset/system"
SAV    = HERE / "FIST.SAV"


def main() -> int:
    if not SAV.exists():
        print(f"missing {SAV}; build it first (build.py build.toml)", file=sys.stderr)
        return 1

    boot = Path(tempfile.gettempdir()) / "fist_validate"
    shutil.rmtree(boot, ignore_errors=True)
    shutil.copytree(SYSTEM, boot)
    shutil.copy(SAV, boot / "FIST.SAV")
    (boot / "STARTS.COM").write_bytes(b"SET TT QUIET\r\n")

    emu = EmulatorDriver([str(CLI), "--no-config", "--rom", str(ROM),
                          "--disk0-side0", str(boot / "device.rtfs")])
    emu.start()
    try:
        rt = RT11Session(emu)
        rt.boot()
        marker = emu.buffer_len()
        emu.send("RUN FIST\r")
        # Let SPSCR paint the static screen; once VRAM is quiet the CLI
        # bridge permits keystroke injection, so the key below reaches the
        # emulated keyboard FIST polls at 177440.
        time.sleep(4.0)
        for _ in range(3):
            emu.send(" ")
            time.sleep(0.3)
        emu.wait_for(DOT_PROMPT, "return to monitor after FIST", timeout=30)
        with emu._buf_lock:
            log = emu._decode(bytes(emu._buf[marker:]))
    finally:
        emu.kill()

    trap = re.search(r"\?\w*-F-[^\r\n]*|\?TRAP|\?M-|Trap to", log)
    if trap:
        print("FAIL: fault during/after FIST run:")
        print("  " + trap.group(0).strip())
        return 1
    print("PASS: FIST ran and returned cleanly to the monitor.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
