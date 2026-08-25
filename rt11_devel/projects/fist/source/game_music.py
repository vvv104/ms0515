"""The intro tune ($90D1): played at every opponent presentation ($AC69).

A record of the original's note table ($9128) is (half-period, cycles):
the beeper toggles every half-period x 26 T-states, for `cycles` periods;
half-period 1 is a rest of $0AF4 x 26 T; 0 ends the tune.  Here the
speaker is reg C bit 6 (the SNDON / SPKTOG / SNDOFF drivers of the sound
effects), and one Z80 T-state is 7.5 / 3.5 CPU cycles - a SOB turn being
~9.3 cycles, one iteration of the original's delay (26 T) is 6 turns.  The
tune is blocking and silent with the sound off (SNDENA / $B2FA), as in the
original.  The code lives in the dojo block and runs from RENDBG.

Every function returns MACRO-11 text; game_build.py assembles the game.
"""

# (half-period, cycles) from $9128: the note data the original plays.
NOTES = [(0x6D, 0x8C), (1, 0), (0x6D, 0x8C), (1, 0),
         (0x81, 0x76), (1, 0), (0x81, 0x76), (1, 0),
         (0x90, 0x69), (1, 0), (0x90, 0x69), (1, 0),
         (0x81, 0x76), (1, 0), (0x81, 0x76), (1, 0)]
NOTES += [(0x6D, 0x26), (1, 0)] * 12
NOTES += [(0x6D, 0x125), (1, 0), (0, 0)]

REST_TURNS = 0x0AF4 * 6                      # the rest: $0AF4 iterations of 26 T


def music():
    """MUSIC and its note table."""
    words = "\n        .WORD   ".join(
        ",".join(f"{h}.,{c}." for h, c in NOTES[i:i + 6]) for i in range(0, len(NOTES), 6))
    return f"""        ; --- MUSIC: the intro tune ($90D1), see game_music.py.  Clobbers R0-R5. -
MUSIC:  TST     SNDENA
        BEQ     9$
        JSR     PC,SNDON
        MOV     #MUSDAT,R3
1$:     MOV     (R3)+,R1             ; the half-period (0 = the end, 1 = a rest)
        BEQ     8$
        MOV     (R3)+,R2             ; the cycles
        CMP     R1,#1
        BNE     2$
        MOV     #{REST_TURNS}.,R0
7$:     SOB     R0,7$
        BR      1$
2$:     ASL     R1                   ; turns per half-period = half x 6
        MOV     R1,R0
        ASL     R1
        ADD     R0,R1
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
