"""dis2mac.py - a stretch of a monitor file as MACRO-11 source that
assembles back to the same words: code found by following the flow from
the entry points, the rest as data, a label on every word the code or the
data points at inside the stretch.

    python dis2mac.py FILE START END ENTRY[,ENTRY...] [--syms FILE] [--prefix P]

(octal).  File offset = link address, as in the RT11SJ link map.  A target
outside the stretch is written as a symbol from --syms (lines "ADDR NAME",
octal) when it has one, else as its octal address with a ";?" note - to
be named by hand.  Labels are P and the address (P defaults to "R").
"""
import struct
import sys
from pathlib import Path

REG = ['R0', 'R1', 'R2', 'R3', 'R4', 'R5', 'SP', 'PC']
SINGLE = {0o0050: 'CLR', 0o0051: 'COM', 0o0052: 'INC', 0o0053: 'DEC', 0o0054: 'NEG',
          0o0055: 'ADC', 0o0056: 'SBC', 0o0057: 'TST', 0o0060: 'ROR', 0o0061: 'ROL',
          0o0062: 'ASR', 0o0063: 'ASL', 0o0067: 'SXT', 0o1050: 'CLRB', 0o1051: 'COMB',
          0o1052: 'INCB', 0o1053: 'DECB', 0o1054: 'NEGB', 0o1055: 'ADCB', 0o1056: 'SBCB',
          0o1057: 'TSTB', 0o1060: 'RORB', 0o1061: 'ROLB', 0o1062: 'ASRB', 0o1063: 'ASLB',
          0o1064: 'MTPS', 0o1067: 'MFPS', 0o0003: 'SWAB'}
DOUBLE = {0o01: 'MOV', 0o02: 'CMP', 0o03: 'BIT', 0o04: 'BIC', 0o05: 'BIS', 0o06: 'ADD',
          0o11: 'MOVB', 0o12: 'CMPB', 0o13: 'BITB', 0o14: 'BICB', 0o15: 'BISB', 0o16: 'SUB'}
BRANCH = {0o000400: 'BR', 0o001000: 'BNE', 0o001400: 'BEQ', 0o002000: 'BGE', 0o002400: 'BLT',
          0o003000: 'BGT', 0o003400: 'BLE', 0o100000: 'BPL', 0o100400: 'BMI', 0o101000: 'BHI',
          0o101400: 'BLOS', 0o102000: 'BVC', 0o102400: 'BVS', 0o103000: 'BCC', 0o103400: 'BCS'}
ZERO = {0: 'HALT', 1: 'WAIT', 2: 'RTI', 3: 'BPT', 4: 'IOT', 5: 'RESET', 6: 'RTT', 0o240: 'NOP'}
FLAGS = {0o241: 'CLC', 0o242: 'CLV', 0o244: 'CLZ', 0o250: 'CLN', 0o257: 'CCC',
         0o261: 'SEC', 0o262: 'SEV', 0o264: 'SEZ', 0o270: 'SEN', 0o277: 'SCC'}


class Insn:
    """One instruction: its words, text pieces and the addresses it names."""

    def __init__(self, addr, words, mnem, ops, flow):
        self.addr, self.words, self.mnem, self.ops, self.flow = addr, words, mnem, ops, flow


def word(data, a):
    return struct.unpack_from('<H', data, a)[0] if 0 <= a and a + 2 <= len(data) else 0


def operand(data, addr, pos, mode, reg):
    """(piece, extra words, target) - piece has {T} where a target goes."""
    if mode == 0:
        return REG[reg], 0, None
    if mode == 1:
        return f'@{REG[reg]}', 0, None
    if mode == 2:
        if reg == 7:
            return ('#', word(data, pos), 'imm'), 1, None
        return f'({REG[reg]})+', 0, None
    if mode == 3:
        if reg == 7:
            return ('@#', word(data, pos), 'abs'), 1, None
        return f'@({REG[reg]})+', 0, None
    if mode == 4:
        return f'-({REG[reg]})', 0, None
    if mode == 5:
        return f'@-({REG[reg]})', 0, None
    x = word(data, pos)
    at = '@' if mode == 7 else ''
    if reg == 7:
        t = (pos + 2 + x) & 0o177777
        return (at, t, 'rel'), 1, t
    return f'{at}{oct(x)[2:]}({REG[reg]})', 1, None


def decode(data, a):
    op = word(data, a)
    pos = a + 2
    if op in ZERO:
        return Insn(a, 1, ZERO[op], [], 'stop' if op in (0, 2, 6) else 'next')
    if op in FLAGS:
        return Insn(a, 1, FLAGS[op], [], 'next')
    if op & 0o177770 == 0o200:
        return Insn(a, 1, 'RETURN' if op == 0o207 else 'RTS', [] if op == 0o207 else [REG[op & 7]], 'stop')
    if op & 0o177770 == 0o230:
        return Insn(a, 1, 'SPL', [str(op & 7)], 'next')
    br = op & 0o177400
    if br in BRANCH:
        off = op & 0o377
        off = off - 256 if off & 0o200 else off
        t = (pos + 2 * off) & 0o177777
        return Insn(a, 1, BRANCH[br], [('', t, 'br')], 'stop' if br == 0o400 else 'next')
    if op & 0o177000 == 0o077000:
        t = (pos - 2 * (op & 0o77)) & 0o177777
        return Insn(a, 1, 'SOB', [REG[(op >> 6) & 7], ('', t, 'br')], 'next')
    if op & 0o177400 in (0o104000, 0o104400):
        return Insn(a, 1, 'EMT' if op < 0o104400 else 'TRAP', [oct(op & 0o377)[2:]], 'next')
    if op & 0o177700 == 0o000100 and (op >> 3) & 7:
        o, n, _ = operand(data, a, pos, (op >> 3) & 7, op & 7)
        return Insn(a, 1 + n, 'JMP', [o], 'stop')
    if op & 0o177000 == 0o004000 and (op >> 3) & 7:
        o, n, _ = operand(data, a, pos, (op >> 3) & 7, op & 7)
        r = (op >> 6) & 7
        return Insn(a, 1 + n, 'CALL' if r == 7 else 'JSR', [o] if r == 7 else [REG[r], o], 'next')
    if op & 0o177000 == 0o074000:
        o, n, _ = operand(data, a, pos, (op >> 3) & 7, op & 7)
        return Insn(a, 1 + n, 'XOR', [REG[(op >> 6) & 7], o], 'next')
    top10 = (op >> 6) & 0o1777
    if top10 in SINGLE:
        o, n, _ = operand(data, a, pos, (op >> 3) & 7, op & 7)
        return Insn(a, 1 + n, SINGLE[top10], [o], 'next')
    top4 = (op >> 12) & 0o17
    if top4 in DOUBLE:
        s, n1, _ = operand(data, a, pos, (op >> 9) & 7, (op >> 6) & 7)
        d, n2, _ = operand(data, a, pos + 2 * n1, (op >> 3) & 7, op & 7)
        # MOV x,PC etc. end the flow
        flow = 'stop' if top4 == 0o01 and op & 0o77 == 0o07 else 'next'
        return Insn(a, 1 + n1 + n2, DOUBLE[top4], [s, d], flow)
    return None


def main():
    args = sys.argv[1:]
    syms, prefix = {}, 'R'
    if '--syms' in args:
        i = args.index('--syms')
        for line in Path(args[i + 1]).read_text().splitlines():
            if line.strip() and not line.startswith(';'):
                a, n = line.split()[:2]
                syms[int(a, 8)] = n
        del args[i:i + 2]
    if '--prefix' in args:
        i = args.index('--prefix')
        prefix = args[i + 1]
        del args[i:i + 2]
    data = Path(args[0]).read_bytes()
    lo, hi = int(args[1], 8), int(args[2], 8)
    entries = [int(x, 8) for x in args[3].split(',')]
    code, todo = {}, list(entries)
    while todo:
        a = todo.pop()
        while lo <= a < hi and a not in code:
            ins = decode(data, a)
            if ins is None:
                break
            code[a] = ins
            for o in ins.ops:
                if isinstance(o, tuple) and o[2] in ('br', 'rel') and ins.mnem in (
                        'BR', 'BNE', 'BEQ', 'BGE', 'BLT', 'BGT', 'BLE', 'BPL', 'BMI', 'BHI',
                        'BLOS', 'BVC', 'BVS', 'BCC', 'BCS', 'SOB', 'JMP', 'CALL', 'JSR') \
                        and not (o[0] == '@'):
                    todo.append(o[1])
            if ins.flow == 'stop':
                break
            a += 2 * ins.words
    labels = set(entries)
    for ins in code.values():
        for o in ins.ops:
            if isinstance(o, tuple) and o[2] in ('br', 'rel') and lo <= o[1] < hi:
                labels.add(o[1])
    # An instruction a label points into (code that rewrites its own
    # operands) is written as words, so the label can stand on its word.
    for a in [a for a, ins in code.items()
              if any(x in labels for x in range(a + 2, a + 2 * ins.words, 2))]:
        del code[a]
    covered = set()
    for a, ins in code.items():
        covered.update(range(a, a + 2 * ins.words, 2))
    # A branch into the middle of an instruction cannot be labelled.
    for t in sorted(labels):
        if t in covered and t not in code:
            print(f';? label {t:o} falls inside an instruction', file=sys.stderr)

    def name(t, kind):
        if lo <= t < hi:
            return f'{prefix}{t:05o}'
        if t in syms:
            return syms[t]
        return None

    out, notes = [], set()

    def render(o):
        if not isinstance(o, tuple):
            return o
        pre, t, kind = o
        if kind in ('imm', 'abs'):
            return f'{pre}{oct(t)[2:]}'
        n = name(t, kind)
        if n is None:
            notes.add(t)
            return f'{pre}{oct(t)[2:]}'
        return f'{pre}{n}'

    a = lo
    while a < hi:
        lab = f'{prefix}{a:05o}:' if a in labels else ''
        if a in code:
            ins = code[a]
            text = f'{ins.mnem}\t' + ','.join(render(o) for o in ins.ops) if ins.ops else ins.mnem
            ext = [o[1] for o in ins.ops if isinstance(o, tuple) and o[2] in ('br', 'rel')
                   and name(o[1], o[2]) is None]
            note = f'\t;? {" ".join(oct(t)[2:] for t in ext)}' if ext else ''
            out.append(f'{lab}\t{text}{note}')
            a += 2 * ins.words
        elif a in covered:
            a += 2
        else:
            w = word(data, a)
            out.append(f'{lab}\t.WORD\t{oct(w)[2:]}')
            a += 2
    print('\n'.join(out))
    if notes:
        print(';? outside the stretch, unnamed: ' + ' '.join(oct(t)[2:] for t in sorted(notes)),
              file=sys.stderr)


main()
