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
        ; keyboard without release codes.  The settings screen can replace the
        ; set with nine keys of its own (KEYMOD / KEYTAB, as the original's
        ; redefinition); the fixed keys ("1", "0", "G" + "H") stay.
        ; The UART presents one byte per ~2 ms (4800 baud) from a 16-byte FIFO, so
        ; after a byte, poll ~3 ms for the next one so the queue drains each frame.
"""


def kscan(ktmout):
    """KSCAN: drain the MS7004 keyboard into the hold timers."""
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
        CMP     R0,#300              ; "1": start a 1-player game from the demo
        BNE     15$
        MOV     #1,KSTART
        BR      KS0
15$:    CMP     R0,#341              ; "G" / "H": held together they quit the game
        BNE     16$
        MOV     #{ktmout}.,KTG
        BR      KS0
16$:    CMP     R0,#366
        BNE     17$
        MOV     #{ktmout}.,KTH
        BR      KS0
17$:    CMP     R0,#357              ; "0": the settings screen, from the demo
        BNE     18$
        MOV     #1,KOPT
        JMP     KS0
18$:    TST     KEYMOD               ; the keys the settings screen defined?
        BEQ     2$
        MOV     #KEYTAB,R3           ; the nine controls: a match -> its bits
        MOV     #9.,R4
19$:    CMPB    R0,(R3)+
        BEQ     20$
        DEC     R4
        BNE     19$
        JMP     KS0
20$:    MOVB    KEYBIT-KEYTAB-1(R3),R1
        BR      10$
2$:     CMP     R0,#324              ; the default set.  fire: Space, VR/Shift
        BEQ     3$                   ;   (0256), SU/Ctrl (0257)
        CMP     R0,#256
        BEQ     3$
        CMP     R0,#257
        BEQ     3$
        MOV     R0,R1                ; directions: DIRTAB[scancode - 0226] = bits
        SUB     #226,R1
        BLT     7$
        CMP     R1,#20.
        BHI     7$
        MOVB    DIRTAB(R1),R1
        BEQ     7$                   ; not a direction key
10$:    BIT     #1,R1                ; the bits -> the hold timers
        BEQ     11$
        MOV     #{ktmout}.,KTUP
11$:    BIT     #2,R1
        BEQ     12$
        MOV     #{ktmout}.,KTDN
12$:    BIT     #4,R1
        BEQ     13$
        MOV     #{ktmout}.,KTLF
13$:    BIT     #10,R1
        BEQ     14$
        MOV     #{ktmout}.,KTRT
14$:    BIT     #20,R1
        BEQ     7$
3$:     MOV     #{ktmout}.,KTFR
7$:     JMP     KS0
"""


def kctrl(ktmout):
    """KREFR (the chord rule), KCTRL (timers -> control bits) and DIRTAB."""
    return f"""        ; KREFR: the chord rule - every timer still running gets the full hold
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
"""


def c98a0(cmap):
    """C98A0: control bits -> &move in MTAB.  Returns the routine + MTAB."""
    return f"""        ; --- C98A0: control (R0) -> &move ($98DD table; +0x21 if P1 is mid-move) ------
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
""" + mtab(cmap)
