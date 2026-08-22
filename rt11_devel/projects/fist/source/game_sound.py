"""$B15A: the six beeper effects, bit-banged on reg C bit 5.

Every function returns MACRO-11 text; game_build.py assembles the game.
"""
def effects():
    """SNDFX, the effect table, SND1..SND6 and NOISE."""
    return f"""        ; --- SNDFX: $B15A, the six beeper effects, blocking as in the original.
        ;     The Spectrum bit-bangs its beeper: an effect is a run of half-periods
        ;     whose lengths come from ROM bytes masked to $7F/$3F/$FF (B x 29 T-
        ;     states each) - i.e. NOISE of a given grain - except code 4, a low
        ;     rumble of 9-bit random half-periods (26/30/38 T per unit, $B19D), and
        ;     code 5 = two deep bursts around a 61 ms pause ($B2F0).  Here the
        ;     speaker is reg C bit 5 (direct drive, tech desc 4.8) with bit 6 on
        ;     and the timer gate (7) off; an LFSR replaces the ROM bytes; one
        ;     Z80 T-state = 7.5/3.5 CPU cycles, calibrated into the delay loops.
        ;     in: R0 = code 1..6 (0 / other = no-op). -------------------------------
SNDFX:  TST     SNDENA               ; silent with the sound off ($B15D: $B2FA)
        BEQ     9$
        TST     R0
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
"""


def support():
    """SND4's rumble segments, the speaker control and the LFSR."""
    return f"""        ; SND4 ($B19D): three rumble segments of D x E cycles, half-periods of
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
"""


def sound():
    """The whole sound driver."""
    return effects() + support()
