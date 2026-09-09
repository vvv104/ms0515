"""The intro tune ($90D1): played at every opponent presentation ($AC69).

A record of the original's note table ($9128) is (half-period, cycles):
the beeper toggles every half-period x 26 T-states, for `cycles` periods;
half-period 1 is a rest of $0AF4 x 26 T; 0 ends the tune.  Here the
speaker is reg C bit 6 (the SNDON / SPKTOG / SNDOFF drivers of the sound
effects).  The delays are turns of a SOB loop, converted from the
original's T-states by `timing.py`, and the table below carries the turn
counts rather than a runtime multiply.  The tune is blocking and silent
with the sound off (SNDENA / $B2FA), as in the original.  The code lives in
the dojo block and runs from RENDBG.

Every function returns MACRO-11 text; game_build.py assembles the game.
"""

from timing import turns_for_t

# (half-period, cycles) from $9128: the note data the original plays.
NOTES = [(0x6D, 0x8C), (1, 0), (0x6D, 0x8C), (1, 0),
         (0x81, 0x76), (1, 0), (0x81, 0x76), (1, 0),
         (0x90, 0x69), (1, 0), (0x90, 0x69), (1, 0),
         (0x81, 0x76), (1, 0), (0x81, 0x76), (1, 0)]
NOTES += [(0x6D, 0x26), (1, 0)] * 12
NOTES += [(0x6D, 0x125), (1, 0), (0, 0)]

REST_TURNS = turns_for_t(0x0AF4 * 26)        # the rest: $0AF4 iterations of 26 T

# The table the code walks: turns of the delay loop, not half-periods.  0 and
# 1 stay the original's sentinels (end, rest) - a real note is 337 turns and
# up, so they cannot collide.
TURNS = [(h if h < 2 else turns_for_t(h * 26), c) for h, c in NOTES]


def music():
    """MUSIC and its note table."""
    words = "\n        .WORD   ".join(
        ",".join(f"{h}.,{c}." for h, c in TURNS[i:i + 6]) for i in range(0, len(TURNS), 6))
    return f"""        ; --- MUSIC: the intro tune ($90D1), see game_music.py.  Clobbers R0-R5. -
MUSIC:  TST     SNDENA
        BEQ     9$
        JSR     PC,SNDON
        MOV     #MUSDAT,R3
1$:     MOV     (R3)+,R1             ; the delay turns (0 = the end, 1 = a rest)
        BEQ     8$
        MOV     (R3)+,R2             ; the cycles
        CMP     R1,#1
        BNE     2$
        MOV     #{REST_TURNS}.,R0
7$:     SOB     R0,7$
        BR      1$
2$:                              ; R1 already holds the turns per half-period
3$:     JSR     PC,SPKTOG
        MOV     R1,R0
4$:     SOB     R0,4$
        JSR     PC,SPKTOG
        MOV     R1,R0
5$:     SOB     R0,5$
        SOB     R2,3$
        BR      1$
8$:     JSR     PC,SNDOFF
9$:     RTS     PC
        .EVEN
MUSDAT: .WORD   {words}
"""
