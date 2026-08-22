"""The match structure of the 1UP game: the $AD18 round loop, the
round-end sequence, $ACA6's outcome and the $AC3E / $AB70 inits.

Every function returns MACRO-11 text; game_build.py assembles the game.
"""
from gst_addr import g


def scoring(cap):
    """SCDET ($AF01), ROUNDE ($AD18 per frame) and SCOREX ($AD1D..$AD42)."""
    return f"""        ; --- SCDET: $AF01 clean-hit detect.  ($AA03 XOR $AA43) & $10 -> R0 (nz = a
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
ROUNDE: TST     TWOUP                ; a 2UP game has its own loop ($AE14)
        BEQ     71$
        JMP     ROUND2
71$:    MOVB    {g(0x9C28)},R0
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
SCOREX: TST     DEMO                 ; the demo scores its own way ($ABC8)
        BEQ     76$
        JMP     DSCORE
76$:    JSR     PC,SCDET
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
"""


def decision(cap):
    """DECIDE ($AD44), WIN1 / WIN2 ($AE7A / $AE9D) and HOLD: the bow phase."""
    return f"""        ; --- DECIDE: $AD44 - on the clock the yin-yang totals decide, then the
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
DRAWN:  MOV     #201,RESULT          ; a draw ($81): $ACE8 - both bow ($19),
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
"""


def round_end(pause):
    """RNDEND (one frame of the round-end sequence), HOLDFR, TBONUS ($AD5F)."""
    return f"""        ; --- RNDEND: one frame of the round-end sequence (RPHASE).  2: the $AD0B
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
"""


def outcome(withbg):
    """OUTCOM ($ACA6), SETUP ($AC5F) and NEWRND ($AC9D)."""
    return f"""        ; --- OUTCOM: $ACA6, after $AD18 returns RESULT.  P1 won: pay out the clock,
        ;     count the round; the 2nd round ($AA3C) won moves to the next opponent
        ;     - rank / background ($AF27), dan $B05F (BCD; at 10 it stays and the
        ;     opponent is a random 7..10), $AA80++ - and flags the set-up.  A draw
        ;     ($81) replays the round.  P2 won: game over - a fresh 1UP game. -----
OUTCOM: TST     TWOUP                ; a 2UP game's bows were its end ($AE12):
        BEQ     3$                   ;   back to the demo
        JSR     PC,DINIT
        BR      SETUP
3$:     TST     DEMO                 ; the demo's round-end: the next dojo ($ABBB)
        BEQ     4$
        JMP     DEMEND
4$:     MOV     RESULT,R0
        CMP     R0,#1
        BEQ     1$
        CMP     R0,#201
        BEQ     SETUP                ; draw: same opponent, new round
        JSR     PC,HISCK             ; $AC39: a new high score?
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
"""


def inits():
    """RSTAI ($9D0B tail), GINIT ($AC3E) and DINIT ($AB70)."""
    return f"""        ; --- RSTAI: the $9D0B..$9D26 tail of the exchange reset.  For each
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
        CLR     TWOUP
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
        ; --- GINIT2: $AD9C Start_2UP_Game - both human, rank 0, background 2,
        ;     the scores 0 ($AF0B); the set-up flagged. -------------------------
GINIT2: JSR     PC,GINIT
        MOV     #1,TWOUP
        CLRB    {g(0xAA46)}          ; P2 human too
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
"""


def hiscore():
    """HISCK ($AC39 -> $A647 / $A697): the high score check after a game."""
    return f"""        ; --- HISCK: $AC39 -> $A647 / $A697 - a new high score?  P1's score
        ;     ($B02D..$B02F, BCD, most significant last) against the high score
        ;     ($B033..$B035, the default 1000); higher -> copied over it. ----------
HISCK:  CMPB    {g(0xB02F)},{g(0xB035)}
        BHI     1$
        BLO     9$
        CMPB    {g(0xB02E)},{g(0xB034)}
        BHI     1$
        BLO     9$
        CMPB    {g(0xB02D)},{g(0xB033)}
        BLOS    9$
1$:     MOVB    {g(0xB02D)},{g(0xB033)}
        MOVB    {g(0xB02E)},{g(0xB034)}
        MOVB    {g(0xB02F)},{g(0xB035)}
9$:     RTS     PC
"""


def twoup():
    """ROUND2 ($AE14: the 2UP round loop, per frame) and HISCK2 ($ADF3 ->
    $A647: the winner and the 2UP high score)."""
    return f"""        ; --- ROUND2: $AE14 - a 2UP game, per frame.  An exchange over (a hit,
        ;     $9C28, or the clock, $9C2B): $909E zeroes the totals and points,
        ;     $AF36 credits the points to the BCD scores; then the next exchange
        ;     ($AE26) - or, on the clock, the round is over ($ADE2): the next
        ;     background ($AF27), the rank + 1, the set-up again; after the third
        ;     round the result ($ADF3). ------------------------------------------
ROUND2: MOVB    {g(0x9C28)},R0
        BIC     #177400,R0
        BNE     1$
        MOVB    {g(0x9C2B)},R0
        BIC     #177400,R0
        BNE     1$
        RTS     PC                   ; neither -> the exchange continues
1$:     CLRB    {g(0xAA01)}          ; $909E
        CLRB    {g(0xAA41)}
        CLRB    {g(0xAA02)}
        CLRB    {g(0xAA42)}
        JSR     PC,AWARD             ; $AF36
        MOVB    {g(0x9C2B)},R0
        BIC     #177400,R0
        BNE     2$
        JSR     PC,RSTFRM            ; the next exchange
        JSR     PC,RSTAI
        RTS     PC
2$:     JSR     PC,RANKTK            ; $AF27: the next background
        MOVB    {g(0xB05F)},R0       ; the rank + 1 (0..3, no BCD needed)
        BIC     #177400,R0
        INC     R0
        MOVB    R0,{g(0xB05F)}
        CMP     R0,#4
        BEQ     3$
        MOVB    #1,{g(0xAF35)}       ; the set-up: the tune, the dojo, the prints
        JMP     SETUP
3$:     JSR     PC,HISCK2            ; $ADF3: the winner, the 2UP high score
        CMP     R0,#1
        BLO     4$
        BEQ     5$
        JMP     WIN1                 ; P1's score is higher: $AE7A
4$:     JMP     WIN2                 ; P2's: $AE9D
5$:     JMP     DRAWN                ; equal: $ACE8
        ; --- HISCK2: $A647 in a 2UP game - R0 = 2 if P1's BCD score ($B02D..)
        ;     beats P2's ($B030..), 0 if P2's beats P1's, 1 if equal ($A6B6);
        ;     the winner's score against the 2UP high score ($B036..$B038),
        ;     higher -> copied over it. ------------------------------------------
HISCK2: MOV     #2,R0
        CMPB    {g(0xB02F)},{g(0xB032)}
        BHI     1$
        BLO     2$
        CMPB    {g(0xB02E)},{g(0xB031)}
        BHI     1$
        BLO     2$
        CMPB    {g(0xB02D)},{g(0xB030)}
        BHI     1$
        BLO     2$
        MOV     #1,R0                ; equal: either against the high score
1$:     MOV     #{g(0xB02D)},R1      ; the winner's score
        BR      3$
2$:     CLR     R0
        MOV     #{g(0xB030)},R1
3$:     CMPB    2(R1),{g(0xB038)}
        BHI     4$
        BLO     9$
        CMPB    1(R1),{g(0xB037)}
        BHI     4$
        BLO     9$
        CMPB    (R1),{g(0xB036)}
        BLOS    9$
4$:     MOVB    (R1),{g(0xB036)}
        MOVB    1(R1),{g(0xB037)}
        MOVB    2(R1),{g(0xB038)}
9$:     RTS     PC
"""


def demo():
    """DSCORE ($ABC8's scoring: the demo's exchange over) and DEMEND ($ABBB:
    its round over - the next dojo, three of them, then the demo restarts)."""
    return f"""        ; --- DSCORE: $ABCD..$AC04 - the demo's exchange over: a clean hit scores
        ;     ($900E / $AF36); the one who was hit decides whose total is
        ;     checked - at two yin-yang the winner bows and the round is over;
        ;     on the clock the round is over without a bow; else the next
        ;     exchange. -----------------------------------------------------------
DSCORE: JSR     PC,SCDET
        TST     R0
        BEQ     2$
        JSR     PC,YINYNG
        JSR     PC,AWARD
        TSTB    {g(0xAA03)}          ; $ABD8: P1 was hit -> P2's total...
        BEQ     3$
        MOVB    {g(0xAA41)},R0
        BIC     #177400,R0
        CMP     R0,#4.
        BLO     2$
        JMP     WIN2                 ; $AE9D
3$:     MOVB    {g(0xAA01)},R0       ; ...else P1's
        BIC     #177400,R0
        CMP     R0,#4.
        BLO     2$
        JMP     WIN1                 ; $AE7A
2$:     CLRB    {g(0xAA08)}          ; $ABF1
        CLRB    {g(0xAA48)}
        MOVB    {g(0x9C2B)},R0
        BIC     #177400,R0
        BEQ     4$
        JMP     DEMEND               ; the clock: the round is over, no bow
4$:     JSR     PC,RSTFRM            ; the next exchange ($AE26)
        JSR     PC,RSTAI
        RTS     PC
        ; --- DEMEND: $ABBB - the demo's round over: the next background ($AF27),
        ;     the rank + 1; after the third round the demo starts over ($AC09:
        ;     new personalities, background 2). ---------------------------------
DEMEND: JSR     PC,RANKTK
        MOVB    {g(0xB05F)},R0
        BIC     #177400,R0
        INC     R0
        MOVB    R0,{g(0xB05F)}
        CMP     R0,#4
        BNE     1$
        JSR     PC,DINIT
        JMP     SETUP
1$:     MOVB    #1,{g(0xAF35)}
        JMP     SETUP
"""
