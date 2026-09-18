"""msgmap.py - a kit's own texts beside DEC's.

    python msgmap.py BUILDDIR KIT.SYS [PART]

BUILDDIR holds a build with its listings (build_monitor.py --list).  DEC's
.ASCII/.ASCIZ lines that follow each other make a block; the block's place
in the kit's monitor lies between the words that match on either side of
it (the build and the kit aligned word by word).  Each block that differs
is shown: DEC's lines, then the kit's bytes of the same place, split at the
NULs and at 200 (DEC's "no CR LF" end), KOI-8 decoded, control characters
as <n> in octal.
"""
import bisect
import difflib
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from compare import part, words  # noqa: E402
from where import lines, sections  # noqa: E402


def text(data: bytes) -> str:
    out = []
    for b in data:
        if 0o40 <= b < 0o177:
            out.append(chr(b))
        elif b >= 0o300:
            out.append(bytes([b]).decode('koi8-r'))
        else:
            out.append(f'<{b:o}>')
    return ''.join(out)


def pieces(data: bytes):
    cur = bytearray()
    for b in data:
        cur.append(b)
        if b in (0, 0o200):
            yield bytes(cur)
            cur = bytearray()
    if cur:
        yield bytes(cur)


def string_lines(build: Path):
    """(address, end, object, line) of DEC's text lines, in address order;
    a line ends where the next line with an address of its section starts."""
    secs = sections(build)
    rows = sorted((secs[sec][0] + off, sec, obj, line)
                  for sec, off, obj, line in lines(build) if sec in secs)
    out = []
    for n, (addr, sec, obj, line) in enumerate(rows):
        if '.ASCI' not in line.upper():
            continue
        end = next((r[0] for r in rows[n + 1:] if r[0] > addr), addr + 2)
        out.append((addr, end, obj, line))
    return out


def blocks(src):
    """Runs of text lines that follow each other (a .EVEN between allowed)."""
    run = []
    for s in src:
        if run and s[0] > run[-1][1] + 1:
            yield run
            run = []
        run.append(s)
    if run:
        yield run


def matched(a, b):
    """Built word index -> kit word index, for the words that match."""
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    m = {}
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal':
            for k in range(i2 - i1):
                m[i1 + k] = j1 + k
    return m


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    build, kit = Path(sys.argv[1]), sys.argv[2]
    which = sys.argv[3] if len(sys.argv) > 3 else None
    built = (build / 'RT11SJ.SYG').read_bytes()
    theirs = Path(kit).read_bytes()
    m = matched(words(str(build / 'RT11SJ.SYG')), words(kit))
    keys = sorted(m)
    for run in blocks(string_lines(build)):
        lo, hi = run[0][0] & ~1, run[-1][1]
        # between the matching words on either side of the block
        i = bisect.bisect_left(keys, lo // 2)
        before = keys[i - 1] if i else None
        after = next((k for k in keys[i:] if 2 * k >= hi - 1), None)
        if before is None or after is None:
            continue
        k_lo, k_hi = 2 * m[before] + 2, 2 * m[after]
        b_lo, b_hi = 2 * before + 2, 2 * after
        if built[b_lo:b_hi] == theirs[k_lo:k_hi]:
            continue
        if which and part(k_lo) != which:
            continue
        print(f'==== {part(k_lo)}  built {b_lo:06o}..{b_hi:06o}  kit {k_lo:06o}..{k_hi:06o}')
        for addr, _, obj, line in run:
            print(f'  DEC {addr:06o} {obj} | {line.split(chr(9), 3)[-1][:100]}')
        for p in pieces(theirs[k_lo:k_hi]):
            print(f'  KIT        | {text(p)}')


if __name__ == '__main__':
    main()
