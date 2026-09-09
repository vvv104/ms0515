"""Repack the assembled game as a small loader plus the one data file.

An RT-11 .SAV is a memory image from address 0, so every hole in the
program's address map is stored on disk: FIST's image is 38400 bytes of
which 11194 are holes and buffers it never reads.  A data file has no such
property - the loader puts each piece where it belongs - which is how
SABOT2 ships a tiny .SAV and a big .DAT.

The program's pieces are appended to FIST.DAT, so the game keeps two files
rather than gaining a third:

    blocks 0..N-1   FIST.DAT exactly as the build made it - the game's own
                    loader reads it from block 0 and is untouched
    blocks N..      the program's regions, LZSS-packed where that helps and
                    each block-aligned, so one .READW brings a whole region
    the last block  the table: the count, then (load address, word count,
                    start block, packed byte count) per region - a packed
                    count of zero means the region is stored as it is

FLOAD.MAC finds the table without being told where it is: .LOOKUP hands
back the file's length in blocks, and the table is the last one.

`verify()` puts the pieces back together and checks the result is the image
they came from, byte for byte, and that the game's own part of the file did
not move.  That is the whole correctness argument: nothing about the
program changes, only where its bytes are kept.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

import lzss

BLOCK = 512
GAP = 256            # a run of zeros this long or longer separates two regions
MAX_REGION = 8192    # a region's packed form has to fit FLOAD's scratch, and
                     # that has to fit below RT-11's monitor: see FLOAD.MAC


def regions(image: bytes) -> list[tuple[int, int]]:
    """(start, end) of every run of content, split on long runs of zeros."""
    out, i = [], 0
    while i < len(image):
        if image[i]:
            j, zeros = i, 0
            while j < len(image):
                if image[j]:
                    zeros = 0
                else:
                    zeros += 1
                    if zeros > GAP:
                        break
                j += 1
            end = j - zeros + 1              # just past the last content byte
            a, b = i & ~1, (end + 1) & ~1
            while b - a > MAX_REGION:        # split what the scratch cannot hold
                out.append((a, a + MAX_REGION))
                a += MAX_REGION
            out.append((a, b))
            i = j
        else:
            i += 1
    return out


def pack(image: bytes, dat: bytes) -> tuple[bytes, list[tuple[int, int]]]:
    """FIST.DAT with the program's regions and their table appended."""
    assert len(dat) % BLOCK == 0, 'the data file is not a whole number of blocks'
    regs = regions(image)
    first = len(dat) // BLOCK
    body, table, blk = b'', [len(regs)], first
    for start, end in regs:
        piece = image[start:end]
        squeezed = lzss.compress(piece)
        stored, n = ((squeezed, len(squeezed)) if len(squeezed) < len(piece)
                     else (piece, 0))       # a zero count: stored as it is
        pad = (-len(stored)) % BLOCK
        table += [start, len(piece) // 2, blk, n]
        body += stored + b'\0' * pad
        blk += (len(stored) + pad) // BLOCK
    head = struct.pack(f'<{len(table)}H', *table)
    return dat + body + head + b'\0' * (BLOCK - len(head)), regs


def unpack(packed: bytes, size: int) -> bytes:
    """What the loader does, in Python: the image the pieces rebuild."""
    table = packed[-BLOCK:]
    out = bytearray(size)
    n = struct.unpack_from('<H', table, 0)[0]
    for k in range(n):
        addr, words, blk, clen = struct.unpack_from('<4H', table, 2 + 8 * k)
        raw = packed[blk * BLOCK:]
        piece = (lzss.decompress(raw[:clen], 2 * words) if clen
                 else raw[:2 * words])
        out[addr:addr + 2 * words] = piece
    return bytes(out)


def already_packed(dat: bytes) -> bool:
    """A table in the last block means this file has been packed before."""
    if len(dat) < 2 * BLOCK:
        return False
    n = struct.unpack_from('<H', dat, len(dat) - BLOCK)[0]
    return 1 <= n <= 32 and 2 + 8 * n <= BLOCK


def main(argv: list[str]) -> int:
    """With no arguments, work on the project's own files - which is how
    build.toml's post_build hook calls this."""
    here = Path(__file__).resolve().parent.parent
    sav = Path(argv[1]) if len(argv) > 1 else here / 'FIST.SAV'
    dat = Path(argv[2]) if len(argv) > 2 else here / 'FIST.DAT'
    loader = here / 'FLOAD.SAV'
    if already_packed(dat.read_bytes()):
        print(f'{dat.name} already carries a table - build it afresh first',
              file=sys.stderr)
        return 1
    image, data = sav.read_bytes(), dat.read_bytes()
    packed, regs = pack(image, data)

    print(f'{sav.name}: {len(image)} B, {len(regs)} regions, '
          f'{sum(e - s for s, e in regs)} B of content')
    for s, e in regs:
        n = len(lzss.compress(image[s:e]))
        how = f'{n:6d} packed' if n < e - s else '  stored'
        print(f'    {s:#08o} .. {e:#08o}  {e - s:6d} B ->{how}')

    if unpack(packed, len(image)) != image:
        print('VERIFY FAILED: the pieces do not rebuild the image', file=sys.stderr)
        return 1
    if packed[:len(data)] != data:
        print("VERIFY FAILED: the game's own part of the file moved", file=sys.stderr)
        return 1
    print(f'{dat.name}: {len(data)} -> {len(packed)} B '
          f'(table in block {len(packed) // BLOCK - 1})')
    print('verified: the pieces rebuild the image byte for byte, '
          "and the game's own blocks did not move")
    dat.write_bytes(packed)
    if loader.exists():
        sav.write_bytes(loader.read_bytes())     # FLOAD is what ships as FIST.SAV
        loader.unlink()
        print(f'{sav.name}: {len(image)} -> {sav.stat().st_size} B (the loader)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv))
