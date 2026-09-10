"""The MS7004 keyboard -> the original's control bits -> the move ($98DD).

Every function returns MACRO-11 text; game_build.py assembles the game.
"""
import os

from gst_addr import g

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


def control_map():
    """The map in force for this build."""
    return ORIG_MAP if os.environ.get("FIST_ORIG_KEYS") else USER_MAP


def ctrl_table(cmap, fwd_bit, back_bit):
    """The 32 moves for one facing (bit 4 = fire, 0 up, 1 down, fwd / back)."""
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


def mtab(cmap):
    """MTAB: the two facing halves of the $98DD table."""
    tab_a = ctrl_table(cmap, 8, 4)                # facing right: forward = RIGHT
    tab_b = ctrl_table(cmap, 4, 8)                # facing left:  forward = LEFT
    return ("MTAB:   .BYTE   " + ",".join(f"{v}." for v in tab_a[:16]) + "\n"
            "        .BYTE   " + ",".join(f"{v}." for v in tab_a[16:]) + "\n"
            "        .BYTE   1\n"
            "        .BYTE   " + ",".join(f"{v}." for v in tab_b[:16]) + "\n"
            "        .BYTE   " + ",".join(f"{v}." for v in tab_b[16:]) + "\n"
            "        .BYTE   1\n")


KSCAN_NOTE = """        ; --- KSCAN: drain the MS7004 keyboard into the control state -----------
        ; The original reads 9 definable keys (8 directions + fire) per player.
        ; The MS7004 sends make codes only: no release codes, auto-repeat for
        ; the LAST regular key, modifiers emit their own code on every press
        ; and ALL-UP once everything is released.  So each control (UP, DOWN,
        ; LEFT, RIGHT, FIRE) of each player has a hold timer: a key's make /
        ; repeat code sets its timer to KTMR frames, and - the CHORD rule -
        ; refreshes every other timer still running: keys pressed together
        ; stay "held" as long as any one of them repeats (the keyboard only
        ; repeats the last one).  A key released less than KTMR before the
        ; next press is read as part of the chord - the price of a keyboard
        ; without release codes.  Each player's keys are a table of (scancode,
        ; bits) pairs - the defaults DEF1 (the keypad, the arrows, Space / VR /
        ; SU) and DEF2 (Q W E / A S D / Z X C, S = fire), or the nine keys the
        ; settings screen defined (KEYTAB / KEYTB2); the fixed keys ("1", "2",
        ; "0", "G" + "H") stay.
        ; The UART presents one byte per ~2 ms (4800 baud) from a 16-byte FIFO,
        ; so after a byte, poll ~3 ms for the next one so the queue drains.
"""


def kscan(ktmout):
    """KSCAN: drain the MS7004 keyboard into the hold timers; KMATCH: one
    player's key table against a scancode."""
    return KSCAN_NOTE + f"""KSCAN:  CLR     R2                   ; poll budget: none until a byte was read
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
        MOV     #KTUP,R3             ; the twelve timers: both players' and G / H
        MOV     #12.,R1
2$:     CLR     (R3)+
        DEC     R1
        BNE     2$
        BR      KS0
1$:     JSR     PC,KREFR             ; any key event: refresh the running timers
        CMP     R0,#254              ; auto-repeat code (real MS7004): that is all
        BEQ     KS0
        CMP     R0,#300              ; "1": start a 1-player game from the demo
        BNE     15$
        MOV     #1,KSTART
        BR      KS0
15$:    CMP     R0,#305              ; "2": a 2-player game
        BNE     16$
        MOV     #2,KSTART
        BR      KS0
16$:    CMP     R0,#341              ; "G" / "H": held together they quit the game
        BNE     17$
        MOV     #{ktmout}.,KTG
        BR      KS0
17$:    CMP     R0,#366
        BNE     18$
        MOV     #{ktmout}.,KTH
        BR      KS0
18$:    CMP     R0,#357              ; "0": the settings screen, from the demo
        BNE     19$
        MOV     #1,KOPT
        BR      KS0
19$:    MOV     KMAP1,R3             ; player 1's keys -> its timers
        MOV     #KTUP,R4
        JSR     PC,KMATCH
        MOV     KMAP2,R3             ; player 2's
        MOV     #KT2UP,R4
        JSR     PC,KMATCH
        JMP     KS0
        ; KMATCH: the scancode R0 in the (scancode, bits) table R3 (0 ends it)
        ; -> the bits' timers in the block R4 (UP, DOWN, LEFT, RIGHT, FIRE)
KMATCH: MOVB    (R3)+,R1
        BEQ     9$
        CMPB    R1,R0
        BEQ     1$
        INC     R3
        BR      KMATCH
1$:     MOVB    (R3),R1
        BIT     #1,R1
        BEQ     2$
        MOV     #{ktmout}.,(R4)
2$:     BIT     #2,R1
        BEQ     3$
        MOV     #{ktmout}.,2(R4)
3$:     BIT     #4,R1
        BEQ     4$
        MOV     #{ktmout}.,4(R4)
4$:     BIT     #10,R1
        BEQ     5$
        MOV     #{ktmout}.,6(R4)
5$:     BIT     #20,R1
        BEQ     9$
        MOV     #{ktmout}.,10(R4)
9$:     RTS     PC
"""


def kctrl(ktmout):
    """KREFR (the chord rule) and KCTRL (one player's timers -> control bits)."""
    return f"""        ; KREFR: the chord rule - every timer still running gets the full hold
KREFR:  MOV     #KTUP,R3
        MOV     #12.,R4
1$:     TST     (R3)
        BEQ     2$
        MOV     #{ktmout}.,(R3)
2$:     TST     (R3)+
        DEC     R4
        BNE     1$
        RTS     PC
        ; --- KCTRL: a player's hold timers (R1 = its block) -> the original's
        ;     control bits (the $8B4x key scan: bit0 UP, bit1 DOWN, bit2 LEFT,
        ;     bit3 RIGHT, bit4 FIRE) in R0, each timer counting down one per
        ;     frame.  The $98DD table then resolves the bits by facing. ---------
KCTRL:  CLR     R0
        TST     (R1)
        BEQ     1$
        DEC     (R1)
        BIS     #1,R0
1$:     TST     2(R1)
        BEQ     2$
        DEC     2(R1)
        BIS     #2,R0
2$:     TST     4(R1)
        BEQ     3$
        DEC     4(R1)
        BIS     #4,R0
3$:     TST     6(R1)
        BEQ     4$
        DEC     6(R1)
        BIS     #10,R0
4$:     TST     10(R1)
        BEQ     9$
        DEC     10(R1)
        BIS     #20,R0
9$:     CMP     R1,#KTUP             ; the joystick (the controls menu's "5":
        BNE     8$                   ;   KEMPSTON) OR-ed in for the player
        TST     JOY1                 ;   whose flag is on
        BNE     JOYRD
        RTS     PC
8$:     TST     JOY2
        BNE     JOYRD
        RTS     PC
        ; --- JOYRD: the joystick on the MS7007 port (0177542: five lines to
        ;     ground - right, left, down, up, fire, the Kempston order - open
        ;     lines high, as SABOT2 reads it) -> the control bits, into R0. ---
JOYRD:  MOV     R1,-(SP)
        MOV     @#177542,R1
        COM     R1
        BIC     #177740,R1
        MOVB    JOYMAP(R1),R1
        BIS     R1,R0
        MOV     (SP)+,R1
        RTS     PC
JOYMAP: .BYTE   0,8.,4.,12.,2,10.,6,14.,1,9.,5,13.,3,11.,7,15.
        .BYTE   16.,24.,20.,28.,18.,26.,22.,30.,17.,25.,21.,29.,19.,27.,23.,31.
        .EVEN
"""


def c98a0(cmap):
    """C98A0: control bits -> &move in MTAB, for the player R5 (0 = P1, 0o100
    = P2).  Returns the routine + MTAB."""
    return f"""        ; --- C98A0: control (R0) -> &move ($98DD table; the second table when
        ;     the fighter faces left).  R5 = the player's cell offset (C). ------
C98A0:  BIT     #40,R0
        BEQ     1$
        BIS     #20,R0
1$:     BIC     #177740,R0           ; keep 5 control bits
        MOV     #MTAB,R1
        ADD     R0,R1
        MOVB    {g(0xAA17, 'R5')},R0 ; the player's facing
        BIC     #177400,R0
        BEQ     2$
        ADD     #41,R1               ; facing left: the second 0x21-offset table
2$:     MOV     R1,R0
        RTS     PC
        .EVEN
""" + mtab(cmap)


# The default key tables - (scancode, bits) pairs, 0-terminated.  Bits: 1 up,
# 2 down, 4 left, 8 right, 16 fire.  Player 1: the keypad as the original's
# 3x3 block (KP5 = fire), the arrows, Space / VR / SU as fire; player 2: Q W
# E / A S D / Z X C with S as fire.  KEYTAB / KEYTB2: the settings screen's
# nine keys (the scancodes filled in there; up, up-right, right, down-right,
# down, down-left, left, up-left, fire).
DEF1 = [(0o236, 1), (0o237, 9), (0o233, 8), (0o230, 10), (0o227, 2), (0o226, 6),
        (0o231, 4), (0o235, 5), (0o232, 16),
        (0o252, 1), (0o250, 8), (0o251, 2), (0o247, 4),
        (0o324, 16), (0o256, 16), (0o257, 16)]
DEF2 = [(0o315, 1), (0o327, 9), (0o354, 8), (0o306, 10), (0o343, 2), (0o360, 6),
        (0o322, 4), (0o303, 5), (0o316, 16)]
REDEF_BITS = [1, 9, 8, 10, 2, 6, 4, 5, 16]


def tables():
    """The key tables and the active-table pointers (banks 0-1 data)."""
    def pairs(label, t):
        rows = [",".join(f"{c}.,{b}." for c, b in t[i:i + 6]) for i in range(0, len(t), 6)]
        return f"{label}: .BYTE   " + "\n        .BYTE   ".join(rows) + ",0\n"
    return ("        .EVEN\nKMAP1:  .WORD   DEF1\nKMAP2:  .WORD   DEF2\n"
            + pairs("DEF1", DEF1) + pairs("DEF2", DEF2)
            + pairs("KEYTAB", [(0, b) for b in REDEF_BITS])
            + pairs("KEYTB2", [(0, b) for b in REDEF_BITS]))
