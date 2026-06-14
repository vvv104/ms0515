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
    return


BUFLO, BUFHI = 0xF730, 0xFAA4                         # the $F730 compose buffer


def main(n_elements=400):
    """Self-test: drive the real game in the Z80 sim and check decode_element
    reproduces every $8A30 element's writes to the compose buffer."""
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from trace_sprites import build_sim, PC as PCi

    sim, _ = build_sim(watch=(0, 0))
    regs, memory, ops = sim.registers, sim.memory, sim.opcodes
    fd, ia = sim.frame_duration, sim.int_active
    done = {0x8833, 0x8AD0}
    tested = match = mismatch = 0
    for _ in range(2000000):
        if regs[PCi] == 0x8A30:
            before = bytes(memory)
            rin = list(regs)
            for _ in range(3000):                    # run the real element
                ops[memory[regs[PCi]]]()
                if regs[26] and regs[25] % fd < ia:
                    sim.accept_interrupt(regs, memory, regs[PCi])
                if regs[PCi] in done:
                    break
            mine = bytearray(before)
            decode_element(mine, rin)
            if any(mine[i] != memory[i] for i in range(BUFLO, BUFHI)):
                mismatch += 1
            else:
                match += 1
            tested += 1
            if tested >= n_elements:
                break
            continue
        ops[memory[regs[PCi]]]()
        if regs[26] and regs[25] % fd < ia:
            sim.accept_interrupt(regs, memory, regs[PCi])
    print(f"$8A30 decoder reference: {match}/{tested} elements match, "
          f"{mismatch} mismatch")
    return mismatch == 0


if __name__ == "__main__":
    main()
