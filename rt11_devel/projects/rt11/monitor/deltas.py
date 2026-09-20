"""deltas.py - the differences of one part that are single changed words,
with Omega's value minus the built one: a run of equal deltas is a
pointer shifted by code that moved, not a change of its own.

    python deltas.py BUILT OMEGA PART
"""
import collections
import difflib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from compare import part, words  # noqa: E402


def main():
    a, b = words(sys.argv[1]), words(sys.argv[2])
    which = sys.argv[3]
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    counts = collections.Counter()
    other = []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal' or part(j1 * 2) != which:
            continue
        if tag == 'replace' and i2 - i1 == j2 - j1:
            for k in range(j2 - j1):
                d = (b[j1 + k] - a[i1 + k]) & 0xFFFF
                counts[d] += 1
                other.append((j1 + k, d, b[j1 + k], a[i1 + k]))
        else:
            print('%-7s omega %06o (%d w)  built %06o (%d w)' % (tag, j1 * 2, j2 - j1, i1 * 2, i2 - i1))
    print('single-word deltas:', ', '.join('%o x%d' % (d, n) for d, n in counts.most_common()))
    rare = {d for d, n in counts.items() if n <= 2}
    for w, d, x, y in other:
        if d in rare:
            print('  %06o  omega %06o  built %06o  delta %o' % (w * 2, x, y, d))


if __name__ == '__main__':
    main()
