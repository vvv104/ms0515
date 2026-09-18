"""compare.py - how far a built monitor is from the Omega one.

    python compare.py BUILT.SYS OMEGA.SYS [--diff]

Per block: the words equal in place.  Overall: the words a sequence
alignment matches (code that moved still counts), and with --diff the
unmatched runs, as octal offsets and words, to read what Omega changed.
"""
import difflib
import struct
import sys


def words(path):
    b = open(path, 'rb').read()
    if len(b) % 2:
        b += b'\0'
    return list(struct.unpack('<%dH' % (len(b) // 2), b))


def main():
    a, b = words(sys.argv[1]), words(sys.argv[2])
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
            if tag == 'equal':
                continue
            print('%-7s built %06o..%06o  omega %06o..%06o' % (tag, i1 * 2, i2 * 2, j1 * 2, j2 * 2))
            if j2 - j1 <= 16:
                print('         omega:', ' '.join('%06o' % w for w in b[j1:j2]))
            if i2 - i1 <= 16:
                print('         built:', ' '.join('%06o' % w for w in a[i1:i2]))


if __name__ == '__main__':
    main()
