"""compare.py - how far a built monitor is from the Omega one.

    python compare.py BUILT.SYS OMEGA.SYS [--diff] [--min N]

Per block: the words equal in place.  Overall: the words a sequence
alignment matches (code that moved still counts).  With --diff, every
unmatched run of at least N words (default 1), named by the monitor part
it falls in (the RT11SJ link map's layout: the file offset is the link
address), with its words when short, to read what Omega changed.
"""
import difflib
import struct
import sys

PARTS = [(0o000000, 'BSTRAP'), (0o011000, 'KMON'), (0o031000, 'USR'),
         (0o041000, 'RMON'), (0o053000, 'KMON overlays')]


def part(off):
    name = PARTS[0][1]
    for start, n in PARTS:
        if off >= start:
            name = n
    return name


def words(path):
    b = open(path, 'rb').read()
    if len(b) % 2:
        b += b'\0'
    return list(struct.unpack('<%dH' % (len(b) // 2), b))


def main():
    a, b = words(sys.argv[1]), words(sys.argv[2])
    minrun = int(sys.argv[sys.argv.index('--min') + 1]) if '--min' in sys.argv else 1
    print('built %d words, omega %d words' % (len(a), len(b)))
    line = []
    for blk in range(max(len(a), len(b)) // 256):
        x, y = a[blk * 256:(blk + 1) * 256], b[blk * 256:(blk + 1) * 256]
        same = sum(1 for i in range(min(len(x), len(y))) if x[i] == y[i])
        line.append('%3d:%3d%%' % (blk, same * 100 // 256))
    for i in range(0, len(line), 8):
        print('  ' + '  '.join(line[i:i + 8]))
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    matched = sum(m.size for m in sm.get_matching_blocks())
    print('aligned: %d of %d omega words (%.1f%%)' % (matched, len(b), 100.0 * matched / len(b)))
    if '--diff' in sys.argv:
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag == 'equal' or max(i2 - i1, j2 - j1) < minrun:
                continue
            print('%-7s %-13s omega %06o..%06o (%4d w)  built %06o..%06o (%4d w)'
                  % (tag, part(j1 * 2), j1 * 2, j2 * 2, j2 - j1, i1 * 2, i2 * 2, i2 - i1))
            if 0 < j2 - j1 <= 8:
                print('         omega:', ' '.join('%06o' % w for w in b[j1:j2]))
            if 0 < i2 - i1 <= 8:
                print('         built:', ' '.join('%06o' % w for w in a[i1:i2]))


if __name__ == '__main__':
    main()
