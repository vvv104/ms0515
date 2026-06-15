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


def emit_hitdet(label, A):
    """hit_detect: does this fighter's attack reach the opponent?  Walks the
    reach tables ($A98A/$A9BC by facing) to a pointer, derefs m[ptr+ridx], and
    compares the measured distance to set the result A['result'] (2 full /
    1 half).  Parametrized by the P1 ($9D29) / P2 ($9ED2) address set A; P1
    latches both x-positions ($A071/$A072) first, P2 reuses them."""
    latch = ""
    if A['setpos']:
        sp0, sp1 = A['setpos']
        latch = (f"        MOVB    {g(sp0)},R0\n"
                 f"        MOVB    R0,{g(0xA071)}\n"
                 f"        MOVB    {g(sp1)},R0\n"
                 f"        MOVB    R0,{g(0xA072)}\n")
    return f"""
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

AIDEC:  CLR     ARNDI
AI090:  TSTB    {g(0xA5F4)}
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
; ORCH - integration driver: run the ported routines in $9745 order.
ORCH:   JSR     PC,TIMER
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


def emit_combined():
    """All ported game-logic routines in one module (proves they coexist - no
    label or space conflicts) plus the ORCH integration driver.  The AI is
    included (with a dummy random array) to prove it assembles alongside the
    rest; ORCH does not call it."""
    return (emit_timer() + emit_recover() +
            emit_hitdet("HITDET", ref.HIT_P1) + emit_hitdet("HITDP2", ref.HIT_P2) +
            emit_anim() +
            emit_apply("APLYHT", 0xAA44, 0xAA57, 0xAA17, 0xAA03) +
            emit_apply("APLYP1", 0xAA04, 0xAA17, 0xAA57, 0xAA43) +
            emit_score() + emit_yinyang() + emit_timetik() +
            emit_ranktk() + emit_rstacf() + emit_rstfrm() + emit_ai([0]) +
            emit_orch())


def orch_subset(m):
    """Python reference for ORCH - the same sequence on the captured state."""
    ref.update_timer(m)
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
    win_end = 0xB500
    win_size = win_end - GBASE
    snap, _ = capture_state(0x9745, lambda m, r: orch_subset(m), win_end, [])
    expected = bytearray(snap)
    orch_subset(expected)
    EXP_BIN.write_bytes(bytes(expected[GBASE:win_end]))
    WIN_JSON.write_text(json.dumps({"base": GBASE, "size": win_size}))
    src = HEADER + emit_combined()
    src += "\n        .EVEN\n"
    src += _emit_window("GST", snap[GBASE:win_end])
    src += "\n        .EVEN\n        .END    START\n"
    src = (src.replace("%ENTRY%", "ORCH").replace("%REGSET%", "")
              .replace("%WORDS%", str(win_size // 2) + "."))
    src.encode("ascii")
    OUT_MAC.write_text(src, encoding="ascii", newline="\r\n")
    print(f"gamelogic_mac: wrote {OUT_MAC} (combined, all routines + ORCH, "
          f"window {win_size} B, expected -> {EXP_BIN.name})")


def main():
    name = os.environ.get("FIST_GL", "timer")
    if name == "ai":
        return main_ai()
    if name == "combined":
        return main_combined()
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
