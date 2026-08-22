"""Generate FIST.MAC for a game-logic routine + its VRAM-oracle self-test.

The validated Python reference (gamelogic_ref.py) is written in absolute
Spectrum addresses ($9C00..$F801).  This module transcribes those routines to
MACRO-11 against a single mirror block GST that stands for Spectrum $9C00, so
a cell $V is GST+(V-$9C00) - see LAYOUT.md.  For per-routine unit tests we
only need the game-state window $9C00..$AB00 (3.8 KB, well below the octal
040000 VRAM window), preloaded with a real state captured from the running
game; the routine runs in place and the driver copies the window to VRAM so
the existing VRAM oracle (test_fist_screen.cpp) can dump it for comparison.

  FIST_GL = the routine to emit (default 'timer' = $9C6F update_timer).

Outputs (build artifacts, gitignored):
  FIST.MAC                 the assembled program
  gl_expected.bin          the reference's expected state window (host check)
  gl_window.json           {base, size} of the dumped window
"""
import json
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from trace_sprites import build_sim, PC                      # noqa: E402
import gamelogic_ref as ref                                  # noqa: E402

OUT_MAC = HERE.parent / "FIST.MAC"
EXP_BIN = HERE.parent / "gl_expected.bin"
WIN_JSON = HERE.parent / "gl_window.json"

GBASE = 0x9C00                       # GST stands for this Spectrum address
SP = 12                              # Z80 SP register index in the sim
Z80_REG = {'A': 0, 'B': 2, 'C': 3, 'D': 4, 'E': 5, 'H': 6, 'L': 7}


def g(addr, reg=None):
    """MACRO-11 operand for Spectrum address `addr` in GST - relative if `reg`
    is None, else indexed `GST+off(reg)` (used for the fighter selector C and
    the table-index registers)."""
    off = f"GST+{addr - GBASE}."
    return f"{off}({reg})" if reg else off


def gp(reg):
    """MACRO-11 operand for the byte at the *runtime* Spectrum address in `reg`.
    GST stands for $9C00 (octal 116000), so GST-116000(reg) = GST+(reg-$9C00)
    is the mirror byte for Spectrum address `reg`."""
    return f"GST-116000({reg})"


def gb(reg, off):
    """Byte at `base+off` in GST, where `reg` holds a runtime Spectrum address
    (>= $9C00).  Offset is emitted decimal (.) so the octal default radix does
    not corrupt it."""
    return (f"GST-116000+{off}.({reg})" if off >= 0
            else f"GST-116000-{-off}.({reg})")


def lpb(reg, off=0):
    """Byte at `addr+off` in the low-data mirror LDAT, where `reg` holds a
    runtime Spectrum address in the LDAT span ($9368.. = octal 111550..)."""
    return (f"LDAT-111550+{off}.({reg})" if off >= 0
            else f"LDAT-111550-{-off}.({reg})")


# ── routine capture (entry state + entry registers) ───────────────────────────

def capture_ai(addr, refdecide, win_end, budget=4000000):
    """Capture an exercising $A090 (AI) call: a 64K snapshot at entry plus the
    sequence of $A3FF (RNG) return values the real call consumed.  Prefer a
    call that consumed randoms (so the replay path is exercised)."""
    sim, mem = build_sim(watch=(0, 0))
    regs, memory, ops = sim.registers, sim.memory, sim.opcodes
    fd, ia = sim.frame_duration, sim.int_active
    fallback = None
    for _ in range(budget):
        if regs[PC] == addr:
            s0 = regs[SP]
            ret = memory[s0] | (memory[s0 + 1] << 8)
            snap = bytes(memory)
            randoms = []
            for _ in range(200000):
                cur = regs[PC]
                ops[memory[cur]]()
                if cur == 0xA3FF:
                    randoms.append(regs[0])
                if regs[26] and regs[25] % fd < ia:
                    sim.accept_interrupt(regs, memory, regs[PC])
                if regs[PC] == ret and regs[SP] == s0 + 2:
                    break
            trial = bytearray(snap)
            refdecide(trial, list(randoms))
            if trial[GBASE:win_end] != snap[GBASE:win_end]:
                if randoms:
                    return snap, randoms
                if fallback is None:
                    fallback = (snap, randoms)
            continue
        cur = regs[PC]
        ops[memory[cur]]()
        if regs[26] and regs[25] % fd < ia:
            sim.accept_interrupt(regs, memory, regs[PC])
    if fallback:
        return fallback
    raise SystemExit(f"no exercising AI call at {addr:#06x}")


def capture_state(addr, refapply, win_end, reg_setup, witness=None,
                  budget=4000000):
    """Run the game; return (snapshot, entry_regs) for a call to `addr` that
    exercises the routine well.  Preference order: (1) the `witness` cell
    changes (so a guarded deep path actually ran) with all param registers
    non-zero, (2) witness changes, (3) the GST window changes (any work) with
    non-zero param regs, (4) any window change, (5) the first call seen.  The
    non-zero-regs bias keeps an R4=0 selector from hiding a missing index."""
    sim, mem = build_sim(watch=(0, 0))
    regs, memory, ops = sim.registers, sim.memory, sim.opcodes
    fd, ia = sim.frame_duration, sim.int_active
    zregs = [z for _, z in reg_setup]
    best = {}                               # rank -> (snap, entry)
    for _ in range(budget):
        if regs[PC] == addr:
            snap = bytes(memory)
            entry = {n: regs[i] for n, i in Z80_REG.items()}
            best.setdefault(5, (snap, entry))
            trial = bytearray(snap)
            refapply(trial, entry)
            changed = trial[GBASE:win_end] != snap[GBASE:win_end]
            nz = all(entry[z] != 0 for z in zregs)
            deep = witness is not None and trial[witness] != snap[witness]
            if deep and nz:
                return snap, entry
            if deep:
                best.setdefault(2, (snap, entry))
            if changed and nz:
                best.setdefault(3, (snap, entry))
            if changed:
                best.setdefault(4, (snap, entry))
        ops[memory[regs[PC]]]()
        if regs[26] and regs[25] % fd < ia:
            sim.accept_interrupt(regs, memory, regs[PC])
    for rank in (2, 3, 4, 5):
        if rank in best:
            return best[rank]
    raise SystemExit(f"no call to {addr:#06x} found")


# ── MACRO-11 routine bodies (transcribed from gamelogic_ref) ──────────────────

def emit_timer():
    """$9C6F update_timer: tick the round-time divider/seconds."""
    return f"""
;-------------------------------------------------------------------
; TIMER - $9C6F update_timer.  Gated off while a reaction is pending
; ($AA03|$AA43) or the action is idle ($17); ticks the 13-frame divider
; $9CA6, and on each expiry decrements the seconds $9CA5, raising the
; timeout flag $9C2B at zero.
TIMER:  TSTB    {g(0x9C2B)}            ; timeout already raised?
        BNE     9$
        MOVB    {g(0xAA03)},R0
        BISB    {g(0xAA43)},R0         ; R0 = AA03 | AA43
        BNE     9$                     ; a reaction is pending -> hold
        CMPB    {g(0xAA04)},#27        ; idle stance $17?
        BEQ     9$
        DECB    {g(0x9CA6)}            ; tick the divider
        BNE     9$
        MOVB    #15.,{g(0x9CA6)}       ; reload $0D
        DECB    {g(0x9CA5)}            ; one second elapsed
        BNE     9$
        MOVB    #1,{g(0x9C2B)}         ; time up
9$:     RTS     PC
"""


def emit_recover():
    """$9AD7 recover_9ad7: get-up / recovery pass, fighter selector C in R4.
    Loads (D,E) from $AA04+C/$AA05+C, applies the recovery when the move-
    pending flag $AA0D+C is set, stores (D,E) back."""
    return f"""
;-------------------------------------------------------------------
; RECOV - $9AD7 recover_9ad7.  R4 = C (fighter selector, 0 or $40).
; R1 = D (action, zero-extended for indexing $B462), R2 = E (sub-state).
RECOV:  MOV     #1,R3                  ; XOR mask for the facing toggle
        MOVB    {g(0xAA04, 'R4')},R1   ; D = m[$AA04+C]
        BIC     #177400,R1             ; zero-extend (used as a table index)
        MOVB    {g(0xAA05, 'R4')},R2   ; E = m[$AA05+C]
        TSTB    {g(0xAA0D, 'R4')}      ; move-pending flag set?
        BEQ     8$
        CLRB    {g(0xAA0D, 'R4')}
        MOVB    {g(0xAA03, 'R4')},R0   ; a03 = m[$AA03+C]
        BEQ     1$
        MOVB    R0,R2                  ; queued reaction -> E = a03
        MOVB    R0,{g(0x9C28)}         ; m[$9C28] = a03
        BR      8$
1$:     TSTB    {g(0xAA16, 'R4')}      ; m[$AA16+C]
        BEQ     2$
        CMPB    R1,#21                 ; D == $11 ?
        BNE     3$
        MOVB    {g(0xAA17, 'R4')},R0   ; facing flip: m[$AA17+C] ^= 1
        XOR     R3,R0
        MOVB    R0,{g(0xAA17, 'R4')}
3$:     CLRB    {g(0xAA07, 'R4')}
        CLRB    {g(0xAA09, 'R4')}
        CLRB    {g(0xAA0B, 'R4')}
        MOVB    #1,{g(0xAA0C, 'R4')}
        CLRB    {g(0xAA16, 'R4')}
        MOVB    #1,R1                  ; D = 1
        BR      8$
2$:     MOVB    {g(0xB462, 'R1')},R0   ; t = m[$B462+D]
        BEQ     4$
        MOVB    #1,{g(0xAA00, 'R4')}
        CLRB    {g(0xAA07, 'R4')}
        CLRB    {g(0xAA0B, 'R4')}
        MOVB    #1,R1                  ; D = 1
        BR      8$
4$:     MOVB    #1,{g(0xAA09, 'R4')}
8$:     MOVB    R1,{g(0xAA04, 'R4')}   ; store D
        MOVB    R2,{g(0xAA05, 'R4')}   ; store E
        RTS     PC
"""


def emit_hitdet(label, A, apply_label=None):
    """hit_detect: does this fighter's attack reach the opponent?  Walks the
    reach tables ($A98A/$A9BC by facing) to a pointer, derefs m[ptr+ridx], and
    compares the measured distance to set the result A['result'] (2 full /
    1 half).  Parametrized by the P1 ($9D29) / P2 ($9ED2) address set A; P1
    latches both x-positions ($A071/$A072) first, P2 reuses them.  With
    apply_label set, a hit JMPs there (the $9E7F/$A01C tail-call) instead of
    returning - used by the orchestrator; a miss always RETs."""
    latch = ""
    if A['setpos']:
        sp0, sp1 = A['setpos']
        latch = (f"        MOVB    {g(sp0)},R0\n"
                 f"        MOVB    R0,{g(0xA071)}\n"
                 f"        MOVB    {g(sp1)},R0\n"
                 f"        MOVB    R0,{g(0xA072)}\n")
    body = f"""
;-------------------------------------------------------------------
; {label} - hit_detect.  Sets {A['result']:#06x} on a connecting strike.
{label}:
{latch}        MOVB    {g(A['act'])},R1
        BIC     #177400,R1
        TSTB    {g(0xA90D, 'R1')}      ; striking action? ($A90D[d])
        BNE     10$
11$:    RTS     PC                     ; near guard-exit trampoline
10$:    TSTB    {g(A['g1'])}           ; g1 must be set
        BEQ     11$
        TSTB    {g(A['g2'])}           ; g2 must be clear
        BNE     11$
        TSTB    {g(A['g3'])}           ; g3 must be clear
        BNE     11$
        MOVB    {g(0xA971, 'R1')},R0   ; fg must equal $A971[d]
        CMPB    {g(A['fg'])},R0
        BNE     11$
        MOVB    {g(A['aface'])},R0     ; same facing -> $A9BC, else $A98A
        CMPB    R0,{g(A['tface'])}
        BNE     1$
        MOV     #43452.,R2             ; $A9BC
        BR      2$
1$:     MOV     #43402.,R2             ; $A98A
2$:     MOV     R1,R0                  ; paddr = tbl + 2*d
        ASL     R0
        ADD     R0,R2
        MOV     {gp('R2')},R0          ; ptr = word m[paddr] (paddr even)
        MOVB    R0,{g(0xA06F)}         ; store ptr low -> $A06F (odd: byte
        MOV     R0,R3                  ;   stores; a word MOV would hit $A06E)
        SWAB    R3
        MOVB    R3,{g(0xA070)}
        MOVB    {g(A['ridx'])},R3      ; reach = m[ptr + ridx]
        BIC     #177400,R3
        ADD     R3,R0
        MOVB    {gp('R0')},R0
        BIC     #177400,R0
        CMP     R0,#200                ; reach == $80 -> no
        BEQ     9$
        ADD     #200,R0                ; e = (reach + $80) & $FF
        BIC     #177400,R0
        MOV     R0,R2
        TSTB    {g(A['aface'])}        ; dist by facing
        BEQ     3$
        MOVB    {g(0xA071)},R0         ; facing!=0: $A071 - $A072
        BIC     #177400,R0
        MOVB    {g(0xA072)},R3
        BIC     #177400,R3
        BR      4$
3$:     MOVB    {g(0xA072)},R0         ; facing==0: $A072 - $A071
        BIC     #177400,R0
        MOVB    {g(0xA071)},R3
        BIC     #177400,R3
4$:     SUB     R3,R0
        BIC     #177400,R0
        ADD     #200,R0                ; c = (dist + $80) & $FF
        BIC     #177400,R0
        TSTB    {g(0xB47E, 'R1')}      ; B47E[d] selects the comparison form
        BEQ     6$
        CMP     R0,R2                  ; --- type != 0 ---
        BNE     5$
        MOVB    #2,{g(A['result'])}    ; c == e -> full
        BR      9$
5$:     BLO     9$                     ; c < e -> miss
        MOVB    {g(0xA93F, 'R1')},R3   ; (c - $A93F[d]) & $FF < e -> full
        BIC     #177400,R3
        MOV     R0,R5
        SUB     R3,R5
        BIC     #177400,R5
        CMP     R5,R2
        BHIS    52$
        MOVB    #2,{g(A['result'])}
        BR      9$
52$:    MOVB    {g(0xA958, 'R1')},R3   ; (c - $A958[d]) & $FF < e -> half
        BIC     #177400,R3
        MOV     R0,R5
        SUB     R3,R5
        BIC     #177400,R5
        CMP     R5,R2
        BHIS    9$
        MOVB    #1,{g(A['result'])}
        BR      9$
6$:     CMP     R0,R2                  ; --- type == 0 ---
        BNE     7$
        MOVB    #2,{g(A['result'])}    ; c == e -> full
        BR      9$
7$:     BHIS    9$                     ; c > e -> miss
        MOVB    {g(0xA93F, 'R1')},R3   ; (c + $A93F[d]) & $FF >= e -> full
        BIC     #177400,R3
        MOV     R0,R5
        ADD     R3,R5
        BIC     #177400,R5
        CMP     R5,R2
        BHIS    8$
        MOVB    {g(0xA958, 'R1')},R3   ; (c + $A958[d]) & $FF >= e -> half
        BIC     #177400,R3
        MOV     R0,R5
        ADD     R3,R5
        BIC     #177400,R5
        CMP     R5,R2
        BLO     9$
        MOVB    #1,{g(A['result'])}
        BR      9$
8$:     MOVB    #2,{g(A['result'])}
9$:     RTS     PC
"""
    if apply_label:
        body = body.replace("        BR      9$",
                            f"        JMP     {apply_label}")
        tag = f"8$:     MOVB    #2,{g(A['result'])}\n9$:"
        body = body.replace(
            tag, f"8$:     MOVB    #2,{g(A['result'])}\n"
                 f"        JMP     {apply_label}\n9$:")
    return body


def emit_anim():
    """$97BB update_fighter: per-fighter animation chain.  R4 = C (selector),
    R5 = Q (= m[$9C29], the opponent's selector), R1 = D (action), R2 = E
    (sub-state); R0/R3 scratch.  Threads (D,E) through anim_9920/9994/9aa1
    (inline) with the range_9ba7 helper (RNG9BA), then stores (D,E) back."""
    return f"""
;-------------------------------------------------------------------
; UPDFGT - $97BB update_fighter.  Stage exits branch to the next stage's
; global label (= the Z80 'return D,E').
UPDFGT: MOVB    {g(0x9C29)},R5         ; Q = m[$9C29]
        BIC     #177400,R5
        MOVB    {g(0xAA04, 'R4')},R1   ; D = action
        BIC     #177400,R1
        MOVB    {g(0xAA05, 'R4')},R2   ; E = sub-state
        BIC     #177400,R2
; --- anim_9920: launch/recover a move ---
A9920:  CMP     R2,#3                  ; E == 3 ?
        BNE     20$
        CMP     R1,#23                 ; D == $13 -> E = D
        BEQ     21$
        CMP     R1,#24                 ; D == $14 -> E = D
        BNE     22$
21$:    MOV     R1,R2                  ; E = D
        BR      A9994
22$:    TSTB    {g(0xAA16, 'R5')}      ; opponent guards busy -> hold
        BNE     A9994
        TSTB    {g(0xAA09, 'R5')}
        BNE     A9994
        MOVB    {g(0xAA04, 'R5')},R0   ; a = opponent action
        BIC     #177400,R0
        CMP     R0,#20                 ; a == $10 -> hold
        BEQ     A9994
        CMP     R0,#12                 ; a == $0A -> hold
        BEQ     A9994
        TSTB    {g(0xA90D, 'R0')}      ; opponent not striking -> hold
        BEQ     A9994
        JSR     PC,RNG9BA              ; in range?
        TST     R0
        BEQ     A9994
        MOVB    {g(0xAA04, 'R5')},R0   ; E = m[$A926 + opponent action]
        BIC     #177400,R0
        MOVB    {g(0xA926, 'R0')},R2
        BIC     #177400,R2
        CLRB    {g(0xAA09, 'R4')}
        CLRB    {g(0xAA16, 'R5')}
        BR      A9994
20$:    CMP     R2,#7                  ; E == 7 ?
        BNE     A9994
        CMP     R1,#4                  ; D == $04 -> hold
        BEQ     A9994
        CMP     R1,#7                  ; D == $07 -> hold
        BEQ     A9994
        MOV     #30,R2                 ; E = $18
; --- anim_9994: action transitions / knock-downs ---
A9994:  CMP     R2,#32                 ; E in ($1A,$1B,$16) -> knock-down
        BEQ     94$
        CMP     R2,#33
        BEQ     94$
        CMP     R2,#26
        BNE     30$
94$:    MOV     R2,R1                  ; D = E
        MOV     #172,R0
        MOVB    R0,{g(0xAA18, 'R4')}   ; $AA18+C = $7A
        CLRB    {g(0xAA16, 'R4')}
        CLRB    {g(0xAA09, 'R4')}
        TSTB    {g(0xAA13, 'R4')}      ; guard not raised -> done
        BEQ     98$                    ; (A9AA1 is just out of branch range)
        MOVB    {g(0xAA12, 'R4')},R0   ; fg in ($2C,$28) ?
        BIC     #177400,R0
        CMP     R0,#54
        BEQ     95$
        CMP     R0,#50
        BNE     A9AA1
95$:    TSTB    {g(0x9CA7)}            ; once-per-fall credit already taken?
        BNE     A9AA1
        MOV     #1,R0
        MOVB    R0,{g(0x9CA7)}
        MOV     #5,R0
        MOVB    R0,{g(0xB150)}
        BR      A9AA1
98$:    BR      A9AA1                  ; near relay for the AA13 guard exit
30$:    CMP     R1,R2                  ; D == E -> done
        BEQ     A9AA1
        CMP     R1,#1                  ; D == 1 ?
        BNE     31$
        CMP     R2,#21                 ; E == $11 -> D = $12
        BNE     301$
        MOV     #22,R1
        BR      A9AA1
301$:   CMP     R2,#7                  ; E in (7,$10,$0A) -> D = $04
        BEQ     302$
        CMP     R2,#20
        BEQ     302$
        CMP     R2,#12
        BEQ     302$
        MOV     R2,R1                  ; else D = E
        BR      A9AA1
302$:   MOV     #4,R1
        BR      A9AA1
31$:    CMP     R2,#7                  ; E in (7,$10,$0A) ?
        BEQ     310$
        CMP     R2,#20
        BEQ     310$
        CMP     R2,#12
        BNE     32$
310$:   CMP     R1,#4                  ; D == $04 ?
        BNE     311$
        MOVB    {g(0xAA09, 'R4')},R0
        CMP     R0,#1                  ; AA09+C != 1 -> done
        BNE     A9AA1
        MOV     R2,R1                  ; D = E
        BR      A9AA1
311$:   CMP     R1,#22                 ; D == $12 -> AA16+C = 1
        BNE     A9AA1
        MOV     #1,R0
        MOVB    R0,{g(0xAA16, 'R4')}
        BR      A9AA1
32$:    CMP     R1,#22                 ; D == $12 and E == $11 ?
        BNE     33$
        CMP     R2,#21
        BNE     33$
        MOVB    {g(0xAA09, 'R4')},R0
        CMP     R0,#1
        BNE     A9AA1
        MOV     R2,R1                  ; D = E
        BR      A9AA1
33$:    CMP     R1,#21                 ; D == $11 and AA09+C == 1 ?
        BNE     34$
        MOVB    {g(0xAA09, 'R4')},R0
        CMP     R0,#1
        BNE     34$
        MOV     #1,R0
        MOVB    R0,{g(0xAA16, 'R4')}
        CLRB    {g(0xAA07, 'R4')}
        CLRB    {g(0xAA0B, 'R4')}
        CLRB    {g(0xAA09, 'R4')}
        MOV     #25,R1                 ; D = $15
        BR      A9AA1
34$:    MOVB    {g(0xB462, 'R1')},R0   ; t = m[$B462+D]
        BIC     #177400,R0
        CMP     R0,#200                ; t == $80 ?
        BNE     35$
        CLRB    {g(0xAA09, 'R4')}
        CMP     R2,#21                 ; E == $11 -> D = $12
        BNE     341$
        MOV     #22,R1
        BR      A9AA1
341$:   MOV     R2,R1                  ; D = E
        BR      A9AA1
35$:    TST     R0                     ; t != 0 ?
        BEQ     36$
        CLRB    {g(0xAA09, 'R4')}
        BR      A9AA1
36$:    MOV     #1,R0                  ; t == 0
        MOVB    R0,{g(0xAA16, 'R4')}
        CLRB    {g(0xAA09, 'R4')}
; --- anim_9aa1: commit action to the animation slot ---
A9AA1:  MOVB    {g(0xAA0B, 'R4')},R0   ; a = m[$AA0B+C]
        BIC     #177400,R0
        CMP     R0,R1                  ; a == D ?
        BEQ     40$
        MOVB    R1,{g(0xAA0C, 'R4')}   ; a != D: new action
        CLRB    {g(0xAA0B, 'R4')}
        CLRB    {g(0xAA09, 'R4')}
        BR      49$
40$:    MOVB    {g(0xAA09, 'R4')},R0
        CMP     R0,#1                  ; AA09+C == 1 ?
        BNE     41$
        CLRB    {g(0xAA0C, 'R4')}
        BR      49$
41$:    MOVB    {g(0xAA0B, 'R4')},R0   ; else AA0C+C = AA0B+C
        MOVB    R0,{g(0xAA0C, 'R4')}
49$:    MOVB    R1,{g(0xAA04, 'R4')}   ; store D, E
        MOVB    R2,{g(0xAA05, 'R4')}
        RTS     PC

;-------------------------------------------------------------------
; RNG9BA - range_9ba7.  R4=C, R5=Q preserved; D,E (R1,R2) untouched.
; Returns R0 = 1 if the opponent is in striking range, else 0.
RNG9BA: MOVB    {g(0xAA19, 'R4')},R0   ; d = m[$AA19+C]
        BIC     #177400,R0
        MOVB    {g(0xAA19, 'R5')},R3   ; e = m[$AA19+Q]
        BIC     #177400,R3
        TSTB    {g(0xAA17, 'R4')}      ; v = (d-e) if facing else (e-d)
        BEQ     1$
        SUB     R3,R0
        BR      2$
1$:     SUB     R0,R3
        MOV     R3,R0
2$:     BIC     #177400,R0
        MOVB    R0,{g(0x9C2D)}         ; m[$9C2D] = v
        MOVB    {g(0xAA04, 'R5')},R3   ; t = m[$B47E + opponent action]
        BIC     #177400,R3
        MOVB    {g(0xB47E, 'R3')},R3
        MOVB    {g(0xAA57)},R0         ; same facing ?
        CMPB    R0,{g(0xAA17)}
        BNE     4$
        TSTB    R3                     ; same: t==0 -> 0
        BEQ     8$
        MOVB    {g(0x9C2D)},R0         ; 1 if $03 <= v < $10
        BIC     #177400,R0
        CMP     R0,#3
        BLO     8$
        CMP     R0,#20
        BHIS    8$
        BR      9$
4$:     TSTB    R3                     ; differ: t!=0 -> 0
        BNE     8$
        MOVB    {g(0x9C2D)},R0         ; 1 if v >= $EF or v < $16
        BIC     #177400,R0
        CMP     R0,#357
        BHIS    9$
        CMP     R0,#26
        BLO     9$
8$:     CLR     R0                     ; return 0
        RTS     PC
9$:     MOV     #1,R0                  ; return 1
        RTS     PC
"""


def emit_ai(randoms):
    """$A090 ai_decide: the computer-opponent move-selection trampoline.  Each
    Python block is a global label AIxxxx; inter-block transitions use JMP
    (unlimited range); rnd() = JSR ARNG, which replays the recorded $A3FF
    sequence ARND (so the decision logic is checked while the RNG source is
    abstracted - the real game swaps ARNG for an MS-0515 RNG).  `rnd() & mask`
    is COM R1 / BIC R1,R0 (PDP-11 has BIC, not AND).  R3 carries reg['a']."""
    data = "        .EVEN\nARNDI:  .WORD   0\n"
    if randoms:
        data += _emit_window("ARND", bytes(randoms))
    else:
        data += "ARND:   .BYTE   0\n"
    data += "        .EVEN\n"          # the data block may be odd-length; the
    return data + f"""                 ; code that follows must be word-aligned
;-------------------------------------------------------------------
; AIDEC - $A090 ai_decide.  ARNG replays the recorded RNG stream.
ARNG:   MOV     ARNDI,R0
        INC     ARNDI
        MOVB    ARND(R0),R0
        BIC     #177400,R0
        RTS     PC
AIA47C: MOVB    {g(0xA62F)},R1         ; ai_a47c: kick-counter remap (R0 in/out)
        BIC     #177400,R1
        CMP     R1,#12
        BEQ     1$
        CMP     R1,#20
        BEQ     1$
        CMP     R1,#4
        BEQ     1$
        CMP     R1,#7
        BEQ     1$
        RTS     PC
1$:     MOVB    {g(0xB449, 'R0')},R0
        BIC     #177400,R0
        RTS     PC
AIA583: MOVB    {g(0xA60A)},R0         ; ai_a583: in striking range? -> R0 0/1
        BIC     #177400,R0
        ASL     R0
        MOVB    {g(0xA644)},R1
        BIC     #177400,R1
        ASL     R1
        TSTB    {g(0xA608)}
        BEQ     1$
        SUB     R1,R0
        BR      2$
1$:     SUB     R0,R1
        MOV     R1,R0
2$:     BIC     #177400,R0
        MOVB    R0,{g(0xA5EE)}
        MOVB    {g(0xA62F)},R1
        BIC     #177400,R1
        MOVB    {g(0xB47E, 'R1')},R1
        MOVB    {g(0xA642)},R2
        CMPB    R2,{g(0xA608)}
        BNE     4$
        TSTB    R1
        BEQ     8$
        CMP     R0,#3
        BLO     8$
        CMP     R0,#20
        BHIS    8$
        BR      9$
4$:     TSTB    R1
        BNE     8$
        CMP     R0,#357
        BHIS    9$
        CMP     R0,#26
        BLO     9$
8$:     CLR     R0
        RTS     PC
9$:     MOV     #1,R0
        RTS     PC

AIDEC:                                 ; ARNDI starts 0 (.WORD 0 init); not reset
AI090:  TSTB    {g(0xA5F4)}             ; here, so two per-frame calls share ARND
        BEQ     1$
        MOVB    {g(0xA5F4)},R0
        MOVB    R0,{g(0xA5F6)}
        JMP     AIEND
1$:     MOVB    {g(0xA60A)},R0
        MOVB    R0,{g(0xA5EC)}
        MOVB    {g(0xA644)},R0
        MOVB    R0,{g(0xA5ED)}
        TSTB    {g(0xA62E)}
        BEQ     5$
        MOVB    {g(0xA5F5)},R0
        BIC     #177400,R0
        CMP     R0,#16
        BEQ     4$
        DECB    {g(0xA616)}
        BNE     6$
4$:     MOV     #1,R0
        MOVB    R0,{g(0xA5F6)}
6$:     JMP     AIEND
5$:     TSTB    {g(0xA618)}
        BEQ     7$
        JMP     AI553
7$:     TSTB    {g(0xA641)}
        BEQ     8$
        JMP     AI1B5
8$:     MOVB    {g(0xA62F)},R0
        BIC     #177400,R0
        TSTB    {g(0xA90D, 'R0')}
        BNE     9$
        JMP     AI1B5
9$:     MOVB    {g(0xA5F5)},R0
        BIC     #177400,R0
        CMP     R0,#23
        BEQ     10$
        CMP     R0,#24
        BNE     11$
10$:    JMP     AI0E4
11$:    JMP     AI0FC

AI553:  MOVB    {g(0xA618)},R0
        BIC     #177400,R0
        CMP     R0,#1
        BNE     1$
        JMP     AI49A
1$:     CMP     R0,#2
        BNE     2$
        JMP     AI53E
2$:     CMP     R0,#3
        BNE     3$
        JMP     AI4C8
3$:     CMP     R0,#4
        BNE     4$
        JMP     AI4D5
4$:     CMP     R0,#5
        BNE     5$
        JMP     AI4E2
5$:     CMP     R0,#6
        BNE     6$
        JMP     AI50C
6$:     CMP     R0,#7
        BNE     7$
        JMP     AI524
7$:     CMP     R0,#10
        BNE     8$
        JMP     AI4FE
8$:     JMP     AI560

AI49A:  MOVB    {g(0xA614)},R0
        BIC     #177400,R0
        CMP     R0,#7
        BHIS    1$
        JMP     AI4BE
1$:     MOVB    {g(0xA63D)},R0
        BIC     #177400,R0
        CMP     R0,#31
        BNE     2$
        JMP     AI4B1
2$:     JMP     AI4A8
AI4A8:  MOV     #4,R0
        MOVB    R0,{g(0xA5F6)}
        MOVB    R0,{g(0xA5F1)}
        JMP     AIEND
AI4B1:  MOV     #12,R0
        MOVB    R0,{g(0xA5F6)}
        MOVB    R0,{g(0xA5F1)}
        CLRB    {g(0xA618)}
        JMP     AIEND
AI4BE:  MOVB    {g(0xA62F)},R0
        BIC     #177400,R0
        CMP     R0,#1
        BEQ     1$
        JMP     AI4A8
1$:     JMP     AI4B1
AI4C8:  MOV     #16,R0
        MOVB    R0,{g(0xA5F6)}
        MOVB    R0,{g(0xA5F1)}
        CLRB    {g(0xA618)}
        JMP     AIEND
AI4D5:  MOV     #11,R0
        MOVB    R0,{g(0xA5F1)}
        MOVB    R0,{g(0xA5F6)}
        CLRB    {g(0xA618)}
        JMP     AIEND
AI4E2:  TSTB    {g(0xA5FA)}
        BEQ     1$
        MOV     #7,R0
        MOVB    R0,{g(0xA5F1)}
        MOVB    R0,{g(0xA5F6)}
        CLRB    {g(0xA618)}
        JMP     AIEND
1$:     MOV     #4,R0
        MOVB    R0,{g(0xA5F6)}
        MOVB    R0,{g(0xA5F1)}
        JMP     AIEND
AI4FE:  JSR     PC,ARNG
        CMP     R0,#200
        BHIS    1$
        JMP     AI50C
1$:     MOV     #5,R0
        MOVB    R0,{g(0xA618)}
        JMP     AI4E2
AI50C:  JSR     PC,ARNG
        MOV     #30,R0
        MOVB    R0,{g(0xA5F6)}
        MOVB    R0,{g(0xA5F1)}
        CLRB    {g(0xA618)}
        JMP     AIEND
AI524:  MOVB    {g(0xA608)},R0
        CMPB    R0,{g(0xA642)}
        BNE     1$
        MOV     #12,R0
        BR      2$
1$:     MOV     #20,R0
2$:     MOVB    R0,{g(0xA5F1)}
        MOVB    R0,{g(0xA5F6)}
        CLRB    {g(0xA618)}
        JMP     AIEND
AI53E:  MOVB    {g(0xA5F5)},R0
        BIC     #177400,R0
        CMP     R0,#12
        BNE     1$
        CLRB    {g(0xA618)}
        JMP     AIEND
1$:     MOV     #12,R0
        MOVB    R0,{g(0xA5F6)}
        MOVB    R0,{g(0xA5F1)}
        JMP     AIEND
AI560:  MOVB    {g(0xA614)},R0
        BIC     #177400,R0
        CMP     R0,#7
        BHIS    1$
        JMP     AI57B
1$:     JSR     PC,ARNG
        BIT     #200,R0
        BEQ     3$
        JMP     AI57B
3$:     CLRB    {g(0xA618)}
        MOV     #13,R0
        MOVB    R0,{g(0xA5F1)}
        MOVB    R0,{g(0xA5F6)}
        JMP     AIEND
AI57B:  MOV     #1,R0
        MOVB    R0,{g(0xA618)}
        JMP     AI49A

AI0E4:  MOVB    {g(0xA62F)},R0
        BIC     #177400,R0
        MOVB    {g(0xA926, 'R0')},R0
        BIC     #177400,R0
        CMPB    R0,{g(0xA5F5)}
        BNE     1$
        JMP     AI13E
1$:     MOV     #1,R0
        MOVB    R0,{g(0xA5F6)}
        MOVB    R0,{g(0xA5F1)}
        JMP     AIEND
AI0FC:  MOVB    {g(0xA5F5)},R0
        BIC     #177400,R0
        CMP     R0,#22
        BNE     1$
        JMP     AI1B5
1$:     CMP     R0,#1
        BEQ     2$
        CMP     R0,#3
        BEQ     2$
        CMP     R0,#2
        BEQ     2$
        BR      3$
2$:     JMP     AI145
3$:     TSTB    {g(0xA5FA)}
        BNE     4$
        TSTB    {g(0xA607)}
        BEQ     5$
4$:     JMP     AI0F3
5$:     MOVB    {g(0xA62F)},R0
        BIC     #177400,R0
        CMP     R0,#12
        BEQ     6$
        CMP     R0,#20
        BEQ     6$
        JMP     AI13E
6$:     JMP     AI127
AI0F3:  MOV     #1,R0
        MOVB    R0,{g(0xA5F6)}
        MOVB    R0,{g(0xA5F1)}
        JMP     AIEND
AI127:  MOVB    {g(0xA5F5)},R0
        BIC     #177400,R0
        CMP     R0,#12
        BEQ     1$
        CMP     R0,#20
        BEQ     1$
        CMP     R0,#4
        BEQ     1$
        CMP     R0,#7
        BEQ     1$
        CMP     R0,#13
        BEQ     1$
        JMP     AI0F3
1$:     JMP     AI13E
AI13E:  MOVB    {g(0xA5F1)},R0
        MOVB    R0,{g(0xA5F6)}
        JMP     AIEND
AI145:  JSR     PC,AIA583
        TST     R0
        BNE     1$
        JMP     AI1B5
1$:     MOVB    {g(0xA61B)},R0
        BIC     #177400,R0
        TST     R0
        BNE     2$
        JMP     AI16D
2$:     BIT     #200,R0
        BEQ     3$
        JSR     PC,ARNG
        MOVB    {g(0xA60B)},R1
        BIC     #177400,R1
        COM     R1
        BIC     R1,R0
        BIC     #177400,R0
        TST     R0
        BNE     4$
        JMP     AI16D
4$:     MOVB    R0,{g(0xA61B)}
        JMP     AI161
3$:     JMP     AI161
AI161:  DECB    {g(0xA61B)}
        BEQ     1$
        JMP     AI1B5
1$:     MOV     #200,R0
        MOVB    R0,{g(0xA61B)}
        JMP     AI16D
AI16D:  JSR     PC,ARNG
        MOVB    {g(0xA646)},R1
        BIC     #177400,R1
        COM     R1
        BIC     R1,R0
        BIC     #177400,R0
        BIT     #200,R0
        BEQ     1$
        JMP     AI2E7
1$:     BIT     #160,R0
        BEQ     2$
        JMP     AI1A5
2$:     BIT     #17,R0
        BEQ     3$
        JMP     AI195
3$:     JSR     PC,ARNG
        BIT     #200,R0
        BNE     4$
        TST     R0
        BEQ     4$
        MOV     #13,R0
        BR      5$
4$:     MOV     #11,R0
5$:     MOVB    R0,{g(0xA5F1)}
        MOVB    R0,{g(0xA5F6)}
        JMP     AIEND
AI195:  MOVB    {g(0xA62F)},R0
        BIC     #177400,R0
        MOVB    {g(0xB3BD, 'R0')},R0
        BIC     #177400,R0
        TST     R0
        BNE     1$
        JMP     AI1A5
1$:     MOVB    R0,{g(0xA618)}
        JMP     AIEND
AI1A5:  MOVB    {g(0xA62F)},R0
        BIC     #177400,R0
        MOVB    {g(0xA926, 'R0')},R0
        MOVB    R0,{g(0xA5F6)}
        MOVB    R0,{g(0xA5F1)}
        JMP     AIEND
AI1B5:  TSTB    {g(0xA607)}
        BEQ     1$
        JMP     AI22A
1$:     TSTB    {g(0xA5FA)}
        BNE     2$
        JMP     AI21B
2$:     MOVB    {g(0xA5F5)},R0
        BIC     #177400,R0
        CMP     R0,#4
        BNE     3$
        JMP     AI22A
3$:     CMP     R0,#22
        BEQ     4$
        JMP     AI1F4
4$:     TSTB    {g(0xA60E)}
        BNE     5$
        JMP     AI22A
5$:     MOV     #1,R0
        MOVB    R0,{g(0xA5F1)}
        MOVB    R0,{g(0xA5F6)}
        MOVB    R0,{g(0xA5F5)}
        MOVB    R0,{g(0xA5FD)}
        MOVB    {g(0xA608)},R1
        BIC     #177400,R1
        MOV     #1,R2
        XOR     R2,R1
        MOVB    R1,{g(0xA608)}
        CLRB    {g(0xA5FA)}
        CLRB    {g(0xA60E)}
        CLRB    {g(0xA5FC)}
        JMP     AIEND
AI1F4:  MOVB    {g(0xA61C)},R0
        BIC     #177400,R0
        BIT     #200,R0
        BNE     1$
        JMP     AI207
1$:     JSR     PC,ARNG
        MOVB    {g(0xA60F)},R1
        BIC     #177400,R1
        COM     R1
        BIC     R1,R0
        BIC     #177400,R0
        MOVB    R0,{g(0xA61C)}
        JMP     AI22A
AI207:  DECB    {g(0xA61C)}
        BEQ     1$
        JMP     AI22A
1$:     MOV     #1,R0
        MOVB    R0,{g(0xA5F1)}
        MOVB    R0,{g(0xA5F6)}
        MOV     #200,R0
        MOVB    R0,{g(0xA61C)}
        JMP     AIEND
AI21B:  MOVB    {g(0xA5F5)},R0
        BIC     #177400,R0
        CMP     R0,#1
        BEQ     1$
        CMP     R0,#3
        BEQ     1$
        CMP     R0,#2
        BEQ     1$
        JMP     AI22A
1$:     JMP     AI231
AI22A:  MOVB    {g(0xA5F1)},R0
        MOVB    R0,{g(0xA5F6)}
        JMP     AIEND
AI231:  MOVB    {g(0xA608)},R0
        CMPB    R0,{g(0xA642)}
        BNE     1$
        JMP     AI2A0
1$:     TSTB    {g(0xA608)}
        BEQ     2$
        MOVB    {g(0xA5EC)},R0
        BIC     #177400,R0
        MOVB    {g(0xA5ED)},R1
        BIC     #177400,R1
        SUB     R1,R0
        BR      3$
2$:     MOVB    {g(0xA5ED)},R0
        BIC     #177400,R0
        MOVB    {g(0xA5EC)},R1
        BIC     #177400,R1
        SUB     R1,R0
3$:     BIC     #177400,R0
        MOVB    R0,{g(0xA5EE)}
        CMP     R0,#325
        BHIS    4$
        CMP     R0,#25
        BLO     4$
        BR      5$
4$:     JMP     AI2C7
5$:     CMP     R0,#200
        BLO     6$
        JMP     AI292
6$:     JMP     AI25D
AI25D:  DECB    {g(0xA610)}
        BEQ     1$
        JMP     AI22A
1$:     MOVB    {g(0xA5F5)},R0
        BIC     #177400,R0
        CMP     R0,#2
        BEQ     2$
        JMP     AI27F
2$:     JSR     PC,ARNG
        MOVB    {g(0xA61A)},R1
        BIC     #177400,R1
        COM     R1
        BIC     R1,R0
        BIC     #177400,R0
        TST     R0
        BNE     3$
        JMP     AI27F
3$:     MOVB    R0,{g(0xA610)}
        MOV     #1,R0
        MOVB    R0,{g(0xA5F6)}
        MOVB    R0,{g(0xA5F1)}
        JMP     AIEND
AI27F:  JSR     PC,ARNG
        MOVB    {g(0xA619)},R1
        BIC     #177400,R1
        COM     R1
        BIC     R1,R0
        BIC     #177400,R0
        MOVB    R0,{g(0xA610)}
        MOV     #2,R0
        MOVB    R0,{g(0xA5F6)}
        MOVB    R0,{g(0xA5F1)}
        JMP     AIEND
AI292:  MOV     #22,R0
        MOVB    R0,{g(0xA5F1)}
        MOVB    R0,{g(0xA5F6)}
        MOV     #1,R0
        MOVB    R0,{g(0xA60E)}
        JMP     AIEND
AI2A0:  TSTB    {g(0xA608)}
        BEQ     1$
        MOVB    {g(0xA5ED)},R0
        BIC     #177400,R0
        MOVB    {g(0xA5EC)},R1
        BIC     #177400,R1
        SUB     R1,R0
        BR      2$
1$:     MOVB    {g(0xA5EC)},R0
        BIC     #177400,R0
        MOVB    {g(0xA5ED)},R1
        BIC     #177400,R1
        SUB     R1,R0
2$:     BIC     #177400,R0
        MOVB    R0,{g(0xA5EE)}
        CMP     R0,#337
        BHIS    3$
        CMP     R0,#37
        BLO     3$
        BR      4$
3$:     JMP     AI2C7
4$:     CMP     R0,#200
        BLO     5$
        JMP     AI25D
5$:     JMP     AI292
AI2C7:  MOVB    {g(0xA611)},R0
        BIC     #177400,R0
        BIT     #200,R0
        BNE     1$
        JMP     AI2DB
1$:     JSR     PC,ARNG
        MOVB    {g(0xA617)},R1
        BIC     #177400,R1
        COM     R1
        BIC     R1,R0
        BIC     #177400,R0
        MOVB    R0,{g(0xA611)}
        JMP     AI35F
AI2DB:  DECB    {g(0xA611)}
        BEQ     1$
        JMP     AI35F
1$:     MOV     #200,R0
        MOVB    R0,{g(0xA611)}
        JMP     AI2E7
AI2E7:  MOVB    {g(0xA608)},R0
        CMPB    R0,{g(0xA642)}
        BNE     1$
        JMP     AI3AF
1$:     MOVB    {g(0xA62F)},R0
        BIC     #177400,R0
        CMP     R0,#23
        BNE     2$
        JMP     AI313
2$:     CMP     R0,#24
        BEQ     3$
        JMP     AI325
3$:     JSR     PC,ARNG
        BIC     #177774,R0
        MOVB    {g(0xA904, 'R0')},R0
        BIC     #177400,R0
        CMP     R0,#7
        BNE     4$
        JMP     AI3A1
4$:     MOVB    R0,{g(0xA5F1)}
        MOVB    R0,{g(0xA5F6)}
        JMP     AIEND
AI313:  JSR     PC,ARNG
        BIC     #177774,R0
        MOVB    {g(0xA900, 'R0')},R0
        MOVB    R0,{g(0xA5F1)}
        MOVB    R0,{g(0xA5F6)}
        JMP     AIEND
AI325:  MOVB    {g(0xA5EE)},R0
        BIC     #177400,R0
        ADD     #63,R0
        BIC     #177400,R0
        MOVB    R0,{g(0xA613)}
        JSR     PC,ARNG
        MOVB    {g(0xA612)},R1
        BIC     #177400,R1
        COM     R1
        BIC     R1,R0
        MOVB    {g(0xA613)},R1
        BIC     #177400,R1
        ADD     R1,R0
        BIC     #177400,R0
        MOVB    {g(0xB300, 'R0')},R0
        BIC     #177400,R0
        CMP     R0,#16
        BNE     1$
        MOV     R0,R3
        JMP     AI383
1$:     JSR     PC,AIA47C
        CMP     R0,#12
        BNE     2$
        JMP     AI390
2$:     CMP     R0,#7
        BNE     3$
        JMP     AI3A1
3$:     CMP     R0,#17
        BEQ     4$
        CMP     R0,#20
        BEQ     4$
        MOVB    R0,{g(0xA5F1)}
        MOVB    R0,{g(0xA5F6)}
        JMP     AIEND
4$:     MOV     R0,R3
        JMP     AI3DA
AI35F:  TSTB    {g(0xA641)}
        BNE     1$
        TSTB    {g(0xA634)}
        BEQ     2$
1$:     JMP     AI22A
2$:     MOVB    {g(0xA62F)},R0
        BIC     #177400,R0
        TSTB    {g(0xA90D, 'R0')}
        BNE     3$
        JMP     AI22A
3$:     MOV     #1,R0
        MOVB    R0,{g(0xA5F6)}
        MOVB    R0,{g(0xA5F1)}
        JMP     AIEND
AI383:  MOV     R3,R4
        MOVB    {g(0xA614)},R0
        BIC     #177400,R0
        CMP     R0,#7
        BLO     1$
        MOV     #2,R3
        CMP     R0,#7
        BEQ     2$
        JMP     AI358
2$:     JMP     AI390
1$:     JMP     AI358
AI358:  MOVB    R3,{g(0xA5F1)}
        MOVB    R3,{g(0xA5F6)}
        JMP     AIEND
AI390:  MOVB    {g(0xA614)},R0
        BIC     #177400,R0
        CMP     R0,#2
        BLO     1$
        MOV     #12,R0
        MOVB    R0,{g(0xA5F1)}
        MOVB    R0,{g(0xA5F6)}
1$:     JMP     AIEND
AI3A1:  MOV     #5,R0
        MOVB    R0,{g(0xA618)}
        MOV     #4,R0
        MOVB    R0,{g(0xA5F6)}
        MOVB    R0,{g(0xA5F1)}
        JMP     AIEND
AI3AF:  MOVB    {g(0xA5EE)},R0
        BIC     #177400,R0
        ADD     #51,R0
        BIC     #177400,R0
        MOVB    R0,{g(0xA613)}
        JSR     PC,ARNG
        MOVB    {g(0xA612)},R1
        BIC     #177400,R1
        COM     R1
        BIC     R1,R0
        MOVB    {g(0xA613)},R1
        BIC     #177400,R1
        ADD     R1,R0
        BIC     #177400,R0
        MOVB    {g(0xB352, 'R0')},R0
        BIC     #177400,R0
        JSR     PC,AIA47C
        CMP     R0,#17
        BEQ     1$
        CMP     R0,#20
        BEQ     1$
        MOVB    R0,{g(0xA5F1)}
        MOVB    R0,{g(0xA5F6)}
        JMP     AIEND
1$:     MOV     R0,R3
        JMP     AI3DA
AI3DA:  MOV     R3,R4
        JSR     PC,ARNG
        MOVB    {g(0xA615)},R1
        BIC     #177400,R1
        COM     R1
        BIC     R1,R0
        BIC     #177400,R0
        BIT     #200,R0
        BEQ     1$
        JMP     AI292
1$:     CMP     R0,#100
        BLO     2$
        JMP     AI3F7
2$:     CMP     R0,#40
        BLO     3$
        MOV     #10,R4
3$:     MOVB    R4,{g(0xA5F6)}
        MOVB    R4,{g(0xA5F1)}
        JMP     AIEND
AI3F7:  MOV     #3,R0
        MOVB    R0,{g(0xA5F6)}
        MOVB    R0,{g(0xA5F1)}
        JMP     AIEND
AIEND:  RTS     PC
"""


def emit_orch():
    """A per-fighter slice of the $9745 orchestrator built from the ported
    routines: timer, the two animation passes (C=0 then C=$40, with $9C29 set
    to the opponent each time), the two recovery passes, then scoring.  Hit
    detect / input are skipped here (the matching Python orch_subset skips them
    too, so this checks integration, not the full $9745)."""
    return f"""
;-------------------------------------------------------------------
; ORCH - the $9745 orchestrator (deterministic part): timer, the full hit
; mechanic for both fighters (detect tail-calls apply on a hit), the two
; animation passes, the two recovery passes, then scoring.  Sound ($B15A) and
; the keyboard check ($97CB) are skipped (orch_subset skips them too).
ORCH:   JSR     PC,TIMER
        JSR     PC,HITDP2              ; $9ED2 hit-detect P2 (+apply on hit)
        JSR     PC,HITDET              ; $9D29 hit-detect P1 (+apply on hit)
        JSR     PC,MOVSEL              ; $97CB move-selection (AI both fighters)
        MOV     #100,R0                ; $9C29 = $40 (opponent of fighter 0)
        MOVB    R0,{g(0x9C29)}
        CLR     R4
        JSR     PC,UPDFGT              ; anim, C = 0
        CLRB    {g(0x9C29)}            ; $9C29 = 0 (opponent of fighter $40)
        MOV     #100,R4
        JSR     PC,UPDFGT              ; anim, C = $40
        MOV     #100,R0
        MOVB    R0,{g(0x9C29)}
        CLR     R4
        JSR     PC,RECOV               ; recover, C = 0
        CLRB    {g(0x9C29)}
        MOV     #100,R4
        JSR     PC,RECOV               ; recover, C = $40
        JSR     PC,AWARD
        JSR     PC,YINYNG
        RTS     PC
"""


def emit_combined(randoms):
    """The full $9745 frame in one module: all ported routines + the input/AI
    layer (A402, the memcpy wrappers, MOVSEL, the AI with the recorded random
    stream) + the ORCH driver (timer, hit detect+apply x2, move-selection,
    anim x2, recover x2, scoring)."""
    return (emit_timer() + emit_recover() +
            emit_hitdet("HITDET", ref.HIT_P1, apply_label="APLYP1") +
            emit_hitdet("HITDP2", ref.HIT_P2, apply_label="APLYHT") +
            emit_anim() +
            emit_apply("APLYHT", 0xAA44, 0xAA57, 0xAA17, 0xAA03) +
            emit_apply("APLYP1", 0xAA04, 0xAA17, 0xAA57, 0xAA43) +
            emit_score() + emit_yinyang() + emit_timetik() +
            emit_ranktk() + emit_rstacf() + emit_rstfrm() + emit_a402() +
            emit_wrappers() + emit_movsel() + emit_ai(randoms) +
            emit_orch())


def emit_orch_full():
    """ORCH - the EXACT $9745 per-frame orchestrator (the in-round path), matching
    the disassembly's order incl. the animation advance ($95D4 -> ANIM5E x2) and
    the draw bridge ($BF13).  The round-end score block (gated by $9C2C==2:
    $AF01/$900E/clears) is skipped - the attract path never takes it, and it needs
    $AF01 ported (a TODO).  Pairs with gamelogic_ref.frame_9745."""
    return f"""
;-------------------------------------------------------------------
; ORCH - the exact $9745 per-frame orchestrator (in-round path).
ORCH:   JSR     PC,TIMER               ; $9C6F round timer
        JSR     PC,HITDP2              ; $9ED2 hit-detect P2 (+apply on hit)
        JSR     PC,HITDET              ; $9D29 hit-detect P1 (+apply on hit)
        ; $9754 CALL $B15A (sound) - skipped
        JSR     PC,MOVSEL              ; $97CB move-selection (AI both fighters)
        MOV     #100,R0                ; $9C29 = $40 (opponent of fighter 0)
        MOVB    R0,{g(0x9C29)}
        CLR     R4
        JSR     PC,UPDFGT              ; $97BB anim, C = 0
        CLRB    {g(0x9C29)}            ; $9C29 = 0 (opponent of fighter $40)
        MOV     #100,R4
        JSR     PC,UPDFGT              ; $97BB anim, C = $40
        ; round-end score block ($9C2C==2) - skipped (attract path; TODO $AF01)
        MOV     #125026,R5             ; $95D4: hl = $AA16 ->
        JSR     PC,ANIM5E              ;   $95E1 anim advance, fighter 0
        MOV     #125126,R5             ; hl = $AA56 ->
        JSR     PC,ANIM5E              ;   $95E1 anim advance, fighter 1
        JSR     PC,BF13                ; $BF13 logic->graphics bridge
        MOV     #100,R0                ; $9C29 = $40
        MOVB    R0,{g(0x9C29)}
        CLR     R4
        JSR     PC,RECOV               ; $9AD7 recover, C = 0
        CLRB    {g(0x9C29)}
        MOV     #100,R4
        JSR     PC,RECOV               ; $9AD7 recover, C = $40
        RTS     PC
"""


def emit_fullframe(randoms):
    """The EXACT full $9745 frame: every combined-mode routine PLUS the animation
    advance ($95E1) and the draw bridge ($BF13), driven by emit_orch_full."""
    return (emit_timer() + emit_recover() +
            emit_hitdet("HITDET", ref.HIT_P1, apply_label="APLYP1") +
            emit_hitdet("HITDP2", ref.HIT_P2, apply_label="APLYHT") +
            emit_anim() +
            emit_apply("APLYHT", 0xAA44, 0xAA57, 0xAA17, 0xAA03) +
            emit_apply("APLYP1", 0xAA04, 0xAA17, 0xAA57, 0xAA43) +
            emit_score() + emit_yinyang() + emit_timetik() +
            emit_ranktk() + emit_rstacf() + emit_rstfrm() + emit_a402() +
            emit_wrappers() + emit_movsel() + emit_ai(randoms) +
            emit_95e1() + emit_bf13() +
            emit_orch_full())


def orch_subset(m, randoms):
    """Python reference for ORCH - the full $9745 sequence (incl. the $97CB AI
    move-selection) on the captured state, sharing the recorded random stream."""
    rnd = randoms if callable(randoms) else ref._Rnd(randoms)
    ref.update_timer(m)
    if ref.hit_detect(m, ref.HIT_P2):            # $9ED2 -> $A01C on a hit
        ref.apply_hit(m, ref.HIT_P2)
    if ref.hit_detect(m, ref.HIT_P1):            # $9D29 -> $9E7F on a hit
        ref.apply_hit(m, ref.HIT_P1)
    ref.move_select(m, rnd)                      # $97CB move-selection (AI)
    m[0x9C29] = 0x40
    ref.update_fighter(m, 0)
    m[0x9C29] = 0
    ref.update_fighter(m, 0x40)
    m[0x9C29] = 0x40
    ref.recover_9ad7(m, 0)
    m[0x9C29] = 0
    ref.recover_9ad7(m, 0x40)
    ref.award_points(m)
    ref.yinyang_total(m)


def emit_copy(label, src, dst, count):
    """A context-switch memcpy ($AADC/$AB0A/$AAF3/... etc.): copy `count` bytes
    from Spectrum address `src` to `dst`.  R2/R3 = pointers, R1 = counter."""
    return (f"\n;----------------------------------------------------\n"
            f"{label}:   MOV     #GST+{src - GBASE}.,R2\n"
            f"        MOV     #GST+{dst - GBASE}.,R3\n"
            f"        MOV     #{count}.,R1\n"
            f"1$:     MOVB    (R2)+,(R3)+\n"
            f"        DEC     R1\n"
            f"        BNE     1$\n"
            f"        RTS     PC\n")


def emit_wrappers():
    """The eight $9CA8/$97CB context-switch memcpy wrappers."""
    return (emit_copy("AADC", 0xAA00, 0xA5F1, 0x1A) +
            emit_copy("AB0A", 0xAA8B, 0xA60B, 0x12) +
            emit_copy("AAF3", 0xA5F1, 0xAA00, 0x1A) +
            emit_copy("AB16", 0xA60B, 0xAA8B, 0x12) +
            emit_copy("AB22", 0xAA40, 0xA5F1, 0x1A) +
            emit_copy("AB50", 0xAA77, 0xA60B, 0x12) +
            emit_copy("AB39", 0xA5F1, 0xAA40, 0x1A) +
            emit_copy("AB5C", 0xA60B, 0xAA77, 0x12))


def emit_movsel():
    """$983D move-selection: per fighter, a queued reaction forces the move,
    else the AI decides on a scratch copy (save -> AIDEC -> restore).  The
    keyboard branch (human player) is a stub - the attract demo is all-AI."""
    return f"""
;-------------------------------------------------------------------
; MOVSEL - $983D per-fighter move selection (AI / reaction; kbd stubbed).
MOVSEL: TSTB    {g(0xAA03)}            ; P1 queued reaction?
        BEQ     1$
        MOVB    {g(0xAA03)},R0
        MOVB    R0,{g(0xAA05)}
        BR      2$
1$:     TSTB    {g(0xAA06)}            ; P1 AI-controlled?
        BEQ     2$                     ; (else keyboard - stub: skip)
        JSR     PC,AADC
        JSR     PC,AB0A
        JSR     PC,AIDEC
        JSR     PC,AAF3
        JSR     PC,AB16
2$:     TSTB    {g(0xAA43)}            ; P2 queued reaction?
        BEQ     3$
        MOVB    {g(0xAA43)},R0
        MOVB    R0,{g(0xAA45)}
        RTS     PC
3$:     TSTB    {g(0xAA46)}            ; P2 AI-controlled?
        BEQ     9$
        JSR     PC,AB22
        JSR     PC,AB50
        JSR     PC,AIDEC
        JSR     PC,AB39
        JSR     PC,AB5C
9$:     RTS     PC
"""


def emit_a402():
    """$A402 ai_load_params: load the AI personality into the working state
    $A60E-$A61C from eight tables ($B3D6/$B3EC/$B401/$B40D/$B41A/$B426/$B432/
    $B43E) indexed by the AI id $A614, plus fixed seeds.  R1 = b, R0 scratch."""
    loads = [(0xB3D6, 0xA60F), (0xB3EC, 0xA612), (0xB426, 0xA615),
             (0xB401, 0xA617), (0xB432, 0xA646), (0xB40D, 0xA619),
             (0xB41A, 0xA61A), (0xB43E, 0xA60B)]
    consts = [(0xA616, 2), (0xA61C, 0o200), (0xA5F1, 1), (0xA611, 0o200),
              (0xA610, 1), (0xA61B, 0o200)]
    body = [";-------------------------------------------------------------------",
            "; A402 - ai_load_params.  R1 = AI id $A614.",
            f"A402:   MOVB    {g(0xA614)},R1",
            "        BIC     #177400,R1"]
    for tbl, dst in loads:
        body.append(f"        MOVB    {g(tbl, 'R1')},R0")
        body.append(f"        MOVB    R0,{g(dst)}")
    for dst, val in consts:
        body.append(f"        MOV     #{val:o},R0")    # octal MACRO literal
        body.append(f"        MOVB    R0,{g(dst)}")
    body.append(f"        CLRB    {g(0xA60E)}")
    body.append(f"        CLRB    {g(0xA618)}")
    body.append("        RTS     PC")
    return "\n" + "\n".join(body) + "\n"


def emit_yinyang():
    """$900E yin-yang total: add the score flag $AA08/$AA48 to the running
    half-point total $AA01/$AA41 (one fighter per call)."""
    return f"""
;-------------------------------------------------------------------
; YINYNG - $900E yin-yang total (state part; image draws are separate).
YINYNG: TSTB    {g(0xAA08)}
        BEQ     1$
        MOVB    {g(0xAA01)},R0
        BIC     #177400,R0
        MOVB    {g(0xAA08)},R1
        BIC     #177400,R1
        ADD     R1,R0
        MOVB    R0,{g(0xAA01)}
        RTS     PC
1$:     TSTB    {g(0xAA48)}
        BEQ     9$
        MOVB    {g(0xAA41)},R0
        BIC     #177400,R0
        MOVB    {g(0xAA48)},R1
        BIC     #177400,R1
        ADD     R1,R0
        MOVB    R0,{g(0xAA41)}
9$:     RTS     PC
"""


def emit_timetik():
    """$9CA0 time tick: decrement the round-time counter $9CA5."""
    return f"""
;-------------------------------------------------------------------
; TIMTIK - $9CA0 time tick.
TIMTIK: DECB    {g(0x9CA5)}
        RTS     PC
"""


def emit_ranktk():
    """$AF27 rank/round counter $AF34 (1..3, wrapping 4 -> 1)."""
    return f"""
;-------------------------------------------------------------------
; RANKTK - $AF27 dan/round counter.
RANKTK: INCB    {g(0xAF34)}
        MOVB    {g(0xAF34)},R0
        BIC     #177400,R0
        CMP     R0,#4
        BNE     9$
        MOV     #1,R0
        MOVB    R0,{g(0xAF34)}
9$:     RTS     PC
"""


def emit_rstacf():
    """$ACF0 exchange reset: clear recovery/guard flags, park both upright."""
    return f"""
;-------------------------------------------------------------------
; RSTACF - $ACF0 exchange reset (state part; the redraw-wait loop is separate).
RSTACF: CLRB    {g(0xAA0D)}
        CLRB    {g(0xAA0B)}
        CLRB    {g(0xAA4B)}
        CLRB    {g(0xAA16)}
        CLRB    {g(0xAA56)}
        MOV     #172,R0
        MOVB    R0,{g(0xAA18)}
        MOVB    R0,{g(0xAA58)}
        RTS     PC
"""


def emit_contact():
    """$AE2E contact flag $C427: on if either fighter is mid-strike or P1 in the
    $11 lunge; off otherwise."""
    return f"""
;-------------------------------------------------------------------
; CONTACT - $AE2E contact flag $C427.
CONTACT:MOVB    {g(0xAA44)},R0
        BIC     #177400,R0
        CMP     R0,#21                 ; P2 action == $11 -> off
        BNE     1$
        CLRB    {g(0xC427)}
        RTS     PC
1$:     MOVB    {g(0xAA04)},R0
        BIC     #177400,R0
        CMP     R0,#21                 ; P1 action == $11 -> on
        BNE     2$
        MOV     #1,R1
        MOVB    R1,{g(0xC427)}
        RTS     PC
2$:     TSTB    {g(0xA90D, 'R0')}      ; P1 striking -> off (R0 = P1 action)
        BNE     3$
        MOVB    {g(0xAA44)},R0
        BIC     #177400,R0
        TSTB    {g(0xA90D, 'R0')}      ; P2 striking -> on
        BNE     4$
3$:     CLRB    {g(0xC427)}
        RTS     PC
4$:     MOV     #1,R1
        MOVB    R1,{g(0xC427)}
        RTS     PC
"""


def emit_rstfrm():
    """$9CA8 head: per-frame default-state reset before the input/AI chain."""
    body = ["", ";-------------------------------------------------------------------",
            "; RSTFRM - $9CA8 head: per-frame state reset ($A645 RNG seed omitted).",
            "RSTFRM:"]
    for a in (0x9C2B, 0x9CA7, 0xAA4D, 0xAA0D, 0xAA03, 0xAA43, 0xAA16, 0xAA56,
              0xAA17, 0xAA0B, 0xAA4B, 0xAA09, 0xAA49, 0x9C28):
        body.append(f"        CLRB    {g(a)}")
    body.append(f"        MOV     #40,R0")
    body.append(f"        MOVB    R0,{g(0xAA19)}")        # $20
    body.append(f"        MOV     #74,R0")
    body.append(f"        MOVB    R0,{g(0xAA59)}")        # $3C
    body.append(f"        MOV     #172,R0")               # $7A
    body.append(f"        MOVB    R0,{g(0xAA18)}")
    body.append(f"        MOVB    R0,{g(0xAA58)}")
    body.append(f"        MOV     #27,R0")                # $17
    for a in (0xAA0C, 0xAA4C, 0xAA05, 0xAA45, 0xAA04, 0xAA44):
        body.append(f"        MOVB    R0,{g(a)}")
    body.append(f"        MOV     #1,R0")
    for a in (0xAA0A, 0xAA4A, 0xAA57):
        body.append(f"        MOVB    R0,{g(a)}")
    body.append("        RTS     PC")
    return "\n".join(body) + "\n"


def emit_score():
    """$AF36 award_points: credit a yin-yang point.  Score flag $AA08(P1)/
    $AA48(P2) = 1 half / 2 full; value from $B00B[$AA3F], halved for a half
    point, into $AA02/$AA42 + the BCD display buffer ($B02D/$B030).  The Z80
    uses ADD/DAA; PDP-11 has neither DAA nor SUBB, so BCDADD does BCD addition
    by digit decomposition (matches the reference's decimal arithmetic)."""
    return f"""
;-------------------------------------------------------------------
; AWARD - $AF36 award_points.  R0/R1 scratch, R5 = BCD buffer addr.
AWARD:  TSTB    {g(0xAA08)}            ; P1 scored?
        BEQ     5$
        MOVB    {g(0xAA3F)},R0         ; b = $B00B[attacker action]
        BIC     #177400,R0
        MOVB    {g(0xB00B, 'R0')},R1
        BIC     #177400,R1
        MOVB    {g(0xAA08)},R0
        CMP     R0,#1                  ; half point -> b >>= 1
        BNE     1$
        ASR     R1
        BIC     #177400,R1
1$:     MOVB    {g(0xAA02)},R0         ; $AA02 += b
        BIC     #177400,R0
        ADD     R1,R0
        MOVB    R0,{g(0xAA02)}
        MOV     #45101.,R5             ; score_calc into $B02D
        JSR     PC,SCORE
        CLRB    {g(0xAA08)}
        RTS     PC
5$:     TSTB    {g(0xAA48)}            ; P2 scored?
        BEQ     9$
        MOVB    {g(0xAA3F)},R0
        BIC     #177400,R0
        MOVB    {g(0xB00B, 'R0')},R1
        BIC     #177400,R1
        MOVB    {g(0xAA48)},R0
        CMP     R0,#1
        BNE     6$
        ASR     R1
        BIC     #177400,R1
6$:     MOVB    {g(0xAA42)},R0         ; $AA42 += b
        BIC     #177400,R0
        ADD     R1,R0
        MOVB    R0,{g(0xAA42)}
        MOV     #45104.,R5             ; score_calc into $B030
        JSR     PC,SCORE
        CLRB    {g(0xAA48)}
9$:     RTS     PC

;-------------------------------------------------------------------
; SCORE - $AFC2.  R5 = 3-byte LE BCD buffer (Spectrum addr); R1 = b.
; Adds b (BCD) with carry across the three bytes ($0A remaps to $10).
SCORE:  CMP     R1,#12                 ; b == $0A -> $10
        BNE     1$
        MOV     #20,R1
1$:     CLR     R4                     ; carry_in = 0
        MOVB    {gp('R5')},R0          ; byte 0 += b
        BIC     #177400,R0
        JSR     PC,BCDADD
        MOVB    R0,{gp('R5')}
        CLR     R1                     ; bytes 1,2 add only the carry
        INC     R5
        MOVB    {gp('R5')},R0
        BIC     #177400,R0
        JSR     PC,BCDADD
        MOVB    R0,{gp('R5')}
        INC     R5
        MOVB    {gp('R5')},R0
        BIC     #177400,R0
        JSR     PC,BCDADD
        MOVB    R0,{gp('R5')}
        RTS     PC

;-------------------------------------------------------------------
; BCDADD - R0 = x (BCD), R1 = addend (BCD), R4 = carry_in.  Returns
; R0 = BCD result, R4 = carry_out.  R2/R3 scratch; R1,R5 preserved.
BCDADD: MOV     R0,R2                  ; lo = (x & $F) + (addend & $F) + carry
        BIC     #177760,R2
        MOV     R1,R3
        BIC     #177760,R3
        ADD     R3,R2
        ADD     R4,R2
        MOV     R0,R3                  ; hi = (x >> 4) + (addend >> 4)
        ASR     R3
        ASR     R3
        ASR     R3
        ASR     R3
        BIC     #177760,R3
        MOV     R1,R0
        ASR     R0
        ASR     R0
        ASR     R0
        ASR     R0
        BIC     #177760,R0
        ADD     R0,R3
        CMP     R2,#12                 ; lo >= 10 -> borrow into hi
        BLO     1$
        SUB     #12,R2
        INC     R3
1$:     CLR     R4                     ; hi >= 10 -> carry_out
        CMP     R3,#12
        BLO     2$
        SUB     #12,R3
        MOV     #1,R4
2$:     MOV     R3,R0                  ; result = (hi << 4) | lo
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0
        BIS     R2,R0
        RTS     PC
"""


def emit_apply(label, act, aface, tface, react):
    """apply_hit: set the opponent reaction `react` ($AA43 for the P1 attacker
    $9E7F / $AA03 for the P2 attacker $A01C) and the hit value $B150 from the
    attacker action `act`, keyed on facing (`aface` vs `tface`) and the
    $B47E[d] heavy-action table.  $AA3F/$A073/$B150 are shared."""
    return f"""
;-------------------------------------------------------------------
; {label} - apply_hit.  Sets opponent reaction {react:#06x} + $B150.
{label}: MOVB    {g(act)},R1            ; d = attacker action
        BIC     #177400,R1
        MOVB    R1,{g(0xAA3F)}
        MOVB    {g(0xA073, 'R1')},R0   ; $B150 = m[$A073 + d]
        MOVB    R0,{g(0xB150)}
        MOVB    {g(aface)},R0          ; same facing ?
        CMPB    R0,{g(tface)}
        BNE     2$
        TSTB    {g(0xB47E, 'R1')}      ; same: $16 if B47E[d] else $1A
        BNE     1$
        MOV     #32,R0
        BR      8$
1$:     MOV     #26,R0
        BR      8$
2$:     TSTB    {g(0xB47E, 'R1')}      ; differ + B47E[d] != 0 -> $1A
        BEQ     3$
        MOV     #32,R0
        BR      8$
3$:     CMP     R1,#30                 ; heavy actions ($18,$07,$0C) -> $1B,B150=4
        BEQ     4$
        CMP     R1,#7
        BEQ     4$
        CMP     R1,#14
        BEQ     4$
        MOV     #26,R0                 ; else $16
        BR      8$
4$:     MOV     #4,R0
        MOVB    R0,{g(0xB150)}
        MOV     #33,R0
8$:     MOVB    R0,{g(react)}
        RTS     PC
"""


def emit_bf13():
    """$BF13 logic->graphics bridge: copy each fighter's logic state ($AA..)
    into the render area ($C41B-$C420), resolve the pose-record pointers
    ($C428/$C42A = $44CC + word[$C440 + 2*frame-index]), build the erase+redraw
    bounding box ($C434-$C43B) over this frame's and last frame's positions
    (read OLD $C42C-$C433 FIRST), then save this frame's positions to
    $C42C-$C433.  Scratch bytes BIX1/BIX2/BIY1/BIY2 hold the pose-extent deltas
    m[ix+1..2]/m[iy+1..2]; SUMX1/SUMX2/SUMY1/SUMY2 hold the clamped position+
    extent sums (each reused by both the bbox max and the saved position)."""
    n = [0]
    def mm(op, a, b, dst):                    # min/max of two unsigned bytes
        n[0] += 1; lbl = n[0]
        br = "BLOS" if op == "min" else "BHIS"
        return (f"        MOVB    {a},R0\n"
                f"        CMPB    R0,{b}\n"
                f"        {br}    {lbl}$\n"
                f"        MOVB    {b},R0\n"
                f"{lbl}$:     MOVB    R0,{dst}\n")
    def sum8(a, b, dst):                      # dst = (m[a] + m[b]) & 0xFF
        return (f"        MOVB    {a},R0\n"
                f"        MOVB    {b},R1\n"
                f"        ADD     R1,R0\n"
                f"        MOVB    R0,{dst}\n")
    s = f"""
;-------------------------------------------------------------------
; BF13 - $BF13 logic->graphics bridge.
BF13:   MOVB    {g(0xAA52)},{g(0xC425)}
        CLRB    {g(0xC426)}
        MOVB    {g(0xAA59)},{g(0xC41D)}
        MOVB    {g(0xAA58)},{g(0xC41E)}
        MOVB    {g(0xAA57)},{g(0xC420)}
        MOVB    {g(0xAA12)},{g(0xC423)}
        CLRB    {g(0xC424)}
        MOVB    {g(0xAA19)},{g(0xC41B)}
        MOVB    {g(0xAA18)},{g(0xC41C)}
        MOVB    {g(0xAA17)},{g(0xC41F)}
        ; pose-record pointer P1 = $44CC + word[$C440 + 2*m[$C423]]
        MOVB    {g(0xC423)},R0
        BIC     #177400,R0
        ASL     R0
        MOV     {g(0xC440, 'R0')},R1
        ADD     #{0x44CC:o},R1
        MOV     R1,{g(0xC428)}
        ; pose-record pointer P2 = $44CC + word[$C440 + 2*m[$C425]]
        MOVB    {g(0xC425)},R0
        BIC     #177400,R0
        ASL     R0
        MOV     {g(0xC440, 'R0')},R1
        ADD     #{0x44CC:o},R1
        MOV     R1,{g(0xC42A)}
        ; pose-extent deltas: ix1=m[ix+1] ix2=m[ix+2] iy1=m[iy+1] iy2=m[iy+2]
        MOV     {g(0xC428)},R2
        MOVB    GST-116000+1(R2),BIX1
        MOVB    GST-116000+2(R2),BIX2
        MOV     {g(0xC42A)},R2
        MOVB    GST-116000+1(R2),BIY1
        MOVB    GST-116000+2(R2),BIY2
"""
    s += sum8(g(0xC41B), "BIX1", "SUMX1")
    s += sum8(g(0xC41C), "BIX2", "SUMX2")
    s += sum8(g(0xC41D), "BIY1", "SUMY1")
    s += sum8(g(0xC41E), "BIY2", "SUMY2")
    # DELIBERATE DEVIATION: the original's box is the union of this frame's and
    # LAST frame's extents because it erases (restores background under) the old
    # sprite through the same box.  This port's compositor rebuilds the whole band
    # every frame, so nothing needs erasing - and after a teleport (RSTFRM's
    # exchange reset) the union spanned the old and the new position, far wider
    # than the 884-byte per-fighter compose buffer: the sprite came out cut at
    # the waist with its legs displaced.  The box is this frame's extents only.
    s += "        ; bounding box = this frame's sprite extents (no erase union)\n"
    s += f"        MOVB    {g(0xC41B)},{g(0xC434)}\n"
    s += f"        MOVB    SUMX1,{g(0xC435)}\n"
    s += f"        MOVB    {g(0xC41C)},{g(0xC436)}\n"
    s += f"        MOV     #276,R0\n        MOVB    R0,{g(0xC437)}\n"
    s += f"        MOVB    {g(0xC41D)},{g(0xC438)}\n"
    s += f"        MOVB    SUMY1,{g(0xC439)}\n"
    s += f"        MOVB    {g(0xC41E)},{g(0xC43A)}\n"
    s += f"        MOV     #276,R0\n        MOVB    R0,{g(0xC43B)}\n"
    s += "        ; save this frame's positions to $C42C-$C433\n"
    s += f"        MOVB    {g(0xC41B)},{g(0xC42C)}\n"
    s += f"        MOVB    SUMX1,{g(0xC42D)}\n"
    s += f"        MOVB    {g(0xC41C)},{g(0xC42E)}\n"
    s += f"        MOVB    SUMX2,{g(0xC42F)}\n"
    s += f"        MOVB    {g(0xC41D)},{g(0xC430)}\n"
    s += f"        MOVB    SUMY1,{g(0xC431)}\n"
    s += f"        MOVB    {g(0xC41E)},{g(0xC432)}\n"
    s += f"        MOVB    SUMY2,{g(0xC433)}\n"
    s += ("        ; --- second pass ($C03B): if the two fighters' boxes are\n"
          "        ;     close, MERGE them into one combined box + compute the\n"
          "        ;     fighter dimensions; else ($C101 path) the per-fighter\n"
          "        ;     boxes stand and the draw handles each separately. ---\n")
    # width/height of the box $C434-$C437 -> $C40A/$C40F/$C409 (+ top $C41A).  The
    # MERGE path runs this after combining the two fighters' boxes.  The SEPARATE path
    # (fighters far apart) MUST run it too: the first pass already set $C434/$C435 to
    # the span of both fighters, but the original $C101 path left $C40A/$C409 STALE
    # (it draws each fighter in its own box).  Our port composes both fighters into one
    # buffer, so a stale-narrow $C40A makes the decode wrap the moved fighter past the
    # buffer stride -> the sprite splits into horizontal lines.  Recomputing the stride
    # from the real span keeps the single-box draw coherent.
    dims = ("        ; width = ((C435-C434) >> 2) + 2  ->  $C40A / $C40F\n"
            f"        MOVB    {g(0xC435)},R0\n        BIC     #177400,R0\n"
            f"        MOVB    {g(0xC434)},R1\n        BIC     #177400,R1\n"
            f"        SUB     R1,R0\n        ASR     R0\n        ASR     R0\n        ADD     #2,R0\n"
            f"        MOVB    R0,{g(0xC40A)}\n        MOVB    R0,{g(0xC40F)}\n"
            "        ; height = C437 - C436  ->  $C409 ;  top $C41A := C436\n"
            f"        MOVB    {g(0xC437)},R0\n        BIC     #177400,R0\n"
            f"        MOVB    {g(0xC436)},R1\n        BIC     #177400,R1\n"
            f"        SUB     R1,R0\n        MOVB    R0,{g(0xC409)}\n"
            f"        MOVB    {g(0xC436)},{g(0xC41A)}\n")
    # Always keep the per-fighter boxes: box A ($C434-$C437) = P1, box B ($C438-$C43B)
    # = P2.  The original MERGES them when close and draws both into one buffer, but our
    # bank-6 FBUF (~1.2 KB) can't hold a tall merged box (a jumper spanning down to the
    # grounded fighter) -> overflow/garbage.  So we always take the original's $C101
    # separate path: C101C/C1CC decode each fighter in its OWN box (<= 884 B) and the
    # compositor blits them side by side.  (dims above are recomputed per-box by C101C.)
    del dims
    s += "        ; second pass omitted: per-fighter boxes ($C101 path) - see C101C/C1CC\n"
    s += "        RTS     PC\n"
    s += "        .EVEN\n"
    s += "BIX1:   .BYTE   0\nBIX2:   .BYTE   0\nBIY1:   .BYTE   0\nBIY2:   .BYTE   0\n"
    s += "SUMX1:  .BYTE   0\nSUMX2:  .BYTE   0\nSUMY1:  .BYTE   0\nSUMY2:  .BYTE   0\n"
    s += "        .EVEN\n"
    return s


def emit_95e1():
    """$95E1 anim advance (entry R5 = hl = $AA16/$AA56).  base = hl-10 ($AA0C)
    kept in R5.  PHASE scratch = m[hl].  Sub-blocks: ANIM5E dispatch, POSUPD
    ($9698 position tail), META ($9649 sprite-meta load -> 0xFF fill -> $9698),
    ADV962B (frame-pointer +/-4), FL96CE (new-frame-load from the $9368 table),
    SWAP71 ($971E facing swap), RESET5 ($9613 reset).  The frame pointer and the
    $9368 table live in the low-data mirror LDAT; the sprite-meta source tables
    ($3900/$3A00) are ROM filler so the meta is a 0xFF fill."""
    return f"""
;-------------------------------------------------------------------
; ANIM5E - $95E1 per-fighter animation advance.  R5 = hl at entry.
ANIM5E: MOVB    {gp('R5')},R0          ; phase = m[hl]
        BIC     #177400,R0
        MOVB    R0,PHASE
        SUB     #10.,R5                ; R5 = base = hl-10 (decimal 10)
        TSTB    {gb('R5', 0)}          ; m[$AA0C]==0 -> RET
        BNE     1$
        RTS     PC
1$:     TSTB    {gb('R5', -1)}         ; m[$AA0B]==0 -> $96CE new-frame-load
        BNE     2$
        JSR     PC,FL96CE
        JMP     META
2$:     TSTB    PHASE
        BNE     20$
        ; ---- phase == 0 ----
        TSTB    {gb('R5', 4)}          ; a = m[$AA10]; if 0 -> $9609
        BNE     12$
        TSTB    {gb('R5', 2)}          ; m[$AA0E]==0 -> $9613 reset
        BNE     11$
        JMP     RESET5
11$:    DECB    {gb('R5', 2)}          ; m[$AA0E] -= 1
        JSR     PC,ADV962B
        JMP     META
12$:    DECB    {gb('R5', 4)}          ; m[$AA10] -= 1 ($9605)
        MOV     R5,R1
        ADD     #4.,R1                 ; R1 = rec = $AA10
        JSR     PC,POSUPD
        RTS     PC
        ; ---- phase != 0 ($961A) ----
20$:    MOVB    {gb('R5', 4)},R0       ; a = m[$AA10]
        CMPB    R0,{gb('R5', 5)}       ; a == m[$AA11] ? -> $9623
        BNE     22$
        MOVB    {gb('R5', 3)},R0       ; (m[$AA0F]-1) == m[$AA0E] ? -> $9613
        BIC     #177400,R0
        DEC     R0
        CMPB    R0,{gb('R5', 2)}
        BNE     21$
        JMP     RESET5
21$:    INCB    {gb('R5', 2)}          ; m[$AA0E] += 1 ($962A)
        JSR     PC,ADV962B
        JMP     META
22$:    INCB    {gb('R5', 4)}          ; m[$AA10] += 1 ($961F)
        MOV     R5,R1
        ADD     #4.,R1
        JSR     PC,POSUPD
        RTS     PC

;-------------------------------------------------------------------
; RESET5 - $9613: m[$AA0D]=1, m[$AA0C]=0, return.
RESET5: MOV     #1,R0
        MOVB    R0,{gb('R5', 1)}
        CLRB    {gb('R5', 0)}
        RTS     PC

;-------------------------------------------------------------------
; ADV962B - $962B: m[$AA13]=0, then step the 16-bit frame pointer
; m[$AA14]/m[$AA15] by +4 (phase==0) or -4 (phase!=0).
ADV962B:CLRB    {gb('R5', 7)}          ; m[$AA13]=0
        MOVB    {gb('R5', 8)},R1
        BIC     #177400,R1
        MOVB    {gb('R5', 9)},R0
        BIC     #177400,R0
        SWAB    R0
        BIS     R0,R1                  ; R1 = pointer
        TSTB    PHASE
        BNE     1$
        ADD     #4,R1                  ; phase==0 -> +4
        BR      2$
1$:     SUB     #4,R1                  ; phase!=0 -> -4
2$:     MOVB    R1,{gb('R5', 8)}
        SWAB    R1
        MOVB    R1,{gb('R5', 9)}
        RTS     PC

;-------------------------------------------------------------------
; META - $9649: ptr=m[$AA14/15]; stage frame data into $AA10-$AA13; fill the
; sprite-meta $AA1A-$AA2B with 0xFF; optional $971E swap; then $9698.
META:   MOVB    {gb('R5', 8)},R1
        BIC     #177400,R1
        MOVB    {gb('R5', 9)},R0
        BIC     #177400,R0
        SWAB    R0
        BIS     R0,R1                  ; R1 = ptr (LDAT addr)
        MOV     #1,R0
        MOVB    R0,{gb('R5', 4)}       ; m[$AA10]=1
        MOVB    {lpb('R1', 0)},R2      ; v = m[ptr]
        BIC     #177400,R2
        TSTB    PHASE
        BNE     1$
        MOV     R2,R0                  ; phase==0 -> m[$AA10]=v-1
        DEC     R0
        MOVB    R0,{gb('R5', 4)}
1$:     MOVB    R2,{gb('R5', 5)}       ; m[$AA11]=v
        MOVB    {lpb('R1', 1)},R3      ; a2 = m[ptr+1]
        MOVB    R3,{gb('R5', 6)}       ; m[$AA12]=a2
        MOVB    R3,{gb('R5', 7)}       ; m[$AA13]=a2
        ; sprite-meta = 0xFF fill, $AA1A..$AA2B (18 bytes)
        MOV     R5,R1
        ADD     #14.,R1
        MOV     #18.,R2
        MOV     #377,R0
2$:     MOVB    R0,{gp('R1')}
        INC     R1
        DEC     R2
        BNE     2$
        ; facing swap if m[$AA17]==1
        MOVB    {gb('R5', 11)},R0
        BIC     #177400,R0
        CMP     R0,#1
        BNE     3$
        JSR     PC,SWAP71
3$:     MOV     R5,R1
        ADD     #4.,R1
        JSR     PC,POSUPD
        RTS     PC

;-------------------------------------------------------------------
; SWAP71 - $971E: in each of the 3 six-byte meta groups (base+14/+20/+26),
; swap [0]<->[4] and [1]<->[5].
SWAP71: MOV     R5,R2
        ADD     #14.,R2                ; first group at base+14
        MOV     #3,R4                  ; 3 groups
1$:     MOV     #2,R3                  ; 2 pairs per group
        MOV     R2,R1
2$:     MOVB    {gp('R1')},R0          ; tmp = m[i]
        MOVB    {gb('R1', 4)},{gp('R1')}   ; m[i] = m[i+4]
        MOVB    R0,{gb('R1', 4)}       ; m[i+4] = tmp
        INC     R1
        DEC     R3
        BNE     2$
        ADD     #6,R2                  ; next group
        DEC     R4
        BNE     1$
        RTS     PC

;-------------------------------------------------------------------
; POSUPD - $9698: advance position.  R1 = rec ($AA10).
POSUPD: MOVB    {gb('R1', 4)},R2       ; ptr = m[rec+4] | m[rec+5]<<8
        BIC     #177400,R2
        MOVB    {gb('R1', 5)},R3
        BIC     #177400,R3
        SWAB    R3
        BIS     R3,R2                  ; R2 = ptr (LDAT addr)
        MOVB    {lpb('R2', 2)},R3      ; m[rec+8] += m[ptr+2]
        MOVB    {gb('R1', 8)},R4
        ADD     R4,R3
        MOVB    R3,{gb('R1', 8)}
        MOVB    {gb('R1', 6)},R0       ; flag = m[rec+6]^m[rec+7]
        BIC     #177400,R0
        MOVB    {gb('R1', 7)},R3
        BIC     #177400,R3
        XOR     R3,R0
        MOVB    {lpb('R2', 3)},R4      ; b = m[ptr+3]
        BIC     #177400,R4
        MOVB    {gb('R1', 9)},R3       ; pos = m[rec+9]
        BIC     #177400,R3
        TST     R0
        BNE     1$
        ADD     R4,R3                  ; flag==0 -> pos + b
        BR      2$
1$:     SUB     R4,R3                  ; flag!=0 -> pos - b
2$:     BIC     #177400,R3             ; r = result & 0xFF
        CMP     R3,#310                ; >= 0xC8 -> 0
        BHIS    3$
        CMP     R3,#137                ; < 0x5F -> keep
        BLO     4$
        MOV     #137,R3                ; else 0x5F
        BR      4$
3$:     CLR     R3
4$:     MOVB    R3,{gb('R1', 9)}
        RTS     PC

;-------------------------------------------------------------------
; FL96CE - $96CE new-frame-load: pull the next animation's pointer pair from
; the $9368 table (LDAT) into m[$AA14/15], seed m[$AA0E]/m[$AA0F].
FL96CE: MOVB    {gb('R5', 0)},R0       ; idx = m[$AA0C]
        BIC     #177400,R0
        MOVB    R0,{gb('R5', -1)}      ; m[$AA0B]=idx
        CLRB    {gb('R5', 1)}          ; m[$AA0D]=0
        ASL     R0                     ; 2*idx
        MOV     #LDAT,R1               ; LDAT stands for $9368
        ADD     R0,R1                  ; R1 = &word1 = LDAT + 2*idx
        MOV     (R1),R2                ; word1 (LDAT is even-based; even offset)
        MOV     2(R1),R3               ; ptr  = word at +2
        MOV     R3,R0
        SUB     R2,R0                  ; span = ptr - word1
        CLC                            ; logical span >> 2 (SRL H; RR L x2)
        ROR     R0
        CLC
        ROR     R0
        BIC     #177400,R0             ; B (0..255)
        ; choose stored pointer: phase!=0 -> ptr-4 ; phase==0 -> word1
        TSTB    PHASE
        BEQ     1$
        MOV     R3,R4
        SUB     #4,R4                  ; ptr-4
        BR      2$
1$:     MOV     R2,R4                  ; word1
2$:     MOVB    R4,{gb('R5', 8)}       ; m[$AA14]=low
        SWAB    R4
        MOVB    R4,{gb('R5', 9)}       ; m[$AA15]=high
        CLRB    {gb('R5', 2)}          ; m[$AA0E]=0
        TSTB    PHASE
        BNE     3$
        MOV     R0,R4                  ; phase==0 -> m[$AA0E]=B-1
        DEC     R4
        MOVB    R4,{gb('R5', 2)}
3$:     MOVB    R0,{gb('R5', 3)}       ; m[$AA0F]=B
        RTS     PC

PHASE:  .BYTE   0
        .EVEN
"""


# name -> (addr, label, emit, refapply(m,regs), win_end, reg_setup, witness, budget)
_B = 4000000
ROUTINES = {
    "timer": (0x9C6F, "TIMER", emit_timer,
              lambda m, r: ref.update_timer(m), 0xAB00, [], None, _B),
    "recover": (0x9AD7, "RECOV", emit_recover,
                lambda m, r: ref.recover_9ad7(m, r['C']), 0xB500,
                [("R4", "C")], None, _B),
    # win_end $C000: the $A98A/$A9BC reach pointers index data up in the
    # $BB00.. region, which must be inside the window for the m[ptr+ridx] read.
    "hitdet": (0x9D29, "HITDET", lambda: emit_hitdet("HITDET", ref.HIT_P1),
               lambda m, r: ref.hit_detect(m, ref.HIT_P1), 0xC000, [], 0xA06F, _B),
    "hitdp2": (0x9ED2, "HITDP2", lambda: emit_hitdet("HITDP2", ref.HIT_P2),
               lambda m, r: ref.hit_detect(m, ref.HIT_P2), 0xC000, [], 0xA06F, _B),
    "anim": (0x97BB, "UPDFGT", emit_anim,
             lambda m, r: ref.update_fighter(m, r['C']), 0xB500,
             [("R4", "C")], None, _B),
    "apply": (0xA01C, "APLYHT",
              lambda: emit_apply("APLYHT", 0xAA44, 0xAA57, 0xAA17, 0xAA03),
              lambda m, r: ref.apply_hit(m, ref.HIT_P2), 0xB500, [], 0xAA3F, _B),
    # applyp1 ($9E7F) is never reached in the attract demo (P1 lands no hit);
    # it is verified by symmetry with APLYHT and assembled in the combined build.
    "score": (0xAF36, "AWARD", emit_score,
              lambda m, r: ref.award_points(m), 0xB100, [], None, _B),
    "yinyang": (0x900E, "YINYNG", emit_yinyang,
                lambda m, r: ref.yinyang_total(m), 0xAB00, [], None, _B),
    "timetik": (0x9CA0, "TIMTIK", emit_timetik,
                lambda m, r: ref.time_tick(m), 0xAB00, [], None, _B),
    "ranktk": (0xAF27, "RANKTK", emit_ranktk,
               lambda m, r: ref.rank_tick(m), 0xB000, [], None, 40000000),
    "rstacf": (0xACF0, "RSTACF", emit_rstacf,
               lambda m, r: ref.reset_acf0(m), 0xAB00, [], None, 40000000),
    "contact": (0xAE2E, "CONTACT", emit_contact,
                lambda m, r: ref.contact_flag(m), 0xC500, [], None, _B),
    "rstfrm": (0x9CA8, "RSTFRM", emit_rstfrm,
               lambda m, r: ref.reset_frame_9ca8(m), 0xAB00, [], None, _B),
    "a402": (0xA402, "A402", emit_a402,
             lambda m, r: ref.ai_load_params(m), 0xB500, [], None, _B),
}


# ── program skeleton ──────────────────────────────────────────────────────────

HEADER = r"""        .TITLE  FIST
;
; The Way Of The Exploding Fist - MS-0515 port: game-logic unit test.
; Generated by source/gamelogic_mac.py - do not edit by hand.
;
; Runs one ported game-logic routine against a real captured state held in
; the mirror block GST ($9C00..$AB00), then copies the window to VRAM so the
; VRAM oracle can dump it for a byte-exact check against the Python reference.
;

DPRAM  = 157700
DISPAT = 177400
SYSC   = 177604
VRAM   = 40000
KBST   = 177442
KBDT   = 177440

        .ASECT                          ; absolute layout: code at 01000, so a
        . = 1000                        ; high GST (.=100000) can sit in banks 4-7
        .EVEN
START:  MOV     #340,R0
        MTPS    R0
        MOV     @#DISPAT,ORIGDP
        MOVB    @#SYSC,ORIGRC
        MOVB    @#SYSC,R0
        BIC     #17,R0
        MOVB    R0,@#SYSC               ; medium 320x200 colour, black border
        MOV     #3377,@#DPRAM
        MOV     #3377,@#DISPAT          ; VRAM window @40000 enabled

%REGSET%        JSR     PC,%ENTRY%      ; run the ported routine on GST

        ; --- copy the state window GST[0..WIN] to VRAM low bytes ---
        MOV     #GST,R1
        MOV     #VRAM,R2
        MOV     #%WORDS%,R3
1$:     MOV     (R1)+,(R2)+
        DEC     R3
        BNE     1$

WKEY:   MOV     @#KBST,R0
        BIT     #2,R0
        BEQ     WKEY
        MOV     @#KBDT,R0
        MOV     ORIGDP,@#DPRAM
        MOV     ORIGDP,@#DISPAT
        MOVB    ORIGRC,@#SYSC
        EMT     350

ORIGDP: .WORD   0
ORIGRC: .WORD   0
"""


def _emit_window(label, data, per=16):
    out = [f"{label}:"]
    for i in range(0, len(data), per):
        out.append("        .BYTE   " + ",".join(f"{b}." for b in data[i:i + per]))
    return "\n".join(out) + "\n"


def main_ai():
    """AI ($A090): captured with its recorded RNG stream, which is embedded as
    ARND and replayed by ARNG so the decision logic is checked bit-exactly."""
    win_end = 0xB500
    win_size = win_end - GBASE
    snap, randoms = capture_ai(0xA090, lambda m, rs: ref.ai_decide(m, rs),
                               win_end)
    expected = bytearray(snap)
    ref.ai_decide(expected, list(randoms))
    EXP_BIN.write_bytes(bytes(expected[GBASE:win_end]))
    WIN_JSON.write_text(json.dumps({"base": GBASE, "size": win_size}))
    src = HEADER + emit_ai(randoms)
    src += "\n        .EVEN\n"
    src += _emit_window("GST", snap[GBASE:win_end])
    src += "\n        .EVEN\n        .END    START\n"
    src = (src.replace("%ENTRY%", "AIDEC").replace("%REGSET%", "")
              .replace("%WORDS%", str(win_size // 2) + "."))
    src.encode("ascii")
    OUT_MAC.write_text(src, encoding="ascii", newline="\r\n")
    print(f"gamelogic_mac: wrote {OUT_MAC} (ai @ 0xa090, window {win_size} B, "
          f"{len(randoms)} randoms, expected -> {EXP_BIN.name})")


def main_combined():
    """Integration build: all routines in one module + the ORCH driver,
    verified against orch_subset over the GST window."""
    win_end = 0xC000                     # covers the hit-detect reach data $BB00..
    win_size = win_end - GBASE
    snap, randoms = capture_ai(0x9745, lambda m, rs: orch_subset(m, list(rs)),
                               win_end)
    expected = bytearray(snap)
    orch_subset(expected, list(randoms))
    EXP_BIN.write_bytes(bytes(expected[GBASE:win_end]))
    WIN_JSON.write_text(json.dumps({"base": GBASE, "size": win_size}))
    src = HEADER + emit_combined(randoms)
    # GST in the high banks (4-7, octal 0100000+) - above the VRAM window, so
    # it is not capped by the 040000 boundary and loads fine with VRAM on.
    src += "\n        .ASECT\n        . = 100000\n"
    src += _emit_window("GST", snap[GBASE:win_end])
    src += "\n        .EVEN\n        .END    START\n"
    src = (src.replace("%ENTRY%", "ORCH").replace("%REGSET%", "")
              .replace("%WORDS%", str(win_size // 2) + "."))
    src.encode("ascii")
    OUT_MAC.write_text(src, encoding="ascii", newline="\r\n")
    print(f"gamelogic_mac: wrote {OUT_MAC} (combined, all routines + ORCH, "
          f"window {win_size} B, expected -> {EXP_BIN.name})")


def main_fullframe():
    """The EXACT full $9745 frame in one module (FIST_GL=fullframe): combined +
    the animation advance ($95E1 x2) + the draw bridge ($BF13), in $9745's real
    order, verified against gamelogic_ref.frame_9745.  Decouples the GST data
    extent ($E000, holds the pose records bf13 dereferences) from the 16 KB
    VRAM-mirror verify window ($DC00, covers every changed cell $9C00-$C43B), and
    adds the $95E1 low-data mirror LDAT ($9368-$9600)."""
    data_end = 0xE000                     # GST data extent (pose records etc.)
    verify_end = 0xDC00                   # VRAM-mirror compare window = 16 KB
    verify_size = verify_end - GBASE
    ldat_base, ldat_end = 0x9368, 0x9600

    def safe_frame(m, rs):
        # round-end frames ($9C2C==2) aren't modelled yet; leave m unchanged so
        # capture_ai treats them as non-exercising and looks for an in-round frame.
        tmp = bytearray(m)
        try:
            ref.frame_9745(tmp, list(rs))
        except NotImplementedError:
            return
        m[:] = tmp

    snap, randoms = capture_ai(0x9745, safe_frame, verify_end)
    expected = bytearray(snap)
    ref.frame_9745(expected, list(randoms))
    EXP_BIN.write_bytes(bytes(expected[GBASE:verify_end]))
    WIN_JSON.write_text(json.dumps({"base": GBASE, "size": verify_size}))
    src = HEADER + emit_fullframe(randoms)
    src += "\n        .ASECT\n        . = 100000\n"
    src += _emit_window("GST", snap[GBASE:data_end])
    src += "\n        .EVEN\n"
    src += _emit_window("LDAT", snap[ldat_base:ldat_end])
    src += "\n        .EVEN\n        .END    START\n"
    src = (src.replace("%ENTRY%", "ORCH").replace("%REGSET%", "")
              .replace("%WORDS%", str(verify_size // 2) + "."))
    src.encode("ascii")
    OUT_MAC.write_text(src, encoding="ascii", newline="\r\n")
    print(f"gamelogic_mac: wrote {OUT_MAC} (FULL FRAME: exact $9745 + 95e1 + bf13, "
          f"window {verify_size} B, expected -> {EXP_BIN.name})")


# Full game-data block: the SHARED GST for the unified render image (logic state
# $AA, render area $C4xx, pose data $C427+, the $B500-$B900 decoder tables, and
# the compose buffer $F730).  The compose buffer is 884 bytes ($F730..$FAA4), so
# the block must reach $FAA4 (NOT the nominal $F801).  At .=100000 the 24228-byte
# block spans 0100000..0157244 - still fits banks 4-6, below bank 7 (0160000).
GST_FULL_END = 0xFAA4


def main_render():
    """Render foundation (task 11 step 1a): emit the FULL $9C00..$F801 GST (the
    shared data block the unified logic+decoder image will use) and run the
    verified combined logic frame against it, proving the 23 KB GST loads in
    banks 4-6 and the logic still works byte-exact over the 16 KB verify window.
    The decoder/draw get layered on top in later steps."""
    verify_end = 0xC000                   # 16 KB-safe verify window (logic outputs)
    verify_size = verify_end - GBASE
    snap, randoms = capture_ai(0x9745, lambda m, rs: orch_subset(m, list(rs)),
                               verify_end)
    expected = bytearray(snap)
    orch_subset(expected, list(randoms))
    EXP_BIN.write_bytes(bytes(expected[GBASE:verify_end]))
    WIN_JSON.write_text(json.dumps({"base": GBASE, "size": verify_size}))
    src = HEADER + emit_combined(randoms)
    src += "\n        .ASECT\n        . = 100000\n"
    src += _emit_window("GST", snap[GBASE:GST_FULL_END])   # FULL shared GST
    src += "\n        .EVEN\n        .END    START\n"
    src = (src.replace("%ENTRY%", "ORCH").replace("%REGSET%", "")
              .replace("%WORDS%", str(verify_size // 2) + "."))
    src.encode("ascii")
    OUT_MAC.write_text(src, encoding="ascii", newline="\r\n")
    print(f"gamelogic_mac: wrote {OUT_MAC} (render foundation: full GST "
          f"{GST_FULL_END - GBASE} B, verify {verify_size} B -> {EXP_BIN.name})")


def main_loader():
    """FIST_GL=loader - park-RMON full-GST loader, VERIFY stage.

    The full 24 KB GST is embedded in the .SAV at 020000 (banks 1-3, which RT-11
    loads as RAM because the monitor runs with VRAM disabled).  The loader parks
    banks 4-6 (RT-11's USR + RMON) into the extended set, relocates the GST up to
    its runtime home 0100000 (now fresh extended RAM), checksums it there, and
    signals success by a clean .EXIT (a mismatch spins forever - a visible hang).
    This validates the embed -> load -> park -> relocate pipeline end to end; the
    renderer is layered on top in the next stage.  Park-RMON itself is proven in
    rt11_devel/projects/banktest/ (BFLIP)."""
    import setup_ref as sr
    snap, b_in, c_in, pose = sr.capture_c34f_low(0xD400)
    gst = snap[GBASE:GST_FULL_END]
    assert len(gst) % 2 == 0, "GST must be word-aligned"
    nwords = len(gst) // 2
    checksum = 0
    for i in range(0, len(gst), 2):
        checksum = (checksum + gst[i] + (gst[i + 1] << 8)) & 0xFFFF
    src = f"""        .TITLE  FISTLDR
;
; park-RMON full-GST loader (verify stage) - generated, do not edit by hand.
; Memory map under live RT-11 SJ V5.04 (see rt11_devel/projects/banktest):
;   banks 0-3  001000-077777  user area (our code + the GST embed)  PRIMARY
;   banks 4-5  100000-137777  RT-11 USR   - parked to extended, become the
;   bank  6    140000-157777  RMON        - GST 24 KB runtime home
;
        .MCALL  .EXIT
DISPAT = 177400                 ; dispatcher (the only bank control; write-only)
RELOC  = 17                     ; banks 0-3 primary, banks 4-6 EXTENDED, VRAM off
PRIM   = 3177                   ; RT-11 SJ active dispatcher (all banks primary)
GSTLD  = 20000                  ; GST embed/load address  (banks 1-3, primary)
GSTRT  = 100000                 ; GST runtime address     (banks 4-6, extended)
NWORD  = {nwords}.
CKSUM  = {checksum}.

        .ASECT
        . = 1000
        .EVEN
START:  MOV     #340,R0
        MTPS    R0                      ; mask IRQs across the flip
        MOV     #RELOC,@#DISPAT         ; park banks 4-6 to extended (VRAM off)

        ; --- relocate the full GST: 020000 (banks 1-3) -> 100000 (extended 4-6)
        MOV     #GSTLD,R0
        MOV     #GSTRT,R1
        MOV     #NWORD,R2
1$:     MOV     (R0)+,(R1)+
        SOB     R2,1$

        ; --- checksum the GST at its runtime home (16-bit word sum) ---
        MOV     #GSTRT,R0
        MOV     #NWORD,R2
        CLR     R3
2$:     ADD     (R0)+,R3
        SOB     R2,2$

        MOV     #PRIM,@#DISPAT          ; unpark: all banks primary, RMON restored
        CLR     R0
        MTPS    R0                      ; unmask before returning to the monitor
        CMP     R3,#CKSUM               ; GST relocated intact?
        BNE     FAIL
        .EXIT                           ; success: clean return to the dot prompt
FAIL:   BR      FAIL                    ; mismatch -> spin (a visible hang)

        .ASECT
        . = GSTLD
{_emit_window("GSTEMB", gst)}        .EVEN
        .END    START
"""
    src.encode("ascii")
    OUT_MAC.write_text(src, encoding="ascii", newline="\r\n")
    print(f"gamelogic_mac: wrote {OUT_MAC} (LOADER verify: full GST "
          f"{len(gst)} B @020000 -> 0100000, cksum {checksum:06o})")


def main_loaderdat():
    """FIST_GL=loaderdat - chunked-.DAT park-RMON loader, VERIFY stage.  Solves the
    memory wall: the GST ships as GST.DAT (NOT embedded), so the .SAV is code only
    and the full game code fits the 32 KB primary banks later.  The loader
    .LOOKUP/.READW's GST.DAT in 2 chunks into a banks-2-3 buffer (040000, RAM while
    VRAM is off), parks banks 4-6 and copies each chunk to the GST runtime home in
    extended RAM (0100000), checksums it, and clean-.EXITs on match (a failure spins
    - a visible hang).  SABOT2/SAPER .READW pattern (rt11_devel/projects/saper)."""
    import setup_ref as sr
    snap, b_in, c_in, pose = sr.capture_c34f_low(0xD400)
    gst = bytes(snap[GBASE:0xF730])           # GST data ($F730+ compose buffer is scratch)
    if len(gst) % 512:                         # pad to whole 512-byte blocks for .READW
        gst = gst + bytes(512 - (len(gst) % 512))
    nwords = len(gst) // 2
    checksum = sum(gst[i] | (gst[i + 1] << 8)
                   for i in range(0, len(gst), 2)) & 0xFFFF
    blk1 = 24                                   # chunk 1 = 24 blocks = 6144 words (12 KB)
    n1 = blk1 * 256
    n2 = nwords - n1
    (OUT_MAC.parent / "GST.DAT").write_bytes(gst)
    src = f"""        .TITLE  FISTLDR
;
; .DAT park-RMON loader (verify stage) - generated, do not edit by hand.
; GST.DAT is read (one .READW) into a banks-1-3 buffer, then relocated into the
; parked extended banks 4-6 (the GST's runtime home).  The .SAV carries NO GST -
; only this code - so the full game code fits the 32 KB primary budget later.
; RT-11 plumbing learned the hard way (see project_fist_port memory):
;   * run as a COMMAND (R FIST), not RUN FIST, or EMT 375 USR requests -> Invalid EMT;
;   * .FETCH the device handler first, or .LOOKUP -> No device;
;   * set SP explicitly - RT-11's default SP sits inside our read buffer.
;
        .MCALL  .FETCH,.LOOKUP,.READW,.CLOSE,.EXIT
DISPAT = 177400
HSPACE = 2000                   ; device handler loads here (above the code, bank 0)
BUF    = 40000                  ; read buffer = banks 2-3 (RAM while VRAM off)
GSTRT  = 100000                 ; GST runtime home (extended banks 4-6)
EXT    = 17                     ; banks 0-3 primary, banks 4-6 EXTENDED (low byte)
PRIM   = 177                    ; banks 0-6 primary, VRAM off (low byte)
N1     = {n1}.
N2     = {n2}.
NWORD  = {nwords}.
CKSUM  = {checksum}.

        .ASECT
        . = 44
        .WORD   21000                  ; JSW - job/USR flags (a working file-I/O .SAV)
        . = 1000
        .EVEN
START:  MOV     #37776,SP               ; stack above the (future) game code, below BUF
        .FETCH  #HSPACE,#DATFIL         ; load the device handler (DK) into memory
        BCS     FAIL
        .LOOKUP #LKAREA,#0,#DATFIL      ; open GST.DAT on channel 0 (USR; IRQs on)
        BCS     FAIL
        ; --- chunk 1: read N1 words from block 0 into BUF (banks 2-3), park, copy ---
        .READW  #LKAREA,#0,#BUF,#N1,#0
        BCS     FAIL
        MTPS    #340
        MOVB    #EXT,@#DISPAT          ; park banks 4-6 (RMON gone)
        MOV     #BUF,R0
        MOV     #GSTRT,R1
        MOV     #N1,R2
1$:     MOV     (R0)+,(R1)+
        SOB     R2,1$
        MOVB    #PRIM,@#DISPAT         ; unpark - the next .READW needs the USR
        MTPS    #0
        ; --- chunk 2: read N2 words from block N1blk into BUF, park, copy after chunk 1 ---
        .READW  #LKAREA,#0,#BUF,#N2,#{blk1}.
        BCS     FAIL
        MTPS    #340
        MOVB    #EXT,@#DISPAT
        MOV     #BUF,R0
        MOV     #GSTRT+N1+N1,R1        ; dest = right after chunk 1 (N1 words)
        MOV     #N2,R2
2$:     MOV     (R0)+,(R1)+
        SOB     R2,2$
        MOVB    #PRIM,@#DISPAT
        MTPS    #0
        .CLOSE  #0
        MTPS    #340                  ; mask across the checksum park
        MOVB    #EXT,@#DISPAT         ; checksum the GST at GSTRT (banks 4-6 extended)
        MOV     #GSTRT,R0
        MOV     #NWORD,R2
        CLR     R3
3$:     ADD     (R0)+,R3
        SOB     R2,3$
        MOVB    #PRIM,@#DISPAT        ; unpark before returning to the monitor
        MTPS    #0
        CMP     R3,#CKSUM
        BNE     FAIL
        .EXIT                          ; success: clean return to the dot prompt
FAIL:   BR      FAIL                   ; any failure -> spin (a visible hang)

DATFIL: .RAD50  /DK GST   DAT/
        .EVEN
LKAREA: .BLKW   5
        .END    START
"""
    src.encode("ascii")
    OUT_MAC.write_text(src, encoding="ascii", newline="\r\n")
    print(f"gamelogic_mac: wrote {OUT_MAC} + GST.DAT ({len(gst)} B, {nwords} words, "
          f"cksum {checksum:06o})")


def main_movsel():
    """$983D move-selection: capture with the recorded RNG stream (shared by the
    two AI calls) and verify the wrappers + AIDEC + MOVSEL against move_select."""
    win_end = 0xC000
    win_size = win_end - GBASE
    snap, randoms = capture_ai(0x983D,
                               lambda m, rs: ref.move_select(m, list(rs)),
                               win_end)
    expected = bytearray(snap)
    ref.move_select(expected, list(randoms))
    EXP_BIN.write_bytes(bytes(expected[GBASE:win_end]))
    WIN_JSON.write_text(json.dumps({"base": GBASE, "size": win_size}))
    src = (HEADER + emit_wrappers() + emit_ai(randoms) + emit_movsel())
    src += "\n        .EVEN\n"
    src += _emit_window("GST", snap[GBASE:win_end])
    src += "\n        .EVEN\n        .END    START\n"
    src = (src.replace("%ENTRY%", "MOVSEL").replace("%REGSET%", "")
              .replace("%WORDS%", str(win_size // 2) + "."))
    src.encode("ascii")
    OUT_MAC.write_text(src, encoding="ascii", newline="\r\n")
    print(f"gamelogic_mac: wrote {OUT_MAC} (movsel @ 0x983d, window {win_size} B, "
          f"{len(randoms)} randoms, expected -> {EXP_BIN.name})")


def emit_cov_driver(label, regset, n):
    """Branch-coverage fuzzer driver: run `label` on N random states (each a
    fresh $AA00-$AA7F), record the $AA00-$AA7F output to OUTBUF, copy to VRAM.
    i/pointers live in memory (the tested routine clobbers R0-R5)."""
    st00 = f"GST+{0xAA00 - GBASE}."
    return f"""
;-------------------------------------------------------------------
; COV - branch-coverage fuzzer over {label}.
COV:    MOV     #INPUT,CINPTR
        MOV     #OUTBUF,COUTPTR
        CLR     CIDX
CLOOP:  MOV     CINPTR,R1              ; load state i into $AA00..$AA7F
        MOV     #{st00},R2
        MOV     #128.,R3
1$:     MOVB    (R1)+,(R2)+
        DEC     R3
        BNE     1$
        MOV     R1,CINPTR
{regset}        JSR     PC,{label}
        MOV     COUTPTR,R1            ; record $AA00..$AA7F output
        MOV     #{st00},R2
        MOV     #128.,R3
2$:     MOVB    (R2)+,(R1)+
        DEC     R3
        BNE     2$
        MOV     R1,COUTPTR
        INC     CIDX
        CMP     CIDX,#{n}.
        BLO     CLOOP
        MOV     #OUTBUF,R1            ; OUTBUF -> VRAM for the oracle
        MOV     #VRAM,R2
        MOV     #{n * 64}.,R3
3$:     MOV     (R1)+,(R2)+
        DEC     R3
        BNE     3$
        JMP     WKEY
CIDX:   .WORD   0
CINPTR: .WORD   0
COUTPTR:.WORD   0
"""


def emit_cov_ai_driver(n, k):
    """Coverage driver for the AI: run AIDEC on N random $A5EC-$A650 states,
    each with its own k-byte slice of ARND (ARNDI reset to i*k each state)."""
    st = f"GST+{0xA5EC - GBASE}."
    return f"""
;-------------------------------------------------------------------
; COVAI - branch-coverage fuzzer over AIDEC ($A090).
COV:    MOV     #INPUT,CINPTR
        MOV     #OUTBUF,COUTPTR
        CLR     CIDX
        CLR     CRIDX
CLOOP:  MOV     CINPTR,R1              ; load state i into $A5EC..$A650
        MOV     #{st},R2
        MOV     #100.,R3
1$:     MOVB    (R1)+,(R2)+
        DEC     R3
        BNE     1$
        MOV     R1,CINPTR
        MOV     CRIDX,ARNDI           ; this state's RNG slice
        JSR     PC,AIDEC
        MOV     COUTPTR,R1            ; record $A5EC..$A650 output
        MOV     #{st},R2
        MOV     #100.,R3
2$:     MOVB    (R2)+,(R1)+
        DEC     R3
        BNE     2$
        MOV     R1,COUTPTR
        MOV     CRIDX,R0
        ADD     #{k}.,R0
        MOV     R0,CRIDX
        INC     CIDX
        CMP     CIDX,#{n}.
        BLO     CLOOP
        MOV     #OUTBUF,R1            ; OUTBUF -> VRAM
        MOV     #VRAM,R2
        MOV     #{n * 50}.,R3
3$:     MOV     (R1)+,(R2)+
        DEC     R3
        BNE     3$
        JMP     WKEY
CIDX:   .WORD   0
CRIDX:  .WORD   0
CINPTR: .WORD   0
COUTPTR:.WORD   0
"""


def capture_ai_states(n, win_end, k, budget=4000000):
    """Collect up to n distinct real $A090 calls (snapshot + recorded $A3FF),
    preferring those that consumed randoms (the deep RNG decision paths) so the
    fuzz actually exercises the weighted-random branches."""
    sim, mem = build_sim(watch=(0, 0))
    regs, memory, ops = sim.registers, sim.memory, sim.opcodes
    fd, ia = sim.frame_duration, sim.int_active
    deep, shallow = [], []
    for _ in range(budget):
        if regs[PC] == 0xA090:
            s0 = regs[SP]
            ret = memory[s0] | (memory[s0 + 1] << 8)
            snap = bytes(memory)
            randoms = []
            for _ in range(200000):
                cur = regs[PC]
                ops[memory[cur]]()
                if cur == 0xA3FF:
                    randoms.append(regs[0])
                if regs[26] and regs[25] % fd < ia:
                    sim.accept_interrupt(regs, memory, regs[PC])
                if regs[PC] == ret and regs[SP] == s0 + 2:
                    break
            pad = (randoms + [0] * k)[:k]
            (deep if randoms else shallow).append((snap, pad))
            if len(deep) >= n:
                break
            continue
        cur = regs[PC]
        ops[memory[cur]]()
        if regs[26] and regs[25] % fd < ia:
            sim.accept_interrupt(regs, memory, regs[PC])
    states = (deep + shallow)[:n]
    if not states:
        raise SystemExit("no AI calls captured")
    return states


def main_coverage_ai():
    """Fuzz the AI ($A090) against n real captured calls (deep RNG paths first),
    each replaying its recorded $A3FF stream: MACRO (AIDEC) vs Python ai_decide."""
    win_end = 0xC500
    win_size = win_end - GBASE
    n, k = 48, 16
    states = capture_ai_states(n, win_end, k)
    base = bytearray(states[0][0])              # GST base (tables) from call 0
    inputs = [bytes(snap[0xA5EC:0xA650]) for snap, _ in states]
    rnds = [bytes(r) for _, r in states]
    flat_rnd = b"".join(rnds)
    expect = []
    for snap, r in states:
        m = bytearray(snap)
        ref.ai_decide(m, list(r))
        expect.append(bytes(m[0xA5EC:0xA650]))
    EXP_BIN.write_bytes(b"".join(expect))
    WIN_JSON.write_text(json.dumps({"base": 0xA5EC, "size": n * 100}))
    src = HEADER + emit_ai(flat_rnd) + emit_cov_ai_driver(n, k)
    src += "\n        .ASECT\n        . = 100000\n"
    src += _emit_window("GST", bytes(base[GBASE:win_end]))
    src += "\n        .EVEN\n"
    src += _emit_window("INPUT", b"".join(inputs))
    src += f"\n        .EVEN\nOUTBUF: .BLKB   {n * 100}.\n"
    src += "\n        .EVEN\n        .END    START\n"
    src = (src.replace("%ENTRY%", "COV").replace("%REGSET%", "")
              .replace("%WORDS%", "1."))
    src.encode("ascii")
    OUT_MAC.write_text(src, encoding="ascii", newline="\r\n")
    print(f"gamelogic_mac: wrote {OUT_MAC} (coverage ai, {n} states, "
          f"expected -> {EXP_BIN.name})")


def main_coverage():
    """Fuzz one routine: N random $AA00-$AA7F states, MACRO vs the Python
    reference (the validated ground truth), to exercise branches the captured
    demo state never reached."""
    import random
    name = os.environ.get("FIST_COV", "hitdet")
    addr, label, emit, refapply, _we, reg_setup, _w, budget = ROUTINES[name]
    win_end = 0xC500       # covers every cell a routine touches with random
    win_size = win_end - GBASE   # byte indices (reach data $BB00.., $C427)
    win_size = win_end - GBASE
    base, _ = capture_state(addr, refapply, win_end, reg_setup, budget=budget)
    base = bytearray(base)
    regval = {z: 0x40 for _, z in reg_setup}
    if reg_setup:
        base[0x9C29] = 0          # opponent = fighter 0, whose cells are mutated
    # Keep GST + INPUT + OUTBUF in banks 4-6 (below octal 0160000); bank 7's
    # select shares bit 7 with VRAM_EN, so data there behaves differently.
    n = 48
    rng = random.Random(0xF1)
    # By default randomize all of $AA00-$AA7F.  Hit-detect needs the action/
    # guards/fg kept valid (else the reach pointer leaves the window); only the
    # positions/faces/ridx vary, so the deep distance logic runs each time.
    cov_free = {"hitdet": [0xAA19, 0xAA59, 0xAA17, 0xAA57, 0xAA52],
                "hitdp2": [0xAA19, 0xAA59, 0xAA17, 0xAA57, 0xAA12]}
    def cov_val():
        t = rng.randrange(10)            # bias to the game's value range so the
        if t < 6:                        # value-specific compares (E==3, ==$1A,
            return rng.randrange(0x1C)   # action indices ...) actually fire;
        return 0 if t < 8 else rng.randrange(256)   # plus 0-flags + wide edges
    # Force half of anim's states through the E==3 guards so range_9ba7 runs
    # reliably (with $AA19/$AA57/$AA17 still varying around it).
    cov_force = {"anim": {0xAA45: 3, 0xAA44: 1, 0xAA16: 0, 0xAA09: 0, 0xAA04: 7}}
    force = cov_force.get(name, {})
    free = [a - 0xAA00 for a in cov_free.get(name, range(0xAA00, 0xAA80))]
    inputs = []
    for _i in range(n):
        blk = bytearray(base[0xAA00:0xAA80])
        for off in free:
            blk[off] = cov_val()
        if force and _i % 2 == 0:
            for a, v in force.items():
                blk[a - 0xAA00] = v
        inputs.append(bytes(blk))
    expect = []
    for blk in inputs:
        m = bytearray(base)
        m[0xAA00:0xAA80] = blk
        refapply(m, regval)
        expect.append(bytes(m[0xAA00:0xAA80]))
    EXP_BIN.write_bytes(b"".join(expect))
    WIN_JSON.write_text(json.dumps({"base": 0xAA00, "size": n * 128}))
    regset = "".join(f"        MOV     #{regval[z]}.,{pdp}\n"
                     for pdp, z in reg_setup)
    src = HEADER + emit() + emit_cov_driver(label, regset, n)
    src += "\n        .ASECT\n        . = 100000\n"
    src += _emit_window("GST", bytes(base[GBASE:win_end]))
    src += "\n        .EVEN\n"
    src += _emit_window("INPUT", b"".join(inputs))
    src += f"\n        .EVEN\nOUTBUF: .BLKB   {n * 128}.\n"
    src += "\n        .EVEN\n        .END    START\n"
    src = (src.replace("%ENTRY%", "COV").replace("%REGSET%", "")
              .replace("%WORDS%", "1."))
    src.encode("ascii")
    OUT_MAC.write_text(src, encoding="ascii", newline="\r\n")
    print(f"gamelogic_mac: wrote {OUT_MAC} (coverage {name}, {n} states, "
          f"expected -> {EXP_BIN.name})")


def main_bf13():
    """$BF13 logic->graphics bridge: capture a real call, verify the ported
    bridge against ref.bf13.  The pose-record reads reach up to ~$DCBF, so the
    GST DATA must extend that far - but the VRAM-mirror VERIFY window is capped
    at 16 KB (VRAM size).  So decouple: emit GST data to data_end ($E000, holds
    the pose records) yet copy/compare only verify_end ($DC00 = 16 KB, which
    still covers every cell bf13 changes, $C41B-$C439).  High banks (.=100000)
    like the combined build."""
    data_end = 0xE000                    # GST data extent (holds pose records)
    verify_end = 0xDC00                  # VRAM-mirror window: exactly 16 KB
    verify_size = verify_end - GBASE
    assert verify_size % 2 == 0 and verify_size <= 0x4000
    snap, regs = capture_state(0xBF13, lambda m, r: ref.bf13(m), data_end, [],
                               witness=0xC428)
    expected = bytearray(snap)
    ref.bf13(expected)
    EXP_BIN.write_bytes(bytes(expected[GBASE:verify_end]))
    WIN_JSON.write_text(json.dumps({"base": GBASE, "size": verify_size}))
    src = HEADER + emit_bf13()
    src += "\n        .ASECT\n        . = 100000\n"
    src += _emit_window("GST", snap[GBASE:data_end])
    src += "\n        .EVEN\n        .END    START\n"
    src = (src.replace("%ENTRY%", "BF13").replace("%REGSET%", "")
              .replace("%WORDS%", str(verify_size // 2) + "."))
    src.encode("ascii")
    OUT_MAC.write_text(src, encoding="ascii", newline="\r\n")
    print(f"gamelogic_mac: wrote {OUT_MAC} (bf13 @ 0xbf13, data {data_end-GBASE} B, "
          f"verify {verify_size} B, expected -> {EXP_BIN.name})")


def _capture_95e1_path(want):
    """Scan the sim for a $95E1 call whose entry state takes the `want` path
    ('meta' = frame-advance/new-frame-load -> sprite-meta; 'step' = frame-step ->
    $9698 only; 'reset' = $9613 reset).  Returns (snapshot, hl)."""
    sim, _ = build_sim(watch=(0, 0))
    regs, memory, ops = sim.registers, sim.memory, sim.opcodes
    fd, ia = sim.frame_duration, sim.int_active

    def path_of(m, hl):
        base = (hl - 0x0A) & 0xFFFF
        if m[base] == 0:
            return "retz"
        if m[(base - 1) & 0xFFFF] == 0:
            return "meta"                       # $96CE -> META
        phase = m[hl]
        a = m[(base + 4) & 0xFFFF]
        if phase != 0:
            if a != m[(base + 5) & 0xFFFF]:
                return "step"
            return "reset" if ((m[(base + 3) & 0xFFFF] - 1) & 0xFF) == \
                m[(base + 2) & 0xFFFF] else "meta"
        if a != 0:
            return "step"
        return "reset" if m[(base + 2) & 0xFFFF] == 0 else "meta"

    for _ in range(8000000):
        if regs[24] == 0x95E1:
            hl = (regs[6] << 8) | regs[7]
            if path_of(memory, hl) == want:
                return bytes(memory), hl
        ops[memory[regs[24]]]()
        if regs[26] and regs[25] % fd < ia:
            sim.accept_interrupt(regs, memory, regs[24])
    raise SystemExit(f"no $95E1 call taking the {want!r} path")


def main_95e1():
    """$95E1 anim advance: verify the port against ref.anim_95e1 over the $AA
    window.  FIST_95PATH selects which branch to exercise (meta/step/reset).
    Needs a second low-data mirror LDAT ($9368..$9600 = the $9368 table + the
    frame-data records that the frame pointer dereferences)."""
    win_end = 0xAB00
    win_size = win_end - GBASE
    ldat_base, ldat_end = 0x9368, 0x9600
    want = os.environ.get("FIST_95PATH", "meta")
    snap, hl = _capture_95e1_path(want)
    expected = bytearray(snap)
    ref.anim_95e1(expected, hl)
    EXP_BIN.write_bytes(bytes(expected[GBASE:win_end]))
    WIN_JSON.write_text(json.dumps({"base": GBASE, "size": win_size}))
    src = HEADER + emit_95e1()
    src += "\n        .ASECT\n        . = 100000\n"
    src += _emit_window("GST", snap[GBASE:win_end])
    src += "\n        .EVEN\n"
    src += _emit_window("LDAT", snap[ldat_base:ldat_end])
    src += "\n        .EVEN\n        .END    START\n"
    regset = f"        MOV     #{hl}.,R5\n"
    src = (src.replace("%ENTRY%", "ANIM5E").replace("%REGSET%", regset)
              .replace("%WORDS%", str(win_size // 2) + "."))
    src.encode("ascii")
    OUT_MAC.write_text(src, encoding="ascii", newline="\r\n")
    print(f"gamelogic_mac: wrote {OUT_MAC} (95e1 @ 0x95e1 [{want}], hl={hl:#06x}, "
          f"window {win_size} B, expected -> {EXP_BIN.name})")


def main_decgst():
    """Render step 1b: drive the (already-verified) fighter decoder from the
    SHARED full GST.  The $B500-$B900 tables, the pose control stream, and the
    $F730 compose buffer all live inside the full $9C00..$F801 GST, so the
    decoder's data labels become GST-relative EQUs (TB5xx/FBUF/FCTRL) instead of
    separate captured blocks - the decoder code itself is reused verbatim.  The
    work area W stays a local 64-byte scratch (it holds PDP pointers, not $8AE0
    Spectrum addresses).  Verify the composed $F730 == the decoder reference
    (FEXP) via the VRAM oracle (TOVRAM copies $F730 -> VRAM[0..884])."""
    import fighter_mac as fm
    import fighter_data as fd
    fm.STAGE_LEVEL = int(os.environ.get("FGHT_LEVEL", "1"))   # full decoder, not the no-op
    nelem = int(os.environ.get("FGHT_NELEM", "5000"))
    c40e = int(os.environ.get("FGHT_C40E", "4"))
    c407 = int(os.environ.get("FGHT_C407", "0"))
    before, hl, de = fd.capture(c40e, c407)
    m = bytearray(before)
    fd.run_loop(m, hl, de, limit=nelem)
    fexp = bytes(m[fd.FBUF:fd.FBUF + fd.FBUF_LEN])      # composed fighter
    EXP_BIN.write_bytes(fexp)
    WIN_JSON.write_text(json.dumps({"base": fd.FBUF, "size": fd.FBUF_LEN}))

    preamble = ("        .TITLE  FIST\nDPRAM  = 157700\nDISPAT = 177400\n"
                "SYSC   = 177604\nVRAM   = 40000\n")
    driver = fm.HEADER.split("VRAM   = 40000", 1)[1]    # .EVEN START: ... PRESENT
    # GST-relative EQUs: tables, compose buffer, pose control stream
    equs = "\n"
    for t in fd.TABLES:
        equs += f"T{t:04X}  = GST+{t - GBASE}.\n"
    equs += f"FBUF   = GST+{fd.FBUF - GBASE}.\n"
    equs += f"FCTRL  = GST+{hl - GBASE}.\n"
    gst = ("\n        .ASECT\n        . = 100000\n"
           + _emit_window("GST", before[GBASE:GST_FULL_END]) + "        .EVEN\n")
    data = (_emit_window("WINIT", before[0x8AE0:0x8B20])
            + _emit_window("FINIT", before[fd.FBUF:fd.FBUF + fd.FBUF_LEN]))
    src = (preamble + gst + equs
           + "\n        .ASECT\n        . = 1000\n" + driver
           + fm.emit_decrun() + fm.TAIL + "\n" + data
           + "\n        .EVEN\n        .END    START\n")
    fwid, fhgt = before[0xC40A], before[0xC409]
    top, left = (200 - fhgt) // 2, (40 - fwid) // 2
    finish = "PRESENT" if os.environ.get("FGHT_PRESENT") else "TOVRAM"
    src = (src.replace("%C408%", str(before[0xC408]))
              .replace("%C40E%", str(before[0xC40E]))
              .replace("%C407%", str(before[0xC407]))
              .replace("%DEOFF%", str(de - fd.FBUF))
              .replace("%NELEM%", str(nelem))
              .replace("%FWID%", str(fwid))
              .replace("%FHGT%", str(fhgt))
              .replace("%DSTOFF%", str((top * 40 + left) * 2))
              .replace("%FINISH%", finish))
    src.encode("ascii")
    OUT_MAC.write_text(src, encoding="ascii", newline="\r\n")
    print(f"gamelogic_mac: wrote {OUT_MAC} (decgst: decoder on shared GST, "
          f"pose @${hl:04X}, expected $F730 -> {EXP_BIN.name})")


def emit_setupchain():
    """MACRO-11 decode-setup chain $C34F (SETUPC header) + $C36E/$C2B5/$C319/
    $8803 (SEGSET per-segment).  SETUPC: R3=B_in, R4=C_in; reads the pose pointer
    from $C428; SEGCNT=segcount, SEGHL=first segment.  SEGSET: build the decoder
    inputs for the current segment (SRCP/DSTP + the four $8803 cells in W +
    $C40E/$C407/$C408 in the GST), advance SEGHL.  The driver loops SEGCNT times,
    running the element loop per segment (multi-segment fighters layer blits)."""
    return f"""
;-------------------------------------------------------------------
; SETUPC - $C34F header: store the sub-offsets, read the pose header.
SETUPC: MOVB    R3,{g(0xC412)}
        MOVB    R4,{g(0xC413)}
        MOV     {g(0xC428)},R5         ; R5 = pose ptr (Spectrum addr)
        MOVB    GST-116000(R5),SEGCNT
        MOVB    GST-116000+1(R5),{g(0xC416)}
        MOVB    GST-116000+2(R5),{g(0xC417)}
        ADD     #3,R5
        MOV     R5,SEGHL
        RTS     PC

;-------------------------------------------------------------------
; SEGSET - $C36E/$C2B5/$8803 for the current segment (R5 walks SEGHL).
SEGSET: MOV     SEGHL,R5
        MOVB    GST-116000(R5),{g(0xC414)}
        MOVB    GST-116000+1(R5),{g(0xC415)}
        MOVB    GST-116000+2(R5),{g(0xC418)}
        MOVB    GST-116000+3(R5),{g(0xC419)}
        ADD     #4,R5                  ; R5 = seg_data
        MOV     R5,SEGDAT
        MOV     R5,R0                  ; next SEGHL = seg_data + seg_len
        ADD     {g(0xC418)},R0
        MOV     R0,SEGHL
        MOVB    {g(0xC40F)},{g(0xC40D)}
        MOVB    {g(0xC410)},{g(0xC40C)}
        MOVB    {g(0xC411)},{g(0xC40B)}
        MOVB    {g(0xC413)},R0         ; C = $C413 + $C415
        BIC     #177400,R0
        MOVB    {g(0xC415)},R1
        BIC     #177400,R1
        ADD     R1,R0
        CMPB    {g(0xC415)},#377       ; $C415 == $FF special
        BNE     1$
        MOV     #276,R0                ; $BE - $C41A - $0C
        MOVB    {g(0xC41A)},R1
        BIC     #177400,R1
        SUB     R1,R0
        SUB     #14,R0
1$:     BIC     #177400,R0
        MOV     R0,CVAL
        TSTB    {g(0xC410)}            ; B
        BNE     2$
        MOVB    {g(0xC412)},R0
        BIC     #177400,R0
        MOVB    {g(0xC414)},R1
        BIC     #177400,R1
        ADD     R1,R0
        BR      3$
2$:     MOVB    {g(0xC416)},R0
        BIC     #177400,R0
        MOVB    {g(0xC414)},R1
        BIC     #177400,R1
        SUB     R1,R0
        MOVB    GST-116000(R5),R1      ; m[seg_data] << 2
        BIC     #177400,R1
        ASL     R1
        ASL     R1
        SUB     R1,R0
        MOVB    {g(0xC412)},R1
        BIC     #177400,R1
        ADD     R1,R0
3$:     BIC     #177400,R0
        MOV     R0,BVAL
        CLR     R2                     ; dest = $C40D*C (repeated add) + (B>>2) + FBUF
        MOVB    {g(0xC40D)},R0
        BIC     #177400,R0
        BEQ     5$
        MOV     CVAL,R1
4$:     ADD     R1,R2
        DEC     R0
        BNE     4$
5$:     MOV     BVAL,R0
        ASR     R0
        ASR     R0
        BIC     #177400,R0
        ADD     R0,R2
        ADD     #FBUF,R2
        MOV     R2,DSTP
        MOV     BVAL,R0                ; mode = {{0:0,1:2,2:4,3:8}}[B&3]
        BIC     #177774,R0
        BEQ     6$
        MOV     #1,R1
7$:     ASL     R1
        DEC     R0
        BNE     7$
        MOV     R1,R0
        BR      8$
6$:     CLR     R0
8$:     MOVB    {g(0xC40C)},R1
        BIC     #177400,R1
        BIS     R0,R1
        MOVB    R1,{g(0xC40E)}
        MOVB    {g(0xC40B)},{g(0xC407)}
        MOVB    {g(0xC40D)},{g(0xC408)}
        MOVB    {g(0xC40E)},R0         ; $8803: $8B0A = (A & $FE) != 0
        BIC     #177401,R0
        CLR     R1
        TST     R0
        BEQ     9$
        MOV     #1,R1
9$:     MOVB    R1,W8B0A
        CLRB    WAF3
        MOV     SEGDAT,R5
        MOVB    GST-116000(R5),R0      ; a = m[seg_data]
        BIC     #177400,R0
        MOVB    R0,WB1B
        MOVB    R0,WB1C
        TST     R1
        BEQ     10$
        INC     R0
        MOVB    R0,WB1C
10$:    MOV     SEGDAT,R0              ; SRCP = mirror(seg_data + 1)
        INC     R0
        ADD     #GST-116000,R0
        MOV     R0,SRCP
        RTS     PC
        .EVEN
SEGDAT: .WORD   0
SEGHL:  .WORD   0
CVAL:   .WORD   0
BVAL:   .WORD   0
SEGCNT: .BYTE   0
        .EVEN
"""


def emit_c101c1a2():
    """$C101 fighter-1 geometry + $C1A2 dispatch in MACRO-11.  Reads the bbox
    $C434-$C437 + $C41x (all set by $BF13); sets $C40A/$C40F (width), $C409
    (height), $C41A, $C410/$C411.  Out: R3 = B_in, R4 = C_in for SETUPC."""
    return f"""
;-------------------------------------------------------------------
; C101C - $C101 geometry + $C1A2 dispatch (params from the bf13 bbox).
C101C:  MOVB    {g(0xC435)},R0         ; width = (($C435-$C434)>>2)+2
        BIC     #177400,R0
        MOVB    {g(0xC434)},R1
        BIC     #177400,R1
        SUB     R1,R0
        BIC     #177400,R0
        ASR     R0
        ASR     R0
        ADD     #2,R0
        MOVB    R0,{g(0xC40A)}
        MOVB    R0,{g(0xC40F)}
        MOVB    {g(0xC437)},R0         ; height = $C437-$C436
        BIC     #177400,R0
        MOVB    {g(0xC436)},R1
        BIC     #177400,R1
        SUB     R1,R0
        MOVB    R0,{g(0xC409)}
        MOVB    {g(0xC436)},{g(0xC41A)}
        MOVB    {g(0xC421)},{g(0xC411)}
        MOVB    {g(0xC41F)},{g(0xC410)}
        MOVB    {g(0xC41B)},R3         ; B_in = $C41B - ($C434 & $FC)
        BIC     #177400,R3
        MOVB    {g(0xC434)},R0
        BIC     #177400,R0
        BIC     #3,R0
        SUB     R0,R3
        BIC     #177400,R3
        MOVB    {g(0xC41C)},R4         ; C_in = $C41C - $C436
        BIC     #177400,R4
        MOVB    {g(0xC436)},R0
        BIC     #177400,R0
        SUB     R0,R4
        BIC     #177400,R4
        RTS     PC

;-------------------------------------------------------------------
; C1CC - $C101 block2 + $C1CC dispatch for fighter P2 (mirror of C101C):
; geometry from the bbox $C438-$C43B, pose pointer $C42A.  Copies $C42A -> $C428
; so SETUPC (which reads $C428) decodes P2's pose.  Out: R3 = B_in, R4 = C_in.
C1CC:   MOVB    {g(0xC439)},R0         ; width = (($C439-$C438)>>2)+2
        BIC     #177400,R0
        MOVB    {g(0xC438)},R1
        BIC     #177400,R1
        SUB     R1,R0
        BIC     #177400,R0
        ASR     R0
        ASR     R0
        ADD     #2,R0
        MOVB    R0,{g(0xC40A)}
        MOVB    R0,{g(0xC40F)}
        MOVB    {g(0xC43B)},R0         ; height = $C43B-$C43A
        BIC     #177400,R0
        MOVB    {g(0xC43A)},R1
        BIC     #177400,R1
        SUB     R1,R0
        MOVB    R0,{g(0xC409)}
        MOVB    {g(0xC43A)},{g(0xC41A)}
        MOVB    {g(0xC422)},{g(0xC411)}
        MOVB    {g(0xC420)},{g(0xC410)}
        MOVB    {g(0xC41D)},R3         ; B_in = $C41D - ($C438 & $FC)
        BIC     #177400,R3
        MOVB    {g(0xC438)},R0
        BIC     #177400,R0
        BIC     #3,R0
        SUB     R0,R3
        BIC     #177400,R3
        MOVB    {g(0xC41E)},R4         ; C_in = $C41E - $C43A
        BIC     #177400,R4
        MOVB    {g(0xC43A)},R0
        BIC     #177400,R0
        SUB     R0,R4
        BIC     #177400,R4
        MOV     {g(0xC42A)},{g(0xC428)}   ; SETUPC reads the pose ptr from $C428
        RTS     PC
"""


def main_drawgst(mode="chain"):
    """Render step 3: draw a fighter from the LOGIC state on the shared GST.
    Modes (increasing how much runs in MACRO vs is taken from the capture):
    - chain (FIST_GL=drawgst): capture at $C34F (params already set); SETUPC
      reads the pose pointer from $C428 + the captured sub-offsets.
    - full (FIST_GL=fulldraw): capture at $C101; C101C computes the positioning
      from the bbox $C434-$C437; only the GST comes from the capture.
    - bridge (FIST_GL=bridgedraw): capture at $BF13 (raw logic state); BF13 builds
      the bbox + pose pointers, then C101C -> SETUPC -> decode.  The whole
      bridge->draw runs in MACRO from the $AA logic state + pose data.
    The element loop is made runtime-driven: C40EM/C407M are GST EQUs; the C408
    stride is read into C408W; DECRUN's own SRCP/DSTP init is dropped (SETUPC
    sets them).  Verify the composed $F730 byte-exact vs the Python chain."""
    import fighter_mac as fm
    import fighter_data as fd
    import setup_ref as sr
    from decoder_ref import run_loop
    fm.STAGE_LEVEL = 1
    nelem = int(os.environ.get("FGHT_NELEM", "5000"))
    if mode == "chain":
        snap, b_in, c_in, pose = sr.capture_c34f(0x04, 0)
        mm = bytearray(snap)
    else:
        snap = sr.capture_bf13() if mode == "bridge" else sr.capture_c101()
        mm = bytearray(snap)
        if mode == "bridge":
            ref.bf13(mm)
        sr.c101_block1(mm)
        b_in, c_in, pose = sr.c1a2(mm)
    sr.draw_fighter(mm, pose, b_in, c_in)
    fexp = bytes(mm[fd.FBUF:fd.FBUF + fd.FBUF_LEN])
    EXP_BIN.write_bytes(fexp)
    WIN_JSON.write_text(json.dumps({"base": fd.FBUF, "size": fd.FBUF_LEN}))

    preamble = ("        .TITLE  FIST\nDPRAM  = 157700\nDISPAT = 177400\n"
                "SYSC   = 177604\nVRAM   = 40000\nKBST   = 177442\n")
    equs = "\n"
    for t in fd.TABLES:
        equs += f"T{t:04X}  = GST+{t - GBASE}.\n"
    equs += f"FBUF   = GST+{fd.FBUF - GBASE}.\n"
    equs += f"C40EM  = GST+{0xC40E - GBASE}.\n"     # runtime mode flags
    equs += f"C407M  = GST+{0xC407 - GBASE}.\n"     # runtime facing flag
    equs += "WB1C   = W+60.\n"
    gst = ("\n        .ASECT\n        . = 100000\n"
           + _emit_window("GST", snap[GBASE:GST_FULL_END]) + "        .EVEN\n")
    driver = f"""
        .ASECT
        . = 1000
        .EVEN
START:  MOV     #340,R0
        MTPS    R0
        MOV     @#DISPAT,ORIGDP
        MOVB    @#SYSC,ORIGRC
        MOVB    @#SYSC,R0
        BIC     #17,R0
        MOVB    R0,@#SYSC
        MOV     #3377,@#DPRAM
        MOV     #3377,@#DISPAT
        MOV     #W,R0                  ; zero the work area
        MOV     #32.,R1
8$:     CLR     (R0)+
        DEC     R1
        BNE     8$
%PARAMS%        JSR     PC,SETUPC
9$:     JSR     PC,SEGSET              ; per-segment setup + decode
        MOVB    {g(0xC408)},R0         ; C408W = runtime stride
        BIC     #177400,R0
        MOV     R0,C408W
        JSR     PC,DECRUN
        DECB    SEGCNT
        BNE     9$
        JSR     PC,%FINISH%
WKEY:   MOV     @#KBST,R0
        BIT     #2,R0
        BEQ     WKEY
        MOV     ORIGDP,@#DPRAM
        MOV     ORIGDP,@#DISPAT
        MOVB    ORIGRC,@#SYSC
        EMT     350
"""
    tovram_present = fm.HEADER[fm.HEADER.index("TOVRAM:"):]
    # Use the chain's SRCP/DSTP (drop DECRUN's own init) and the runtime stride.
    decrun = (fm.emit_decrun()
              .replace("MOV     #FCTRL,SRCP\n        "
                       "MOV     #FBUF+%DEOFF%.,DSTP\n        ", "")
              .replace("ADD     #C408V,R0", "ADD     C408W,R0"))
    # C40EM/C407M are now GST EQUs (runtime) - drop the baked .BYTE definitions.
    tail = (fm.TAIL
            .replace("C40EM:  .BYTE   %C40E%.                ; per-fighter mode flags ($C40E)\n", "")
            .replace("C407M:  .BYTE   %C407%.                ; facing flag ($C407)\n", ""))
    chain = (emit_setupchain()
             + (emit_c101c1a2() if mode in ("full", "bridge") else "")
             + (emit_bf13() if mode == "bridge" else ""))
    if mode == "bridge":
        params = "        JSR     PC,BF13\n        JSR     PC,C101C\n"
    elif mode == "full":
        params = "        JSR     PC,C101C\n"
    else:
        params = f"        MOV     #{b_in}.,R3\n        MOV     #{c_in}.,R4\n"
    src_txt = (preamble + gst + equs + driver.replace("%PARAMS%", params)
               + tovram_present + decrun + chain + tail
               + "\n        .EVEN\nC408W:  .WORD   0\n        .END    START\n")
    fwid, fhgt = mm[0xC40A], mm[0xC409]
    top, left = (200 - fhgt) // 2, (40 - fwid) // 2
    finish = "PRESENT" if os.environ.get("FGHT_PRESENT") else "TOVRAM"
    src_txt = (src_txt.replace("%C408%", str(snap[0xC408]))
               .replace("%C40E%", str(snap[0xC40E]))
               .replace("%C407%", str(snap[0xC407]))
               .replace("%NELEM%", str(nelem))
               .replace("%FWID%", str(fwid)).replace("%FHGT%", str(fhgt))
               .replace("%DSTOFF%", str((top * 40 + left) * 2))
               .replace("%FINISH%", finish))
    src_txt.encode("ascii")
    OUT_MAC.write_text(src_txt, encoding="ascii", newline="\r\n")
    print(f"gamelogic_mac: wrote {OUT_MAC} (drawgst: chain+decoder from pose "
          f"${pose:04X}, B_in={b_in} C_in={c_in}, expected $F730 -> {EXP_BIN.name})")


def main_framedraw():
    """FIST_GL=framedraw - UNIFY the logic and draw generators (task #11): run the
    EXACT full $9745 frame (logic, incl. $BF13) then DRAW fighter P1 from the
    resulting live state ($C101 geometry from the bbox bf13 set -> setup chain ->
    decoder), and verify the composed $F730 byte-exact vs frame_9745 + the Python
    draw chain.  This proves the logic feeds the draw correctly in one image - the
    foundation for the per-frame game loop."""
    import fighter_mac as fm
    import fighter_data as fd
    import setup_ref as sr
    fm.STAGE_LEVEL = 1
    nelem = int(os.environ.get("FGHT_NELEM", "5000"))
    ldat_base, ldat_end = 0x9368, 0x9600

    def safe_frame(m, rs):
        tmp = bytearray(m)
        try:
            ref.frame_9745(tmp, list(rs))
        except NotImplementedError:
            return
        m[:] = tmp

    snap, randoms = capture_ai(0x9745, safe_frame, 0xC440)
    mm = bytearray(snap)
    ref.frame_9745(mm, list(randoms))      # the logic frame (incl. bf13)
    sr.c101_block1(mm)                      # $C101 geometry for fighter P1
    b_in, c_in, pose = sr.c1a2(mm)          # $C1A2 dispatch -> decode params
    sr.draw_fighter(mm, pose, b_in, c_in)   # decode P1 into $F730
    sr.c101_block2(mm)                      # $C101 geometry for fighter P2
    b2, c2, pose2 = sr.c1cc(mm)             # $C1CC dispatch (mirror)
    sr.draw_fighter(mm, pose2, b2, c2)      # decode P2 into the same $F730
    fexp = bytes(mm[fd.FBUF:fd.FBUF + fd.FBUF_LEN])
    EXP_BIN.write_bytes(fexp)
    WIN_JSON.write_text(json.dumps({"base": fd.FBUF, "size": fd.FBUF_LEN}))

    preamble = ("        .TITLE  FIST\nDPRAM  = 157700\nDISPAT = 177400\n"
                "SYSC   = 177604\nVRAM   = 40000\nKBST   = 177442\n")
    equs = "\n"
    for t in fd.TABLES:
        equs += f"T{t:04X}  = GST+{t - GBASE}.\n"
    equs += f"FBUF   = GST+{fd.FBUF - GBASE}.\n"
    equs += f"C40EM  = GST+{0xC40E - GBASE}.\n"
    equs += f"C407M  = GST+{0xC407 - GBASE}.\n"
    equs += "WB1C   = W+60.\n"
    gst = ("\n        .ASECT\n        . = 100000\n"
           + _emit_window("GST", snap[GBASE:GST_FULL_END]) + "        .EVEN\n"
           + _emit_window("LDAT", snap[ldat_base:ldat_end]) + "        .EVEN\n")
    driver = """
        .ASECT
        . = 1000
        .EVEN
START:  MOV     #340,R0
        MTPS    R0
        MOV     @#DISPAT,ORIGDP
        MOVB    @#SYSC,ORIGRC
        MOVB    @#SYSC,R0
        BIC     #17,R0
        MOVB    R0,@#SYSC
        MOV     #3377,@#DPRAM
        MOV     #3377,@#DISPAT
        MOV     #W,R0
        MOV     #32.,R1
8$:     CLR     (R0)+
        DEC     R1
        BNE     8$
        JSR     PC,ORCH                ; run the EXACT full $9745 logic frame
%PRECLR%        JSR     PC,C101C       ; geometry from the bbox bf13 just set
        JSR     PC,SETUPC
9$:     JSR     PC,SEGSET
        MOVB    %C408RT%,R0            ; runtime $C408 stride
        BIC     #177400,R0
        MOV     R0,C408W
        JSR     PC,DECRUN
        DECB    SEGCNT
        BNE     9$
        JSR     PC,C1CC                ; fighter P2 geometry + pose
        JSR     PC,SETUPC
7$:     JSR     PC,SEGSET
        MOVB    %C408RT%,R0
        BIC     #177400,R0
        MOV     R0,C408W
        JSR     PC,DECRUN
        DECB    SEGCNT
        BNE     7$
        JSR     PC,%FINISH%
WKEY:   MOV     @#KBST,R0
        BIT     #2,R0
        BEQ     WKEY
        MOV     ORIGDP,@#DPRAM
        MOV     ORIGDP,@#DISPAT
        MOVB    ORIGRC,@#SYSC
        EMT     350
"""
    driver = driver.replace("%C408RT%", g(0xC408))
    tovram_present = fm.HEADER[fm.HEADER.index("TOVRAM:"):]
    decrun = (fm.emit_decrun()
              .replace("MOV     #FCTRL,SRCP\n        "
                       "MOV     #FBUF+%DEOFF%.,DSTP\n        ", "")
              .replace("ADD     #C408V,R0", "ADD     C408W,R0"))
    tail = (fm.TAIL
            .replace("C40EM:  .BYTE   %C40E%.                ; per-fighter mode flags ($C40E)\n", "")
            .replace("C407M:  .BYTE   %C407%.                ; facing flag ($C407)\n", "")
            .replace("ORIGDP: .WORD   0\n", "").replace("ORIGRC: .WORD   0\n", ""))
    logic = emit_fullframe(randoms)        # all logic routines + 95e1 + bf13 + ORCH
    chain = emit_setupchain() + emit_c101c1a2()
    src = (preamble + gst + equs + driver + tovram_present + decrun
           + logic + chain + tail
           + "\n        .EVEN\nC408W:  .WORD   0\n"
           + "ORIGDP: .WORD   0\nORIGRC: .WORD   0\n"
           + "        .END    START\n")
    fwid, fhgt = mm[0xC40A], mm[0xC409]
    top, left = (200 - fhgt) // 2, (40 - fwid) // 2
    finish = "PRESENT" if os.environ.get("FGHT_PRESENT") else "TOVRAM"
    # present mode = a clean visual: zero $F730 so only the fighters show (no
    # $C234 bg-fill).  verify mode must NOT zero (it matches the ref's $F730).
    preclr = ("        MOV     #FBUF,R0\n        MOV     #442.,R1\n"
              "6$:     CLR     (R0)+\n        DEC     R1\n        BNE     6$\n"
              if os.environ.get("FGHT_PRESENT") else "")
    src = (src.replace("%NELEM%", str(nelem)).replace("%FINISH%", finish)
              .replace("%PRECLR%", preclr)
              .replace("%C408%", str(snap[0xC408]))
              .replace("%C40E%", str(snap[0xC40E])).replace("%C407%", str(snap[0xC407]))
              .replace("%FWID%", str(fwid)).replace("%FHGT%", str(fhgt))
              .replace("%DSTOFF%", str((top * 40 + left) * 2)))
    src.encode("ascii")
    OUT_MAC.write_text(src, encoding="ascii", newline="\r\n")
    print(f"gamelogic_mac: wrote {OUT_MAC} (FRAMEDRAW: $9745 logic + draw P1 from "
          f"pose ${pose:04X}, B_in={b_in} C_in={c_in}, $F730 -> {EXP_BIN.name})")


def main_game(withbg=False):
    """FIST_GL=game - the STANDALONE game, runnable on RT-11 via 'R FIST'.
    Loads the full GST from GST.DAT into the parked extended banks 4-6 (the proven
    chunked .READW + park/copy loader), then runs the live per-frame loop (keyboard
    -> P1, LFSR AI -> P2, sound) and draws BOTH fighters from the live state with a
    flicker-free per-row compositor.

    withbg (FIST_GL=gamebg): also render the dojo background.  The bg engine
    (CHGBG/CREBG) renders the Spectrum-format dojo into the resident SCRBUF once at
    start-up and SPSCR presents it to VRAM; the compositor then seeds each rebuilt
    row with that clean dojo row (converted SCRBUF->VRAM format inline) instead of
    black, so the two fighters composite transparently over the dojo.  SCRBUF is the
    only extra resident buffer - no per-fighter save-under, no 16 KB dojo copy."""
    import fighter_mac as fm
    import fighter_data as fd
    import setup_ref as sr
    import gen_fist
    from bg_data import emit_all
    fm.STAGE_LEVEL = 1
    nelem = int(os.environ.get("FGHT_NELEM", "5000"))
    bgn = int(os.environ.get("FGHT_BG", "2"))    # $AF34 at the 1UP start ($AC59)
    ldat_base, ldat_end = 0x9368, 0x9600

    def safe_frame(m, rs):
        tmp = bytearray(m)
        try:
            ref.frame_9745(tmp, list(rs))
        except NotImplementedError:
            return
        m[:] = tmp
    snap, randoms = capture_ai(0x9745, safe_frame, 0xC440)
    mm = bytearray(snap)
    ref.frame_9745(mm, list(randoms))
    sr.c101_block1(mm)
    sr.c1a2(mm)
    sr.c101_block2(mm)
    sr.c1cc(mm)
    fwid, fhgt = mm[0xC40A], mm[0xC409]
    top, left = (200 - fhgt) // 2, (40 - fwid) // 2
    dstoff = (top * 40 + left) * 2               # centred first-row byte offset in VRAM
    present_words = (fwid * fhgt + 1) // 2       # bytes PRESENT reads, rounded to words
    fbuf_addr = 0o100000 + (fd.FBUF - GBASE)     # compose buffer home (extended bank 6)
    safe_words = (0o157777 - fbuf_addr + 1) // 2  # composed words that fit below bank 7
    copy_words = min(present_words, safe_words)   # copy only what the decode could write
    # per-frame present clamps: a runtime box can grow taller/wider than the captured
    # one (a jump/somersault) - cap it so the blit can't over-read LOWBUF or run off
    # the screen (both showed as garbage).  LOWBUF holds the largest clamped box.
    fwmax, fhmax = 40, 96
    # Per-fighter compose buffers: each holds ONE fighter (the original FBUF_LEN = 884 B),
    # which fits below bank 7 (safe_words).  The decode writes each fighter into FBUF
    # (bank 6) then we copy it down to LBUF1 / LBUF2 for the compositor to blit.
    # The low-RAM per-fighter copies only need one fighter (FBUF_LEN); the fatter
    # safe_words bound is for the extended FBUF the decode writes into.  With the bg,
    # trim the copies to the real fighter size so SCRBUF (6912 B) fits banks 0-1.
    lb_words = ((fd.FBUF_LEN + 1) // 2) if withbg else safe_words
    # FIST_DBGMOVE=1: a test hook - a non-zero $B156 (an unused sound scratch cell)
    # overrides P1's selected move, so a test can play every move for a look.
    dbgmove = ("" if not os.environ.get("FIST_DBGMOVE") else
               f"        MOVB    {g(0xB156)},R1       ; FIST_DBGMOVE: forced move, if any\n"
               "        BIC     #177400,R1\n"
               "        BEQ     79$\n"
               "        MOV     R1,R0\n"
               "79$:")
    ktmout = 3                                   # game frames a control stays held after its last event:
                                                 # the MS7004 game preset repeats after 125 ms then every
                                                 # 50 ms, so three frames bridge the first gap (a TAP = 1-3
                                                 # steps); 7 was a 1.5 s ghost hold at ~7 game-fps
    frame_delay = 20000                          # crude pacing (busy loop), tune later

    gstdat = bytes(snap[GBASE:0xF730])           # GST data; $F730+ compose is scratch
    if len(gstdat) % 512:
        gstdat = gstdat + bytes(512 - (len(gstdat) % 512))
    nblocks = len(gstdat) // 512
    # The tape's loading screen (SCREEN$, 6912 B) rides behind the game state in
    # GST.DAT: the loader reads it straight into SCRBUF and presents it while the
    # state loads - the picture the original shows while its tape loads.
    scrdat = bytes(gen_fist.load_loading_screen()) if withbg else b""
    if len(scrdat) % 512:
        scrdat = scrdat + bytes(512 - (len(scrdat) % 512))
    SCRBLK = nblocks                             # its first block in GST.DAT
    (OUT_MAC.parent / "GST.DAT").write_bytes(gstdat + scrdat)
    # The .DAT is read in CHUNK-block pieces into BUF, the top of the primary
    # banks 2-3 just under the dojo block at 0100000 (VRAM is off under RT-11, so
    # that is plain RAM), each piece copied into the parked extended banks.  The
    # rest of banks 2-3 (040000..BUF) is free for data the .SAV carries itself -
    # the three backgrounds' tables live there (see bgdat_src).
    CHUNK = 8                                    # blocks per .READW (4 KB)
    BUF = 0o100000 - CHUNK * 512
    chunks = [(sb, min(CHUNK, nblocks - sb)) for sb in range(0, nblocks, CHUNK)]
    ldr_reads = ""
    for i, (sb, nb) in enumerate(chunks):
        ldr_reads += f"""        .READW  #LKAREA,#0,#BUF,#{nb * 256}.,#{sb}.
        BCC     .+6
        JMP     LDERR
        MOV     #GST+{sb * 512}.,R1
        MOV     #{nb * 256}.,R2
        JSR     PC,CHUNK
"""

    preamble = (
        "        .TITLE  FIST\n"
        "        .MCALL  .FETCH,.LOOKUP,.READW,.CLOSE,.EXIT\n"
        "DISPAT = 177400\nSYSC   = 177604\nVRAM   = 40000\nVRAMEN = 100000\n"
        f"KBST   = 177442\nGST    = 100000\nHSPACE = 30000\nBUF    = {BUF:o}\n"
        "EXT    = 17\nPRIM   = 177\nGAME   = 3217\n")
    equs = "\nFWHITE = 043400\n"          # bright-white attribute high byte ($47)
    if withbg:
        title_load = f"""        ; --- the loading screen: read it into SCRBUF (plain RAM under RT-11),
        ;     switch to the medium-res colour mode and present it - then load
        ;     the game state behind it, as the tape loader did ---
        .READW  #LKAREA,#0,#SCRBUF,#3456.,#{SCRBLK}.
        BCC     .+6
        JMP     LDERR
        MTPS    #340
        MOVB    @#SYSC,R0
        BIC     #17,R0
        MOVB    R0,@#SYSC
        MOVB    R0,RCSHAD              ; reg C shadow: the sound driver toggles bit 5 in it
        MOV     #3377,@#DISPAT         ; VRAM on @40000, banks 4-6 primary (SCRBUF)
        MOV     #VRAM,R0
7$:     CLR     (R0)+
        CMP     R0,#VRAMEN
        BLO     7$
        JSR     PC,SPSCR
        MOV     #3177,@#DISPAT         ; window off again for the reads (the picture stays)
        MTPS    #0
"""
        after_load = """        ; --- state loaded: hold the loading screen ~3 s, or until fire / "1" ---
        MTPS    #340
        MOV     #1000.,R3
8$:     JSR     PC,KSCAN
        TST     KTFR
        BNE     9$
        TST     KSTART
        BNE     9$
        MOV     #2500.,R4              ; ~3 ms
81$:    SOB     R4,81$
        SOB     R3,8$
9$:     CLR     KSTART
        CLR     KTFR
        JMP     BOOT2
"""
    else:
        title_load = ""
        after_load = """        ; --- GST loaded; set medium video ---
        MTPS    #340
        MOVB    @#SYSC,R0
        BIC     #17,R0
        MOVB    R0,@#SYSC
        MOVB    R0,RCSHAD              ; reg C shadow: the sound driver toggles bit 5 in it
        JMP     BOOT2
"""
    # The loader (boot-only code: .FETCH/.LOOKUP, the loading screen, the chunk
    # reads, the hold) lives in the dojo block at 0100000 when there is one -
    # banks 0-1 are full - and runs there at RT-11's all-primary banking; the
    # chunk copies (which hide banks 4-6) go through CHUNK in banks 0-1.
    boot_code = f"""BOOT:   .FETCH  #HSPACE,#DATFIL
        BCC     .+6
        JMP     LDERR
        .LOOKUP #LKAREA,#0,#DATFIL
        BCC     .+6
        JMP     LDERR
{title_load}{ldr_reads}        .CLOSE  #0
{after_load}"""
    if not withbg:
        boot_inline = boot_code                  # no dojo block: the loader stays inline
    for t in fd.TABLES:
        equs += f"T{t:04X}  = GST+{t - GBASE}.\n"
    equs += f"FBUF   = GST+{fd.FBUF - GBASE}.\n"
    equs += f"C40EM  = GST+{0xC40E - GBASE}.\n"
    equs += f"C407M  = GST+{0xC407 - GBASE}.\n"
    equs += "WB1C   = W+60.\n"

    # --- dojo background: engine + data + the driver fragments (empty when !withbg) ---
    dojo_boot, dojo_row, bgsrc, srows_src = "", "", "", ""
    boot_inline = ""
    bgdat_src, bgvars, rendbg = "", "", ""
    # fighter ink in the overlay: white on the plain black-background game (else the
    # fighter is black-on-black), BLACK over the dojo (black figures on the light
    # dojo paper, as on the Spectrum) - keep the dojo paper colour either way.
    ovl_ink = ("BICB    #7,1(R0)             ; black ink, keep the dojo paper colour"
               if withbg else
               "BISB    #107,1(R0)           ; white bright ink on the black background")
    if withbg:
        equs += ("LMARG  = 8.\nTMARG  = 4.\nLSTRID = 80.\n"
                 "SVBASE = 40000\nSVATTR = 54000\nSVTOP  = 40200\n")
        # render the dojo once, right after the VRAM clear (banking already GAME 3217)
        if "FGHT_BG" in os.environ:                # override the opening background
            dojo_boot = f"        MOVB    #{bgn}.,{g(0xAF34)}     ; FGHT_BG: opening background override\n"
        # per-row: copy the clean dojo row for ROWN (DOJOBUF, VRAM word format)
        # straight over the VRAM row, then the fighter overlays write over it,
        # zero cells transparent -> the dojo shows through.
        # NB: R2 holds the persistent VRAM row pointer across the whole CLOOP
        # iteration (set before CLOOP, advanced at C2SK) - so this must touch
        # only R0/R1/R3/R4/R5 and leave R2 alone.
        dojo_row = ("""        MOV     ROWN,R4              ; dojo row: y = ROWN - TMARG
        SUB     #TMARG,R4
        BLT     CCLR                 ; above the dojo band -> clear the row
        CMP     R4,#192.
        BGE     CCLR                 ; below the dojo band -> clear the row
        MOV     R4,R0                ; src = DOJOBUF + y*80 + 8 (pre-converted, VRAM row layout)
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0
        MOV     R0,R1
        ASL     R0
        ASL     R0
        ADD     R1,R0
        ADD     #DOJOBUF+8.,R0
        MOV     R2,R1                ; straight into the VRAM row, past the 4-cell margin
        ADD     #8.,R1
"""
        # copy the 32 clean dojo cells fully unrolled (no loop overhead per word)
        + "".join("        MOV     (R0)+,(R1)+\n" for _ in range(32))
        + """        BR      CDDN                 ; the 32 picture cells are rewritten in place;
                                     ; the margins stay black (fighters are clamped to the picture)
""")
        eng_start = gen_fist.PROGRAM.index(
            ";-------------------------------------------------------------------\n; CHGBG")
        engine = gen_fist.PROGRAM[eng_start:]
        # main_game owns ORIGRC (datblk) and its own exit; drop the engine's copies
        # (ORIGDP is only used by the demo's EXITP, which we don't include).
        engine = (engine.replace("ORIGDP: .WORD   0\n", "")
                  .replace("ORIGRC: .WORD   0\n", "")
                  .replace("BGREF:  .WORD   0                       ; selected background (1..3)\n", "")
                  .replace("%BGN%", str(bgn)))
        # Change_Background's reference dispatch ($5F3C..$5F52): BGREF 1..3 picks
        # the definition table.  BGREF/BGTAB live in banks 0-1 (always mapped).
        assert engine.count("        MOV     #%BGDEF%,R1") == 1
        engine = engine.replace(
            "        MOV     #%BGDEF%,R1            ; definition for the built-in background\n",
            "        MOV     BGREF,R1               ; background reference 1..3 ($5F00)\n"
            "        ASL     R1\n"
            "        MOV     BGTAB-2(R1),R1         ; -> its definition table\n")
        # The dojo engine + bg data + SROWS + SCRBUF live at 0100000 in the PRIMARY
        # banks 4-6 (embedded in the .SAV), which the compositor's 3377 banking makes
        # visible; the GST loads into the EXTENDED banks 12-14 at the same window
        # (decode's 3217 banking).  One dispatcher bit switches between them, so the
        # 6912 B SCRBUF costs nothing in the tight banks 0-1.
        # No row table: SPSCR / BUILDDB compute the Spectrum row offset (ROWOFF),
        # and the per-frame compositor reads DOJOBUF (VRAM row layout) - banks
        # 0-1 are full, every byte there counts.
        # BUILDDB: pre-convert the whole dojo (SCRBUF Spectrum planes -> VRAM word
        # format) into DOJOBUF once at boot.  Same per-cell convert as the CLOOP, but
        # for all 192 dojo rows, stored in the VRAM row layout (40 words/row, the
        # 4-cell margins zero) so DOJOBUF + (vram - VRAM - 4*80) is the clean cell
        # under any picture cell - the HUD restores cells from it.  Runs at 3377.
        builddb = ("""
BUILDDB: CLR     R2                   ; dojo y = 0..191
BDB1:    MOV     R2,R0                ; pix = SCRBUF + ROWOFF(y)
        JSR     PC,ROWOFF
        ADD     #SCRBUF,R1
        MOV     R2,R5                ; attr = SCRBUF+6144 + (y>>3)*32
        ASR     R5
        ASR     R5
        ASR     R5
        ASL     R5
        ASL     R5
        ASL     R5
        ASL     R5
        ASL     R5
        ADD     #SCRBUF+6144.,R5
        MOV     R2,R0                ; dst = DOJOBUF + y*80 (VRAM row layout, 40 words)
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0
        MOV     R0,R3
        ASL     R0
        ASL     R0
        ADD     R3,R0
        ADD     #DOJOBUF,R0
        CLR     (R0)+                ; 4-cell left margin
        CLR     (R0)+
        CLR     (R0)+
        CLR     (R0)+
        MOV     #32.,R3
BDB2:    MOVB    (R5)+,R4
        SWAB    R4
        BIC     #377,R4
        BISB    (R1)+,R4
        MOV     R4,(R0)+
        DEC     R3
        BNE     BDB2
        CLR     (R0)+                ; 4-cell right margin
        CLR     (R0)+
        CLR     (R0)+
        CLR     (R0)+
        INC     R2
        CMP     R2,#192.
        BLO     BDB1
        RTS     PC
""")
        bgsrc = ("\n        .ASECT\n        . = 100000\n"
                 + engine + builddb + boot_code
                 + "\n        .EVEN\nSCRBUF: .BLKB   6912.\n"
                 # DOJOBUF: the dojo pre-converted to VRAM word format (40 words x 192
                 # rows = the picture's VRAM rows 4..195, margins zero), built once at
                 # boot; the compositor copies a band row from here
                 # instead of re-converting SCRBUF every frame (the ~62%-of-frame cost).
                 # An address EQU (runtime RAM right above SCRBUF), NOT reserved storage,
                 # so it stays out of the .SAV image (keeps the load small).
                 + "DOJOBUF = SCRBUF+6912.\n        .EVEN\n")
        # All three backgrounds' tables (UDGs, position + attribute streams) in
        # the primary bank 2 at 040000: under the VRAM window, so only reachable
        # with the window off - which is how RENDBG runs CHGBG.  RT-11 loads it
        # as plain RAM (the monitor runs with VRAM off); it must end below BUF.
        bgdat, bgdat_len = emit_all((1, 2, 3))
        assert 0o40000 + bgdat_len <= BUF, f"bg data {bgdat_len} B overruns BUF {BUF:o}"
        bgdat_src = "\n        .ASECT\n        . = 40000\n" + bgdat
        bgvars = ("        .EVEN\nBGREF:  .WORD   0                ; background reference ($5F00), 1..3\n"
                  "BGTAB:  .WORD   BG1DEF,BG2DEF,BG3DEF\n")
        rendbg = f"""        ; --- RENDBG: render the background $AF34 selects and present it ($9200 ->
        ;     Change_Background).  The bg tables sit in bank 2 (040000, under the
        ;     VRAM window) and the engine + SCRBUF in the primary banks 4-6, so the
        ;     render runs with every slot primary and the window OFF; the convert +
        ;     present then run with the window on.  Returns at GAME banking. --------
RENDBG: MOVB    {g(0xAF34)},R0
        BIC     #177400,R0
        MOV     R0,BGREF
        MOV     #3177,@#DISPAT       ; all slots primary, VRAM window off
        JSR     PC,CHGBG             ; bg tables -> SCRBUF (Spectrum format)
        MOV     #3377,@#DISPAT       ; VRAM on @40000, banks 4-6 primary
        JSR     PC,BUILDDB           ; SCRBUF -> DOJOBUF (VRAM word format)
        JSR     PC,SPSCR             ; present the dojo 1:1 centred
        MOV     #1,HUDDRT            ; the strip was wiped: redraw it next frame
        MOV     #GAME,@#DISPAT
        RTS     PC
"""
    c408 = g(0xC408)
    # yin-yang HUD symbols (2x2 UDGs) - extracted from the snapshot ($928A full,
    # $92AA half), embedded as data like the rest of the art.
    def _yybytes(b):
        # MACRO-11 numbers default to OCTAL - suffix each byte with '.' for decimal.
        return "\n".join("        .BYTE   " + ",".join(f"{x}." for x in b[i:i + 8])
                         for i in range(0, len(b), 8))
    yyfull_s = _yybytes(snap[0x928A:0x928A + 32])
    yyhalf_s = _yybytes(snap[0x92AA:0x92AA + 32])

    # An OWN 8x8 font: digits 0-9 (codes 0-9), space (10), A-Z (11-36).  The
    # original's text uses the copyrighted Spectrum ROM font, not ours to copy.
    _FONT_HEX = {
        '0': [0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0], '1': [0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0],
        '2': [0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0], '3': [0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0],
        '4': [0x0C,0x1C,0x2C,0x6C,0x7E,0x0C,0x0C,0], '5': [0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0],
        '6': [0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0], '7': [0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0],
        '8': [0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0], '9': [0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0],
        ' ': [0,0,0,0,0,0,0,0],
        'A': [0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0], 'B': [0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0],
        'C': [0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0], 'D': [0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0],
        'E': [0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0], 'F': [0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0],
        'G': [0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0], 'H': [0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0],
        'I': [0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0], 'J': [0x1E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0],
        'K': [0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0], 'L': [0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0],
        'M': [0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0], 'N': [0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0],
        'O': [0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0], 'P': [0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0],
        'Q': [0x3C,0x66,0x66,0x66,0x6E,0x6C,0x36,0], 'R': [0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0],
        'S': [0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0], 'T': [0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0],
        'U': [0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0], 'V': [0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0],
        'W': [0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0], 'X': [0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0],
        'Y': [0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0], 'Z': [0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0],
    }
    _FONT_ORDER = "0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    _font_bytes = [b for ch in _FONT_ORDER for b in _FONT_HEX[ch]]
    font_s = _yybytes(_font_bytes)
    # --- the control map ($98DD): control bits -> move, per facing -----------
    # Keys: U D F B = up, down, forward (towards the opponent), back; X = fire.
    # The original's own map (FIST_ORIG_KEYS=1) and the port's default, chosen
    # by the user: fire on Space, the sweeps on the down diagonals without fire,
    # the punches on fire+up / fire+down, the somersaults on the up diagonals.
    # (original: up+forward = 6 high punch, up+back = 9 forward somersault,
    #  down+forward = 7 low punch, down+back = 8 backward somersault)
    ORIG_MAP = {"U": 5, "D": 4, "F": 2, "B": 3, "UF": 6, "UB": 9, "DF": 7, "DB": 8,
                "XU": 14, "XD": 10, "XF": 12, "XB": 17, "XDF": 11, "XUF": 13,
                "XUB": 15, "XDB": 16}
    USER_MAP = {"U": 5, "D": 4, "F": 2, "B": 3, "UB": 8, "UF": 9, "DF": 10, "DB": 16,
                "XU": 6, "XD": 7, "XF": 12, "XB": 13, "XDF": 11, "XUF": 14,
                "XUB": 15, "XDB": 17}
    cmap = ORIG_MAP if os.environ.get("FIST_ORIG_KEYS") else USER_MAP

    def _ctrl_table(fwd_bit, back_bit):
        out = []
        for idx in range(32):
            k = ""
            if idx & 16: k += "X"
            if idx & 1: k += "U"
            if idx & 2: k += "D"
            if idx & fwd_bit: k += "F"
            if idx & back_bit: k += "B"
            out.append(cmap.get(k, 1))
        return out
    tab_a = _ctrl_table(8, 4)                     # facing right: forward = RIGHT
    tab_b = _ctrl_table(4, 8)                     # facing left:  forward = LEFT
    mtab = ("MTAB:   .BYTE   " + ",".join(f"{v}." for v in tab_a[:16]) + "\n"
            "        .BYTE   " + ",".join(f"{v}." for v in tab_a[16:]) + "\n"
            "        .BYTE   1\n"
            "        .BYTE   " + ",".join(f"{v}." for v in tab_b[:16]) + "\n"
            "        .BYTE   " + ",".join(f"{v}." for v in tab_b[16:]) + "\n"
            "        .BYTE   1\n")

    def _strb(label, s):
        codes = ",".join(f"{_FONT_ORDER.index(c)}." for c in s.upper())
        return f"{label}: .BYTE   {codes},377\n        .EVEN"
    def _fc(ch):
        return _FONT_ORDER.index(ch)
    cap = 120                                     # game frames that cap a round-end wait
    pause = 40                                    # the $AF1A x2 pause after a time-out: held frames (~33 ms each) -> ~1.3 s
    # HUD cells: restore the clean dojo cell (DOJOBUF is the picture in VRAM row
    # layout, rows offset by the 4-row top margin) before the glyph goes on, so
    # a changed digit / a cleared yin-yang leaves no trace.  No dojo -> clear.
    cell_restore = ("MOV     DOJOBUF-VRAM-320.(R0),(R0) ; restore the clean dojo cell row"
                    if withbg else "CLR     (R0)                 ; clear the cell row")
    driver = f"""
        .ASECT
        . = 44
        .WORD   21000                  ; JSW (file-I/O .SAV flags)
        . = 1000
        .EVEN
START:  MOV     #37776,SP              ; stack above the code, below BUF
        MOVB    @#SYSC,ORIGRC
        JMP     BOOT                   ; the loader (boot-only code, see boot_code)
{boot_inline}        ; CHUNK: copy R2 words from BUF into the parked extended banks at R1
CHUNK:  MTPS    #340
        MOVB    #EXT,@#DISPAT
        MOV     #BUF,R0
1$:     MOV     (R0)+,(R1)+
        SOB     R2,1$
        MOVB    #PRIM,@#DISPAT
        MTPS    #0
        RTS     PC
BOOT2:  MOV     #GAME,@#DISPAT         ; 03217: VRAM on, window @40000, banks 4-6 ext
        MOV     #VRAM,R0
3$:     CLR     (R0)+
        CMP     R0,#VRAMEN
        BLO     3$
        MOV     #3003,@#DISPAT         ; the sprite cache (extended banks 10-11): empty
        MOV     #40000,R0
10$:    CLR     (R0)+
        CMP     R0,#100000
        BLO     10$
        MOV     #GAME,@#DISPAT
        ; --- $AC3E Start_1UP_Game: the match-state batch (P1 human, P2 the
        ;     computer, score 0, rank 0), then the first opponent's set-up (the
        ;     background) and a new round.  GST.DAT is a mid-attract snapshot, so
        ;     every cell this touches is deliberately re-initialised here. ---
        MOV     #12345.,RSEED
        JSR     PC,DINIT             ; $AC05: the attract demo first ($9C2C = 0)
{dojo_boot}        JSR     PC,SETUP
        ; tell the MS7004 keyboard 0o231 (keyclick off) - the firmware treats this as the
        ; "a game is running" signal and switches auto-repeat to the fast game preset
        ; (125 ms delay vs 250 ms typing), so held-key tracking is snappier.
83$:    MOVB    @#177442,R0          ; wait for the keyboard UART transmitter
        BITB    #1,R0                ; TXRDY?
        BEQ     83$
        MOVB    #231,@#177440
GLOOP:  MOV     #GAME,@#DISPAT       ; (re-)park: 03217, banks 4-6 extended
        MOV     #W,R0                ; clear decoder scratch
        MOV     #32.,R1
4$:     CLR     (R0)+
        DEC     R1
        BNE     4$
        ; --- keyboard -> P1 move (AA05).  P1 is human (AA06=0), so MOVSEL leaves AA05
        ;     unless a reaction is queued (which correctly overrides player input). ---
        JSR     PC,KSCAN             ; drain the keyboard into the hold timers
        JSR     PC,KCTRL             ; R0 = the control bits (ticks the timers)
        TST     DEMO
        BNE     79$
        TST     KTG                  ; $9827: "G" and "H" held together quit the
        BEQ     79$                  ;   game -> the demo ($9C2C = 0, A = $80)
        TST     KTH
        BEQ     79$
        CLR     KTG
        CLR     KTH
        CLR     KTFR                 ; (the chord must not restart the game at once)
        CLR     KSTART
        CLR     RPHASE
        JSR     PC,DINIT
        JSR     PC,SETUP
        CLR     R0
79$:    TST     DEMO                 ; in the demo, fire ($97E3) or "1" ($97DC) starts
        BEQ     70$                  ;   a 1-player game
        BIT     #20,R0
        BNE     71$
        TST     KSTART
        BEQ     70$
71$:    CLR     KSTART
        CLR     RPHASE                ; (the demo's round-end sequence, if any, is over)
        JSR     PC,GINIT             ; $AC3E
        JSR     PC,SETUP
        CLR     R0
70$:    JSR     PC,C98A0             ; R0 = &move ($98DD table)
        MOVB    (R0),R0
{dbgmove}        MOVB    R0,{g(0xAA05)}       ; P1 selected move
        TST     RPHASE                ; a round-end sequence in progress?
        BNE     83$
        JSR     PC,ORCH              ; one logic frame (AI driven by the LFSR ARNG)
        JSR     PC,ROUNDE            ; $AD18: score the exchange, end the round
        BR      84$
83$:    JSR     PC,RNDEND            ; one frame of the round-end sequence
84$:    MOVB    {g(0xAA01)},SC1      ; stash the scores for the HUD (GST unseen at 3377)
        MOVB    {g(0xAA41)},SC2
        MOVB    {g(0xB02D)},SCRBCD   ; and P1's BCD point score, for DRWSCR at 3377
        MOVB    {g(0xB02D)}+1.,SCRBCD+1.
        MOVB    {g(0xB02D)}+2.,SCRBCD+2.
        MOVB    {g(0x9CA5)},STIM     ; and the round timer, for DRWTIM at 3377
        MOVB    {g(0xB05F)},RANKB    ; and the rank ($B05F, BCD), for DRWRNK at 3377
        TST     DEMO
        BEQ     69$
        MOVB    #377,RANKB           ; (377 = "DEMO")
69$:        ; --- sound ($9754): play the effect the hit logic queued in $B150 ---
80$:    MOVB    {g(0xB150)},R0       ; $B150 = sound code queued by $9ED2/$9D29 hit-detect
        BIC     #177400,R0
        BEQ     81$
        JSR     PC,SNDFX             ; $B15A: play it (blocking, bit-banged speaker)
        CLRB    {g(0xB150)}          ; ($B15A clears it after playing)
81$:    ; --- Fighter 1: clear FBUF, decode box A, stash its box, copy to LBUF1 ---
        ; Decode cache: the draw set-up ($C101 / $C1A2) reads only these render
        ; cells (box, sprite origin, pose record, facing, mode); when none changed
        ; since the last decode, LBUF1 / RW1 / RT1 / RL1 still hold its result.
        CMPB    {g(0xc41b)},KEY1+0.
        BNE     87$
        CMPB    {g(0xc41c)},KEY1+1.
        BNE     87$
        CMPB    {g(0xc41f)},KEY1+2.
        BNE     87$
        CMPB    {g(0xc421)},KEY1+3.
        BNE     87$
        CMPB    {g(0xc428)},KEY1+4.
        BNE     87$
        CMPB    {g(0xc429)},KEY1+5.
        BNE     87$
        CMPB    {g(0xc434)},KEY1+6.
        BNE     87$
        CMPB    {g(0xc435)},KEY1+7.
        BNE     87$
        CMPB    {g(0xc436)},KEY1+8.
        BNE     87$
        CMPB    {g(0xc437)},KEY1+9.
        BNE     87$
        CMPB    {g(0xc407)},KEY1+10.
        BNE     87$
        CMPB    {g(0xc40e)},KEY1+11.
        BNE     87$
        JMP     86$
87$:        MOVB    {g(0xc41b)},KEY1+0.
        MOVB    {g(0xc41c)},KEY1+1.
        MOVB    {g(0xc41f)},KEY1+2.
        MOVB    {g(0xc421)},KEY1+3.
        MOVB    {g(0xc428)},KEY1+4.
        MOVB    {g(0xc429)},KEY1+5.
        MOVB    {g(0xc434)},KEY1+6.
        MOVB    {g(0xc435)},KEY1+7.
        MOVB    {g(0xc436)},KEY1+8.
        MOVB    {g(0xc437)},KEY1+9.
        MOVB    {g(0xc407)},KEY1+10.
        MOVB    {g(0xc40e)},KEY1+11.
        ; sprite cache: the decoded image depends only on the pose record, the
        ; sub-cell x shift, facing and mode - look it up (16 slots in the extended
        ; banks 10-11, visible at 040000 with slots 2-3 extended and the VRAM
        ; window off: 03003) before decoding; a hit is a copy instead of a decode.
        JSR     PC,CKEY1
        MOV     #3003,@#DISPAT
        MOV     #LBUF1,R1
        JSR     PC,CLOOK
        MOV     #GAME,@#DISPAT
        TST     R0
        BNE     63$
        MOV     R2,RW1               ; hit: the image is in LBUF1, its width in R2;
        MOVB    {g(0xc41c)},R0       ;   the (tight) box is the sprite origin
        BIC     #177400,R0
        MOV     R0,RT1
        MOVB    {g(0xc41b)},R0
        BIC     #177400,R0
        MOV     R0,RL1
        JMP     86$
63$:
        MOV     #FBUF,R0
        MOV     #{lb_words}.,R1
5$:     CLR     (R0)+
        DEC     R1
        BNE     5$
        JSR     PC,C101C
        JSR     PC,SETUPC
6$:     JSR     PC,SEGSET
        MOVB    {c408},R0
        BIC     #177400,R0
        MOV     R0,C408W
        JSR     PC,DECRUN
        DECB    SEGCNT
        BNE     6$
        MOVB    {g(0xC40A)},R0       ; box A: width, top ($C436), left ($C434)
        BIC     #177400,R0
        MOV     R0,RW1
        MOVB    {g(0xC436)},R0
        BIC     #177400,R0
        MOV     R0,RT1
        MOVB    {g(0xC434)},R0
        BIC     #177400,R0
        MOV     R0,RL1
        MOV     #FBUF,R1
        MOV     #LBUF1,R0
        MOV     #{lb_words}.,R2
62$:    MOV     (R1)+,(R0)+
        DEC     R2
        BNE     62$
        MOV     #3003,@#DISPAT       ; a miss: remember the decode in the cache
        MOV     #LBUF1,R1
        MOV     RW1,R2
        JSR     PC,CSTOR
        MOV     #GAME,@#DISPAT
86$:    ; --- Fighter 2: clear FBUF, decode box B, stash its box, copy to LBUF2 ---
        CMPB    {g(0xc41d)},KEY2+0.
        BNE     89$
        CMPB    {g(0xc41e)},KEY2+1.
        BNE     89$
        CMPB    {g(0xc420)},KEY2+2.
        BNE     89$
        CMPB    {g(0xc422)},KEY2+3.
        BNE     89$
        CMPB    {g(0xc42a)},KEY2+4.
        BNE     89$
        CMPB    {g(0xc42b)},KEY2+5.
        BNE     89$
        CMPB    {g(0xc438)},KEY2+6.
        BNE     89$
        CMPB    {g(0xc439)},KEY2+7.
        BNE     89$
        CMPB    {g(0xc43a)},KEY2+8.
        BNE     89$
        CMPB    {g(0xc43b)},KEY2+9.
        BNE     89$
        CMPB    {g(0xc407)},KEY2+10.
        BNE     89$
        CMPB    {g(0xc40e)},KEY2+11.
        BNE     89$
        JMP     88$
89$:        MOVB    {g(0xc41d)},KEY2+0.
        MOVB    {g(0xc41e)},KEY2+1.
        MOVB    {g(0xc420)},KEY2+2.
        MOVB    {g(0xc422)},KEY2+3.
        MOVB    {g(0xc42a)},KEY2+4.
        MOVB    {g(0xc42b)},KEY2+5.
        MOVB    {g(0xc438)},KEY2+6.
        MOVB    {g(0xc439)},KEY2+7.
        MOVB    {g(0xc43a)},KEY2+8.
        MOVB    {g(0xc43b)},KEY2+9.
        MOVB    {g(0xc407)},KEY2+10.
        MOVB    {g(0xc40e)},KEY2+11.
        ; sprite cache: the decoded image depends only on the pose record, the
        ; sub-cell x shift, facing and mode - look it up (16 slots in the extended
        ; banks 10-11, visible at 040000 with slots 2-3 extended and the VRAM
        ; window off: 03003) before decoding; a hit is a copy instead of a decode.
        JSR     PC,CKEY2
        MOV     #3003,@#DISPAT
        MOV     #LBUF2,R1
        JSR     PC,CLOOK
        MOV     #GAME,@#DISPAT
        TST     R0
        BNE     73$
        MOV     R2,RW2               ; hit: the image is in LBUF2, its width in R2;
        MOVB    {g(0xc41e)},R0       ;   the (tight) box is the sprite origin
        BIC     #177400,R0
        MOV     R0,RT2
        MOVB    {g(0xc41d)},R0
        BIC     #177400,R0
        MOV     R0,RL2
        JMP     88$
73$:
        MOV     #FBUF,R0
        MOV     #{lb_words}.,R1
56$:    CLR     (R0)+
        DEC     R1
        BNE     56$
        JSR     PC,C1CC
        JSR     PC,SETUPC
7$:     JSR     PC,SEGSET
        MOVB    {c408},R0
        BIC     #177400,R0
        MOV     R0,C408W
        JSR     PC,DECRUN
        DECB    SEGCNT
        BNE     7$
        MOVB    {g(0xC40A)},R0       ; box B: width, top ($C43A), left ($C438)
        BIC     #177400,R0
        MOV     R0,RW2
        MOVB    {g(0xC43A)},R0
        BIC     #177400,R0
        MOV     R0,RT2
        MOVB    {g(0xC438)},R0
        BIC     #177400,R0
        MOV     R0,RL2
        MOV     #FBUF,R1
        MOV     #LBUF2,R0
        MOV     #{lb_words}.,R2
72$:    MOV     (R1)+,(R0)+
        DEC     R2
        BNE     72$
        MOV     #3003,@#DISPAT       ; a miss: remember the decode in the cache
        MOV     #LBUF2,R1
        MOV     RW2,R2
        JSR     PC,CSTOR
        MOV     #GAME,@#DISPAT
88$:    ; --- on-screen geometry for both fighters (each clamped to the screen) ---
        MOV     RW1,R3               ; fighter 1: raw width / top / left -> COL1/TOP1/BWID1/W1
        MOV     RT1,R4
        MOV     RL1,R5
        JSR     PC,GEOMC
        MOV     R0,COL1
        MOV     R1,TOP1
        MOV     R2,BWID1
        MOV     R3,W1
        MOV     RW2,R3               ; fighter 2
        MOV     RT2,R4
        MOV     RL2,R5
        JSR     PC,GEOMC
        MOV     R0,COL2
        MOV     R1,TOP2
        MOV     R2,BWID2
        MOV     R3,W2
        MOV     #3377,@#DISPAT       ; unpark: 03377 (RMON back, VRAM on) - present-safe
        ; --- flicker-free compositor: per screen row, CLEAR then overlay each fighter ---
        ; Each fighter is drawn from its own buffer (LBUF1 / LBUF2) at its own column
        ; (COL) and top (TOP).  SRCn walks the sprite one stride (BWIDn) per row once the
        ; row reaches TOPn, and stops at the buffer end; black (zero) cells are skipped so
        ; the two sprites overlay transparently.  Clearing each row before overlay means
        ; the screen is never globally blank -> no flicker.
        ; Only clear/composite the fighters' active band [TOPCLR..200): TOPCLR =
        ; min(TOP1, TOP2, last frame's min top) so a descending fighter's old rows still
        ; get erased.  Rows above stay black from the start-up clear -> big speed win.
        MOV     TOP1,R0
        CMP     R0,TOP2
        BLE     60$
        MOV     TOP2,R0
60$:    MOV     R0,R1                ; R1 = this frame's min top
        CMP     R0,LASTTP
        BLE     61$
        MOV     LASTTP,R0            ; include last frame's top so descents don't ghost
61$:    MOV     R1,LASTTP
        MOV     R0,ROWN
        MOV     R0,R2                ; VRAM row ptr = VRAM + ROWN*80
        ASL     R2
        ASL     R2
        ASL     R2
        ASL     R2
        MOV     R2,R1
        ASL     R2
        ASL     R2
        ADD     R1,R2
        ADD     #VRAM,R2
        MOV     #LBUF1,SRC1
        MOV     #LBUF2,SRC2
        ; Each row is composed IN PLACE in VRAM: the clean dojo row is copied
        ; over it, then the fighters are overlaid - a cell is never black in
        ; between, only "dojo without the fighter" for the few microseconds
        ; between the copy and the overlay.
CLOOP:  {dojo_row}CCLR:   MOV     R2,R0                ; outside the dojo band: clear the row
        MOV     #10.,R3              ; clear 40 words, unrolled x4 (less loop overhead)
CCL1:   CLR     (R0)+
        CLR     (R0)+
        CLR     (R0)+
        CLR     (R0)+
        DEC     R3
        BNE     CCL1
CDDN:   MOV     ROWN,R0              ; --- fighter 1 ---
        CMP     R0,TOP1
        BLO     C1SK                 ; row above the sprite
        CMP     SRC1,#LBUF1+{lb_words}.*2
        BHIS    C1SK                 ; sprite exhausted
        MOV     W1,R3
        BEQ     C1AD                 ; off-screen width -> advance src only
        MOV     R2,R0                ; dst = the VRAM row + COL1*2
        MOV     COL1,R4
        ASL     R4
        ADD     R4,R0
        MOV     SRC1,R1
C1OV:   MOVB    (R1)+,R4
        BEQ     C1TR                 ; zero cell = fully transparent (dojo shows)
        BIC     #177400,R4
        BISB    R4,(R0)              ; OR the fighter pixels into the background cell
        {ovl_ink}
C1TR:   TST     (R0)+
        DEC     R3
        BNE     C1OV
C1AD:   ADD     BWID1,SRC1           ; next compose row (full stride)
C1SK:   MOV     ROWN,R0              ; --- fighter 2 ---
        CMP     R0,TOP2
        BLO     C2SK
        CMP     SRC2,#LBUF2+{lb_words}.*2
        BHIS    C2SK
        MOV     W2,R3
        BEQ     C2AD
        MOV     R2,R0
        MOV     COL2,R4
        ASL     R4
        ADD     R4,R0
        MOV     SRC2,R1
C2OV:   MOVB    (R1)+,R4
        BEQ     C2TR
        BIC     #177400,R4
        BISB    R4,(R0)              ; OR the fighter pixels into the background cell
        {ovl_ink}
C2TR:   TST     (R0)+
        DEC     R3
        BNE     C2OV
C2AD:   ADD     BWID2,SRC2
C2SK:   ADD     #80.,R2              ; next screen row
        INC     ROWN
        CMP     ROWN,#196.           ; the band ends with the dojo (the floor is row 194)
        BHIS    58$                  ; done -> next frame
        JMP     CLOOP                ; (JMP: CLOOP is out of branch range)
        ; --- status strip: redrawn only when a shown value changed (or the dojo
        ;     was re-presented, HUDDRT) - it is ~7% of a frame otherwise ---
58$:    TST     HUDDRT
        BNE     59$
        CMPB    SC1,HUDK
        BNE     59$
        CMPB    SC2,HUDK+1.
        BNE     59$
        CMPB    SCRBCD,HUDK+2.
        BNE     59$
        CMPB    SCRBCD+1.,HUDK+3.
        BNE     59$
        CMPB    SCRBCD+2.,HUDK+4.
        BNE     59$
        CMPB    STIM,HUDK+5.
        BNE     59$
        CMPB    RANKB,HUDK+6.
        BNE     59$
        JMP     GLOOP
59$:    MOVB    SC1,HUDK
        MOVB    SC2,HUDK+1.
        MOVB    SCRBCD,HUDK+2.
        MOVB    SCRBCD+1.,HUDK+3.
        MOVB    SCRBCD+2.,HUDK+4.
        MOVB    STIM,HUDK+5.
        MOVB    RANKB,HUDK+6.
        CLR     HUDDRT
        JSR     PC,HUD               ; draw the yin-yang score bar (top border)
        JSR     PC,DRWSCR            ; draw the numeric score across the top strip
        JSR     PC,DRWTIM            ; draw the round timer beside it
        JSR     PC,DRWRNK            ; draw the rank ("NOVICE" / "1ST DAN" ...) at the left
        JMP     GLOOP                ; next frame (no busy-wait; the work itself paces it)
LDERR:  MOV     #2177,@#DISPAT         ; unpark: banks primary, VRAM off (RMON back)
        MOVB    ORIGRC,@#SYSC
        MTPS    #0
        .EXIT
        ; --- sprite cache.  Slot = 5-byte key (pose record lo/hi, x & 3, $C41F,
        ;     $C421 - facing / mode come from the last two), pad, width, pad, the
        ;     884-byte image: 892 bytes, 16 slots direct-mapped by a hash of the
        ;     key.  CKEY1/CKEY2 build the key for a fighter and pick the slot;
        ;     CLOOK / CSTOR run with the cache mapped (03003). ------------------
CKEY1:  MOVB    {g(0xc428)},CKEY
        MOVB    {g(0xc429)},CKEY+1.
        MOVB    {g(0xc41b)},R0
        BIC     #177774,R0
        MOVB    R0,CKEY+2.
        MOVB    {g(0xc41f)},CKEY+3.
        MOVB    {g(0xc421)},CKEY+4.
        BR      CSLOT
CKEY2:  MOVB    {g(0xc42a)},CKEY
        MOVB    {g(0xc42b)},CKEY+1.
        MOVB    {g(0xc41d)},R0
        BIC     #177774,R0
        MOVB    R0,CKEY+2.
        MOVB    {g(0xc420)},CKEY+3.
        MOVB    {g(0xc422)},CKEY+4.
CSLOT:  MOVB    CKEY,R0              ; hash: lo ^ lo>>3 ^ hi ^ sub ^ facing<<2, & 15
        BIC     #177400,R0
        MOV     R0,R1
        ASR     R1
        ASR     R1
        ASR     R1
        XOR     R1,R0
        MOVB    CKEY+1.,R1
        BIC     #177400,R1
        XOR     R1,R0
        MOVB    CKEY+2.,R1
        BIC     #177400,R1
        XOR     R1,R0
        MOVB    CKEY+3.,R1
        BIC     #177400,R1
        ASL     R1
        ASL     R1
        XOR     R1,R0
        BIC     #177760,R0
        ASL     R0
        MOV     SLOTAB(R0),SLOT
        RTS     PC
        ; CLOOK (at 03003): the slot holds CKEY? -> copy its image to (R1), R2 =
        ; width, R0 = 0; else R0 = 1.
CLOOK:  MOV     SLOT,R3
        CMPB    CKEY,(R3)
        BNE     9$
        CMPB    CKEY+1.,1(R3)
        BNE     9$
        CMPB    CKEY+2.,2(R3)
        BNE     9$
        CMPB    CKEY+3.,3(R3)
        BNE     9$
        CMPB    CKEY+4.,4(R3)
        BNE     9$
        MOVB    6(R3),R2
        BIC     #177400,R2
        ADD     #8.,R3
        MOV     #{lb_words}.,R4
1$:     MOV     (R3)+,(R1)+
        DEC     R4
        BNE     1$
        CLR     R0
        RTS     PC
9$:     MOV     #1,R0
        RTS     PC
        ; CSTOR (at 03003): store CKEY, the width R2 and the image at (R1).
CSTOR:  MOV     SLOT,R3
        MOVB    CKEY,(R3)
        MOVB    CKEY+1.,1(R3)
        MOVB    CKEY+2.,2(R3)
        MOVB    CKEY+3.,3(R3)
        MOVB    CKEY+4.,4(R3)
        MOVB    R2,6(R3)
        ADD     #8.,R3
        MOV     #{lb_words}.,R4
1$:     MOV     (R1)+,(R3)+
        DEC     R4
        BNE     1$
        RTS     PC
        .EVEN
SLOTAB: .WORD   16384.,17276.,18168.,19060.,19952.,20844.,21736.,22628.,23520.,24412.,25304.,26196.,27088.,27980.,28872.,29764.
        ; --- GEOMC: clamp one fighter's raw box to the screen ----------------------
        ; in:  R3 = raw width ($C40A), R4 = raw top, R5 = raw left
        ; out: R0 = COL (cell), R1 = TOP (screen row), R2 = BWID (stride), R3 = W (cells)
GEOMC:  MOV     R5,R0                ; col = (left >> 2) + 4
        ASR     R0
        ASR     R0
        ADD     #4,R0
        MOV     R3,R2                ; BWID = min(raw width, fwmax)
        CMP     R2,#{fwmax}.
        BLE     1$
        MOV     #{fwmax}.,R2
1$:     MOV     #36.,R5              ; W = min(BWID, 36 - col): clip to the picture
        SUB     R0,R5
        MOV     R2,R3
        CMP     R3,R5
        BLE     2$
        MOV     R5,R3
2$:     TST     R3
        BGT     3$
        CLR     R3                   ; off the right edge / wrapped -> skip
3$:     MOV     R4,R1                ; top: a high jump wraps above row 0
        CMP     R1,#150.
        BLE     4$
        CLR     R1
4$:     ADD     #4,R1                ; +4 = top centring margin
        RTS     PC
        ; --- SCDET: $AF01 clean-hit detect.  ($AA03 XOR $AA43) & $10 -> R0 (nz = a
        ;     fighter took an un-blocked hit this exchange).  Bit 4 of the reaction
        ;     differs when exactly one guard was open. ------------------------------
SCDET:  MOVB    {g(0xAA03)},R0
        BIC     #177400,R0
        MOVB    {g(0xAA43)},R1
        BIC     #177400,R1
        XOR     R1,R0
        BIC     #177757,R0           ; keep bit 4 ($10)
        RTS     PC
        ; --- ROUNDE: the $AD18 round loop, per frame.  An exchange ends when a
        ;     fighter is knocked into recovery ($9C28, as $AE26 tests) - SCOREX
        ;     scores it - or the clock runs out ($9C2B): $AE67 - both return to
        ;     the stance ($1C), $ACF0 clears the recovery state and $AD0B
        ;     animates until $AA0D is set (RNDEND phase 2), then the $AF1A
        ;     pause (phase 3) and the scoring + decision (SCOREX -> DECIDE). ----
ROUNDE: MOVB    {g(0x9C28)},R0
        BIC     #177400,R0
        BEQ     70$
        JMP     SCOREX
70$:    MOVB    {g(0x9C2B)},R0
        BIC     #177400,R0
        BEQ     78$                  ; neither -> the exchange continues
        MOVB    #34,{g(0xAA0C)}      ; $AE67: both to $1C
        MOVB    #34,{g(0xAA4C)}
        JSR     PC,RSTACF            ; $ACF0
        MOV     #2,RPHASE             ; $AD0B: until $AA0D (WINTMR caps the wait)
        MOV     #{cap}.,WINTMR
78$:    RTS     PC
        ; --- SCOREX: $AD1D..$AD42 - score the exchange: SCDET ($AF01 clean hit?)
        ;     -> YINYNG ($900E total) + AWARD ($AF36 points); two yin-yang
        ;     ($AA01/$AA41 >= 4) wins the round; $AD37 clears the score flags;
        ;     on the clock ($9C2B) the round is decided ($AD44 = DECIDE), else
        ;     RSTFRM ($9CA8, via $AE26) + RSTAI restart the exchange. -----------
SCOREX: JSR     PC,SCDET
        TST     R0
        BEQ     72$                  ; no clean hit -> no score this exchange
        JSR     PC,YINYNG
        JSR     PC,AWARD
        MOVB    {g(0xAA01)},R0       ; P1 at two yin-yang? the round is P1's
        BIC     #177400,R0
        CMP     R0,#4.
        BLO     77$
        JMP     WIN1
77$:    MOVB    {g(0xAA41)},R0       ; P2?
        BIC     #177400,R0
        CMP     R0,#4.
        BLO     72$
        JMP     WIN2
72$:    CLRB    {g(0xAA08)}          ; $AD37: clear the score flags every exchange
        CLRB    {g(0xAA48)}
        MOVB    {g(0x9C2B)},R0       ; on the clock -> the decision
        BIC     #177400,R0
        BEQ     79$
        JMP     DECIDE
79$:    JSR     PC,RSTFRM            ; else the next exchange
        JSR     PC,RSTAI
        RTS     PC
        ; --- DECIDE: $AD44 - on the clock the yin-yang totals decide, then the
        ;     $AA02/$AA42 points, else it is a draw ($ACE8: both bow, $ACF0).
        ;     WIN1 / WIN2: $AE7A / $AE9D - the winner bows ($19) while the
        ;     loser's get-up keeps playing.  HOLD starts RNDEND phase 1, which
        ;     lasts until the winner's move-done flag ($AA0D / $AA4D) is set.
        ;     RESULT = $AD18's A: 1 = P1 won, 0 = P2 won, 201 ($81) = draw. ------
DECIDE: MOVB    {g(0xAA01)},R0
        BIC     #177400,R0
        MOVB    {g(0xAA41)},R1
        BIC     #177400,R1
        CMP     R0,R1
        BLO     WIN2
        BHI     WIN1
        MOVB    {g(0xAA02)},R0
        BIC     #177400,R0
        MOVB    {g(0xAA42)},R1
        BIC     #177400,R1
        CMP     R0,R1
        BLO     WIN2
        BHI     WIN1
        MOV     #201,RESULT          ; a draw ($81): $ACE8 - both bow ($19),
        MOVB    #31,{g(0xAA0C)}      ;   then $ACF0 clears the recovery state
        MOVB    #31,{g(0xAA4C)}
        JSR     PC,RSTACF
        BR      HOLD
WIN1:   MOV     #1,RESULT            ; P1 won the round: $AE7A - P1 bows ($19)
        MOVB    #31,{g(0xAA0C)}
        MOVB    #172,{g(0xAA18)}
        CLRB    {g(0xAA0B)}
        CLRB    {g(0xAA16)}
        CLRB    {g(0xC427)}
        BR      HOLD
WIN2:   CLR     RESULT               ; P2 won the round: $AE9D - P2 bows ($19)
        MOVB    #31,{g(0xAA4C)}
        MOVB    #172,{g(0xAA58)}
        CLRB    {g(0xAA4B)}
        CLRB    {g(0xAA56)}
        MOVB    #1,{g(0xC427)}
HOLD:   MOV     #1,RPHASE
        MOV     #{cap}.,WINTMR       ; (a cap: a missing flag can never hang the game)
        RTS     PC
        ; --- RNDEND: one frame of the round-end sequence (RPHASE).  2: the $AD0B
        ;     loop after a time-out - both animate back to the stance until
        ;     $AA0D; 3: the $AF1A x2 pause (~1.3 s, the frame held), then the
        ;     scoring; 1: the bow ($AE8E / $AEB2 / $AD0B) until the winner's
        ;     flag, P1's clock paying out a second per frame meanwhile ($AD5F);
        ;     then OUTCOM ($ACA6).  WINTMR caps every wait. -----------------------
RNDEND: CMP     RPHASE,#3
        BEQ     3$
        JSR     PC,HOLDFR            ; $95D4 + $BF13: animate + re-bridge the poses
        CMP     RPHASE,#2
        BEQ     2$
        CMP     RESULT,#1
        BNE     1$
        JSR     PC,TBONUS
1$:     TST     RESULT               ; P2 won: its flag is $AA4D, else $AA0D
        BEQ     6$
        TSTB    {g(0xAA0D)}
        BR      7$
6$:     TSTB    {g(0xAA4D)}
7$:     BNE     9$
        DEC     WINTMR
        BNE     8$
9$:     CLR     RPHASE
        JMP     OUTCOM               ; $ACA6: next round / next opponent / game over
2$:     TSTB    {g(0xAA0D)}          ; phase 2: both back in the stance?
        BNE     4$
        DEC     WINTMR
        BNE     8$
4$:     MOV     #3,RPHASE
        MOV     #{pause}.,WINTMR
8$:     RTS     PC
3$:     DEC     WINTMR               ; phase 3: the pause, then the scoring
        BNE     8$
        JMP     SCOREX
        ; --- HOLDFR: one iteration of the original's round-end loops ($AE8E /
        ;     $AEB2 / $AD0B): advance both animations ($95D4) and re-run the
        ;     logic->graphics bridge ($BF13), so the bow plays and the draw chain
        ;     keeps valid inputs (re-drawing without the bridge drifts the poses). -
HOLDFR: MOV     #125026,R5           ; hl = $AA16 -> fighter 0
        JSR     PC,ANIM5E
        MOV     #125126,R5           ; hl = $AA56 -> fighter 1
        JSR     PC,ANIM5E
        JSR     PC,BF13
        RTS     PC
        ; --- TBONUS: one step of $AD5F's clock pay-out: while the clock shows time,
        ;     $AF52 credits a point ($AA02 + the BCD score) and $9CA0 ticks. ------
TBONUS: TSTB    {g(0x9CA5)}
        BEQ     9$
        MOVB    {g(0xAA02)},R0       ; $AF52 B=1: $AA02 += 1
        BIC     #177400,R0
        INC     R0
        MOVB    R0,{g(0xAA02)}
        MOV     #1,R1                ; score += 1 (BCD, $AFC2)
        MOV     #45101.,R5
        JSR     PC,SCORE
        CLRB    {g(0xAA08)}
        JSR     PC,TIMTIK            ; $9CA0
9$:     RTS     PC
        ; --- OUTCOM: $ACA6, after $AD18 returns RESULT.  P1 won: pay out the clock,
        ;     count the round; the 2nd round ($AA3C) won moves to the next opponent
        ;     - rank / background ($AF27), dan $B05F (BCD; at 10 it stays and the
        ;     opponent is a random 7..10), $AA80++ - and flags the set-up.  A draw
        ;     ($81) replays the round.  P2 won: game over - a fresh 1UP game. -----
OUTCOM: MOV     RESULT,R0
        CMP     R0,#1
        BEQ     1$
        CMP     R0,#201
        BEQ     SETUP                ; draw: same opponent, new round
        JSR     PC,DINIT             ; P2 won -> game over -> back to the demo ($AC09)
        BR      SETUP
1$:     JSR     PC,TBONUS            ; flush the clock pay-out ($AD6E loop)
        TSTB    {g(0x9CA5)}
        BNE     1$
        DECB    {g(0xAA3C)}          ; rounds left against this opponent
        BNE     SETUP                ; (no set-up flagged: the same opponent again)
        MOVB    #2,{g(0xAA3C)}
        JSR     PC,RANKTK            ; $AF27: rank / background 1..3
        MOVB    #1,{g(0xAF35)}       ; flag the opponent set-up
        MOVB    {g(0xB05F)},R0       ; dan (BCD)
        BIC     #177400,R0
        CMP     R0,#20               ; 10th dan stays: a random 7..10 opponent ($ACDB)
        BNE     2$
        JSR     PC,ARNG
        BIC     #177774,R0
        ADD     #7,R0
        MOVB    R0,{g(0xAA80)}
        BR      SETUP
2$:     MOV     #1,R1                ; dan += 1 (ADD/DAA)
        CLR     R4
        JSR     PC,BCDADD
        MOVB    R0,{g(0xB05F)}
        INCB    {g(0xAA80)}
        ; --- SETUP: $AC5F - when flagged ($AF35), present the opponent: the
        ;     background ($AF34 -> $9200); then $AC9D: a new round ($909E), the
        ;     clock ($AEF8) and the exchange reset ($AE26 -> $9CA8). ---------------
SETUP:  TSTB    {g(0xAF35)}
        BEQ     NEWRND
        CLRB    {g(0xAF35)}
{"        JSR     PC,RENDBG            ; $9200: render + present the dojo" if withbg else ""}
NEWRND: CLRB    {g(0xAA01)}          ; $909E: both tallies and points to 0
        CLRB    {g(0xAA41)}
        CLRB    {g(0xAA02)}
        CLRB    {g(0xAA42)}
        MOVB    #36,{g(0x9CA5)}      ; $AEF8: 30 seconds on the clock
        JSR     PC,RSTFRM            ; $9CA8: both fighters to the start stance
        JSR     PC,RSTAI             ; $9D0B: and both AI states re-initialised
        RTS     PC
        ; --- RSTAI: the $9D0B..$9D26 tail of the exchange reset.  For each
        ;     fighter: swap its state + its AI block into the scratch area, run
        ;     $A402 (load the AI personality's parameters, reset its counters),
        ;     swap back.  P1's AI block is $AA8B (id $AA94), P2's is $AA77 (id
        ;     $AA80 - the one the 1UP game advances per opponent).  Without this
        ;     the port ran $A402 once at boot on a stale scratch copy, so the
        ;     computer only fought in its first exchange. --------------------
RSTAI:  JSR     PC,AADC
        JSR     PC,AB0A
        JSR     PC,A402
        JSR     PC,AAF3
        JSR     PC,AB16
        JSR     PC,AB22
        JSR     PC,AB50
        JSR     PC,A402
        JSR     PC,AB39
        JSR     PC,AB5C
        RTS     PC
        ; --- GINIT: $AC3E Start_1UP_Game's state batch. --------------------------
GINIT:  CLR     DEMO
        CLRB    {g(0xAA80)}          ; opponent index (= the computer's AI personality)
        CLRB    {g(0xB05F)}          ; rank: novice
        CLRB    {g(0xAA06)}          ; P1 human (keyboard)
        CLRB    {g(0xAA08)}
        CLRB    {g(0xAA48)}
        MOVB    #1,{g(0xAF35)}       ; opponent set-up pending
        MOVB    #1,{g(0xAA46)}       ; P2 = the computer
        MOVB    #2,{g(0xAA3C)}       ; two rounds per opponent
        MOVB    #2,{g(0xAF34)}       ; opens on background 2
        CLRB    {g(0xB02D)}          ; $AF0B: score 000000 (both BCD buffers)
        CLRB    {g(0xB02D)}+1.
        CLRB    {g(0xB02D)}+2.
        CLRB    {g(0xB02D)}+3.
        CLRB    {g(0xB02D)}+4.
        CLRB    {g(0xB02D)}+5.
        RTS     PC
        ; --- DINIT: $AB70 Demo - both fighters computer-controlled with random
        ;     personalities 7..10, rank 0, background 2; "DEMO" on the strip. ----
DINIT:  JSR     PC,GINIT
        MOV     #1,DEMO
        MOVB    #1,{g(0xAA06)}       ; P1 is the computer too ($AB90)
        JSR     PC,ARNG              ; $AB77: P1's AI personality $AA94 = rnd & 3 + 7
        BIC     #177774,R0
        ADD     #7,R0
        MOVB    R0,{g(0xAA94)}
        JSR     PC,ARNG              ; $AB81: P2's $AA80 likewise
        BIC     #177774,R0
        ADD     #7,R0
        MOVB    R0,{g(0xAA80)}
        RTS     PC
{rendbg}        ; --- HUD: the yin-yang score in a top status strip (rows 0-15).  Each fighter
        ;     has two yin-yang slots that fill half then full as its score (SC1/SC2,
        ;     stashed from $AA01/$AA41) climbs 0..4.  The real 2x2-UDG symbol
        ;     (YYFULL/YYHALF, from the snapshot) is drawn white over the cleared strip.
DRAW1U: MOV     #8.,R3               ; one UDG cell: 8 pixel rows of (R1)+ pixels,
1$:     {cell_restore}
        MOVB    (R1)+,R4             ; then the glyph, transparent over the clean cell
        BIC     #177400,R4
        BEQ     2$                   ; blank glyph row -> the clean cell stays
        BISB    R4,(R0)              ; OR the symbol pixels onto the background cell
        {ovl_ink}
2$:     ADD     #80.,R0
        DEC     R3
        BNE     1$
        RTS     PC
DRAWYY: MOV     R2,R0                ; draw the 2x2 symbol (R1 = 32-byte block) at cell R2
        JSR     PC,DRAW1U            ; top-left
        MOV     R2,R0
        ADD     #2,R0
        JSR     PC,DRAW1U            ; top-right
        MOV     R2,R0
        ADD     #640.,R0             ; +8 rows
        JSR     PC,DRAW1U            ; bottom-left
        MOV     R2,R0
        ADD     #642.,R0
        JSR     PC,DRAW1U            ; bottom-right
        RTS     PC
DYYSL:  MOV     #YYNONE,R1           ; R4 = level (0 none / 1 half / >=2 full), R2 = pos
        TST     R4
        BEQ     8$                   ; empty -> the blank symbol (restores the dojo)
        MOV     #YYHALF,R1           ; half point
        CMP     R4,#2.
        BLO     8$
        MOV     #YYFULL,R1           ; full point
8$:     JSR     PC,DRAWYY
        RTS     PC
        ; No strip clear (that black-flash caused the flicker); each of the four
        ; slots is always drawn, in the top corners INSIDE the dojo (row 6, over the
        ; sky, above the fighters' redraw band), so drawing straight to VRAM is stable.
HUD:    MOVB    SC1,R4               ; P1 slot 0 = min(2, score)
        BIC     #177400,R4
        CMP     R4,#2.
        BLE     2$
        MOV     #2.,R4
2$:     MOV     #VRAM+496.,R2        ; P1 first yin-yang: Spectrum col 4 (the inner one)
        JSR     PC,DYYSL
        MOVB    SC1,R4               ; P1 slot 1 = score - 2
        BIC     #177400,R4
        SUB     #2.,R4
        BGT     3$
        CLR     R4
3$:     MOV     #VRAM+490.,R2        ; P1 second yin-yang: Spectrum col 1 (fills 3rd/4th)
        JSR     PC,DYYSL
        MOVB    SC2,R4               ; P2 slot 0
        BIC     #177400,R4
        CMP     R4,#2.
        BLE     5$
        MOV     #2.,R4
5$:     MOV     #VRAM+540.,R2        ; row 6, col 30 (top-right)
        JSR     PC,DYYSL
        MOVB    SC2,R4               ; P2 slot 1
        BIC     #177400,R4
        SUB     #2.,R4
        BGT     6$
        CLR     R4
6$:     MOV     #VRAM+546.,R2        ; row 6, col 33
        JSR     PC,DYYSL
        RTS     PC
        ; --- DRWDIG: draw digit R4 (0..9) as an 8x8 glyph at VRAM cell R2. ---------
DRWDIG: MOV     R4,R1
        ASL     R1
        ASL     R1
        ASL     R1                   ; code * 8
        ADD     #DIGFNT,R1
        MOV     R2,R0
        JSR     PC,DRAW1U
        RTS     PC
        ; --- DRWSTR: draw the font-code string at R1 (ending 377) at VRAM cell R2. ---
DRWSTR: MOVB    (R1)+,R4
        BIC     #177400,R4           ; zero-extend the code
        CMP     R4,#255.
        BEQ     9$                   ; 377 terminator
        MOV     R1,-(SP)             ; DRWDIG clobbers R1 (glyph ptr)
        JSR     PC,DRWDIG
        MOV     (SP)+,R1
        ADD     #2,R2                ; next cell
        BR      DRWSTR
9$:     RTS     PC
        ; --- DRWSCR: draw P1's six-digit BCD score (SCRBCD, stashed from $B02D) across
        ;     the top strip (row 6, centre - clear of the corner yin-yang symbols).
        ;     Own digit font: the original's status text uses the Spectrum ROM font,
        ;     which is not ours to reproduce. ----------------------------------------
DRWSCR: MOV     #VRAM+520.,R2        ; row 6, cells 20-25 (centre)
        MOV     #2.,R5               ; BCD byte index 2..0 (most significant first)
1$:     MOVB    SCRBCD(R5),R0        ; a packed BCD byte = two digits
        BIC     #177400,R0
        MOV     R0,R4
        ASR     R4                   ; high nibble (R0 is 0..255, so ASR fills zeros)
        ASR     R4
        ASR     R4
        ASR     R4
        JSR     PC,DRWDIG
        ADD     #2,R2                ; next character cell
        MOVB    SCRBCD(R5),R4
        BIC     #177760,R4           ; low nibble
        JSR     PC,DRWDIG
        ADD     #2,R2
        DEC     R5
        BGE     1$
        RTS     PC
        ; --- BIN2: split R0 (0..99) into R3 = tens, R0 = ones (no EIS DIV on core). -
BIN2:   CLR     R3
2$:     CMP     R0,#10.
        BLO     3$
        SUB     #10.,R0
        INC     R3
        BR      2$
3$:     RTS     PC
        ; --- DRWTIM: draw the round timer (STIM, stashed from $9CA5) as two digits
        ;     in the top strip, to the right of the score. --------------------------
DRWTIM: MOVB    STIM,R0
        BIC     #177400,R0
        JSR     PC,BIN2              ; R3 = tens, R0 = ones
        MOV     R0,-(SP)             ; DRWDIG (via DRAW1U) clobbers R0 - save the ones
        MOV     #VRAM+534.,R2        ; row 6, cells 27-28 (between score and P2 yin-yang)
        MOV     R3,R4
        JSR     PC,DRWDIG            ; tens digit
        ADD     #2,R2
        MOV     (SP)+,R4             ; ones digit
        JSR     PC,DRWDIG
        RTS     PC
        ; --- DRWRNK: the rank text ($AEBF): "NOVICE" at 0, else "<n>ST|ND|RD|TH DAN"
        ;     from RANKB (the BCD $B05F), built as font codes in RANKS and padded
        ;     to 9 cells so a shorter rank overwrites a longer one. ----------------
DRWRNK: MOVB    RANKB,R0
        BIC     #177400,R0
        CMP     R0,#377
        BNE     11$
        MOV     #DEMSTR,R1           ; the attract demo
        BR      8$
11$:    TST     R0
        BNE     1$
        MOV     #NOVSTR,R1
        BR      8$
1$:     MOV     #RANKS,R1
        MOV     R0,R2                ; tens digit (BCD high nibble), if any
        ASR     R2
        ASR     R2
        ASR     R2
        ASR     R2
        BEQ     2$
        MOVB    R2,(R1)+
2$:     MOV     R0,R2                ; units digit
        BIC     #177760,R2
        MOVB    R2,(R1)+
        BIC     #177740,R0           ; $AEC5: rank & $1F picks the suffix
        MOV     #SFXTH,R2
        CMP     R0,#1
        BNE     3$
        MOV     #SFXST,R2
3$:     CMP     R0,#2
        BNE     4$
        MOV     #SFXND,R2
4$:     CMP     R0,#3
        BNE     5$
        MOV     #SFXRD,R2
5$:     MOVB    (R2)+,(R1)+          ; the two suffix letters
        MOVB    (R2)+,(R1)+
        MOVB    #10.,(R1)+           ; " DAN"
        MOVB    #{_fc('D')}.,(R1)+
        MOVB    #{_fc('A')}.,(R1)+
        MOVB    #{_fc('N')}.,(R1)+
        MOVB    #10.,(R1)+           ; pad (a 7-cell rank covers an 8-cell one)
        MOVB    #377,(R1)+
        MOV     #RANKS,R1
8$:     MOV     #VRAM+502.,R2        ; row 6, cells 11-19 (right of the P1 yin-yang)
        JSR     PC,DRWSTR
        RTS     PC
        .EVEN
{_strb("NOVSTR", "NOVICE   ")}
{_strb("DEMSTR", "DEMO     ")}
{_strb("SFXST", "ST")}
{_strb("SFXND", "ND")}
{_strb("SFXRD", "RD")}
{_strb("SFXTH", "TH")}
        .EVEN
DIGFNT:                              ; codes 0-9 digits, 10 space, 11-36 A-Z
{font_s}
        .EVEN
YYFULL:
{yyfull_s}
YYHALF:
{yyhalf_s}
YYNONE: .BLKB   32.                  ; the blank symbol: restores the dojo cells
        ; --- SNDFX: $B15A, the six beeper effects, blocking as in the original.
        ;     The Spectrum bit-bangs its beeper: an effect is a run of half-periods
        ;     whose lengths come from ROM bytes masked to $7F/$3F/$FF (B x 29 T-
        ;     states each) - i.e. NOISE of a given grain - except code 4, a low
        ;     rumble of 9-bit random half-periods (26/30/38 T per unit, $B19D), and
        ;     code 5 = two deep bursts around a 61 ms pause ($B2F0).  Here the
        ;     speaker is reg C bit 5 (direct drive, tech desc 4.8) with bit 6 on
        ;     and the timer gate (7) off; an LFSR replaces the ROM bytes; one
        ;     Z80 T-state = 7.5/3.5 CPU cycles, calibrated into the delay loops.
        ;     in: R0 = code 1..6 (0 / other = no-op). -------------------------------
SNDFX:  TST     R0
        BEQ     9$
        CMP     R0,#6.
        BHI     9$
        ASL     R0
        JMP     @SNDTAB-2(R0)
9$:     RTS     PC
SNDTAB: .WORD   SND1,SND2,SND3,SND4,SND5,SND6
SND1:   MOV     #177,R1              ; $B179: mask $7F, 1014 half-periods (~540 ms)
        MOV     #1014.,R2
        BR      NOISE
SND2:   MOV     #77,R1               ; $B185: mask $3F, 758 half-periods (~200 ms)
        MOV     #758.,R2
        BR      NOISE
SND3:   MOV     #77,R1               ; $B191: mask $3F, 64 half-periods (~17 ms)
        MOV     #64.,R2
        BR      NOISE
SND6:   MOV     #77,R1               ; $B221: mask $3F, 128 half-periods (~34 ms)
        MOV     #128.,R2
        BR      NOISE
SND5:   MOV     #377,R1              ; $B1FB: mask $FF, 64 half-periods (~68 ms),
        MOV     #64.,R2
        JSR     PC,NOISE
        MOV     #61.,R3              ;   a 61 ms pause ($B2F0: 8192 x 26 T),
51$:    MOV     #800.,R4
52$:    SOB     R4,52$
        SOB     R3,51$
        MOV     #377,R1              ;   and again 64 deep half-periods
        MOV     #64.,R2
        BR      NOISE
        ; NOISE: R1 = mask, R2 = half-periods.  Each: B = rnd & mask (0 -> 1),
        ; toggle the speaker, wait B x 29 T-states.
NOISE:  JSR     PC,SNDON
        COM     R1                   ; R1 = ~mask for BIC
1$:     JSR     PC,SRND              ; R0 = random byte
        BIC     R1,R0
        BNE     2$
        INC     R0
2$:     JSR     PC,SPKTOG
        MOV     R0,R4                ; delay 6.5 x B loop turns (~62 cycles per B
        ASL     R4                   ;   = 29 T-states; calibrated: a SOB turn is
        ASL     R4                   ;   ~9.3 cycles, the per-half overhead ~340)
        ADD     R0,R4
        ADD     R0,R4
        MOV     R0,R5
        ASR     R5
        ADD     R5,R4
3$:     SOB     R4,3$
        DEC     R2
        BNE     1$
        JMP     SNDOFF
        ; SND4 ($B19D): three rumble segments of D x E cycles, half-periods of
        ; BC x (26 / 30 / 38) T where C = rnd | rnd and B = C & 1 (the original's
        ; $B22D / $B244 loops with 0 / 1 / 3 NOPs of padding).
SND4:   JSR     PC,SNDON
        MOV     #3.,R1               ; R1 = inner turns per BC unit: 3 / 4 / 5 ~=
        MOV     #3,R3                ;   26 / 30 / 38 T (measured ~60 / 70 / 82 cycles)
        JSR     PC,RSEG
        MOV     #4.,R1
        MOV     #2,R3
        JSR     PC,RSEG
        MOV     #5.,R1
        MOV     #2,R3
        JSR     PC,RSEG
        JMP     SNDOFF
RSEG:   MOV     #8.,R2               ; E = 8 on/off cycles per D
1$:     JSR     PC,RHALF
        JSR     PC,RHALF
        DEC     R2
        BNE     1$
        DEC     R3
        BNE     RSEG
        RTS     PC
RHALF:  JSR     PC,SRND              ; C = rnd | rnd  (the $B2D7 shift register)
        MOV     R0,-(SP)
        JSR     PC,SRND
        BIS     (SP)+,R0
        JSR     PC,SPKTOG            ; (clobbers R4/R5, keeps R0)
        MOV     R0,R4                ; BC = C + (C & 1) << 8
        BIT     #1,R0
        BEQ     1$
        ADD     #256.,R4
1$:     INC     R4                   ; (a zero count ran the Z80 loop 65536 times: never
2$:     MOV     R1,R5                ;  seen with the OR'd bytes; keep the count >= 1)
3$:     SOB     R5,3$                ; delay BC x R1 loop turns
        SOB     R4,2$
        RTS     PC
        ; speaker control via the reg C shadow: SNDON = bit 6 on, 5 and 7 off;
        ; SPKTOG flips bit 5; SNDOFF = bits 5-7 off (silence).
SNDON:  MOVB    RCSHAD,R4
        BIC     #177640,R4
        BIS     #100,R4
        MOVB    R4,RCSHAD
        MOVB    R4,@#SYSC
        RTS     PC
SPKTOG: MOVB    RCSHAD,R4
        BIC     #177400,R4
        MOV     #40,R5
        XOR     R5,R4
        MOVB    R4,RCSHAD
        MOVB    R4,@#SYSC
        RTS     PC
SNDOFF: MOVB    RCSHAD,R4
        BIC     #177740,R4
        MOVB    R4,RCSHAD
        MOVB    R4,@#SYSC
        RTS     PC
        ; SRND: 16-bit Galois LFSR (own seed - the ROM-byte stream of the original
        ; is independent of the game's RNG); R0 = low byte 0..255.  Clobbers R5.
SRND:   MOV     SSEED,R0
        CLC
        ROR     R0
        BCC     1$
        MOV     #132000,R5
        XOR     R5,R0
1$:     MOV     R0,SSEED
        BIC     #177400,R0
        RTS     PC
        .EVEN
        ; --- KSCAN: drain the MS7004 keyboard into the control state -----------
        ; The original reads 9 definable keys (8 directions + fire) or a Kempston
        ; joystick.  The MS7004 sends make codes only: no release codes, auto-
        ; repeat for the LAST regular key, modifiers emit their own code on every
        ; press and ALL-UP once everything is released.  So each control (UP,
        ; DOWN, LEFT, RIGHT, FIRE) has a hold timer KT..: a key's make/repeat code
        ; sets its timer to KTMR frames, and - the CHORD rule - refreshes every
        ; other timer still running: keys pressed together stay "held" as long
        ; as any one of them repeats (the keyboard only repeats the last one).
        ; Arrows and Space/VR/SU give chords (up+right, right+fire ...); the
        ; keypad 1-9 gives a diagonal in one key.  A key released less than KTMR
        ; before the next press is read as part of the chord - the price of a
        ; keyboard without release codes.
        ; The UART presents one byte per ~2 ms (4800 baud) from a 16-byte FIFO, so
        ; after a byte, poll ~3 ms for the next one so the queue drains each frame.
KSCAN:  CLR     R2                   ; poll budget: none until a byte was read
KS0:    MOVB    @#177442,R0          ; keyboard status
        BITB    #2,R0                ; RXRDY (byte available)?
        BNE     KS1
        TST     R2
        BEQ     6$                   ; nothing read this frame -> done
        DEC     R2                   ; a byte was read: the next may still be
        BR      KS0                  ;   shifting in - keep polling a little
6$:     RTS     PC
KS1:    MOVB    @#177440,R0          ; read the scancode
        MOV     #250.,R2             ; ~3 ms of polling for a follow-up byte
        BIC     #177400,R0
        CMP     R0,#263              ; ALL-UP (only sent with a modifier) -> release all
        BNE     1$
        CLR     KTUP
        CLR     KTDN
        CLR     KTLF
        CLR     KTRT
        CLR     KTFR
        CLR     KTG
        CLR     KTH
        BR      KS0
1$:     JSR     PC,KREFR             ; any key event: refresh the running timers
        CMP     R0,#254              ; auto-repeat code (real MS7004): that is all
        BEQ     KS0
        CMP     R0,#324              ; fire: Space, VR/Shift (0256), SU/Ctrl (0257)
        BEQ     3$
        CMP     R0,#256
        BEQ     3$
        CMP     R0,#257
        BEQ     3$
        CMP     R0,#300              ; "1": start a 1-player game from the demo
        BNE     15$
        MOV     #1,KSTART
        BR      KS0
15$:    CMP     R0,#341              ; "G" / "H": held together they quit the game
        BNE     16$
        MOV     #{ktmout}.,KTG
        BR      KS0
16$:    CMP     R0,#366
        BNE     14$
        MOV     #{ktmout}.,KTH
        BR      KS0
14$:    MOV     R0,R1                ; directions: DIRTAB[scancode - 0226] = bits
        SUB     #226,R1
        BLT     KS0
        CMP     R1,#20.
        BHI     KS0
        MOVB    DIRTAB(R1),R1
        BEQ     KS0                  ; not a direction key
        BIT     #1,R1
        BEQ     11$
        MOV     #{ktmout}.,KTUP
11$:    BIT     #2,R1
        BEQ     12$
        MOV     #{ktmout}.,KTDN
12$:    BIT     #4,R1
        BEQ     13$
        MOV     #{ktmout}.,KTLF
13$:    BIT     #10,R1
        BEQ     KS0
        MOV     #{ktmout}.,KTRT
        BR      KS0
3$:     MOV     #{ktmout}.,KTFR
        BR      KS0
        ; KREFR: the chord rule - every timer still running gets the full hold
KREFR:  TST     KTUP
        BEQ     1$
        MOV     #{ktmout}.,KTUP
1$:     TST     KTDN
        BEQ     2$
        MOV     #{ktmout}.,KTDN
2$:     TST     KTLF
        BEQ     3$
        MOV     #{ktmout}.,KTLF
3$:     TST     KTRT
        BEQ     4$
        MOV     #{ktmout}.,KTRT
4$:     TST     KTFR
        BEQ     5$
        MOV     #{ktmout}.,KTFR
5$:     TST     KTG
        BEQ     6$
        MOV     #{ktmout}.,KTG
6$:     TST     KTH
        BEQ     7$
        MOV     #{ktmout}.,KTH
7$:     RTS     PC
        ; --- KCTRL: the hold timers -> the original's control bits (the $8B4x key
        ;     scan: bit0 UP, bit1 DOWN, bit2 LEFT, bit3 RIGHT, bit4 FIRE), each
        ;     timer counting down one per frame.  The $98DD table then resolves
        ;     the bits by facing into the 16 moves. ------------------------------
KCTRL:  CLR     R0
        TST     KTUP
        BEQ     1$
        DEC     KTUP
        BIS     #1,R0
1$:     TST     KTDN
        BEQ     2$
        DEC     KTDN
        BIS     #2,R0
2$:     TST     KTLF
        BEQ     3$
        DEC     KTLF
        BIS     #4,R0
3$:     TST     KTRT
        BEQ     4$
        DEC     KTRT
        BIS     #10,R0
4$:     TST     KTFR
        BEQ     9$
        DEC     KTFR
        BIS     #20,R0
9$:     RTS     PC
        ; 0226 KP1 DN+LF, 0227 KP2 DN, 0230 KP3 DN+RT, 0231 KP4 LF, 0232 KP5 -,
        ; 0233 KP6 RT, 0234 -, 0235 KP7 UP+LF, 0236 KP8 UP, 0237 KP9 UP+RT,
        ; 0240-0246 -, 0247 LEFT, 0250 RIGHT, 0251 DOWN, 0252 UP
DIRTAB: .BYTE   6.,2.,10.,4.,0.,8.,0.,5.,1.,9.,0.,0.,0.,0.,0.,0.,0.,4.,8.,2.,1.
        .EVEN
        ; --- C98A0: control (R0) -> &move ($98DD table; +0x21 if P1 is mid-move) ------
C98A0:  BIT     #40,R0
        BEQ     1$
        BIS     #20,R0
1$:     BIC     #177740,R0           ; keep 5 control bits
        MOV     #MTAB,R1
        ADD     R0,R1
        MOVB    {g(0xAA17)},R0       ; P1 current pose/move
        BIC     #177400,R0
        BEQ     2$
        ADD     #41,R1               ; mid-move uses the second 0x21-offset table
2$:     MOV     R1,R0
        RTS     PC
        .EVEN
{mtab}"""
    # PRESENT reads the composed buffer; the game feeds it the low-RAM copy LOWBUF
    # (the original FBUF in bank 6 is shadowed by RMON after the unpark).  Drop the
    # routine's own 16 KB VRAM-clear (VRAM was already cleared at render start, and
    # the clear is what the parked present faulted in).
    # The present is now an inline loop in the driver (LOWBUF -> VRAM at 03377);
    # fighter_mac's PRESENT routine is not used here.
    decrun = (fm.emit_decrun()
              .replace("MOV     #FCTRL,SRCP\n        "
                       "MOV     #FBUF+%DEOFF%.,DSTP\n        ", "")
              .replace("ADD     #C408V,R0", "ADD     C408W,R0"))
    tail = (fm.TAIL
            .replace("C40EM:  .BYTE   %C40E%.                ; per-fighter mode flags ($C40E)\n", "")
            .replace("C407M:  .BYTE   %C407%.                ; facing flag ($C407)\n", "")
            .replace("ORIGDP: .WORD   0\n", "").replace("ORIGRC: .WORD   0\n", ""))
    logic = emit_fullframe(randoms)
    # Live RNG for the loop: replace the recorded-replay ARNG with a 16-bit Galois
    # LFSR masked to 0..127 (the Z80 R register's range, so the AI's >=$80 branches
    # stay dead) - so the AI decides fresh each frame instead of replaying 2 bytes.
    logic = logic.replace(
        "ARNG:   MOV     ARNDI,R0\n"
        "        INC     ARNDI\n"
        "        MOVB    ARND(R0),R0\n"
        "        BIC     #177400,R0\n"
        "        RTS     PC\n",
        "ARNG:   MOV     RSEED,R0\n"
        "        CLC\n"
        "        ROR     R0\n"
        "        BCC     91$\n"
        "        MOV     R1,-(SP)\n"
        "        MOV     #132000,R1\n"
        "        XOR     R1,R0\n"
        "        MOV     (SP)+,R1\n"
        "91$:    MOV     R0,RSEED\n"
        "        BIC     #177600,R0\n"
        "        RTS     PC\n")
    chain = emit_setupchain() + emit_c101c1a2()
    ldat = ("\n        .EVEN\n" + _emit_window("LDAT", snap[ldat_base:ldat_end]))
    datblk = ("\n        .EVEN\nDATFIL: .RAD50  /DK GST   DAT/\n"
              "        .EVEN\nLKAREA: .BLKW   5\n"
              "        .EVEN\nC408W:  .WORD   0\nORIGRC: .WORD   0\n"
              "        .EVEN\nRSEED:  .WORD   1\n"
              "        .EVEN\nRW1:    .WORD   0\nRT1:    .WORD   0\nRL1:    .WORD   0\n"
              "RW2:    .WORD   0\nRT2:    .WORD   0\nRL2:    .WORD   0\n"
              "COL1:   .WORD   0\nTOP1:   .WORD   0\nBWID1:  .WORD   0\nW1:     .WORD   0\n"
              "COL2:   .WORD   0\nTOP2:   .WORD   0\nBWID2:  .WORD   0\nW2:     .WORD   0\n"
              "SRC1:   .WORD   0\nSRC2:   .WORD   0\nROWN:   .WORD   0\n"
              "        .EVEN\nRCSHAD: .WORD   0\nSSEED:  .WORD   52525\n"
              "        .EVEN\nLASTTP: .WORD   0\nKTUP:   .WORD   0\nKTDN:   .WORD   0\nKTLF:   .WORD   0\nKTRT:   .WORD   0\nKTFR:   .WORD   0\nKTG:    .WORD   0\nKTH:    .WORD   0\nKSTART: .WORD   0\nDEMO:   .WORD   0\n"
              "        .EVEN\nRESULT: .WORD   0\nSC1:    .WORD   0\nSC2:    .WORD   0\n"
              "        .EVEN\nWINTMR: .WORD   0\nRPHASE: .WORD   0\nRANKB:  .WORD   0\nRANKS:  .BLKB   10.\n"
              "        .EVEN\nKEY1:   .BLKB   12.\nKEY2:   .BLKB   12.\n"
              "        .EVEN\nCKEY:   .BLKB   6.\nSLOT:   .WORD   0\n"
              "        .EVEN\nHUDDRT: .WORD   1\nHUDK:   .BLKB   8.\n"
              "        .EVEN\nSCRBCD: .BLKB   3.\n        .EVEN\nSTIM:   .WORD   0\n"
                            f"        .EVEN\nLBUF1: .BLKW  {lb_words}.    ; per-fighter compose copies (one fighter each)\n"
              f"LBUF2: .BLKW  {lb_words}.\n" + bgvars)
    body = (preamble + equs + driver + decrun
            + logic + chain + tail + datblk + ldat + srows_src)
    # FIST_SYMTAB=1: append a self-describing symbol table (marker words, count,
    # then every global label's address) so a profiler can map sampled PCs to
    # routines without a LINK map; the names go to symtab.json in the same order.
    symtab = ""
    if os.environ.get("FIST_SYMTAB"):
        import re, json
        names = list(dict.fromkeys(re.findall(r"^([A-Z][A-Z0-9.$]*):", body + bgsrc, re.M)))
        symtab = ("\n        .EVEN\nSYMTAB: .WORD   125252,52525," + f"{len(names)}.\n"
                  + "".join(f"        .WORD   {','.join(names[i:i + 8])}\n"
                            for i in range(0, len(names), 8)))
        (OUT_MAC.parent / "symtab.json").write_text(json.dumps(names))
    # (the symbol table goes behind the dojo block: it is only read from the
    #  .SAV file, and banks 0-1 have no room to spare)
    src = body + bgsrc + symtab + bgdat_src + "\n        .END    START\n"
    src = (src.replace("%NELEM%", str(nelem))
              .replace("%C408%", str(snap[0xC408]))
              .replace("%C40E%", str(snap[0xC40E])).replace("%C407%", str(snap[0xC407]))
              .replace("%FWID%", str(fwid)).replace("%FHGT%", str(fhgt))
              .replace("%DSTOFF%", str((top * 40 + left) * 2)))
    src.encode("ascii")
    OUT_MAC.write_text(src, encoding="ascii", newline="\r\n")
    print(f"gamelogic_mac: wrote {OUT_MAC} + GST.DAT (STANDALONE GAME: load GST.DAT "
          f"-> extended banks, one $9745 frame + draw both fighters, {fwid}x{fhgt})")


# Demo: a RUNNABLE fighter-present image (boots under RT-11, no oracle).  The
# full GST (to $FAA4) overlaps RMON ($140054); so trim it to a LOW pose's data
# extent (fits banks 4-5, below RMON) and relocate the compose buffer to low
# memory (a plain .BLKB, NOT the GST $F730 cell).  PRESENT centres the fighter.
DEMO_END = 0xDB00                        # GST data extent: 0100000..0136600 < RMON


def main_demo():
    """Build a runnable single-fighter present demo (FIST_GL=demo).  Chain-mode
    decode of a captured LOW pose, GST trimmed below RMON, compose buffer FBUF
    relocated to low RAM (initialised from the captured background FINIT).  Runs
    under RT-11 (RUN FIST) and centres the fighter on the 320x200 screen."""
    import fighter_mac as fm
    import fighter_data as fd
    import setup_ref as sr
    fm.STAGE_LEVEL = 1
    nelem = int(os.environ.get("FGHT_NELEM", "5000"))
    snap, b_in, c_in, pose = sr.capture_c34f_low(0xD400)
    mm = bytearray(snap)
    mm[fd.FBUF:fd.FBUF + fd.FBUF_LEN] = bytes(fd.FBUF_LEN)  # black background (no bg-fill yet)
    sr.draw_fighter(mm, pose, b_in, c_in)
    fexp = bytes(mm[fd.FBUF:fd.FBUF + fd.FBUF_LEN])
    EXP_BIN.write_bytes(fexp)
    WIN_JSON.write_text(json.dumps({"base": fd.FBUF, "size": fd.FBUF_LEN}))

    preamble = ("        .TITLE  FIST\nDPRAM  = 157700\nDISPAT = 177400\n"
                "SYSC   = 177604\nVRAM   = 40000\nKBST   = 177442\n")
    equs = "\n"                                            # FBUF is LOW here, not a GST EQU
    for t in fd.TABLES:
        equs += f"T{t:04X}  = GST+{t - GBASE}.\n"
    equs += f"C40EM  = GST+{0xC40E - GBASE}.\n"
    equs += f"C407M  = GST+{0xC407 - GBASE}.\n"
    equs += "WB1C   = W+60.\n"
    gst = ("\n        .ASECT\n        . = 100000\n"
           + _emit_window("GST", snap[GBASE:DEMO_END]) + "        .EVEN\n")
    fwid, fhgt = mm[0xC40A], mm[0xC409]
    top, left = (200 - fhgt) // 2, (40 - fwid) // 2
    driver = f"""
        .ASECT
        . = 1000
        .EVEN
START:  MOV     #340,R0
        MTPS    R0
        MOV     @#DISPAT,ORIGDP
        MOVB    @#SYSC,ORIGRC
        MOVB    @#SYSC,R0
        BIC     #17,R0
        MOVB    R0,@#SYSC
        MOV     #3377,@#DPRAM
        MOV     #3377,@#DISPAT
        MOV     #W,R0                  ; zero the work area
        MOV     #32.,R1
8$:     CLR     (R0)+
        DEC     R1
        BNE     8$
        MOV     #FBUF,R0              ; FBUF := black (clean background)
        MOV     #442.,R1
7$:     CLR     (R0)+
        DEC     R1
        BNE     7$
        MOV     #{b_in}.,R3
        MOV     #{c_in}.,R4
        JSR     PC,SETUPC
9$:     JSR     PC,SEGSET
        MOVB    {g(0xC408)},R0
        BIC     #177400,R0
        MOV     R0,C408W
        JSR     PC,DECRUN
        DECB    SEGCNT
        BNE     9$
        JSR     PC,%FINISH%
WKEY:   MOV     @#KBST,R0
        BIT     #2,R0
        BEQ     WKEY
        MOV     ORIGDP,@#DPRAM
        MOV     ORIGDP,@#DISPAT
        MOVB    ORIGRC,@#SYSC
        EMT     350
"""
    tovram_present = fm.HEADER[fm.HEADER.index("TOVRAM:"):]   # TOVRAM + FWHITE + PRESENT
    decrun = (fm.emit_decrun()
              .replace("MOV     #FCTRL,SRCP\n        "
                       "MOV     #FBUF+%DEOFF%.,DSTP\n        ", "")
              .replace("ADD     #C408V,R0", "ADD     C408W,R0"))
    tail = (fm.TAIL
            .replace("C40EM:  .BYTE   %C40E%.                ; per-fighter mode flags ($C40E)\n", "")
            .replace("C407M:  .BYTE   %C407%.                ; facing flag ($C407)\n", ""))
    finish = "PRESENT" if os.environ.get("FGHT_PRESENT") else "TOVRAM"
    src_txt = (preamble + gst + equs + driver + tovram_present + decrun
               + emit_setupchain() + tail
               + "\n        .EVEN\nC408W:  .WORD   0\n"
               + "FBUF:   .BLKB   884.\n        .EVEN\n        .END    START\n")
    src_txt = (src_txt.replace("%C408%", str(snap[0xC408]))
               .replace("%NELEM%", str(nelem)).replace("%FINISH%", finish)
               .replace("%FWID%", str(fwid)).replace("%FHGT%", str(fhgt))
               .replace("%DSTOFF%", str((top * 40 + left) * 2)))
    src_txt.encode("ascii")
    OUT_MAC.write_text(src_txt, encoding="ascii", newline="\r\n")
    print(f"gamelogic_mac: wrote {OUT_MAC} (DEMO: runnable present, pose ${pose:04X} "
          f"B={snap[pose]} c40e={snap[0xC40E]:#x}, {fwid}x{fhgt})")


def main_demo_bg():
    """Runnable demo with the DOJO BACKGROUND (FIST_GL=demobg).  Reuses the
    ported background engine (gen_fist) - renders the dojo into SCRBUF and
    presents it 1:1+coloured via SPSCR - then decodes the fighter and overlays
    it (masked: keep the dojo where the fighter is blank).  The compose buffer
    overlaps SCRBUF (free once the dojo is in VRAM), so it all fits the 15.5 KB
    low RAM below the VRAM window; the trimmed fighter GST sits in banks 4-5."""
    import fighter_mac as fm
    import fighter_data as fd
    import setup_ref as sr
    import gen_fist
    from bg_data import BackgroundData
    fm.STAGE_LEVEL = 1
    nelem = int(os.environ.get("FGHT_NELEM", "5000"))
    bgn = int(os.environ.get("FGHT_BG", "1"))
    snap, b_in, c_in, pose = sr.capture_c34f_low(0xD400)
    fwid, fhgt = snap[0xC40A], snap[0xC409]
    # Real screen position the bridge/geometry computed (bbox $C434/$C436), not
    # centred: TMARG=4 lines + bbox line; LMARG=4 words + bbox column ($C434>>2).
    top = 4 + snap[0xC436]
    left = 4 + (snap[0xC434] >> 2)
    dstoff = (top * 40 + left) * 2

    stage = f"""        JSR     PC,CHGBG               ; render the dojo into SCRBUF
        JSR     PC,SPSCR               ; present it 1:1 + coloured to VRAM
        MOV     #SCRBUF,R0             ; FBUF (=SCRBUF) := black for the fighter
        MOV     #442.,R1
50$:    CLR     (R0)+
        DEC     R1
        BNE     50$
        MOV     #W,R0
        MOV     #32.,R1
51$:    CLR     (R0)+
        DEC     R1
        BNE     51$
        MOV     #{b_in}.,R3
        MOV     #{c_in}.,R4
        JSR     PC,SETUPC
52$:    JSR     PC,SEGSET
        MOVB    {g(0xC408)},R0
        BIC     #177400,R0
        MOV     R0,C408W
        JSR     PC,DECRUN
        DECB    SEGCNT
        BNE     52$
        JSR     PC,OVLAY"""
    program = (gen_fist.PROGRAM.replace("%STAGE%", stage)
               .replace("%BGDEF%", f"BG{bgn}DEF").replace("%BGN%", str(bgn))
               # absolute code at 1000 (match the GST .ASECT, so the .SAV load map
               # carries both the low code and the high-bank GST).
               .replace("\n        .EVEN\nSTART:",
                        "\n        .ASECT\n        . = 1000\n        .EVEN\nSTART:"))
    rows = gen_fist.spectrum_row_offsets()
    bg = BackgroundData(bgn)

    equs = "\nFWHITE = 043400\n"
    for t in fd.TABLES:
        equs += f"T{t:04X}  = GST+{t - GBASE}.\n"
    equs += f"C40EM  = GST+{0xC40E - GBASE}.\n"
    equs += f"C407M  = GST+{0xC407 - GBASE}.\n"
    equs += "FBUF   = SCRBUF\n"            # overlap: SCRBUF is free once the dojo is presented
    equs += "WB1C   = W+60.\n"
    ovlay = f"""
;-------------------------------------------------------------------
; OVLAY - overlay the composed fighter (FBUF) onto VRAM, masked: only the
; non-zero (set) bytes are written white, so the dojo shows through.
OVLAY:  MOV     #FBUF,R1
        MOV     #{fhgt}.,R5
        MOV     #VRAM+{dstoff}.,R2
1$:     MOV     R2,R0
        MOV     #{fwid}.,R4
2$:     MOVB    (R1)+,R3
        BIC     #177400,R3
        BEQ     3$
        BISB    R3,(R0)               ; OR the fighter pixels in - transparent
3$:     ADD     #2,R0
        DEC     R4
        BNE     2$
        ADD     #120,R2
        DEC     R5
        BNE     1$
        RTS     PC
"""
    decrun = (fm.emit_decrun()
              .replace("MOV     #FCTRL,SRCP\n        "
                       "MOV     #FBUF+%DEOFF%.,DSTP\n        ", "")
              .replace("ADD     #C408V,R0", "ADD     C408W,R0"))
    # PROGRAM already defines ORIGDP/ORIGRC - strip them from the fighter tail.
    tail = (fm.TAIL
            .replace("ORIGDP: .WORD   0\n", "").replace("ORIGRC: .WORD   0\n", "")
            .replace("C40EM:  .BYTE   %C40E%.                ; per-fighter mode flags ($C40E)\n", "")
            .replace("C407M:  .BYTE   %C407%.                ; facing flag ($C407)\n", ""))
    gst = ("\n        .ASECT\n        . = 100000\n"
           + _emit_window("GST", snap[GBASE:DEMO_END]) + "        .EVEN\n")
    src = (program
           + f"\n;------ background {bgn} data ------\n" + bg.emit()
           + "\n        .EVEN\n" + gen_fist._emit_words("SROWS", rows)
           + "\n        .EVEN\nSCRBUF: .BLKB   6912.\n"
           + equs + decrun + emit_setupchain() + ovlay + tail
           + "\n        .EVEN\nC408W:  .WORD   0\n" + gst
           + "\n        .END    START\n")
    src = src.replace("%NELEM%", str(nelem)).replace("%C408%", str(snap[0xC408]))
    src.encode("ascii")
    OUT_MAC.write_text(src, encoding="ascii", newline="\r\n")
    print(f"gamelogic_mac: wrote {OUT_MAC} (DEMO+DOJO: bg{bgn} + fighter pose "
          f"${pose:04X} {fwid}x{fhgt} at top={top})")


def main_loader_bg():
    """FIST_GL=loaderbg - the dojo+fighter demo rendered from the FULL GST via the
    park-RMON loader (vs demobg's trimmed GST embedded below RMON).  The full
    24 KB GST is embedded at 020000 (banks 1-3); the prologue parks banks 4-6 and
    relocates it to its runtime home 0100000 (extended RAM), then renders with the
    dispatcher set to keep banks 4-6 EXTENDED (03217) so the GST is reachable
    there while VRAM is windowed at 040000.  Same render path as demobg, so the
    picture must match - proving the full GST works in parked memory.  SCRBUF and
    the compose buffer reuse the embed area (free once the GST is relocated)."""
    import fighter_mac as fm
    import fighter_data as fd
    import setup_ref as sr
    import gen_fist
    from bg_data import BackgroundData
    fm.STAGE_LEVEL = 1
    nelem = int(os.environ.get("FGHT_NELEM", "5000"))
    bgn = int(os.environ.get("FGHT_BG", "1"))
    snap, b_in, c_in, pose = sr.capture_c34f_low(0xD400)
    fwid, fhgt = snap[0xC40A], snap[0xC409]
    top = 4 + snap[0xC436]
    left = 4 + (snap[0xC434] >> 2)
    dstoff = (top * 40 + left) * 2

    gst_full = snap[GBASE:GST_FULL_END]
    nwords = len(gst_full) // 2

    stage = f"""        JSR     PC,CHGBG               ; render the dojo into SCRBUF
        JSR     PC,SPSCR               ; present it 1:1 + coloured to VRAM
        MOV     #SCRBUF,R0             ; FBUF (=SCRBUF) := black for the fighter
        MOV     #442.,R1
50$:    CLR     (R0)+
        DEC     R1
        BNE     50$
        MOV     #W,R0
        MOV     #32.,R1
51$:    CLR     (R0)+
        DEC     R1
        BNE     51$
        MOV     #{b_in}.,R3
        MOV     #{c_in}.,R4
        JSR     PC,SETUPC
52$:    JSR     PC,SEGSET
        MOVB    {g(0xC408)},R0
        BIC     #177400,R0
        MOV     R0,C408W
        JSR     PC,DECRUN
        DECB    SEGCNT
        BNE     52$
        JSR     PC,OVLAY"""

    # the park + relocate prologue (replaces the demos' "all banks primary" set).
    # RT-11 sets SP to the top of our image (077244 = inside the VRAM window);
    # move it to bank-1 RAM above SCRBUF, below the 040000 window, first.
    reloc = f"""        MOV     #37700,SP             ; stack below the VRAM window (bank 1)
        MOV     #17,@#DISPAT           ; park banks 4-6 (extended), VRAM off
        MOV     #GSTLD,R0             ; relocate the full GST 020000 -> 100000
        MOV     #GST,R1
        MOV     #NWORD,R2
8$:     MOV     (R0)+,(R1)+
        SOB     R2,8$
        MOV     #3217,@#DISPAT         ; VRAM on @40000, banks 4-6 EXTENDED"""

    program = (gen_fist.PROGRAM.replace("%STAGE%", stage)
               .replace("%BGDEF%", f"BG{bgn}DEF").replace("%BGN%", str(bgn))
               .replace("\n        .EVEN\nSTART:",
                        "\n        .ASECT\n        . = 1000\n        .EVEN\nSTART:")
               .replace("        MOV     #3377,@#DPRAM\n"
                        "        MOV     #3377,@#DISPAT", reloc))
    rows = gen_fist.spectrum_row_offsets()
    bg = BackgroundData(bgn)

    equs = "\nFWHITE = 043400\n"
    equs += "GST    = 100000\n"           # GST runtime home (extended banks 4-6)
    equs += "GSTLD  = 20000\n"            # GST embed/load address (banks 1-3)
    equs += f"NWORD  = {nwords}.\n"
    equs += "SCRBUF = 20000\n"            # reuse the embed area, free after relocate
    for t in fd.TABLES:
        equs += f"T{t:04X}  = GST+{t - GBASE}.\n"
    equs += f"C40EM  = GST+{0xC40E - GBASE}.\n"
    equs += f"C407M  = GST+{0xC407 - GBASE}.\n"
    equs += "FBUF   = SCRBUF\n"
    equs += "WB1C   = W+60.\n"
    ovlay = f"""
;-------------------------------------------------------------------
; OVLAY - overlay the composed fighter (FBUF) onto VRAM, masked: only the
; non-zero (set) bytes are written white, so the dojo shows through.
OVLAY:  MOV     #FBUF,R1
        MOV     #{fhgt}.,R5
        MOV     #VRAM+{dstoff}.,R2
1$:     MOV     R2,R0
        MOV     #{fwid}.,R4
2$:     MOVB    (R1)+,R3
        BIC     #177400,R3
        BEQ     3$
        BISB    R3,(R0)               ; OR the fighter pixels in - transparent
3$:     ADD     #2,R0
        DEC     R4
        BNE     2$
        ADD     #120,R2
        DEC     R5
        BNE     1$
        RTS     PC
"""
    decrun = (fm.emit_decrun()
              .replace("MOV     #FCTRL,SRCP\n        "
                       "MOV     #FBUF+%DEOFF%.,DSTP\n        ", "")
              .replace("ADD     #C408V,R0", "ADD     C408W,R0"))
    tail = (fm.TAIL
            .replace("ORIGDP: .WORD   0\n", "").replace("ORIGRC: .WORD   0\n", "")
            .replace("C40EM:  .BYTE   %C40E%.                ; per-fighter mode flags ($C40E)\n", "")
            .replace("C407M:  .BYTE   %C407%.                ; facing flag ($C407)\n", ""))
    # full GST embedded at the load address (banks 1-3), relocated at runtime
    gstemb = ("\n        .ASECT\n        . = 20000\n"
              + _emit_window("GSTEMB", gst_full) + "        .EVEN\n")
    src = (program
           + f"\n;------ background {bgn} data ------\n" + bg.emit()
           + "\n        .EVEN\n" + gen_fist._emit_words("SROWS", rows)
           + equs + decrun + emit_setupchain() + ovlay + tail
           + "\n        .EVEN\nC408W:  .WORD   0\n" + gstemb
           + "\n        .END    START\n")
    src = src.replace("%NELEM%", str(nelem)).replace("%C408%", str(snap[0xC408]))
    src.encode("ascii")
    OUT_MAC.write_text(src, encoding="ascii", newline="\r\n")
    print(f"gamelogic_mac: wrote {OUT_MAC} (LOADER+DOJO: full GST relocated "
          f"020000->100000, bg{bgn} + fighter pose ${pose:04X} {fwid}x{fhgt})")


def main_demo_anim():
    """Animated dojo demo (FIST_GL=demoanim), FLICKER-FREE.  The fighter cycles
    its 16-frame animation ($C440 table, poses $C4CC..$D35C, all low).  The dojo
    is presented ONCE; each frame the fighter's fixed region is composed OFF the
    screen (clean-dojo copy + fighter) and written to VRAM in a single pass, so
    no 'dojo without fighter' state is ever visible (a software back-buffer, the
    way the Spectrum original works).  Per-frame draw params are precomputed in
    Python (validated bf13/$C101/$C1A2 refs, $AA52=0 keeps the P2 pose low) into
    ATAB; the MACRO loop just cycles it.  Buffers reuse SCRBUF + the bg data
    (both free after the dojo is rendered+presented once), so it stays in low
    RAM with no loader."""
    import fighter_mac as fm
    import fighter_data as fd
    import setup_ref as sr
    import gen_fist
    from bg_data import BackgroundData
    fm.STAGE_LEVEL = 1
    nelem = int(os.environ.get("FGHT_NELEM", "5000"))
    bgn = int(os.environ.get("FGHT_BG", "1"))
    nframes = 16
    snap, _b0, _c0, _p0 = sr.capture_c34f_low(0xD400)

    # Per-frame params + the union bounding box of all frames (the fixed region
    # the back-buffer covers, so a single saved clean-dojo copy serves all).
    rows_p = []
    for fr in range(nframes):
        m = bytearray(snap)
        m[0xAA52] = 0
        m[0xAA12] = fr
        ref.bf13(m)
        sr.c101_block1(m)
        b, c, _pose = sr.c1a2(m)
        pose = m[0xC428] | (m[0xC429] << 8)
        fwid, fhgt = m[0xC40A], m[0xC409]
        top = 4 + m[0xC436]
        left = 4 + (m[0xC434] >> 2)
        rows_p.append((pose, m[0xC40F], m[0xC410], m[0xC411], m[0xC41A],
                       b, c, fwid, fhgt, top, left))
    BTOP = min(r[9] for r in rows_p)
    BLEFT = min(r[10] for r in rows_p)
    BWID = max(r[10] + r[7] for r in rows_p) - BLEFT      # words
    BHGT = max(r[9] + r[8] for r in rows_p) - BTOP        # lines
    VOFF = (BTOP * 40 + BLEFT) * 2                         # bbox byte offset in VRAM
    SAVESZ = BWID * BHGT * 2                               # back-buffer size (bytes)
    atab = []
    for (pose, c40f, c410, c411, c41a, b, c, fwid, fhgt, top, left) in rows_p:
        bboff = ((top - BTOP) * BWID + (left - BLEFT)) * 2  # offset inside the back-buffer
        atab.append((pose, c40f, c410, c411, c41a, b, c, fwid, fhgt, bboff))

    stage = f"""        JSR     PC,CHGBG               ; render the dojo into SCRBUF (once)
        JSR     PC,SPSCR               ; present it to VRAM (once)
        JSR     PC,SAVEBB              ; save the fighter region's clean dojo
50$:    JSR     PC,RESTBB              ; BBBUF := the clean-dojo region
        MOVB    FRAME,R0               ; entry = ATAB + FRAME*16
        BIC     #177400,R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0
        MOV     #ATAB,R2
        ADD     R0,R2
        MOV     (R2)+,{g(0xC428)}      ; pose pointer
        MOVB    (R2)+,{g(0xC40F)}      ; the $C101/$C1A2 cells SEGSET reads
        MOVB    (R2)+,{g(0xC410)}
        MOVB    (R2)+,{g(0xC411)}
        MOVB    (R2)+,{g(0xC41A)}
        MOVB    (R2)+,R3               ; b_in
        MOVB    (R2)+,R4               ; c_in
        MOVB    (R2)+,FWIDR            ; fwid
        MOVB    (R2)+,FHGTR            ; fhgt
        MOV     (R2)+,BBOFF            ; offset inside the back-buffer
        MOV     #FBUF,R0               ; FBUF := black
        MOV     #442.,R1
51$:    CLR     (R0)+
        DEC     R1
        BNE     51$
        MOV     #W,R0
        MOV     #32.,R1
52$:    CLR     (R0)+
        DEC     R1
        BNE     52$
        JSR     PC,SETUPC
53$:    JSR     PC,SEGSET
        MOVB    {g(0xC408)},R0
        BIC     #177400,R0
        MOV     R0,C408W
        JSR     PC,DECRUN
        DECB    SEGCNT
        BNE     53$
        JSR     PC,OVLBB               ; compose fighter into the back-buffer
        JSR     PC,BLITBB              ; write the region to VRAM in one pass
        MOV     #6.,R1                 ; frame delay (~0.15 s, watchable)
56$:    MOV     #60000.,R0
54$:    SOB     R0,54$
        SOB     R1,56$
        INCB    FRAME                  ; next frame (cycle 0..{nframes-1})
        MOVB    FRAME,R0
        BIC     #177400,R0
        CMP     R0,#{nframes}.
        BLO     55$
        CLRB    FRAME
55$:    MOV     @#KBST,R0              ; any key -> exit
        BIT     #2,R0
        BEQ     50$
        MOV     @#KBDT,R0
        JMP     EXITP"""
    program = (gen_fist.PROGRAM.replace("%STAGE%", stage)
               .replace("%BGDEF%", f"BG{bgn}DEF").replace("%BGN%", str(bgn))
               .replace("\n        .EVEN\nSTART:",
                        "\n        .ASECT\n        . = 1000\n        .EVEN\nSTART:"))
    rows = gen_fist.spectrum_row_offsets()
    bg = BackgroundData(bgn)

    equs = "\nFWHITE = 043400\n"
    for t in fd.TABLES:
        equs += f"T{t:04X}  = GST+{t - GBASE}.\n"
    equs += f"C40EM  = GST+{0xC40E - GBASE}.\n"
    equs += f"C407M  = GST+{0xC407 - GBASE}.\n"
    # Buffers reuse SCRBUF (FBUF) + the bg data (SAVBUF/BBBUF) - all free once the
    # dojo has been rendered into SCRBUF and presented.
    equs += "FBUF   = SCRBUF\n"
    equs += f"SAVBUF = BG{bgn}DEF\n"
    equs += f"BBBUF  = BG{bgn}DEF+{SAVESZ}.\n"
    equs += "WB1C   = W+60.\n"
    ovlay = f"""
;-------------------------------------------------------------------
; SAVEBB - copy the fighter region's clean dojo (VRAM) into SAVBUF.
SAVEBB: MOV     #SAVBUF,R1
        MOV     #VRAM+{VOFF}.,R2
        MOV     #{BHGT}.,R5
1$:     MOV     R2,R0
        MOV     #{BWID}.,R4
2$:     MOV     (R0)+,(R1)+
        DEC     R4
        BNE     2$
        ADD     #120,R2
        DEC     R5
        BNE     1$
        RTS     PC

;-------------------------------------------------------------------
; RESTBB - BBBUF := the clean-dojo region (SAVBUF).
RESTBB: MOV     #SAVBUF,R1
        MOV     #BBBUF,R2
        MOV     #{BWID * BHGT}.,R0
1$:     MOV     (R1)+,(R2)+
        DEC     R0
        BNE     1$
        RTS     PC

;-------------------------------------------------------------------
; OVLBB - transparent overlay FBUF (FWIDR x FHGTR) into BBBUF at BBOFF.
OVLBB:  MOV     #FBUF,R1
        MOVB    FHGTR,R5
        BIC     #177400,R5
        MOV     BBOFF,R2
        ADD     #BBBUF,R2
1$:     MOV     R2,R0
        MOVB    FWIDR,R4
        BIC     #177400,R4
2$:     MOVB    (R1)+,R3
        BIC     #177400,R3
        BEQ     3$
        BISB    R3,(R0)
3$:     ADD     #2,R0
        DEC     R4
        BNE     2$
        ADD     #{BWID * 2}.,R2
        DEC     R5
        BNE     1$
        RTS     PC

;-------------------------------------------------------------------
; BLITBB - write the composed region (BBBUF) to VRAM in one pass.
BLITBB: MOV     #BBBUF,R1
        MOV     #VRAM+{VOFF}.,R2
        MOV     #{BHGT}.,R5
1$:     MOV     R2,R0
        MOV     #{BWID}.,R4
2$:     MOV     (R1)+,(R0)+
        DEC     R4
        BNE     2$
        ADD     #120,R2
        DEC     R5
        BNE     1$
        RTS     PC
"""
    tabsrc = "\n        .EVEN\nATAB:\n"           # 16-byte entries (frame*16 indexing)
    for pose, c40f, c410, c411, c41a, b, c, fwid, fhgt, bboff in atab:
        tabsrc += (f"        .WORD   {pose}.\n"
                   f"        .BYTE   {c40f}.,{c410}.,{c411}.,{c41a}.\n"
                   f"        .BYTE   {b}.,{c}.,{fwid}.,{fhgt}.\n"
                   f"        .WORD   {bboff}.\n        .WORD   0\n        .WORD   0\n")
    tabsrc += "FRAME:  .BYTE   0\nFWIDR:  .BYTE   0\nFHGTR:  .BYTE   0\n        .EVEN\nBBOFF:  .WORD   0\n"

    decrun = (fm.emit_decrun()
              .replace("MOV     #FCTRL,SRCP\n        "
                       "MOV     #FBUF+%DEOFF%.,DSTP\n        ", "")
              .replace("ADD     #C408V,R0", "ADD     C408W,R0"))
    tail = (fm.TAIL
            .replace("ORIGDP: .WORD   0\n", "").replace("ORIGRC: .WORD   0\n", "")
            .replace("C40EM:  .BYTE   %C40E%.                ; per-fighter mode flags ($C40E)\n", "")
            .replace("C407M:  .BYTE   %C407%.                ; facing flag ($C407)\n", ""))
    gst = ("\n        .ASECT\n        . = 100000\n"
           + _emit_window("GST", snap[GBASE:DEMO_END]) + "        .EVEN\n")
    src = (program
           + f"\n;------ background {bgn} data ------\n" + bg.emit()
           + "\n        .EVEN\n" + gen_fist._emit_words("SROWS", rows)
           + "\n        .EVEN\nSCRBUF: .BLKB   6912.\n"
           + equs + decrun + emit_setupchain() + ovlay + tabsrc + tail
           + "\n        .EVEN\nC408W:  .WORD   0\n" + gst
           + "\n        .END    START\n")
    src = src.replace("%NELEM%", str(nelem)).replace("%C408%", str(snap[0xC408]))
    src.encode("ascii")
    OUT_MAC.write_text(src, encoding="ascii", newline="\r\n")
    print(f"gamelogic_mac: wrote {OUT_MAC} (DEMO+ANIM: bg{bgn} + {nframes}-frame "
          f"fighter animation, poses {atab[0][0]:#06x}..{atab[-1][0]:#06x})")


def main_demo_fight():
    """Two-fighter dojo scene (FIST_GL=demofight), flicker-free.  P1 (facing
    right) and P2 (facing left, mirrored) both cycle their 16-frame animations,
    drawn from the ported logic: bf13 computes BOTH fighters; P1 via $C101
    block-1 + $C1A2, P2 via $C101 block-2 + $C1CC.  $AA19/$AA59 set apart
    start-of-round x; $AA12/$AA52 are the per-fighter frame indices.  One wide
    back-buffer covers both fighters: each frame restore the clean dojo, compose
    P1 then P2 into it, write the whole region to VRAM in one pass."""
    import fighter_mac as fm
    import fighter_data as fd
    import setup_ref as sr
    import gen_fist
    from bg_data import BackgroundData
    fm.STAGE_LEVEL = 1
    nelem = int(os.environ.get("FGHT_NELEM", "5000"))
    bgn = int(os.environ.get("FGHT_BG", "1"))
    nframes = 16
    base = bytearray(sr.capture_c34f_low(0xD400)[0])
    base[0xAA19] = 0x18                                # P1 x (left)
    base[0xAA59] = 0x58                                # P2 x (right)

    def frame_params(fr, p2):
        m = bytearray(base)
        if p2:
            m[0xAA12] = 0; m[0xAA52] = fr
        else:
            m[0xAA12] = fr; m[0xAA52] = 0
        ref.bf13(m); ref.bf13(m)                       # twice -> stable bbox
        if p2:
            sr.c101_block2(m); b, c, _ = sr.c1cc(m)
            bx0, bx1, by0 = 0xC438, 0xC439, 0xC43A
        else:
            sr.c101_block1(m); b, c, _ = sr.c1a2(m)
            bx0, bx1, by0 = 0xC434, 0xC435, 0xC436
        pose = (m[0xC428 + 2 * p2]) | (m[0xC429 + 2 * p2] << 8)
        return dict(pose=pose, c40f=m[0xC40F], c410=m[0xC410], c411=m[0xC411],
                    c41a=m[0xC41A], b=b, c=c, fwid=m[0xC40A], fhgt=m[0xC409],
                    top=4 + m[by0], left=4 + (m[bx0] >> 2))

    p1 = [frame_params(fr, 0) for fr in range(nframes)]
    p2 = [frame_params(fr, 1) for fr in range(nframes)]
    allf = p1 + p2
    BTOP = min(f["top"] for f in allf)
    BLEFT = min(f["left"] for f in allf)
    BWID = max(f["left"] + f["fwid"] for f in allf) - BLEFT
    BHGT = max(f["top"] + f["fhgt"] for f in allf) - BTOP
    VOFF = (BTOP * 40 + BLEFT) * 2
    SAVESZ = BWID * BHGT * 2

    def emit_tab(label, frames):
        s = f"\n        .EVEN\n{label}:\n"
        for f in frames:
            bboff = ((f["top"] - BTOP) * BWID + (f["left"] - BLEFT)) * 2
            s += (f"        .WORD   {f['pose']}.\n"
                  f"        .BYTE   {f['c40f']}.,{f['c410']}.,{f['c411']}.,{f['c41a']}.\n"
                  f"        .BYTE   {f['b']}.,{f['c']}.,{f['fwid']}.,{f['fhgt']}.\n"
                  f"        .WORD   {bboff}.\n        .WORD   0\n        .WORD   0\n")
        return s

    # one off-screen compose pass per fighter (reads its table entry via R2,
    # writes the fighter into BBBUF); l1/l2/l3 are distinct numeric local labels.
    def onef(l1, l2, l3):
        return f"""        MOV     (R2)+,{g(0xC428)}
        MOVB    (R2)+,{g(0xC40F)}
        MOVB    (R2)+,{g(0xC410)}
        MOVB    (R2)+,{g(0xC411)}
        MOVB    (R2)+,{g(0xC41A)}
        MOVB    (R2)+,R3
        MOVB    (R2)+,R4
        MOVB    (R2)+,FWIDR
        MOVB    (R2)+,FHGTR
        MOV     (R2)+,BBOFF
        MOV     #FBUF,R0
        MOV     #442.,R1
{l1}$:     CLR     (R0)+
        DEC     R1
        BNE     {l1}$
        MOV     #W,R0
        MOV     #32.,R1
{l2}$:     CLR     (R0)+
        DEC     R1
        BNE     {l2}$
        JSR     PC,SETUPC
{l3}$:     JSR     PC,SEGSET
        MOVB    {g(0xC408)},R0
        BIC     #177400,R0
        MOV     R0,C408W
        JSR     PC,DECRUN
        DECB    SEGCNT
        BNE     {l3}$
        JSR     PC,OVLBB"""

    stage = f"""        JSR     PC,CHGBG               ; dojo -> SCRBUF (once)
        JSR     PC,SPSCR               ; dojo -> VRAM (once)
        JSR     PC,SAVEBB              ; save the (wide) fight region's clean dojo
50$:    JSR     PC,RESTBB              ; BBBUF := clean dojo region
        MOVB    FRAME,R0
        BIC     #177400,R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0                     ; frame*16
        MOV     #PTAB1,R2              ; --- fighter 1 ---
        ADD     R0,R2
{onef(10, 11, 12)}
        MOVB    FRAME,R0
        BIC     #177400,R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0
        MOV     #PTAB2,R2              ; --- fighter 2 ---
        ADD     R0,R2
{onef(20, 21, 22)}
        JSR     PC,BLITBB              ; write the whole region to VRAM, one pass
        MOV     #6.,R1                 ; frame delay
56$:    MOV     #60000.,R0
54$:    SOB     R0,54$
        SOB     R1,56$
        INCB    FRAME
        MOVB    FRAME,R0
        BIC     #177400,R0
        CMP     R0,#{nframes}.
        BLO     55$
        CLRB    FRAME
55$:    MOV     @#KBST,R0
        BIT     #2,R0
        BNE     57$
        JMP     50$                    ; loop back (out of branch range)
57$:    MOV     @#KBDT,R0
        JMP     EXITP"""
    program = (gen_fist.PROGRAM.replace("%STAGE%", stage)
               .replace("%BGDEF%", f"BG{bgn}DEF").replace("%BGN%", str(bgn))
               .replace("\n        .EVEN\nSTART:",
                        "\n        .ASECT\n        . = 1000\n        .EVEN\nSTART:"))
    rows = gen_fist.spectrum_row_offsets()
    bg = BackgroundData(bgn)

    equs = "\nFWHITE = 043400\n"
    for t in fd.TABLES:
        equs += f"T{t:04X}  = GST+{t - GBASE}.\n"
    equs += f"C40EM  = GST+{0xC40E - GBASE}.\n"
    equs += f"C407M  = GST+{0xC407 - GBASE}.\n"
    # The wide region's two buffers (SAVESZ each) go in SCRBUF (6912 B, free
    # after the dojo is presented); the small FBUF in the bg data (free after
    # CHGBG).  Guard the SCRBUF capacity.
    assert 2 * SAVESZ <= 6912, f"region too big for SCRBUF: 2*{SAVESZ} > 6912"
    equs += f"FBUF   = BG{bgn}DEF\n"
    equs += "SAVBUF = SCRBUF\n"
    equs += f"BBBUF  = SCRBUF+{SAVESZ}.\n"
    equs += "WB1C   = W+60.\n"
    ovlay = f"""
;-------------------------------------------------------------------
SAVEBB: MOV     #SAVBUF,R1
        MOV     #VRAM+{VOFF}.,R2
        MOV     #{BHGT}.,R5
1$:     MOV     R2,R0
        MOV     #{BWID}.,R4
2$:     MOV     (R0)+,(R1)+
        DEC     R4
        BNE     2$
        ADD     #120,R2
        DEC     R5
        BNE     1$
        RTS     PC
;-------------------------------------------------------------------
RESTBB: MOV     #SAVBUF,R1
        MOV     #BBBUF,R2
        MOV     #{BWID * BHGT}.,R0
1$:     MOV     (R1)+,(R2)+
        DEC     R0
        BNE     1$
        RTS     PC
;-------------------------------------------------------------------
OVLBB:  MOV     #FBUF,R1
        MOVB    FHGTR,R5
        BIC     #177400,R5
        MOV     BBOFF,R2
        ADD     #BBBUF,R2
1$:     MOV     R2,R0
        MOVB    FWIDR,R4
        BIC     #177400,R4
2$:     MOVB    (R1)+,R3
        BIC     #177400,R3
        BEQ     3$
        BISB    R3,(R0)
3$:     ADD     #2,R0
        DEC     R4
        BNE     2$
        ADD     #{BWID * 2}.,R2
        DEC     R5
        BNE     1$
        RTS     PC
;-------------------------------------------------------------------
BLITBB: MOV     #BBBUF,R1
        MOV     #VRAM+{VOFF}.,R2
        MOV     #{BHGT}.,R5
1$:     MOV     R2,R0
        MOV     #{BWID}.,R4
2$:     MOV     (R1)+,(R0)+
        DEC     R4
        BNE     2$
        ADD     #120,R2
        DEC     R5
        BNE     1$
        RTS     PC
"""
    tabsrc = (emit_tab("PTAB1", p1) + emit_tab("PTAB2", p2)
              + "FRAME:  .BYTE   0\nFWIDR:  .BYTE   0\nFHGTR:  .BYTE   0\n"
              + "        .EVEN\nBBOFF:  .WORD   0\n")

    decrun = (fm.emit_decrun()
              .replace("MOV     #FCTRL,SRCP\n        "
                       "MOV     #FBUF+%DEOFF%.,DSTP\n        ", "")
              .replace("ADD     #C408V,R0", "ADD     C408W,R0"))
    tail = (fm.TAIL
            .replace("ORIGDP: .WORD   0\n", "").replace("ORIGRC: .WORD   0\n", "")
            .replace("C40EM:  .BYTE   %C40E%.                ; per-fighter mode flags ($C40E)\n", "")
            .replace("C407M:  .BYTE   %C407%.                ; facing flag ($C407)\n", ""))
    gst = ("\n        .ASECT\n        . = 100000\n"
           + _emit_window("GST", base[GBASE:DEMO_END]) + "        .EVEN\n")
    src = (program
           + f"\n;------ background {bgn} data ------\n" + bg.emit()
           + "\n        .EVEN\n" + gen_fist._emit_words("SROWS", rows)
           + "\n        .EVEN\nSCRBUF: .BLKB   6912.\n"
           + equs + decrun + emit_setupchain() + ovlay + tabsrc + tail
           + "\n        .EVEN\nC408W:  .WORD   0\n" + gst
           + "\n        .END    START\n")
    src = src.replace("%NELEM%", str(nelem)).replace("%C408%", str(base[0xC408]))
    src.encode("ascii")
    OUT_MAC.write_text(src, encoding="ascii", newline="\r\n")
    print(f"gamelogic_mac: wrote {OUT_MAC} (DEMO+FIGHT: 2 fighters, region "
          f"{BWID}x{BHGT} words at top={BTOP} left={BLEFT})")


def main():
    name = os.environ.get("FIST_GL", "timer")
    if os.environ.get("FIST_COV") == "ai":
        return main_coverage_ai()
    if os.environ.get("FIST_COV"):
        return main_coverage()
    if name == "ai":
        return main_ai()
    if name == "movsel":
        return main_movsel()
    if name == "bf13":
        return main_bf13()
    if name == "95e1":
        return main_95e1()
    if name == "combined":
        return main_combined()
    if name == "fullframe":
        return main_fullframe()
    if name == "render":
        return main_render()
    if name == "loader":
        return main_loader()
    if name == "loaderdat":
        return main_loaderdat()
    if name == "decgst":
        return main_decgst()
    if name == "drawgst":
        return main_drawgst()
    if name == "fulldraw":
        return main_drawgst(mode="full")
    if name == "bridgedraw":
        return main_drawgst(mode="bridge")
    if name == "framedraw":
        return main_framedraw()
    if name == "game":
        return main_game()
    if name == "gamebg":
        return main_game(withbg=True)
    if name == "demo":
        return main_demo()
    if name == "demobg":
        return main_demo_bg()
    if name == "loaderbg":
        return main_loader_bg()
    if name == "demoanim":
        return main_demo_anim()
    if name == "demofight":
        return main_demo_fight()
    (addr, entry, emit, refapply, win_end, reg_setup, witness,
     budget) = ROUTINES[name]
    win_size = win_end - GBASE
    assert win_size % 2 == 0, "window must be word-aligned"

    snap, regs = capture_state(addr, refapply, win_end, reg_setup, witness,
                               budget)
    expected = bytearray(snap)
    refapply(expected, regs)
    EXP_BIN.write_bytes(bytes(expected[GBASE:win_end]))
    WIN_JSON.write_text(json.dumps({"base": GBASE, "size": win_size}))

    regset = "".join(f"        MOV     #{regs[z]}.,{pdp}\n"
                     for pdp, z in reg_setup)

    src = HEADER + emit()
    src += "\n        .EVEN\n"
    src += _emit_window("GST", snap[GBASE:win_end])
    src += "\n        .EVEN\n        .END    START\n"
    src = (src.replace("%ENTRY%", entry)
              .replace("%REGSET%", regset)
              .replace("%WORDS%", str(win_size // 2) + "."))
    src.encode("ascii")
    OUT_MAC.write_text(src, encoding="ascii", newline="\r\n")
    setup = " ".join(f"{p}={regs[z]:#x}" for p, z in reg_setup) or "(no regs)"
    print(f"gamelogic_mac: wrote {OUT_MAC} ({name} @ {addr:#06x}, "
          f"window {win_size} B, {setup}, expected -> {EXP_BIN.name})")


if __name__ == "__main__":
    main()
