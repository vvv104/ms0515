"""Render a raw MS-0515 VRAM dump (medium-res 320x200) to a PNG.

Used to eyeball the pixel-exact output captured by the C++ VRAM oracle
(src/lib/tests/test_fist_screen.cpp), which runs a built FIST.SAV in the
real emulator and dumps the 16 KB VRAM.  Decodes the dump exactly as the
video hardware scans it: 40 words per line, low byte = 8 pixels (D7..D0),
high byte = F I G'R'B' G R B attributes.

    python render_vram.py <fist_vram.bin> [out.png]
"""
import struct
import sys
from pathlib import Path

import preview


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: render_vram.py <vram.bin> [out.png]")
    raw = Path(sys.argv[1]).read_bytes()
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("fist_emu_vram.png")
    words = list(struct.unpack(f"<{len(raw) // 2}H", raw))
    img = preview.render(words[:200 * 40])
    img.resize((640, 400), preview.Image.NEAREST).save(out)
    print(f"render_vram: wrote {out}")


if __name__ == "__main__":
    main()
