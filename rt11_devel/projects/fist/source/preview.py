"""Render what the MS-0515 would scan out of the VRAM that FIST's SPSCR
builds, so the 1:1 centred screen can be eyeballed without the emulator.

This is an independent cross-check of the port's display foundation: it
applies the same Spectrum->VRAM rule SPSCR uses (attr byte -> high byte,
pixel byte -> low byte, centred with a 4-word/4-line margin), then decodes
the VRAM exactly like the medium-res 320x200 video hardware does
(low byte = 8 pixels D7..D0, high byte = F I G'R'B' G R B attributes,
GRB palette with an intensity bit).

    python rt11_devel/projects/fist/source/preview.py [out.png]
"""
import sys
from pathlib import Path

from PIL import Image

import gen_fist

# 3-bit GRB palette (bit2=G, bit1=R, bit0=B), at dim and bright levels.
DIM, BRT = 0xA0, 0xFF


def grb_rgb(grb, bright):
    g = (grb >> 2) & 1
    r = (grb >> 1) & 1
    b = grb & 1
    lvl = BRT if bright else DIM
    return (r * lvl, g * lvl, b * lvl)


def build_vram(screen):
    """Place the Spectrum screen into a 320x200 medium-res VRAM (40 words/
    line), centred, exactly as SPSCR does.  Returns a list of 200*40 words."""
    rows = gen_fist.spectrum_row_offsets()
    vram = [0] * (40 * 200)
    for y in range(192):
        pix = rows[y]
        attr = 6144 + (y >> 3) * 32
        line = 4 + y
        for cx in range(32):
            word = (screen[attr + cx] << 8) | screen[pix + cx]
            vram[line * 40 + (4 + cx)] = word
    return vram


def render(vram):
    img = Image.new("RGB", (320, 200), (0, 0, 0))
    px = img.load()
    for line in range(200):
        for col in range(40):
            word = vram[line * 40 + col]
            low = word & 0xFF
            attr = word >> 8
            bright = (attr >> 6) & 1
            fg = grb_rgb(attr & 0x07, bright)
            bg = grb_rgb((attr >> 3) & 0x07, bright)
            for bit in range(8):              # D7 leftmost
                lit = (low >> (7 - bit)) & 1
                px[col * 8 + bit, line] = fg if lit else bg
    return img


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("fist_screen.png")
    screen = gen_fist.load_loading_screen()
    img = render(build_vram(screen))
    img.resize((640, 400), Image.NEAREST).save(out)
    print(f"preview: wrote {out}")


if __name__ == "__main__":
    main()
