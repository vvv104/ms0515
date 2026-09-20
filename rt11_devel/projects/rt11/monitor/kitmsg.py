"""kitmsg.py - a kit's texts as a module of macros, and DEC's sources calling
them: the patch of the texts, made from a build and the kit's monitor.

    python kitmsg.py BUILT.SYS KIT.SYS WORKDIR OUTDIR

BUILT.SYS is a build with DEC's texts; KIT.SYS the kit's monitor; WORKDIR
the working copies of DEC's sources (changed in place); OUTDIR gets the
modules, one for each part of the monitor that has texts (OMRUSB.MAC -
BSTRAP, OMRUSU.MAC - USR, OMRUSK.MAC - KMON and its overlays), each
included by its DEC file: MACRO's work file holds only so many macros, and
KMON's assembly is the largest.  The texts are paired as msgmap.py pairs them.  Each
text the kit has its own of gets a macro RUnnn with its bytes; called with
an argument, it sets that symbol to its length instead (DEC's text macros
count their tables with it).  DEC's text macros take the macro as RU=RUnnn
and call it where RU$ALL is set; the lines of text that stand alone get the
call and DEC's line, one of them assembled.  The module is included only
where RU$ALL is set, so every other build stays as it was.
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from compare import part, words  # noqa: E402
from koi8mac import lines as mac_lines  # noqa: E402
import msgmap  # noqa: E402

MACROS = ('KMEROR', 'KMRTMG', 'PTXT', 'ERRMSG', 'CSIERR')
COUNTED = ('KMEROR', 'KMRTMG')          # they count their text's length
FILES = {'BSTRAP': ['BSTRAP.MAC'], 'KMON': ['KMON.MAC'], 'USR': ['USR.MAC'],
         'RMON': ['RMONSJ.MAC'], 'KMON overlays': ['KMON.MAC', 'KMOVLY.MAC']}
ENDS = {'KMEROR': b'\0', 'CSIERR': b'\0', 'KMRTMG': b'\200', 'ERRMSG': b'\200',
        'PTXT': b'? \200'}
SKIP = ('?MON-F-System read failure halt',)     # the patch's own (OMRMSG)
MODULES = {'OMRUSB': (['BSTRAP.MAC'], 'BSTRAP'),       # the first file includes it
           'OMRUSU': (['USR.MAC', 'RMONSJ.MAC'], 'USR'),
           'OMRUSK': (['KMON.MAC', 'KMOVLY.MAC'], 'KMON and its overlays')}
INCLUDE = '.IF NE\tRU$ALL\n\t.INCLUDE "SRC:{}.MAC"\t;The kit\'s texts (OMRUS.MAC)\n.ENDC\t;NE RU$ALL'
CALL_HEAD = re.compile(r'^(?P<head>(?:\S+?:+\s*|\s+)(?P<mac>' + '|'.join(MACROS) + r')\s+)')


class Call:
    """A call of one of DEC's text macros: head, arguments, comment."""

    def __init__(self, m, line):
        self.head = m.group('head')
        self.mac = m.group('mac')
        rest, depth = line[m.end():], 0
        for i, ch in enumerate(rest):
            depth += (ch == '<') - (ch == '>')
            if ch == ';' and depth == 0:
                self.args, self.tail = rest[:i], rest[i:]
                break
        else:
            self.args, self.tail = rest, ''

    def group(self, name):
        return getattr(self, name)


class _CallMatcher:
    def match(self, line):
        m = CALL_HEAD.match(line)
        return Call(m, line) if m else None


CALL = _CallMatcher()
TEXT = re.compile(r'^(?P<head>(?:\S+:+)?\s*)\.(?P<op>ASCIZ|ASCII)\s+(?P<d>[^\s<])(?P<text>[^\n]*?)(?P=d)(?P<after>[^;]*?)(?P<tail>\s*(;.*)?)$')


# Texts msgmap cannot pair (two letters are not a text to it): OSA's "From?"
# and "To?" prompts, as its monitor has them (KMON's PTXT table)
EXTRA = [('KMON', b'From? \200', b'\363   ? \200'),
         ('KMON', b'To  ? \200', b'\316\301  ? \200')]


def pairs(built_p, kit_p):
    """(part, DEC bytes, kit bytes) of each text the kit has its own of."""
    return found_pairs(built_p, kit_p) + EXTRA


def found_pairs(built_p, kit_p):
    built, kit = Path(built_p).read_bytes(), Path(kit_p).read_bytes()
    m = msgmap.matched(words(built_p), words(kit_p))
    keys = sorted(m)
    out = []
    import bisect
    for run in msgmap.blocks(msgmap.texts(built)):
        lo, hi = run[0][0], run[-1][1]
        i = bisect.bisect_right(keys, (lo - 2) // 2)
        before = keys[i - 1] if i else None
        after = next((k for k in keys[i:] if 2 * k >= hi), None)
        if before is None or after is None:
            continue
        b_lo, b_hi = 2 * before + 2, 2 * after
        k_lo, k_hi = 2 * m[before] + 2, 2 * m[after]
        mine = msgmap.texts(built, b_lo, b_hi)
        if k_hi <= k_lo:
            at = kit.find(built[mine[0][0]:mine[0][1]])
            if at < 0:
                continue
            k_lo, k_hi = at, min(len(kit), at + 2 * (b_hi - b_lo))
            theirs = msgmap.texts(kit, k_lo, k_hi, least=2, koi8=True)[:len(mine)]
        else:
            theirs = msgmap.texts(kit, k_lo, k_hi, least=2, koi8=True)
        if len(mine) != len(theirs):
            continue
        for (s, e), (ks, ke) in zip(mine, theirs):
            if built[s:e] != kit[ks:ke]:
                out.append((part(ks), built[s:e], kit[ks:ke]))
    return out


def macro_text(args: str, mac: str) -> str | None:
    """The TEXT argument of a call: the first <...> (ERRMSG's third)."""
    found = re.findall(r'<([^<>]*)>', args)
    return found[0] if found else None


def sites(work: Path):
    """Every place DEC's text is written: (file, line no, kind, text)."""
    out = []
    for name in ('BSTRAP.MAC', 'KMON.MAC', 'KMOVLY.MAC', 'USR.MAC', 'RMONSJ.MAC'):
        for n, line in enumerate((work / name).read_text(encoding='latin-1').split('\n')):
            if line.lstrip().startswith(';') or '.MACRO' in line.upper():
                continue
            c = CALL.match(line)
            if c:
                t = macro_text(c.group('args'), c.group('mac'))
                if t:
                    out.append((name, n, c.group('mac'), t))
                continue
            t = TEXT.match(line)
            if t and t.group('text'):
                out.append((name, n, t.group('op'), t.group('text')))
    return out


def ender(kind: str, line: str) -> bytes:
    if kind in ENDS:
        return ENDS[kind]
    return b'\0' if kind == 'ASCIZ' else (b'\200' if '<200>' in line else b'')


WRAPPED = re.compile(r'\.IF NE\tRU\$ALL\n[^\n]*RU\d{3}\t\t\t;The kit\'s text \(OMRUS\.MAC\)\n'
                     r'(?:\t\.BYTE\t\d+\n)?\.IFF\t;NE RU\$ALL\n([^\n]*)\n\.ENDC\t;NE RU\$ALL')
INCLUDED = re.compile(r'\n\.IF NE\tRU\$ALL\n\t\.INCLUDE "SRC:OMRUS\w\.MAC"[^\n]*\n\.ENDC\t;NE RU\$ALL')


def undo(work: Path):
    """Take out what an earlier run put in, so a run starts from DEC's."""
    for name in ('BSTRAP.MAC', 'KMON.MAC', 'KMOVLY.MAC', 'USR.MAC', 'RMONSJ.MAC'):
        p = work / name
        s = p.read_text(encoding='latin-1')
        s = WRAPPED.sub(lambda m: m.group(1), s)
        s = INCLUDED.sub('', s)
        s = re.sub(r',RU=RU\d{3}', '', s)
        p.write_text(s, encoding='latin-1', newline='')


def main():
    built_p, kit_p, work, out_d = sys.argv[1], sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4])
    undo(work)
    texts = {name: (work / name).read_text(encoding='latin-1').split('\n')
             for name in ('BSTRAP.MAC', 'KMON.MAC', 'KMOVLY.MAC', 'USR.MAC', 'RMONSJ.MAC')}
    all_sites = sites(work)
    macros = {}                     # (english, kit text) -> (name, counted)
    edits = {}                      # (file, line) -> macro name
    problems = []
    for where, dec, kit in pairs(built_p, kit_p):
        cand = [s for s in all_sites if s[0] in FILES.get(where, [])
                and s[3].encode('latin-1') in dec and dec.endswith(ender(s[2], texts[s[0]][s[1]]))
                and s[3] not in SKIP]
        if not cand:
            problems.append(f'no site: {where} {dec!r}')
            continue
        best = max(len(s[3]) for s in cand)
        cand = [s for s in cand if len(s[3]) == best]
        english = cand[0][3].encode('latin-1')
        i = dec.index(english)
        head, tail = dec[:i], dec[i + len(english):]
        if not (kit.startswith(head) and kit.endswith(tail)):
            problems.append(f'shape: {where} {dec!r} -> {kit!r}')
            continue
        own = kit[len(head):len(kit) - len(tail)]
        key = (cand[0][3], own)
        if key not in macros:
            macros[key] = [f'RU{len(macros) + 1:03d}', False]
        # every place of that text takes it (a place may sit in a block the
        # build does not assemble); a place another text took stays so
        for s in cand:
            if edits.setdefault((s[0], s[1]), macros[key][0]) != macros[key][0]:
                problems.append(f'two texts for one place: {s[0]}:{s[1] + 1} {dec!r}')
            elif s[2] in COUNTED:
                macros[key][1] = True
    for module, (files, title) in MODULES.items():
        used = {m for (f, _), m in edits.items() if f in files}
        write_module(out_d / f'{module}.MAC', module, title,
                     {k: v for k, v in macros.items() if v[0] in used})
    rewrite(work, texts, all_sites, edits)
    print(f'{len(macros)} macros, {len(edits)} sites')
    for p in problems:
        print(p)


def write_module(out_p: Path, module, title, macros):
    rows = [f'.SBTTL\t{module}\tOSA\'s texts in Russian: {title}', ';',
            '; Generated by kitmsg.py from the kit\'s monitor: one macro for each of',
            '; DEC\'s texts the kit has its own of.  See OMRUS.MAC.', ';']
    for (english, own), (name, counted) in macros.items():
        rows.append(f'.MACRO\t{name}\tLEN\t\t;{english}'[:120])
        if counted:
            rows.append('.IF B\tLEN')
        rows += mac_lines(own)
        if counted:
            rows.append('.IFF')
            rows.append(f'LEN\t= {len(own)}.')
            rows.append('.ENDC')
        rows.append(f'.ENDM\t{name}')
        rows.append('')
    out_p.write_text('\n'.join(rows), encoding='ascii')


def rewrite(work: Path, texts, all_sites, edits):
    kinds = {(s[0], s[1]): s[2] for s in all_sites}
    for (name, n), macro in edits.items():
        line = texts[name][n]
        kind = kinds[(name, n)]
        if kind in MACROS:
            c = CALL.match(line)
            args = c.group('args')
            bare = args.rstrip()
            texts[name][n] = c.group('head') + bare + f',RU={macro}' + args[len(bare):] + c.group('tail')
        else:
            t = TEXT.match(line)
            end = ender(kind, line)
            rows = ['.IF NE\tRU$ALL', f'{t.group("head")}{macro}\t\t\t;The kit\'s text (OMRUS.MAC)']
            if end:
                rows.append(f'\t.BYTE\t{end[0]:o}')
            rows += ['.IFF\t;NE RU$ALL', line, '.ENDC\t;NE RU$ALL']
            texts[name][n] = '\n'.join(rows)
    for module, (files, _) in MODULES.items():
        rows = texts[files[0]]
        at = next(i for i, r in enumerate(rows) if r.startswith('.MODULE'))
        rows.insert(at + 1, INCLUDE.format(module))
    for name, rows in texts.items():
        (work / name).write_text('\n'.join(rows), encoding='latin-1', newline='')


if __name__ == '__main__':
    main()
