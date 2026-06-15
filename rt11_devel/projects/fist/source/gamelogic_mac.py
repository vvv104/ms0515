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
Z80_REG = {'A': 0, 'B': 2, 'C': 3, 'D': 4, 'E': 5, 'H': 6, 'L': 7}


def g(addr, reg=None):
    """MACRO-11 operand for Spectrum address `addr` in GST - relative if `reg`
    is None, else indexed `GST+off(reg)` (used for the fighter selector C and
    the table-index registers)."""
    off = f"GST+{addr - GBASE}."
    return f"{off}({reg})" if reg else off


# ── routine capture (entry state + entry registers) ───────────────────────────

def capture_state(addr, refapply, win_end, reg_setup, budget=4000000):
    """Run the game; return (snapshot, entry_regs) for a call to `addr` that the
    reference exercises (changes the GST window).  Prefer a call whose
    parameter registers are all non-zero, so indexed `(Rn)` offsets are
    genuinely exercised (an R4=0 selector would hide a missing index).  Falls
    back to any exercising call, then to the first call seen."""
    sim, mem = build_sim(watch=(0, 0))
    regs, memory, ops = sim.registers, sim.memory, sim.opcodes
    fd, ia = sim.frame_duration, sim.int_active
    zregs = [z for _, z in reg_setup]
    first = exercising = None
    for _ in range(budget):
        if regs[PC] == addr:
            snap = bytes(memory)
            entry = {n: regs[i] for n, i in Z80_REG.items()}
            if first is None:
                first = (snap, entry)
            trial = bytearray(snap)
            refapply(trial, entry)
            if trial[GBASE:win_end] != snap[GBASE:win_end]:
                if all(entry[z] != 0 for z in zregs):
                    return snap, entry
                if exercising is None:
                    exercising = (snap, entry)
        ops[memory[regs[PC]]]()
        if regs[26] and regs[25] % fd < ia:
            sim.accept_interrupt(regs, memory, regs[PC])
    if exercising:
        return exercising
    if first:
        return first
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


# name -> (addr, label, emit, refapply(m,regs), win_end, reg_setup[(pdp,z80)])
ROUTINES = {
    "timer": (0x9C6F, "TIMER", emit_timer,
              lambda m, r: ref.update_timer(m), 0xAB00, []),
    "recover": (0x9AD7, "RECOV", emit_recover,
                lambda m, r: ref.recover_9ad7(m, r['C']), 0xB500,
                [("R4", "C")]),
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


def main():
    name = os.environ.get("FIST_GL", "timer")
    addr, entry, emit, refapply, win_end, reg_setup = ROUTINES[name]
    win_size = win_end - GBASE
    assert win_size % 2 == 0, "window must be word-aligned"

    snap, regs = capture_state(addr, refapply, win_end, reg_setup)
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
