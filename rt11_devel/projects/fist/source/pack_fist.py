"""Repack the assembled game as a small loader plus the one data file.

An RT-11 .SAV is a memory image from address 0, so every hole in the
program's address map is stored on disk: FIST's image is 38400 bytes of
which 11194 are holes and buffers it never reads.  A data file has no such
property - the loader puts each piece where it belongs - which is how
SABOT2 ships a tiny .SAV and a big .DAT.

The program's pieces are appended to FIST.DAT, so the game keeps two files
rather than gaining a third:

    the front     FIST.DAT exactly as the build made it - the game's own
                  packed pieces, which its own loader reads and is told
                  about in its generated source
    behind it     the program's regions, LZSS-packed, following each other
                  byte for byte
    the tail      the table: (load address, word count, start block, byte
                  offset into it, packed length) per region, then the image's
                  top - what the loader clears up to - and the count as the
                  file's very last word

FLOAD.MAC finds the table without being told where it is: .LOOKUP hands
back the file's length in blocks, the count is the last word of the last
one, and the entries are the ten bytes each in front of it.

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
    """FIST.DAT with the program's regions and their table appended.

    Regions follow each other byte for byte - a read is whole blocks, so the
    table says which block a region starts in and how far into it - and the
    table sits at the very end of the file, its count the last word, so
    .LOOKUP's length is all the loader needs to find it."""
    regs = regions(image)
    body, table = bytearray(dat), []
    for start, end in regs:
        piece = image[start:end]
        squeezed = lzss.compress(piece)
        assert len(squeezed) < len(piece),             f'region {start:#o} does not pack ({len(squeezed)} >= {len(piece)})'
        at = len(body)
        table += [start, len(piece) // 2, at // 512, at % 512, len(squeezed)]
        body += squeezed
    table += [len(image), len(regs)]              # the top, then the count
    head = struct.pack(f'<{len(table)}H', *table)
    size = -(-(len(body) + len(head)) // BLOCK) * BLOCK
    return bytes(body) + b'\0' * (size - len(body) - len(head)) + head, regs


def unpack(packed: bytes, size: int) -> bytes:
    """What the loader does, in Python: the image the pieces rebuild."""
    n = struct.unpack_from('<H', packed, len(packed) - 2)[0]
    at = len(packed) - 4 - 10 * n
    out = bytearray(size)
    for k in range(n):
        addr, words, blk, off, clen = struct.unpack_from('<5H', packed, at + 10 * k)
        raw = packed[blk * BLOCK + off:][:clen]
        out[addr:addr + 2 * words] = lzss.decompress(raw, 2 * words)
    return bytes(out)


def already_packed(dat: bytes) -> bool:
    """A count in the file's last word means this file has been packed."""
    if len(dat) < 2 * BLOCK or len(dat) % BLOCK:
        return False
    n = struct.unpack_from('<H', dat, len(dat) - 2)[0]
    return 1 <= n <= 32 and 4 + 10 * n <= BLOCK


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

    # SCRBUF (6912 B) and DOJOBUF (192 rows of 32 words = 12288 B) live above
    # the image, and the machine's memory ends where the I/O page begins.
    # Overrun it and the dojo's last rows go into device registers, which is
    # what put a strip of noise along the bottom of the picture once.
    hilim = struct.unpack_from('<H', image, 0o50)[0]     # LINK's high limit
    top = hilim + 6912 + 12288
    if top > 0o160000:
        print(f'{sav.name}: the program ends at {hilim:#o}, which is '
              f'{top - 0o160000} bytes too high - SCRBUF and DOJOBUF above it '
              f'would reach {top:#o}, past the I/O page at 0160000',
              file=sys.stderr)
        return 1
    print(f'{sav.name}: ends at {hilim:#o}; SCRBUF + DOJOBUF reach {top:#o}, '
          f'{0o160000 - top} bytes below the I/O page')

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
