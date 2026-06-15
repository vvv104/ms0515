"""Reference for the decode-setup chain $C34F -> $C36E -> $C2B5 -> $C319 ->
$8803 (render step 3).  fighter_mac/decoder_ref model only the inner $8833
element loop and CAPTURE the work-area state ($8AE0-$8B1F = WINIT) it starts
from - which is pose-specific.  This chain reproduces that setup from the pose
pointer (= mem[$C428], the value $BF13 writes) + the bbox/positioning params,
so the decoder can draw ANY logic-produced pose, not just the captured one.

Validated against the Z80 sim: capture at $C34F entry, run the chain, compare
the cells the element loop consumes (the four $8803 cells + $C407/$C408/$C40E +
the source HL and dest DE) to the sim's state at the $8833 element-loop entry.
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from trace_sprites import build_sim, PC                      # noqa: E402

A, B, C, D, E, H, L = 0, 2, 3, 4, 5, 6, 7
FBUF = 0xF730


def _u8(m, a):
    return m[a & 0xFFFF]


def _8803(m, hl):
    """$8803 setup: $8B0A from $C40E; $8AF3=0; $8B1C/$8B1B from the pose's first
    byte; if $8B0A, $8B1C++.  Returns the source pointer (HL+1) for $8833."""
    b0a = 1 if (m[0xC40E] & 0xFE) else 0
    m[0x8B0A] = b0a
    m[0x8AF3] = 0
    a = m[hl & 0xFFFF]
    m[0x8B1C] = a
    m[0x8B1B] = a
    if b0a:
        m[0x8B1C] = (a + 1) & 0xFF
    return (hl + 1) & 0xFFFF                       # $8832 INC HL


def _c319(d, e):
    """$C319: 8x8 -> 16-bit multiply (HL = D*E via the shift-add chain)."""
    return (d * e) & 0xFFFF


def _c2b5(m, hl_seg, bb, cc):
    """$C2B5: compute the dest in $F730 (via $C319), the element mode bits, set
    $C40E/$C407/$C408, run $8803.  Returns (source HL for $8833, dest DE)."""
    dest = (_c319(m[0xC40D], cc) + (bb >> 2) + FBUF) & 0xFFFF
    mode = {0: 0, 1: 2, 2: 4, 3: 8}[bb & 3]
    m[0xC40E] = m[0xC40C] | mode
    m[0xC407] = m[0xC40B]
    m[0xC408] = m[0xC40D]
    src = _8803(m, hl_seg)
    return src, dest


def _c36e(m, hl):
    """$C36E: read the segment header, compute the buffer position (B,C) for the
    segment, then $C2B5.  Returns (source HL, dest DE, next-segment HL)."""
    m[0xC414] = m[hl & 0xFFFF]; hl = (hl + 1) & 0xFFFF
    m[0xC415] = m[hl & 0xFFFF]; hl = (hl + 1) & 0xFFFF
    seg_len = m[hl & 0xFFFF] | (m[(hl + 1) & 0xFFFF] << 8)   # LD C,(HL);LD B,(HL)
    m[0xC418] = seg_len & 0xFF
    m[0xC419] = (seg_len >> 8) & 0xFF
    hl = (hl + 2) & 0xFFFF
    seg_data = hl                                  # $C380 PUSH HL
    m[0xC40D] = m[0xC40F]
    m[0xC40C] = m[0xC410]
    m[0xC40B] = m[0xC411]
    cc = (m[0xC413] + m[0xC415]) & 0xFF
    if m[0xC415] == 0xFF:                          # $C39F special
        cc = (0xBE - m[0xC41A] - 0x0C) & 0xFF
    if m[0xC410] == 0:                             # $C3B0
        bb = (m[0xC412] + m[0xC414]) & 0xFF
    else:                                          # $C3C3 path
        t = (m[0xC416] - m[0xC414] - (m[seg_data] << 2)) & 0xFF
        bb = (m[0xC412] + t) & 0xFF
    src, dest = _c2b5(m, seg_data, bb, cc)
    nxt = (seg_data + seg_len) & 0xFFFF            # $C3DD..$C3E2
    return src, dest, nxt


def setup_chain(m, hl, b_in, c_in):
    """$C34F: store B/C, read the pose header [segcount][$C416][$C417], loop the
    segments.  Returns the (source HL, dest DE) of the LAST segment (B=1 for the
    demo fighter; multi-segment composes each in turn)."""
    m[0xC412] = b_in
    m[0xC413] = c_in
    segcount = m[hl & 0xFFFF]; hl = (hl + 1) & 0xFFFF
    m[0xC416] = m[hl & 0xFFFF]; hl = (hl + 1) & 0xFFFF
    m[0xC417] = m[hl & 0xFFFF]; hl = (hl + 1) & 0xFFFF
    src = dest = None
    for _ in range(max(1, segcount)):
        src, dest, hl = _c36e(m, hl)
    return src, dest


WATCH = [0x8B0A, 0x8AF3, 0x8B1B, 0x8B1C, 0xC407, 0xC408, 0xC40E]


def validate(want_c40e=0x04, want_c407=0, budget=2000000):
    sim, mem = build_sim(watch=(0, 0))
    regs, memory, ops = sim.registers, sim.memory, sim.opcodes
    fdur, ia = sim.frame_duration, sim.int_active
    after_c234 = False
    last = None
    for _ in range(budget):
        pc = regs[PC]
        if pc == 0xC234:
            after_c234 = True
        if pc == 0xC34F:
            last = (bytes(memory), regs[B], regs[C],
                    regs[H] * 256 + regs[L])
        if after_c234 and pc == 0x8833 and memory[0xC40E] == want_c40e \
                and memory[0xC407] == want_c407 and last is not None:
            snap, b_in, c_in, hl = last
            sim_hl = regs[H] * 256 + regs[L]
            sim_de = regs[D] * 256 + regs[E]
            mm = bytearray(snap)
            src, dest = setup_chain(mm, hl, b_in, c_in)
            ok = (src == sim_hl and dest == sim_de
                  and all(mm[a] == memory[a] for a in WATCH))
            print(f"pose HL=${hl:04X}  B={b_in} C={c_in}")
            print(f"  source HL: chain ${src:04X}  sim ${sim_hl:04X}  "
                  f"{'OK' if src == sim_hl else 'MISMATCH'}")
            print(f"  dest   DE: chain ${dest:04X}  sim ${sim_de:04X}  "
                  f"{'OK' if dest == sim_de else 'MISMATCH'}")
            for a in WATCH:
                f = "OK" if mm[a] == memory[a] else "MISMATCH"
                print(f"  ${a:04X}: chain {mm[a]:#04x}  sim {memory[a]:#04x}  {f}")
            print("RESULT:", "MATCH" if ok else "MISMATCH")
            return ok
        after_c234 = after_c234 and pc != 0x8AD0
        ops[memory[pc]]()
        if regs[26] and regs[25] % fdur < ia:
            sim.accept_interrupt(regs, memory, regs[PC])
    raise SystemExit("no matching $C34F->$8833 found")


if __name__ == "__main__":
    validate()
