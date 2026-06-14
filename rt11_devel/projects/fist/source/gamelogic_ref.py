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


def hit_detect(m):
    """$9D29: does the active fighter's attack reach the opponent this frame?
    Sets $AA08 = 0 (no) / 2 / 1 (hit type).  Stops are RET or the $9E7F apply."""
    m[0xA071] = m[0xAA19]                         # fighter x-positions
    m[0xA072] = m[0xAA59]
    d = m[0xAA04]                                 # current action
    if m[(0xA90D + d) & 0xFFFF] == 0:
        return
    if m[0xAA13] == 0:
        return
    if m[0xAA16] != 0:
        return
    if m[0xAA09] != 0:
        return
    if m[0xAA12] != m[(0xA971 + d) & 0xFFFF]:
        return
    tbl = 0xA9BC if m[0xAA17] == m[0xAA57] else 0xA98A
    paddr = (tbl + ((d * 2) & 0xFF)) & 0xFFFF
    m[0xA06F], m[0xA070] = m[paddr], m[(paddr + 1) & 0xFFFF]
    reach = m[(_u16(m, 0xA06F) + m[0xAA52]) & 0xFFFF]
    if reach == 0x80:
        return
    e = (reach + 0x80) & 0xFF
    if m[0xAA17] != 0:
        dist = (m[0xA071] - m[0xA072]) & 0xFF
    else:
        dist = (m[0xA072] - m[0xA071]) & 0xFF
    c = (dist + 0x80) & 0xFF
    a93f = m[(0xA93F + d) & 0xFFFF]
    a958 = m[(0xA958 + d) & 0xFFFF]
    if m[(0xB47E + d) & 0xFFFF] != 0:
        if c == e:
            m[0xAA08] = 2
        elif c < e:
            return
        elif ((c - a93f) & 0xFF) < e:
            m[0xAA08] = 2
        elif ((c - a958) & 0xFF) < e:
            m[0xAA08] = 1
    else:
        if c == e:
            m[0xAA08] = 2
        elif c < e:
            if ((a93f + c) & 0xFF) >= e:
                m[0xAA08] = 2
            elif ((a958 + c) & 0xFF) >= e:
                m[0xAA08] = 1


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

def validate(addr, pyfunc, watch, want=200, stops=()):
    """Run the game; for each call to `addr`, run the real routine until it
    RETs (or reaches a PC in `stops`, e.g. a tail-call), run pyfunc on a copy
    of the entry memory, and compare the `watch` cells."""
    sim, mem = build_sim(watch=(0, 0))
    regs, memory, ops = sim.registers, sim.memory, sim.opcodes
    fd, ia = sim.frame_duration, sim.int_active
    stops = set(stops)
    tested = match = 0
    for _ in range(4000000):
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
    m2, t2 = validate(0x9D29, hit_detect,
                      [0xAA08, 0xA06F, 0xA070, 0xA071, 0xA072], stops=[0x9E7F])
    print(f"$9D29 hit-detection:  {m2}/{t2} calls match")
    return m == t and m2 == t2


if __name__ == "__main__":
    main()
