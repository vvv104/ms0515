"""msgmap.py - a kit's texts beside DEC's, string by string.

    python msgmap.py BUILT.SYS KIT.SYS [--all]

The texts are read out of the built monitor itself: runs of printable
bytes (CR, LF and TAB allowed) with letters in them, ended by a NUL, by 200
(DEC's "no CR LF" end) or by the first byte that is not text.  Texts that
follow each other make a block; the block's place in the kit's monitor lies
between the words that match on either side of it (the two aligned word by
word), and there the kit's bytes are split the same way and paired with
DEC's in order.  Each pair is printed with its state:

    SAME   the kit kept DEC's text          (shown with --all)
    KIT    the kit has its own (KOI-8 decoded, control bytes as <n>)
    ??     the block splits into another number of texts - look by hand
"""
import bisect
import difflib
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from compare import part, words  # noqa: E402

ENDS = (0, 0o200)


def is_char(b: int, koi8=True) -> bool:
    return 0o40 <= b < 0o177 or (koi8 and b >= 0o300) or b in (0o11, 0o12, 0o15)


def wordy(run: bytes) -> bool:
    """Three letters in a row somewhere: code rarely looks like that."""
    n = 0
    for b in run:
        n = n + 1 if chr(b).isalpha() or b >= 0o300 else 0
        if n >= 3:
            return True
    return False


def shown(data: bytes) -> str:
    out = []
    for b in data:
        if 0o40 <= b < 0o177:
            out.append(chr(b))
        elif b >= 0o300:
            out.append(bytes([b]).decode('koi8-r'))
        else:
            out.append(f'<{b:o}>')
    return ''.join(out)


def texts(data: bytes, lo=0, hi=None, least=4, koi8=False):
    """(start, end) of each text in data[lo:hi]; end includes the ender.
    DEC's texts are ASCII; a kit's may be KOI-8 (koi8)."""
    hi = len(data) if hi is None else hi
    out, i = [], lo
    while i < hi:
        if not is_char(data[i], koi8):
            i += 1
            continue
        j = i
        while j < hi and is_char(data[j], koi8):
            j += 1
        run = data[i:j]
        if len(run) >= least and (koi8 or wordy(run)):
            end = j + 1 if j < hi and data[j] in ENDS else j
            out.append((i, end))
        i = j + 1
    return out


def blocks(spans, gap=2):
    run = []
    for s in spans:
        if run and s[0] - run[-1][1] > gap:
            yield run
            run = []
        run.append(s)
    if run:
        yield run


def matched(a, b):
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    m = {}
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal':
            for k in range(i2 - i1):
                m[i1 + k] = j1 + k
    return m


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    built_p, kit_p = sys.argv[1], sys.argv[2]
    show_all = '--all' in sys.argv
    built, kit = Path(built_p).read_bytes(), Path(kit_p).read_bytes()
    m = matched(words(built_p), words(kit_p))
    keys = sorted(m)
    counts = {'SAME': 0, 'KIT': 0, '??': 0}
    for run in blocks(texts(built)):
        lo, hi = run[0][0], run[-1][1]
        i = bisect.bisect_right(keys, (lo - 2) // 2)
        before = keys[i - 1] if i else None
        after = next((k for k in keys[i:] if 2 * k >= hi), None)
        if before is None or after is None:
            continue
        b_lo, b_hi = 2 * before + 2, 2 * after
        k_lo, k_hi = 2 * m[before] + 2, 2 * m[after]
        mine = texts(built, b_lo, b_hi)
        if k_hi <= k_lo:
            # no anchors on the kit's side: find the block by its first text,
            # when the kit kept it, and take as many texts from there
            first = built[mine[0][0]:mine[0][1]]
            at = kit.find(first)
            if at < 0:
                print(f'-- not found: {part(b_lo)} {b_lo:06o}',
                      ' | '.join(shown(built[s:e]) for s, e in mine))
                continue
            k_lo, k_hi = at, min(len(kit), at + 2 * (b_hi - b_lo))
            theirs = texts(kit, k_lo, k_hi, least=2, koi8=True)[:len(mine)]
        else:
            theirs = texts(kit, k_lo, k_hi, least=2, koi8=True)
        if len(mine) != len(theirs):
            counts['??'] += 1
            print(f'?? {part(k_lo)} built {b_lo:06o}..{b_hi:06o} kit {k_lo:06o}..{k_hi:06o}:'
                  f' {len(mine)} texts, kit {len(theirs)}')
            for s, e in mine:
                print(f'     DEC {s:06o} {shown(built[s:e])}')
            for s, e in theirs:
                print(f'     KIT {s:06o} {shown(kit[s:e])}')
            continue
        for (s, e), (ks, ke) in zip(mine, theirs):
            same = built[s:e] == kit[ks:ke]
            counts['SAME' if same else 'KIT'] += 1
            if same and not show_all:
                continue
            print(f'{"SAME" if same else "KIT ":4} {part(ks):7} {s:06o} {ks:06o} '
                  f'{shown(built[s:e])!r} -> {shown(kit[ks:ke])!r}')
    print(counts)


if __name__ == '__main__':
    main()
