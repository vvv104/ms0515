"""scr2png.py - render an MS-0515 screen dump (.SCR, the whole 16 KB VRAM) as PNG.

The dumps that survive on the diskettes (ART.SAV writes them; BASICO's
BSAVE too) are the video RAM as is: 200 rows of 40 words.  In the colour
320x200 mode each word is eight pixels in the low byte (MSB leftmost) and
their attribute in the high byte - bit 7 flash, bit 6 bright, bits 5-3
the background GRB, bits 2-0 the foreground GRB.  In the monochrome
640x200 mode both bytes are pixels, low byte first.  The file carries no
mode flag; a dump whose high bytes are all pixels-looking is hi-res, one
with a handful of distinct attribute bytes is lo-res - `--mode auto`
guesses by that, `--mode lo` / `--mode hi` decide.

    python tools/scr2png.py MORDA.SCR [-o MORDA.png] [--mode auto|lo|hi]

Output is 640x400 (2x2 blocks in lo-res, 1x2 in hi-res), the same picture
libapp's Screen composes for the emulator.  Needs Pillow.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image

ROWS, WORDS_PER_ROW = 200, 40
VRAM_BYTES = 16384


def palette(grb: int, bright: bool) -> tuple[int, int, int]:
    """GRB bits (bit 2 = G, bit 1 = R, bit 0 = B) to RGB, dim or bright."""
    hi = 255 if bright else 128
    return (hi if grb & 2 else 0, hi if grb & 4 else 0, hi if grb & 1 else 0)


def guess_mode(data: bytes) -> str:
    """Lo-res dumps use a few attribute values; hi-res high bytes look like pixels."""
    attrs = {data[i] for i in range(1, ROWS * WORDS_PER_ROW * 2, 2)}
    return "lo" if len(attrs) <= 32 else "hi"


def render_lo(data: bytes) -> Image.Image:
    img = Image.new("RGB", (640, 400))
    px = img.load()
    for y in range(ROWS):
        for wx in range(WORDS_PER_ROW):
            i = (y * WORDS_PER_ROW + wx) * 2
            pixels, attr = data[i], data[i + 1]
            bright = bool(attr & 0x40)
            fg, bg = palette(attr & 7, bright), palette((attr >> 3) & 7, bright)
            for p in range(8):
                c = fg if (pixels >> (7 - p)) & 1 else bg
                x = (wx * 8 + p) * 2
                px[x, y * 2] = px[x + 1, y * 2] = px[x, y * 2 + 1] = px[x + 1, y * 2 + 1] = c
    return img


def render_hi(data: bytes) -> Image.Image:
    img = Image.new("RGB", (640, 400))
    px = img.load()
    for y in range(ROWS):
        for wx in range(WORDS_PER_ROW):
            i = (y * WORDS_PER_ROW + wx) * 2
            for half, byte in enumerate((data[i], data[i + 1])):
                for p in range(8):
                    c = (255, 255, 255) if (byte >> (7 - p)) & 1 else (0, 0, 0)
                    x = wx * 16 + half * 8 + p
                    px[x, y * 2] = px[x, y * 2 + 1] = c
    return img


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("scr", type=Path)
    ap.add_argument("-o", "--out", type=Path, help="PNG to write (default: beside the input)")
    ap.add_argument("--mode", choices=("auto", "lo", "hi"), default="auto")
    args = ap.parse_args()
    data = args.scr.read_bytes()
    if len(data) < ROWS * WORDS_PER_ROW * 2:
        print("not a screen dump: %d bytes, %d needed" % (len(data), VRAM_BYTES), file=sys.stderr)
        return 1
    mode = guess_mode(data) if args.mode == "auto" else args.mode
    img = render_lo(data) if mode == "lo" else render_hi(data)
    out = args.out or args.scr.with_suffix(".png")
    img.save(out)
    print("%s -> %s (%s-res)" % (args.scr.name, out, mode))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
