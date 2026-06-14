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


def _f(m, base, off):
    return m[(base + off) & 0xFFFF]


def range_9ba7(m, C, Q):
    """$9BA7: is the opponent within striking distance / valid facing for the
    pending move?  Writes the signed gap to $9C2D and returns 0 (no) / 1 (yes).
    D/E here are local (reloaded from $AA19); the chain PUSHes DE around it."""
    d = _f(m, 0xAA19, C)
    e = _f(m, 0xAA19, Q)
    same_face = m[0xAA57] == m[0xAA17]
    v = (d - e) & 0xFF if _f(m, 0xAA17, C) != 0 else (e - d) & 0xFF
    m[0x9C2D] = v
    t = _f(m, 0xB47E, _f(m, 0xAA04, Q))
    if same_face:                                # $9BF5
        if t == 0:
            return 0
        return 1 if 0x03 <= v < 0x10 else 0
    # facing differ ($9BC0)
    if t != 0:
        return 0
    return 1 if (v >= 0xEF or v < 0x16) else 0


def anim_9920(m, C, Q, D, E):
    """$9920: launch / recover a move once its wind-up frame ($AA0x==3) clears
    and the opponent is in range (via $9BA7); else hold."""
    if E == 0x03:
        if D in (0x13, 0x14):                    # $992E: E := D, hold
            return D, D
        if _f(m, 0xAA16, Q) != 0:
            return D, E
        if _f(m, 0xAA09, Q) != 0:
            return D, E
        a = _f(m, 0xAA04, Q)
        if a == 0x10 or a == 0x0A:
            return D, E
        if _f(m, 0xA90D, a) == 0:
            return D, E
        if range_9ba7(m, C, Q) == 0:             # PUSH/POP preserves D,E
            return D, E
        E = _f(m, 0xA926, _f(m, 0xAA04, Q))      # $9964: new sub-state
        m[(0xAA09 + C) & 0xFFFF] = 0
        m[(0xAA16 + Q) & 0xFFFF] = 0
        return D, E
    # E != 3 ($9986)
    if E != 0x07:
        return D, E
    if D in (0x04, 0x07):
        return D, E
    return D, 0x18


def anim_9994(m, C, Q, D, E):
    """$9994: drive the action D from the current sub-state E - knock-downs,
    transitions, and the once-per-fall hit-credit tick ($9CA7/$B150)."""
    if E in (0x1A, 0x1B, 0x16):                  # $99A1: knock-down landed
        D = E
        m[(0xAA18 + C) & 0xFFFF] = 0x7A
        m[(0xAA16 + C) & 0xFFFF] = 0x00
        m[(0xAA09 + C) & 0xFFFF] = 0x00
        if _f(m, 0xAA13, C) == 0:
            return D, E
        a = _f(m, 0xAA12, C)
        if a != 0x2C and a != 0x28:
            return D, E
        if m[0x9CA7] != 0:
            return D, E
        m[0x9CA7] = 0x01
        m[0xB150] = 0x05
        return D, E
    # $99DD
    if D == E:
        return D, E
    if D == 0x01:                                # $99E4
        if E == 0x11:
            return 0x12, E
        if E in (0x07, 0x10, 0x0A):
            return 0x04, E
        return E, E
    # $99FD (D != 1)
    if E in (0x07, 0x10, 0x0A):                  # $9A0A
        if D == 0x04:
            if _f(m, 0xAA09, C) != 0x01:
                return D, E
            return E, E
        if D == 0x12:
            m[(0xAA16 + C) & 0xFFFF] = 0x01
            return D, E
        return D, E
    # $9A27
    if D == 0x12 and E == 0x11:
        if _f(m, 0xAA09, C) != 0x01:
            return D, E
        return E, E
    if D == 0x11 and _f(m, 0xAA09, C) == 0x01:   # $9A4D
        m[(0xAA16 + C) & 0xFFFF] = 0x01
        m[(0xAA07 + C) & 0xFFFF] = 0x00
        m[(0xAA0B + C) & 0xFFFF] = 0x00
        m[(0xAA09 + C) & 0xFFFF] = 0x00
        return 0x15, E
    t = _f(m, 0xB462, D)                          # $9A70
    if t == 0x80:
        m[(0xAA09 + C) & 0xFFFF] = 0x00
        if E == 0x11:
            return 0x12, E
        return E, E
    if t != 0:                                   # $9A8D
        m[(0xAA09 + C) & 0xFFFF] = 0x00
        return D, E
    m[(0xAA16 + C) & 0xFFFF] = 0x01
    m[(0xAA09 + C) & 0xFFFF] = 0x00
    return D, E


def anim_9aa1(m, C, Q, D, E):
    """$9AA1: commit the chosen action D into the animation slot ($AA0B/$AA0C),
    resetting the frame counter when the action changes."""
    a = _f(m, 0xAA0B, C)
    if a != D:
        m[(0xAA0C + C) & 0xFFFF] = D
        m[(0xAA0B + C) & 0xFFFF] = 0x00
        m[(0xAA09 + C) & 0xFFFF] = 0x00
    elif _f(m, 0xAA09, C) == 0x01:
        m[(0xAA0C + C) & 0xFFFF] = 0x00
    else:
        m[(0xAA0C + C) & 0xFFFF] = _f(m, 0xAA0B, C)
    return D, E


def update_fighter(m, C):
    """$97BB: per-fighter animation update.  Loads (D,E)=($AA04+C,$AA05+C),
    threads it through the three state machines, stores it back ($9B9D)."""
    Q = m[0x9C29]
    D = _f(m, 0xAA04, C)
    E = _f(m, 0xAA05, C)
    D, E = anim_9920(m, C, Q, D, E)
    D, E = anim_9994(m, C, Q, D, E)
    D, E = anim_9aa1(m, C, Q, D, E)
    m[(0xAA04 + C) & 0xFFFF] = D & 0xFF
    m[(0xAA05 + C) & 0xFFFF] = E & 0xFF


def recover_9ad7(m, C):
    """$9AD7: second per-fighter pass - get-up / recovery state reset, applied
    when the move-pending flag $AA0D+C is set.  Loads (D,E) ($9B93), and the
    orchestrator stores the result back ($9B9D)."""
    D = _f(m, 0xAA04, C)
    E = _f(m, 0xAA05, C)
    # D in (2,3) does a discarded compare ($AA36+C-$AA19+C); no state change.
    if _f(m, 0xAA0D, C) != 0:
        m[(0xAA0D + C) & 0xFFFF] = 0
        a03 = _f(m, 0xAA03, C)
        if a03 != 0:                             # $9B09: queued reaction
            E = a03
            m[0x9C28] = a03
        elif _f(m, 0xAA16, C) != 0:              # $9B0E
            if D == 0x11:
                m[(0xAA17 + C) & 0xFFFF] ^= 0x01
            m[(0xAA07 + C) & 0xFFFF] = 0
            m[(0xAA09 + C) & 0xFFFF] = 0
            m[(0xAA0B + C) & 0xFFFF] = 0
            m[(0xAA0C + C) & 0xFFFF] = 0x01
            m[(0xAA16 + C) & 0xFFFF] = 0
            D = 0x01
        elif _f(m, 0xB462, D) != 0:              # $9B67
            m[(0xAA00 + C) & 0xFFFF] = 0x01
            m[(0xAA07 + C) & 0xFFFF] = 0
            m[(0xAA0B + C) & 0xFFFF] = 0
            D = 0x01
        else:                                    # $9B8A
            m[(0xAA09 + C) & 0xFFFF] = 0x01
    m[(0xAA04 + C) & 0xFFFF] = D & 0xFF           # $9B9D store (orchestrator)
    m[(0xAA05 + C) & 0xFFFF] = E & 0xFF


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

def validate(addr, pyfunc, watch, want=200, stops=(), budget=4000000, until=()):
    """Run the game; for each call to `addr`, run the real routine until it
    RETs (or reaches a PC in `stops`, e.g. a tail-call), run pyfunc on a copy
    of the entry memory, and compare the `watch` cells.  `until` overrides the
    RET stop with an explicit set of PCs - used to run past a trailing helper
    call (e.g. the orchestrator's $9B9D store after $9AD7)."""
    sim, mem = build_sim(watch=(0, 0))
    regs, memory, ops = sim.registers, sim.memory, sim.opcodes
    fd, ia = sim.frame_duration, sim.int_active
    stops = set(stops)
    until = set(until)
    tested = match = 0
    for _ in range(budget):
        if regs[PC] == addr:
            s0 = regs[SP]
            ret = memory[s0] | (memory[s0 + 1] << 8)
            entry = {'A': regs[0], 'B': regs[2], 'C': regs[3], 'D': regs[4],
                     'E': regs[5], 'H': regs[6], 'L': regs[7]}
            before = bytes(memory)
            for _ in range(200000):
                ops[memory[regs[PC]]]()
                if regs[26] and regs[25] % fd < ia:
                    sim.accept_interrupt(regs, memory, regs[PC])
                if until:
                    if regs[PC] in until:
                        break
                elif (regs[PC] == ret and regs[SP] == s0 + 2) or regs[PC] in stops:
                    break
            mine = bytearray(before)
            pyfunc(mine, entry)
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


FIGHTER_WATCH = list(range(0xAA00, 0xAA80)) + [0x9C2D, 0x9CA7, 0xB150]


def main():
    results = []

    def run(addr, label, pyfunc, watch, **kw):
        m, t = validate(addr, pyfunc, watch, **kw)
        print(f"{label:22} {m}/{t} calls match")
        results.append((m, t))

    run(0x9C6F, "$9C6F round timer:", lambda mm, r: update_timer(mm),
        [0x9CA6, 0x9CA5, 0x9C2B])
    run(0x9D29, "$9D29 hit-detect P1:", lambda mm, r: hit_detect(mm, HIT_P1),
        [0xAA08, 0xA06F, 0xA070, 0xA071, 0xA072], stops=[0x9E7F])
    run(0x9ED2, "$9ED2 hit-detect P2:", lambda mm, r: hit_detect(mm, HIT_P2),
        [0xAA48, 0xA06F, 0xA070], stops=[0xA01C])
    run(0x9E7F, "$9E7F apply-hit P1:", lambda mm, r: apply_hit(mm, HIT_P1),
        [0xAA3F, 0xB150, 0xAA43])
    run(0xA01C, "$A01C apply-hit P2:", lambda mm, r: apply_hit(mm, HIT_P2),
        [0xAA3F, 0xB150, 0xAA03])
    run(0x97BB, "$97BB anim update:", lambda mm, r: update_fighter(mm, r['C']),
        FIGHTER_WATCH)
    run(0x9AD7, "$9AD7 recover pass:", lambda mm, r: recover_9ad7(mm, r['C']),
        FIGHTER_WATCH + [0x9C28], until=[0x97A0, 0x97AC])

    return all(m == t for m, t in results)


if __name__ == "__main__":
    main()
