"""Write the expected MS-0515 VRAM image of each background (bg1/2/3) as a raw
16 KB dump, for the lib test that checks the live game's dojo changes with the
rank (src/lib/tests/test_fist_game.cpp, "background follows the rank").

The Python reference renders the background exactly as the ported engine does
(bg_reference.create_background), and preview.build_vram places it on the
320x200 screen exactly as SPSCR does, so the dump is byte-comparable with
board_get_vram() on the static rows (those no fighter or HUD touches).

    python bg_expect.py [out_dir]        -> out_dir/bg{1,2,3}_vram.bin

Output embeds the original art - keep it out of the repo (gitignored *.bin).
"""
import struct
import sys
from pathlib import Path

from skoolkit.snapshot import get_snapshot

import bg_reference
import preview


def main():
    out_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent
    mem = get_snapshot(str(bg_reference.SNAP))
    for ref in (1, 2, 3):
        screen = bg_reference.create_background(mem, ref)
        words = preview.build_vram(screen) + [0] * (8192 - 200 * 40)
        out = out_dir / f"bg{ref}_vram.bin"
        out.write_bytes(struct.pack(f"<{len(words)}H", *words))
        print(f"bg_expect: wrote {out}")


if __name__ == "__main__":
    main()
