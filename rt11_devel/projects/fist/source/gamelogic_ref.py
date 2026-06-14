"""Reference reproductions of WotEF game-logic routines (path A), validated
against the Z80 simulator by before/after memory comparison.

Game-logic routines read/write the fighter/game state (mostly $9C2x, $AA..,
$B0..), so the validation harness captures memory at a routine's entry, runs
the Python reproduction on a copy, and checks the watched state cells match
the sim's memory after the real routine returns - the same method that proved
the sprite decoder, generalized to whole subroutines.

First port: the round timer $9C6F (+ Time_Tick $9CA0).  Every frame it ticks
a 13-frame divider ($9CA6); each expiry decrements the round time ($9CA5) and,
on zero, raises the timeout flag ($9C2B).  ($9C93 Print_Time draws the digits;
the drawing is not part of the state logic checked here.)
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from trace_sprites import build_sim, PC                      # noqa: E402

SP = 12


# ── routine reproductions ─────────────────────────────────────────────────────

def _u16(m, a):
    return m[a] | (m[(a + 1) & 0xFFFF] << 8)


def apply_hit(m, A):
    """$9E7F / $A01C: apply a connected hit - set the opponent's reaction
    action A['react'] (stagger/knockdown) and the hit value $B150."""
    d = m[A['act']]
    react = A['react']
    m[0xAA3F] = d
    m[0xB150] = m[(0xA073 + d) & 0xFFFF]
    type_nz = m[(0xB47E + d) & 0xFFFF] != 0
    if m[A['aface']] == m[A['tface']]:            # same facing
        m[react] = 0x16 if type_nz else 0x1A
    elif type_nz:                                 # facing differ, type != 0
        m[react] = 0x1A
    elif d in (0x18, 0x07, 0x0C):                 # heavy actions
        m[react] = 0x1B
        m[0xB150] = 0x04
    else:
        m[react] = 0x16


# Address sets for the two symmetric hit-detection routines: $9D29 (player 1
# attacks, sets $AA08, applies via $9E7F) and $9ED2 (player 2, $AA48, $A01C).
HIT_P1 = dict(act=0xAA04, g1=0xAA13, g2=0xAA16, g3=0xAA09, fg=0xAA12,
              aface=0xAA17, tface=0xAA57, ridx=0xAA52, result=0xAA08,
              setpos=(0xAA19, 0xAA59), react=0xAA43)
HIT_P2 = dict(act=0xAA44, g1=0xAA53, g2=0xAA56, g3=0xAA49, fg=0xAA52,
              aface=0xAA57, tface=0xAA17, ridx=0xAA12, result=0xAA48,
              setpos=None, react=0xAA03)


def hit_detect(m, A):
    """$9D29 / $9ED2: does this fighter's attack reach the opponent this frame?
    Sets A['result'] (2/1) on a hit; the apply is a separate tail-call."""
    if A['setpos']:                              # $9D29 latches both x-positions
        m[0xA071], m[0xA072] = m[A['setpos'][0]], m[A['setpos'][1]]
    d = m[A['act']]
    if m[(0xA90D + d) & 0xFFFF] == 0:
        return
    if m[A['g1']] == 0 or m[A['g2']] != 0 or m[A['g3']] != 0:
        return
    if m[A['fg']] != m[(0xA971 + d) & 0xFFFF]:
        return
    tbl = 0xA9BC if m[A['aface']] == m[A['tface']] else 0xA98A
    paddr = (tbl + ((d * 2) & 0xFF)) & 0xFFFF
    m[0xA06F], m[0xA070] = m[paddr], m[(paddr + 1) & 0xFFFF]
    reach = m[(_u16(m, 0xA06F) + m[A['ridx']]) & 0xFFFF]
    if reach == 0x80:
        return
    e = (reach + 0x80) & 0xFF
    if m[A['aface']] != 0:
        dist = (m[0xA071] - m[0xA072]) & 0xFF
    else:
        dist = (m[0xA072] - m[0xA071]) & 0xFF
    c = (dist + 0x80) & 0xFF
    a93f = m[(0xA93F + d) & 0xFFFF]
    a958 = m[(0xA958 + d) & 0xFFFF]
    res = A['result']
    if m[(0xB47E + d) & 0xFFFF] != 0:
        if c == e:
            m[res] = 2
        elif c < e:
            return
        elif ((c - a93f) & 0xFF) < e:
            m[res] = 2
        elif ((c - a958) & 0xFF) < e:
            m[res] = 1
    else:
        if c == e:
            m[res] = 2
        elif c < e:
            if ((a93f + c) & 0xFF) >= e:
                m[res] = 2
            elif ((a958 + c) & 0xFF) >= e:
                m[res] = 1


def update_timer(m):
    """$9C6F: round-timer tick (state only; skips the Print_Time draw)."""
    if m[0x9C2B] != 0:
        return
    if (m[0xAA03] | m[0xAA43]) != 0:
        return
    if m[0xAA04] == 0x17:
        return
    m[0x9CA6] = (m[0x9CA6] - 1) & 0xFF
    if m[0x9CA6] != 0:
        return
    m[0x9CA6] = 0x0D
    m[0x9CA5] = (m[0x9CA5] - 1) & 0xFF
    if m[0x9CA5] == 0:
        m[0x9C2B] = 1


# ── validation harness ────────────────────────────────────────────────────────

def validate(addr, pyfunc, watch, want=200, stops=(), budget=4000000):
    """Run the game; for each call to `addr`, run the real routine until it
    RETs (or reaches a PC in `stops`, e.g. a tail-call), run pyfunc on a copy
    of the entry memory, and compare the `watch` cells."""
    sim, mem = build_sim(watch=(0, 0))
    regs, memory, ops = sim.registers, sim.memory, sim.opcodes
    fd, ia = sim.frame_duration, sim.int_active
    stops = set(stops)
    tested = match = 0
    for _ in range(budget):
        if regs[PC] == addr:
            s0 = regs[SP]
            ret = memory[s0] | (memory[s0 + 1] << 8)
            before = bytes(memory)
            for _ in range(200000):
                ops[memory[regs[PC]]]()
                if regs[26] and regs[25] % fd < ia:
                    sim.accept_interrupt(regs, memory, regs[PC])
                if (regs[PC] == ret and regs[SP] == s0 + 2) or regs[PC] in stops:
                    break
            mine = bytearray(before)
            pyfunc(mine)
            tested += 1
            if all(mine[a] == memory[a] for a in watch):
                match += 1
            elif tested - match <= 3:
                bad = [(hex(a), memory[a], mine[a]) for a in watch
                       if mine[a] != memory[a]]
                print(f"  MISMATCH: {bad}")
            if tested >= want:
                break
            continue
        ops[memory[regs[PC]]]()
        if regs[26] and regs[25] % fd < ia:
            sim.accept_interrupt(regs, memory, regs[PC])
    return match, tested


def main():
    m, t = validate(0x9C6F, update_timer, [0x9CA6, 0x9CA5, 0x9C2B])
    print(f"$9C6F round timer:    {m}/{t} calls match")
    m2, t2 = validate(0x9D29, lambda mm: hit_detect(mm, HIT_P1),
                      [0xAA08, 0xA06F, 0xA070, 0xA071, 0xA072], stops=[0x9E7F])
    print(f"$9D29 hit-detect P1:  {m2}/{t2} calls match")
    m3, t3 = validate(0x9ED2, lambda mm: hit_detect(mm, HIT_P2),
                      [0xAA48, 0xA06F, 0xA070], stops=[0xA01C])
    print(f"$9ED2 hit-detect P2:  {m3}/{t3} calls match")
    m4, t4 = validate(0x9E7F, lambda mm: apply_hit(mm, HIT_P1),
                      [0xAA3F, 0xB150, 0xAA43])
    print(f"$9E7F apply-hit P1:   {m4}/{t4} calls match")
    m5, t5 = validate(0xA01C, lambda mm: apply_hit(mm, HIT_P2),
                      [0xAA3F, 0xB150, 0xAA03])
    print(f"$A01C apply-hit P2:   {m5}/{t5} calls match")
    return all(a == b for a, b in
               ((m, t), (m2, t2), (m3, t3), (m4, t4), (m5, t5)))


if __name__ == "__main__":
    main()
