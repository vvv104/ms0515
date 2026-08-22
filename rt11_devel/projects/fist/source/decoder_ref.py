"""Reference reproduction of WotEF's sprite decoder $8A30 (path A, step 2).

A faithful instruction-by-instruction translation of the Z80 routine $8A30
(the pre-shifted masked sprite blit), validated against ground-truth
input/output captured from the Z80 simulator (el_in.bin / el_out.bin /
el_regs.json in WOTEF_DIR).  Once this matches, it transcribes to MACRO-11.

$8A30 executes from entry until it reaches the loop-control $8833 or the
terminator $8AD0; this function mirrors that, mutating `mem` in place.
"""
from pathlib import Path

# register-list indices (skoolkit.simutils)
A, F, B, C, D, E, H, L = 0, 1, 2, 3, 4, 5, 6, 7
xA, xF, xB, xC, xD, xE, xH, xL = 16, 17, 18, 19, 20, 21, 22, 23


class Z80:
    """Just enough Z80 state for the $8A30 decoder."""

    def __init__(self, mem, regs):
        self.m = mem
        self.a, self.f = regs[A], regs[F]
        self.b, self.c, self.d, self.e, self.h, self.l = (
            regs[B], regs[C], regs[D], regs[E], regs[H], regs[L])
        self.a2, self.f2 = regs[xA], regs[xF]
        self.b2, self.c2, self.d2, self.e2, self.h2, self.l2 = (
            regs[xB], regs[xC], regs[xD], regs[xE], regs[xH], regs[xL])

    # flag helpers (bit6 = Z, bit0 = C)
    @property
    def fz(self): return (self.f >> 6) & 1
    @property
    def fc(self): return self.f & 1
    def set_z(self, z): self.f = (self.f & ~0x40) | (0x40 if z else 0)
    def set_c(self, cc): self.f = (self.f & ~0x01) | (1 if cc else 0)

    def hl(self): return (self.h << 8) | self.l
    def de(self): return (self.d << 8) | self.e
    def bc(self): return (self.b << 8) | self.c
    def set_hl(self, v): self.h, self.l = (v >> 8) & 0xFF, v & 0xFF
    def set_de(self, v): self.d, self.e = (v >> 8) & 0xFF, v & 0xFF
    def set_bc(self, v): self.b, self.c = (v >> 8) & 0xFF, v & 0xFF

    def rla(self):
        nc = self.a >> 7
        self.a = ((self.a << 1) | self.fc) & 0xFF
        self.set_c(nc)
    def rra(self):
        nc = self.a & 1
        self.a = ((self.a >> 1) | (self.fc << 7)) & 0xFF
        self.set_c(nc)
    def ex_af(self):
        self.a, self.a2 = self.a2, self.a
        self.f, self.f2 = self.f2, self.f
    def exx(self):
        self.b, self.b2 = self.b2, self.b
        self.c, self.c2 = self.c2, self.c
        self.d, self.d2 = self.d2, self.d
        self.e, self.e2 = self.e2, self.e
        self.h, self.h2 = self.h2, self.h
        self.l, self.l2 = self.l2, self.l


def decode_element(mem, regs):
    z = Z80(mem, regs)
    m = z.m

    z.a = m[0x8B1C]; z.b = z.a                       # 8A30-33
    z.a = m[z.hl()]                                  # 8A34
    z.rla(); z.rla()                                 # 8A35-36
    if z.fc:                                         # 8A37 JP NC,$8A4B (carry set: fall through)
        saved_bc = z.bc()                            # 8A3A PUSH BC
        z.rla(); z.rla(); z.rla(); z.rla()           # 8A3B-3E (keep rotating same A)
        z.a &= 0x07                                  # 8A3F
        z.c = z.a; z.b = 0                           # 8A41-42
        z.set_de((z.de() + z.c) & 0xFFFF)            # 8A44-46 EX DE,HL; ADD HL,BC; EX DE,HL
        z.set_bc(saved_bc)                           # 8A47 POP BC
        z.a = m[z.hl()]; z.rla(); z.rla()            # 8A48-4A
    # 8A4B
    z.rra(); z.rra(); z.a &= 0x07                    # 8A4B-4D
    z.set_z(z.a == 0)
    if z.a == 0:                                     # 8A4F JP Z,$8AC6
        return _reload(z, m)
    z.b = z.a                                        # 8A52
    if m[0x8B0A] != 0:                               # 8A53-5A
        z.b = (z.b + 1) & 0xFF
    z.set_hl((z.hl() + 1) & 0xFFFF)                  # 8A5B
    z.b = (z.b - 1) & 0xFF                           # 8A5C
    if m[0x8AFF] != 0:                               # 8A5D-64
        z.b = (z.b - 1) & 0xFF
    if m[0x8AFE] != 0:                               # 8A65-6E
        z.set_de((z.de() + 1) & 0xFFFF)
        z.set_hl((z.hl() + 1) & 0xFFFF)
        z.b = (z.b - 1) & 0xFF
    if z.b == 0:                                     # 8A6F-71 JP Z,$8AA5
        return _single(z, m)
    # 8A74-84 leading byte
    z.a = m[z.hl()]; z.ex_af()                       # 8A74-75
    z.a = m[z.de()]; z.ex_af()                       # 8A76-77 -> A=src, A'=dest
    z.exx(); z.l = z.a; z.h = 0xB5; z.l = m[z.hl()]  # 8A78-7C: L=B500[src]
    z.ex_af()                                        # 8A7D -> A=dest
    z.a &= z.l                                       # 8A7E AND L
    z.exx()                                          # 8A7F
    z.a |= m[z.hl()]                                 # 8A80 OR (HL)
    m[z.de()] = z.a                                  # 8A81
    z.set_de((z.de() + 1) & 0xFFFF)                  # 8A82
    z.set_hl((z.hl() + 1) & 0xFFFF)                  # 8A83
    z.b = (z.b - 1) & 0xFF                           # 8A84
    if z.b != 0:                                     # 8A85 JP Z,$8A8D
        z.c = z.b; z.b = 0                           # 8A88-89
        for _ in range(z.bc()):                      # 8A8B LDIR
            m[z.de()] = m[z.hl()]
            z.set_de((z.de() + 1) & 0xFFFF)
            z.set_hl((z.hl() + 1) & 0xFFFF)
        z.set_bc(0)
    # 8A8D-9A trailing byte via $B600
    z.a = m[z.hl()]; z.ex_af()                       # 8A8D-8E
    z.a = m[z.de()]; z.ex_af()                       # 8A8F-90
    z.exx(); z.l = z.a; z.h = 0xB6; z.l = m[z.hl()]  # 8A91-95: L=B600[src]
    z.ex_af()                                        # 8A96 -> A=dest
    z.a &= z.l                                        # 8A97
    z.exx()                                          # 8A98
    z.a |= m[z.hl()]                                  # 8A99
    m[z.de()] = z.a                                   # 8A9A
    return _reload(z, m)


def _single(z, m):                                   # $8AA5 single-byte case
    z.a = m[z.hl()]; z.ex_af()                        # 8AA5-A6
    z.a = m[z.de()]; z.ex_af()                        # 8AA7-A8 -> A=src,A'=dest
    z.exx(); z.l = z.a; z.h = 0xB5; z.c = m[z.hl()]   # 8AA9-AD: C=B500[src]
    z.h = 0xB6; z.l = m[z.hl()]                       # 8AAE-B0: L=B600[src]
    z.ex_af()                                         # 8AB1 -> A=dest
    push_a = z.a                                      # 8AB2 PUSH AF
    z.a = z.l                                         # 8AB3
    z.a |= z.c                                        # 8AB4 OR C
    z.l = z.a                                         # 8AB5
    z.a = push_a                                      # 8AB6 POP AF
    z.a &= z.l                                        # 8AB7 AND L
    z.exx()                                           # 8AB8
    z.a |= m[z.hl()]                                  # 8AB9 OR (HL)
    m[z.de()] = z.a                                   # 8ABA
    z.ex_af()                                         # 8ABB
    return _reload(z, m)


def _reload(z, m):                                   # $8A9B / $8AC6: reload ptrs
    z.set_hl(m[0x8B14] | (m[0x8B15] << 8))
    z.set_de(m[0x8B16] | (m[0x8B17] << 8))
    return (z.hl(), z.de())


# ── $8833 loop control: stage one element's work data into $8B00.. ────────────

def _transform(m, a):                                # $8AD1
    return m[((0xB800 if m[0x8AF3] else 0xB900) + a) & 0xFFFF]


def _rrd(m, hl, a):                                  # Z80 RRD on (HL)
    v = m[hl]; lo = a & 0x0F
    a = (a & 0xF0) | (v & 0x0F)
    m[hl] = (((v >> 4) & 0x0F) | (lo << 4)) & 0xFF
    return a


def _rotpass(m, p, n, left):                         # RRA/RLA chain, carry starts 0
    carry = 0
    for _ in range(n):
        v = m[p]
        if left:
            nc = (v >> 7) & 1; m[p] = ((v << 1) | carry) & 0xFF; p = (p - 1) & 0xFFFF
        else:
            nc = v & 1; m[p] = ((v >> 1) | (carry << 7)) & 0xFF; p = (p + 1) & 0xFFFF
        carry = nc


def _mode_bit0(m, hl):                               # $88AD: $B700 mirror-x prefix
    d = 0x8B0B
    c = m[hl] & 7
    b = (m[hl] >> 3) & 7
    a = ((m[0x8B1B] - b - c) << 3) & 0xFF
    a |= 0x40 if a != 0 else 0
    a |= 0x80
    m[d] = (m[hl] & 7) | a; d = (d + 1) & 0xFFFF
    c2 = m[hl] & 7
    hl = (hl + 1) & 0xFFFF
    d = (d + c2 - 1) & 0xFFFF
    for _ in range(c2):
        m[d] = m[(0xB700 + m[hl]) & 0xFFFF]
        d = (d - 1) & 0xFFFF; hl = (hl + 1) & 0xFFFF
    return 0x8B0B


def _mode_scale(m, hl, thresh, lead0, left):         # bit1/bit2/bit3 share this shape
    a = m[hl]; m[0x8B01] = a; hl = (hl + 1) & 0xFFFF
    a &= 7
    if a == 0:
        return 0x8B01
    m[0x8B00] = a
    dst = 0x8B02
    if lead0:                                        # bit3 writes a leading 0
        m[0x8B02] = 0; dst = 0x8B03
    if m[hl] < thresh:
        m[0x8AFE] = 1
    if a == 0:
        return 0x8B01
    s = hl
    for _ in range(a):
        m[dst] = m[s]; dst = (dst + 1) & 0xFFFF; s = (s + 1) & 0xFFFF
    if not lead0:
        m[dst] = 0
    n = (m[0x8B00] + 1) & 0xFF
    if thresh == 0x10:                               # bit2: RRD nibble loop fwd
        a = 0; p = 0x8B02
        for _ in range(n):
            a = _rrd(m, p, a); p = (p + 1) & 0xFFFF
        last = (0x8B02 + n - 1) & 0xFFFF
    elif left:                                       # bit3: RLA twice, backwards
        p = (0x8B02 + m[0x8B00]) & 0xFFFF
        _rotpass(m, p, n, True); _rotpass(m, p, n, True)
        last = p
    else:                                            # bit1: RRA twice, forwards
        _rotpass(m, 0x8B02, n, False); _rotpass(m, 0x8B02, n, False)
        last = (0x8B02 + n - 1) & 0xFFFF
    if m[last] == 0:
        m[0x8AFF] = 1
    return 0x8B01


def stage_element(m, hl, de):
    """$8833: stage one element; return HL for $8A30, or None at terminator."""
    m[0x8AF3] ^= 1
    m[0x8AFF] = 0
    m[0x8AFE] = 0
    if m[hl] == 0:
        return None                                  # $8AD0
    t = (m[0xC408] + de) & 0xFFFF
    m[0x8B16], m[0x8B17] = t & 0xFF, t >> 8
    ctrl = m[hl]
    if ctrl & 0x80:                                  # $8875
        t = (hl + (ctrl & 7) + 1) & 0xFFFF
        m[0x8B14], m[0x8B15] = t & 0xFF, t >> 8
    else:                                            # $8858
        c = m[0x8B1B]
        t = (hl + c) & 0xFFFF
        m[0x8B14], m[0x8B15] = t & 0xFF, t >> 8
        d = 0x8AE9
        m[d] = (c | 0x80) & 0xFF; d += 1
        s = hl
        for _ in range(c):
            m[d] = m[s]; d = (d + 1) & 0xFFFF; s = (s + 1) & 0xFFFF
        hl = 0x8AE9
    if m[0xC407] != 0:                               # $8882 facing mirror
        d = 0x8AF4
        a = m[hl]; m[d] = a; d += 1; hl = (hl + 1) & 0xFFFF
        for _ in range(a & 7):
            m[d] = _transform(m, m[hl]); d = (d + 1) & 0xFFFF; hl = (hl + 1) & 0xFFFF
        hl = 0x8AF4
    c40e = m[0xC40E]
    if c40e & 0x01:
        hl = _mode_bit0(m, hl)
    if c40e & 0x02:
        return _mode_scale(m, hl, 0x04, False, False)
    if c40e & 0x04:
        return _mode_scale(m, hl, 0x10, False, False)
    if c40e & 0x08:
        return _mode_scale(m, hl, 0x40, True, True)
    return hl


BUFLO, BUFHI = 0xF730, 0xFAA4                         # the $F730 compose buffer


def _mkregs(hl, de):
    r = [0] * 30
    r[H], r[L], r[D], r[E] = hl >> 8, hl & 0xFF, de >> 8, de & 0xFF
    return r


def run_loop(m, hl, de, limit=5000):
    """Reproduce the full $8833 -> $8A30 element loop for one fighter, until
    the terminator.  Composes the fighter into the $F730 buffer in `m`."""
    for _ in range(limit):
        hl8a30 = stage_element(m, hl, de)
        if hl8a30 is None:
            return
        hl, de = decode_element(m, _mkregs(hl8a30, de))


def main():
    """Self-test: drive the real game in the Z80 sim and check (1) decode_element
    ($8A30) reproduces each element's compose-buffer writes, (2) stage_element
    ($8833) reproduces each element's staging, (3) run_loop reproduces whole
    fighter composes ($F730) end to end."""
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from trace_sprites import build_sim, PC as PCi

    sim, _ = build_sim(watch=(0, 0))
    regs, memory, ops = sim.registers, sim.memory, sim.opcodes
    fd, ia = sim.frame_duration, sim.int_active

    def step():
        ops[memory[regs[PCi]]]()
        if regs[26] and regs[25] % fd < ia:
            sim.accept_interrupt(regs, memory, regs[PCi])

    el30 = el83 = nel = loops = lok = seen = 0
    while nel < 400 or loops < 15:
        pc = regs[PCi]
        if pc == 0x8A30 and nel < 400:
            before, rin = bytes(memory), list(regs)
            for _ in range(3000):
                step()
                if regs[PCi] in (0x8833, 0x8AD0):
                    break
            mine = bytearray(before)
            decode_element(mine, rin)
            el30 += all(mine[i] == memory[i] for i in range(BUFLO, BUFHI))
            nel += 1
            continue
        if pc == 0x8833:
            seen += 1
            before = bytes(memory)
            hl, de = regs[H] * 256 + regs[L], regs[D] * 256 + regs[E]
            if nel < 400:
                for _ in range(4000):
                    step()
                    if regs[PCi] in (0x8A30, 0x8AD0):
                        break
                if regs[PCi] == 0x8A30:
                    mine = bytearray(before)
                    if stage_element(mine, hl, de) == regs[H] * 256 + regs[L] and \
                       all(mine[i] == memory[i] for i in range(0x8AE0, 0x8B20)):
                        el83 += 1
                continue
            if loops < 15 and seen % 37 == 0:
                for _ in range(200000):
                    step()
                    if regs[PCi] == 0x8AD0:
                        break
                sim_f = bytes(memory[BUFLO:BUFHI])
                mine = bytearray(before)
                run_loop(mine, hl, de)
                lok += (bytes(mine[BUFLO:BUFHI]) == sim_f)
                loops += 1
                continue
        step()
    print(f"$8A30 elements: {el30}/{nel} | $8833 staging: {el83} ok | "
          f"full fighter composes: {lok}/{loops}")
    return el30 == nel and lok == loops


if __name__ == "__main__":
    main()
