"""where.py - the DEC source lines a built monitor address comes from.

    python where.py BUILDDIR ADDR [ADDR2] [--context N]      (octal)

BUILDDIR holds a build's RT11SJ.MAP and the four listings (BTSJ, RMSJ,
KMSJ, TBSJ .LST).  A section's base comes from the map; a listing line's
address is its offset in the section it was assembled into.  Shows the
lines from ADDR to ADDR2 (or N lines of context around ADDR).
"""
import re
import sys
from pathlib import Path

LISTINGS = ['BTSJ', 'RMSJ', 'KMSJ', 'TBSJ']
LINE = re.compile(r'^\s*\d+\s+(\d{6})\s')
PSECT = re.compile(r'^\s*\d+\s+(?:\d{6}\s+)?\.[PC]SECT\s+([A-Z0-9$.]+)', re.I)
ASECT = re.compile(r'^\s*\d+\s+(?:\d{6}\s+)?\.ASECT\b', re.I)


def sections(build):
    out = {}
    for line in (build / 'RT11SJ.MAP').read_bytes().replace(b'\0', b'').decode('latin-1').splitlines():
        m = re.match(r'^ ([A-Z0-9$. ]{1,6})\s+(\d{6})\s+(\d{6}) =', line)
        if m:
            out.setdefault(m.group(1).strip(), (int(m.group(2), 8), int(m.group(3), 8)))
    return out


def lines(build):
    """(section, offset, object, text) for every listing line with an address."""
    rows = []
    for obj in LISTINGS:
        p = build / f'{obj}.LST'
        if not p.exists():
            continue
        cur = '. BLK.'
        for text in p.read_bytes().replace(b'\0', b'').decode('latin-1').splitlines():
            m = PSECT.match(text)
            if m:
                cur = m.group(1).upper()
            elif ASECT.match(text):
                cur = '. ABS.'
            a = LINE.match(text)
            if a:
                rows.append((cur, int(a.group(1), 8), obj, text.rstrip()))
    return rows


def main():
    build = Path(sys.argv[1])
    addr = int(sys.argv[2], 8)
    end = int(sys.argv[3], 8) if len(sys.argv) > 3 and not sys.argv[3].startswith('--') else None
    ctx = int(sys.argv[sys.argv.index('--context') + 1]) if '--context' in sys.argv else 12
    secs = sections(build)
    hits = []
    for sec, off, obj, text in lines(build):
        if sec not in secs:
            continue
        base, size = secs[sec]
        a = base + off
        hits.append((a, sec, obj, text))
    hits.sort(key=lambda h: h[0])
    if end is None:
        idx = [i for i, h in enumerate(hits) if h[0] <= addr]
        i = idx[-1] if idx else 0
        sel = hits[max(0, i - ctx // 2): i + ctx]
    else:
        sel = [h for h in hits if addr <= h[0] <= end]
    for a, sec, obj, text in sel:
        print('%06o %-6s %s | %s' % (a, sec, obj, text[:110]))


if __name__ == '__main__':
    main()
