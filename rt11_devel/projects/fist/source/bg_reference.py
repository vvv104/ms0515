"""Reference implementation of WotEF's background engine (oracle for the port).

A faithful Python re-execution of the Z80 routines Change_Background
($5F22), Create_Background ($5F80), Background_Fetch_Next ($5FBE),
Copy_UDG ($5FF4), Background_Next_Screen_Block ($6007) and
Background_Attributes ($6010), reading the original data tables straight
out of the runtime snapshot.

Its job is to (a) validate that the disassembly logic and the data-table
addresses are understood correctly - if the rendered backgrounds look
right, they are - and (b) serve as the byte-exact spec the MACRO-11 port
is transcribed from and checked against.

    python bg_reference.py            # renders bg1/2/3 to bgN.png
"""
import os
from pathlib import Path

from skoolkit.snapshot import get_snapshot

import preview

from wotef_dir import WOTEF_DIR                            # noqa: E402
SNAP = WOTEF_DIR / "wotef.z80"

# Per-background definition tables (Create_Background copies 0x12 bytes of
# (udg_ptr, pos_ptr) x4 + attr_ptr into the work buffer at $5F10).
BG_DEF = {1: 0x602B, 2: 0x603D, 3: 0x604F}


def _u16(M, a):
    return M[a] | (M[a + 1] << 8)


def copy_udg(M, de, udg):
    """$5FF4 - blit one 8x8 UDG; INC D steps down a pixel row each line.
    DE is pushed/popped, so it is unchanged for the caller."""
    d = de
    for i in range(8):
        M[d & 0xFFFF] = M[(udg + i) & 0xFFFF]
        d = (d + 0x100) & 0xFFFF
    return de


def next_block(de):
    """$6007 - advance one cell to the right, hopping screen thirds."""
    de = (de + 1) & 0xFFFF
    if (de >> 8) & 1:                       # BIT 0,D
        de = (de & 0xFF) | ((((de >> 8) + 7) & 0xFF) << 8)
    return de


def draw_positions(M, scr, pos, udg):
    """$5FBE - walk a block's positioning stream, placing UDGs."""
    de, hl = scr, pos
    while True:
        a = M[hl]; hl += 1                  # $5FBE
        if a == M[hl] and a == 0:           # CP (HL); AND A; RET Z
            return
        rep = (a & 0x80) != 0               # bit 7 = repeat-run flag
        a &= 0x7F
        addr = (udg + a * 8) & 0xFFFF        # UDG index -> address (*8)
        copy_udg(M, de, addr)
        de = next_block(de)
        if rep:
            cnt = M[hl]; hl += 1            # following byte = run length
            for _ in range(cnt - 1):
                copy_udg(M, de, addr)
                de = next_block(de)


def background_attributes(M, hl):
    """$6010 - RLE-unpack attribute data into the $5800 attribute buffer."""
    de = 0x5800
    while True:
        a = M[hl]; hl += 1
        if a == M[hl] and a == 0:
            return
        rep = (a & 0x80) != 0
        a &= 0x7F
        M[de] = a; de += 1
        if rep:
            cnt = M[hl]; hl += 1
            for _ in range(cnt - 1):
                M[de] = a; de += 1


def create_background(mem, ref):
    """Run Change_Background + Create_Background for ref in {1,2,3}; return
    the resulting 6912-byte Spectrum screen ($4000..$5AFF)."""
    M = bytearray(mem)
    # Change_Background screen blank: attrs := $3F, pixels := $00.
    for a in range(0x5800, 0x5800 + 0x300):
        M[a] = 0x3F
    for a in range(0x4000, 0x4000 + 0x1800):
        M[a] = 0x00
    # Copy the 18-byte definition into the work buffer at $5F10.
    src = BG_DEF[ref]
    for i in range(0x12):
        M[0x5F10 + i] = mem[src + i]
    # Create_Background: 4 blocks, each starting 0x80 below the last.
    scr = 0x4080
    hl = 0x5F10
    for _ in range(4):
        udg = _u16(M, hl); hl += 2
        pos = _u16(M, hl); hl += 2
        draw_positions(M, scr, pos, udg)
        scr = (scr + 0x80) & 0xFFFF
        if (scr >> 8) & 1:                  # BIT 0,H -> hop a screen third
            scr = (scr & 0xFF) | ((((scr >> 8) + 7) & 0xFF) << 8)
    background_attributes(M, _u16(M, hl))
    return bytes(M[0x4000:0x4000 + 6912])


def main():
    mem = get_snapshot(str(SNAP))
    for ref in (1, 2, 3):
        screen = create_background(mem, ref)
        img = preview.render(preview.build_vram(screen))
        out = Path(__file__).resolve().parent.parent / f"bg{ref}.png"
        img.resize((640, 400), preview.Image.NEAREST).save(out)
        print(f"bg_reference: wrote {out}")


if __name__ == "__main__":
    main()
