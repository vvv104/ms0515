"""tail.py - MINE.SAV as the game reads it.

    python tail.py LINKED.SAV MINE.DAT MINE.SAV

CHECK (SPR.PAS) opens the program's own file: it sums the words of all its
blocks but the last two, compares the sum with the first word after them,
skips eight words and reads the sprite table - 63 sprites of 8 words - from
there to the file's end.  So the game's MINE.SAV is what LINK made and two
blocks more: the sum, seven words unused, and the table out of MINE.DAT.

No program of the author's that wrote those two blocks has survived; this
does what CHECK's reading implies.
"""
import struct
import sys

SPRITES = 63


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    program = open(sys.argv[1], 'rb').read()
    table = open(sys.argv[2], 'rb').read()[:SPRITES * 16]
    if len(program) % 512:
        raise SystemExit(f'{sys.argv[1]} is not whole blocks')
    if len(table) != SPRITES * 16:
        raise SystemExit(f'{sys.argv[2]} holds fewer than {SPRITES} sprites')
    total = sum(struct.unpack(f'<{len(program) // 2}H', program)) & 0xFFFF
    with open(sys.argv[3], 'wb') as out:
        out.write(program + struct.pack('<H', total) + bytes(14) + table)
    print(f'{len(program) // 512} blocks and 2 more, sum {total:06o}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
