"""$B15A: the six beeper effects, bit-banged on reg C bit 6.

The original's "noise" effects read ROM bytes from fixed addresses ($000A..
$0400) masked to $7F / $3F / $FF as half-period lengths, so every effect
sounds the same each time; the rumble (code 4) draws on a 24-bit shift
register in the game state ($B153..$B155, $B2D7).  The port embeds those
ROM bytes (the first 1 KB of the ROM, read at build time like the rest of
the art) in the dojo block and keeps the shift register in the GST, so
the effects are the original's byte for byte.  Timing: one Z80 T-state =
7.5 / 3.5 CPU cycles, calibrated into the delay loops.

Banks 0-1 being full, the driver lives in the dojo block and runs at 3177
(all slots primary, VRAM off - the display is not affected, only the CPU's
window); the stub SNDFX in banks 0-1 switches the banking and carries the
shift register (in the GST, unseen at 3177) in and out through RUMB.

Every function returns MACRO-11 text; game_build.py assembles the game.
"""
from gst_addr import g

# The ROM byte ranges each effect walks (start, end) - $B179.. / $B1FB.. .
RANGES = {1: (0x0A, 0x400), 2: (0x0A, 0x300), 3: (0xC0, 0x100),
          5: (0xE0, 0x120), "5b": (0x3C0, 0x400), 6: (0x80, 0x100)}


def rom_bytes():
    """ROMBYT: the first 1 KB of the Spectrum ROM (SkoolKit's copy)."""
    from skoolkit import ROM48, read_bin_file
    rom = read_bin_file(ROM48)[:0x400]
    rows = "\n".join("        .BYTE   " + ",".join(f"{x}." for x in rom[i:i + 16])
                      for i in range(0, len(rom), 16))
    return "        .EVEN\nROMBYT:                              ; ROM $0000..$03FF\n" + rows + "\n"


def effects():
    """SNDFX, the effect table, SND1..SND6 and NOISE."""
    r = RANGES
    return f"""        ; --- SNDGO: $B15A's effects (see game_sound.py; SNDFX in banks 0-1 is
        ;     the entry).  The speaker is reg C bit 6 (sound enable) toggled
        ;     with the timer gate (7) off - OUT held high - and bit 5 set, the
        ;     way the machine's Spectrum ports do it (BIRDS: 0x60 / 0x80).
        ;     R0 = code 1..6. -----
SNDGO:  CMP     R0,#6.
        BHI     9$
        ASL     R0
        JMP     @SNDTAB-2(R0)
9$:     RTS     PC
SNDTAB: .WORD   SND1,SND2,SND3,SND4,SND5,SND6
SND1:   MOV     #177,R1              ; $B179: mask $7F, ROM $000A..$0400 (~540 ms)
        MOV     #ROMBYT+{r[1][0]}.,R3
        MOV     #ROMBYT+{r[1][1]}.,R2
        BR      NOISE
SND2:   MOV     #77,R1               ; $B185: mask $3F, ROM $000A..$0300 (~200 ms)
        MOV     #ROMBYT+{r[2][0]}.,R3
        MOV     #ROMBYT+{r[2][1]}.,R2
        BR      NOISE
SND3:   MOV     #77,R1               ; $B191: mask $3F, ROM $00C0..$0100 (~17 ms)
        MOV     #ROMBYT+{r[3][0]}.,R3
        MOV     #ROMBYT+{r[3][1]}.,R2
        BR      NOISE
SND6:   MOV     #77,R1               ; $B221: mask $3F, ROM $0080..$0100 (~34 ms)
        MOV     #ROMBYT+{r[6][0]}.,R3
        MOV     #ROMBYT+{r[6][1]}.,R2
        BR      NOISE
SND5:   MOV     #377,R1              ; $B1FB: mask $FF, ROM $00E0..$0120 (~68 ms),
        MOV     #ROMBYT+{r[5][0]}.,R3
        MOV     #ROMBYT+{r[5][1]}.,R2
        JSR     PC,NOISE
        MOV     #61.,R3              ;   a 61 ms pause ($B2F0: $2000 x 26 T),
51$:    MOV     #800.,R4
52$:    SOB     R4,52$
        SOB     R3,51$
        MOV     #377,R1              ;   and again, ROM $03C0..$0400
        MOV     #ROMBYT+{r["5b"][0]}.,R3
        MOV     #ROMBYT+{r["5b"][1]}.,R2
        BR      NOISE
        ; NOISE: R1 = mask, R3 .. R2 = the ROM bytes ($B25B / $B297).  Each byte
        ; B (& mask, 0 -> 1): toggle the speaker, wait B x 29 T-states.
NOISE:  JSR     PC,SNDON
        COM     R1                   ; R1 = ~mask for BIC
1$:     MOVB    (R3)+,R0             ; the next ROM byte
        BIC     R1,R0                ;   & mask (the high byte goes with it)
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
        CMP     R3,R2
        BLO     1$
        JMP     SNDOFF
"""


def support():
    """SND4's rumble segments, the $B2D7 shift register, the speaker control."""
    return f"""        ; SND4 ($B19D): three rumble segments of D x E cycles, half-periods of
        ; BC x (26 / 30 / 38) T where C = $B153 | $B154 after a step of the
        ; shift register and B = C & 1 (the original's $B22D / $B244 loops with
        ; 0 / 1 / 3 NOPs of padding).
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
RHALF:  JSR     PC,RLFSR             ; C = $B153 | $B154 after a step ($B2D7)
        MOVB    RUMB,R0              ;   (the register is carried in RUMB)
        BIC     #177400,R0
        BISB    RUMB+1.,R0
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
        ; RLFSR: $B2D7 - one step of the 24-bit shift register $B153..$B155:
        ; the new top bit = bit 6 of $B155 ^ bit 5 of $B154, the three bytes
        ; shift right through each other.  Keeps R1-R4.
RLFSR:  MOV     R1,-(SP)
        MOV     R2,-(SP)
        MOV     R3,-(SP)
        MOV     R4,-(SP)
        MOVB    RUMB+1.,R0
        BIC     #177400,R0
        ASL     R0
        MOVB    RUMB+2.,R1
        BIC     #177400,R1
        XOR     R0,R1                ; R1 ^= R0: bit 6 = the new bit
        MOVB    RUMB,R2
        BIC     #177400,R2
        MOVB    RUMB+1.,R3
        BIC     #177400,R3
        MOVB    RUMB+2.,R4
        BIC     #177400,R4
        ASR     R4
        BIT     #1,R3
        BEQ     1$
        BIS     #200,R4
1$:     ASR     R3
        BIT     #1,R2
        BEQ     2$
        BIS     #200,R3
2$:     ASR     R2
        BIT     #100,R1
        BEQ     3$
        BIS     #200,R2
3$:     MOVB    R2,RUMB
        MOVB    R3,RUMB+1.
        MOVB    R4,RUMB+2.
        MOV     (SP)+,R4
        MOV     (SP)+,R3
        MOV     (SP)+,R2
        MOV     (SP)+,R1
        RTS     PC
        ; speaker control via the reg C shadow: SNDON = bits 6 and 5 on, 7
        ; off (0x60, the speaker high); SPKTOG flips bit 6; SNDOFF = bits
        ; 5-7 off (silence).
SNDON:  MOVB    RCSHAD,R4
        BIC     #177640,R4
        BIS     #140,R4
        MOVB    R4,RCSHAD
        MOVB    R4,@#SYSC
        RTS     PC
SPKTOG: MOVB    RCSHAD,R4
        BIC     #177400,R4
        MOV     #100,R5
        XOR     R5,R4
        MOVB    R4,RCSHAD
        MOVB    R4,@#SYSC
        RTS     PC
SNDOFF: MOVB    RCSHAD,R4
        BIC     #177740,R4
        MOVB    R4,RCSHAD
        MOVB    R4,@#SYSC
        RTS     PC
        .EVEN
"""


def stub():
    """SNDFX (banks 0-1): the entry - the banking, the shift register in and
    out of the GST - around SNDGO in the dojo block."""
    return f"""        ; --- SNDFX: $B15A - the six beeper effects, blocking as in the original
        ;     (game_sound.py): the driver runs in the dojo block at 3177, the
        ;     rumble's shift register ($B153..$B155) rides in RUMB.
        ;     in: R0 = code 1..6 (0 / other = no-op). -------------------------------
SNDFX:  TST     SNDENA               ; silent with the sound off ($B15D: $B2FA)
        BEQ     9$
        TST     R0
        BEQ     9$
        MOVB    {g(0xB153)},RUMB
        MOVB    {g(0xB154)},RUMB+1.
        MOVB    {g(0xB155)},RUMB+2.
        MOV     #3177,@#DISPAT
        JSR     PC,SNDGO
        MOV     #GAME,@#DISPAT
        MOVB    RUMB,{g(0xB153)}
        MOVB    RUMB+1.,{g(0xB154)}
        MOVB    RUMB+2.,{g(0xB155)}
9$:     RTS     PC
"""


def driver():
    """The driver in the dojo block: the effects, the rumble, the speaker
    control, the ROM bytes."""
    return effects() + support() + rom_bytes()
