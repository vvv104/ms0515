"""Compare a game-logic VRAM-oracle dump to the reference's expected window.

The game-logic test (gamelogic_mac.py) copies the post-routine state window
GST[$9C00..$AB00] into VRAM as words, so the first WIN_SIZE bytes of the
oracle's VRAM dump are exactly that window.  This checks them against
gl_expected.bin (the same window after the validated Python reference ran on
the same captured input).

    python gl_check.py [path/to/fist_vram.bin]

Default dump path is the in-tree test build output.  Exit status is non-zero
on any mismatch, so it doubles as a CI gate.
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJ = HERE.parent
DEFAULT_DUMP = (PROJ.parent.parent.parent / "src" / "build" / "Release" /
                "lib" / "tests" / "fist_vram.bin")


def main():
    dump = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_DUMP
    exp = (PROJ / "gl_expected.bin").read_bytes()
    win = json.loads((PROJ / "gl_window.json").read_text())
    base = win["base"]

    if not dump.exists():
        raise SystemExit(f"VRAM dump not found: {dump}\n"
                         f"build the game-logic FIST.SAV and run the oracle "
                         f"(ms0515_lib_tests --test-case=\"fist: VRAM oracle\")")
    got = dump.read_bytes()[:len(exp)]

    diffs = [(base + i, exp[i], got[i]) for i in range(len(exp))
             if exp[i] != got[i]]
    print(f"window {len(exp)} bytes @ ${base:04X}; {len(diffs)} mismatches")
    for a, e, g in diffs[:20]:
        print(f"  ${a:04X}: expected {e:#04x} got {g:#04x}")
    if diffs:
        raise SystemExit("MISMATCH")
    print("MATCH")


if __name__ == "__main__":
    main()
