"""The text of the game: the status strip, the settings screens, the border.

All of it prints the way the original does ($923A / $92CA): glyphs from the
Spectrum ROM font (or a 2x2 UDG) written as PIXEL bytes at a (column, pixel
row) position, the attributes untouched, so text sits on the dojo's own
colours.  Positions are the original's, mapped into the centred picture
(VRAM row = y + 4, cell = column + 4).

This code lives in the dojo block (banks 4-6 primary) and runs at 03377 -
VRAM on, the GST unseen - so everything it shows comes from the stashes the
frame loop keeps in banks 0-1 (SCRBCD, STIM, SC1 / SC2, RANKB, HISC, DEMO).

Every function returns MACRO-11 text; game_build.py assembles the game.
"""


def at(col, y):
    """The VRAM byte offset of the original's (column, pixel row)."""
    return (y + 4) * 80 + (col + 4) * 2


def byte_rows(b):
    """.BYTE rows of `b`, eight per line.  MACRO-11 numbers default to OCTAL -
    suffix each byte with '.' for decimal."""
    return "\n".join("        .BYTE   " + ",".join(f"{x}." for x in b[i:i + 8])
                     for i in range(0, len(b), 8))


def rom_font():
    """The ROM font, characters 32..127 (glyph = ROM $3D00 + (code - 32) * 8)
    - read from SkoolKit's copy of the ROM at build time, an external
    resource like the rest of the art."""
    from skoolkit import ROM48, read_bin_file
    rom = read_bin_file(ROM48)
    return list(rom[0x3D00:0x3D00 + 96 * 8])


def asciz(label, s):
    """A 0-terminated string (plain ASCII, as the original's)."""
    assert "/" not in s and s.isascii(), s
    return f"{label}: .ASCIZ  /{s}/\n"


def printer():
    """PRTXT (a string), PRUDG (a 2x2 UDG), their 8-row cell writer."""
    return """        ; --- PRTXT: print the ASCIZ string at R1 at the VRAM cell R0 - pixel
        ;     bytes only, the attributes stay ($923A).  Clobbers R1-R4. ---------
PRTXT:  MOVB    (R1)+,R2
        BEQ     9$
        BIC     #177400,R2
        SUB     #32.,R2
        ASL     R2
        ASL     R2
        ASL     R2
        ADD     #FONT,R2
        MOV     R0,R3
        MOV     #8.,R4
1$:     MOVB    (R2)+,(R3)
        ADD     #80.,R3
        DEC     R4
        BNE     1$
        ADD     #2,R0
        BR      PRTXT
9$:     RTS     PC
        ; --- PRUDG: the 2x2 UDG block at R1 (32 bytes: top-left, top-right,
        ;     bottom-left, bottom-right - $9255's order) at the cell R0. -------
PRUDG:  MOV     R0,R3
        JSR     PC,PR8
        MOV     R0,R3
        ADD     #2,R3
        JSR     PC,PR8
        MOV     R0,R3
        ADD     #640.,R3
        JSR     PC,PR8
        MOV     R0,R3
        ADD     #642.,R3
PR8:    MOV     #8.,R4               ; 8 pixel rows of (R1)+ into the cell R3
1$:     MOVB    (R1)+,(R3)
        ADD     #80.,R3
        DEC     R4
        BNE     1$
        RTS     PC
"""


def numbers():
    """BCD6 (a score: six BCD digits + "00", leading zeros as spaces - $AFDA /
    $AFED / $AFF3), BCD2 (one BCD byte), DEC5 (the clock - $9309 / $9345),
    all into NBUF."""
    return """        ; --- BCD6: the 3-byte BCD number at R1 (least significant first) ->
        ;     NBUF as the original's score text: six digits and "00", the
        ;     leading zeros as spaces ($AFDA / $AFED / $AFF3). -------------------
BCD6:   MOV     #NBUF,R2
        MOVB    2(R1),R0
        JSR     PC,BCD2W
        MOVB    1(R1),R0
        JSR     PC,BCD2W
        MOVB    (R1),R0
        JSR     PC,BCD2W
        MOVB    #60,(R2)+
        MOVB    #60,(R2)+
        CLRB    (R2)
        MOV     #7,R3                ; the first seven: "0" -> " " up to a digit
ZSPC:   MOV     #NBUF,R2
1$:     CMPB    (R2),#60
        BNE     9$
        MOVB    #40,(R2)+
        DEC     R3
        BNE     1$
9$:     RTS     PC
        ; --- BCD2: the BCD byte R0 -> NBUF as two characters, a leading zero as
        ;     a space (B=1 $AFDA + $AFF3). ------------------------------------------
BCD2:   MOV     #NBUF,R2
        JSR     PC,BCD2W
        CLRB    (R2)
        MOV     #1,R3
        BR      ZSPC
BCD2W:  BIC     #177400,R0           ; two digits of the BCD byte R0 at (R2)+
        MOV     R0,R3
        ASR     R3
        ASR     R3
        ASR     R3
        ASR     R3
        ADD     #60,R3
        MOVB    R3,(R2)+
        BIC     #177760,R0
        ADD     #60,R0
        MOVB    R0,(R2)+
        RTS     PC
        ; --- DEC5: R0 (0..65535) -> NBUF as five characters, the leading zeros
        ;     as spaces but the last ($9309 / $9345). -----------------------------
DEC5:   MOV     #NBUF,R2
        MOV     #10000.,R1
        JSR     PC,DIGIT
        MOV     #1000.,R1
        JSR     PC,DIGIT
        MOV     #100.,R1
        JSR     PC,DIGIT
        MOV     #10.,R1
        JSR     PC,DIGIT
        MOV     #1,R1
        JSR     PC,DIGIT
        CLRB    (R2)
        MOV     #4,R3
        BR      ZSPC
DIGIT:  MOV     #57,R3               ; one digit: count R1s out of R0
1$:     INC     R3
        SUB     R1,R0
        BCC     1$
        ADD     R1,R0
        MOVB    R3,(R2)+
        RTS     PC
"""


def strip():
    """The status strip: HUDALL at a set-up ($AC5F / $AB70), HUDSCR / HUDTIM /
    HUDYY when a value changes ($AF36 / $9C93 / $900E)."""
    return f"""        ; --- HUDALL: the strip after a dojo draw - the set-up's prints ($AC5F:
        ;     the score, "1 PLAYER", the high score, the rank; the demo $AB70:
        ;     "DEMO" and the high score instead), the clock ($AEF8) and the
        ;     yin-yang area ($909E). ---------------------------------------------
HUDALL: TST     DEMO
        BNE     1$
        JSR     PC,HUDSCR
        TST     TWOUP                ; a 2UP game: player 2's score too ($AF97)
        BEQ     3$
        JSR     PC,HUDSC2
3$:     MOV     TWOUP,R0             ; $98BF: " 1" / " 2" and "PLAYER"
        INC     R0
        JSR     PC,BCD2
        MOV     #VRAM+{at(0, 25)}.,R0
        MOV     #NBUF,R1
        JSR     PC,PRTXT
        MOV     #VRAM+{at(3, 25)}.,R0
        MOV     #PLAYER,R1
        JSR     PC,PRTXT
        JSR     PC,HUDRNK
        BR      2$
1$:     MOV     #VRAM+{at(2, 34)}.,R0  ; $AB9D
        MOV     #DEMOS,R1
        JSR     PC,PRTXT
2$:     MOV     #HISC,R1             ; $A685: the high score (a 2UP game has its
        TST     TWOUP                ;   own, $B036..$B038)
        BEQ     4$
        MOV     #HISC2,R1
4$:     JSR     PC,BCD6
        MOV     #VRAM+{at(10, 25)}.,R0
        MOV     #NBUF,R1
        JSR     PC,PRTXT
        JSR     PC,HUDTIM
        JMP     HUDYY
        ; --- HUDSCR: P1's score ($AF68) at (1, 0). -------------------------------
HUDSCR: MOV     #SCRBCD,R1
        JSR     PC,BCD6
        MOV     #VRAM+{at(1, 0)}.,R0
        MOV     #NBUF,R1
        JMP     PRTXT
        ; --- HUDSC2: P2's score ($AFAD) at (22, 0). ------------------------------
HUDSC2: MOV     #SC2BCD,R1
        JSR     PC,BCD6
        MOV     #VRAM+{at(22, 0)}.,R0
        MOV     #NBUF,R1
        JMP     PRTXT
        ; --- HUDTIM: the clock ($9C93) at (11, 0). ------------------------------
HUDTIM: MOVB    STIM,R0
        BIC     #177400,R0
        JSR     PC,DEC5
        MOV     #VRAM+{at(11, 0)}.,R0
        MOV     #NBUF,R1
        JMP     PRTXT
        ; --- HUDRNK: the rank line at y = 34 ($AC80: the dan digits at column 0,
        ;     $AEBF: "NOVICE " at 1, else the suffix at 2 and " DAN" at 4). -------
HUDRNK: MOVB    RANKB,R0
        BIC     #177740,R0           ; $AEC5: rank & $1F
        MOV     R0,R5
        JSR     PC,BCD2
        MOV     #VRAM+{at(0, 34)}.,R0
        MOV     #NBUF,R1
        JSR     PC,PRTXT
        TST     R5
        BNE     1$
        MOV     #VRAM+{at(1, 34)}.,R0
        MOV     #NOVICE,R1
        JMP     PRTXT
1$:     MOV     #SFXTH,R1
        CMP     R5,#1
        BNE     2$
        MOV     #SFXST,R1
2$:     CMP     R5,#2
        BNE     3$
        MOV     #SFXND,R1
3$:     CMP     R5,#3
        BNE     4$
        MOV     #SFXRD,R1
4$:     MOV     #VRAM+{at(2, 34)}.,R0
        JSR     PC,PRTXT
        MOV     #VRAM+{at(4, 34)}.,R0
        MOV     #DANS,R1
        JMP     PRTXT
"""


def yinyang():
    """HUDYY: the yin-yang area blanked ($909E) and the symbols of both
    totals ($900E: the inner slot fills first)."""
    return f"""        ; --- HUDYY: $909E blanks the two 9-cell areas of each player, then the
        ;     symbols of the totals SC1 / SC2 ($900E): the inner slot (column 4 /
        ;     26) takes the first two points, the outer (1 / 29) the next two;
        ;     one point = the half symbol, two = the full one. -------------------
HUDYY:  MOV     #VRAM+{at(1, 8)}.,R0
        MOV     #BLANK9,R1
        JSR     PC,PRTXT
        MOV     #VRAM+{at(1, 16)}.,R0
        MOV     #BLANK9,R1
        JSR     PC,PRTXT
        MOV     #VRAM+{at(26, 8)}.,R0
        MOV     #BLANK9,R1
        JSR     PC,PRTXT
        MOV     #VRAM+{at(26, 16)}.,R0
        MOV     #BLANK9,R1
        JSR     PC,PRTXT
        MOVB    SC1,R5
        BIC     #177400,R5
        MOV     #VRAM+{at(4, 8)}.,R0
        MOV     #VRAM+{at(1, 8)}.,R2
        JSR     PC,YYPAIR
        MOVB    SC2,R5
        BIC     #177400,R5
        MOV     #VRAM+{at(26, 8)}.,R0
        MOV     #VRAM+{at(29, 8)}.,R2
        ; YYPAIR: the total R5 as the inner symbol at R0 and the outer at R2
YYPAIR: MOV     R2,-(SP)
        MOV     R5,R4                ; inner = min(2, total)
        CMP     R4,#2
        BLE     1$
        MOV     #2,R4
1$:     JSR     PC,YYSLOT
        MOV     (SP)+,R0
        MOV     R5,R4                ; outer = max(0, total - 2)
        SUB     #2,R4
        BGT     YYSLOT
        RTS     PC
YYSLOT: MOV     #YYHALF,R1           ; the level R4 (1 half / 2 full / else none) at R0
        CMP     R4,#1
        BEQ     1$
        MOV     #YYFULL,R1
        CMP     R4,#2
        BEQ     1$
        RTS     PC
1$:     JMP     PRUDG
"""


def screen():
    """CLS ($8E4C: the picture blank, cyan on blue), BORDER (the cyan border
    $9C2F sets: the margins around the centred picture), KGET (the next
    key's make code, for the settings screens)."""
    return """        ; --- CLS: $8E4C - the picture's pixels cleared, its attributes $0D
        ;     (cyan ink on blue paper). --------------------------------------------
CLS:    MOV     #VRAM+328.,R0        ; row 4, cell 4
        MOV     #192.,R1
1$:     MOV     #32.,R2
2$:     MOV     #6400,(R0)+
        DEC     R2
        BNE     2$
        ADD     #16.,R0              ; the two 4-cell margins
        DEC     R1
        BNE     1$
        RTS     PC
        ; --- BORDER: the original's cyan border ($9C2F) on the margins of the
        ;     320x200 screen around the 256x192 picture: the 4 rows above and
        ;     below, the 4 cells left and right (paper cyan, no pixels). --------
BORDER: MOV     #VRAM,R0
        MOV     #160.,R1             ; rows 0-3
1$:     MOV     #24000,(R0)+
        DEC     R1
        BNE     1$
        MOV     #192.,R1             ; rows 4-195: the side margins
2$:     MOV     #24000,(R0)+
        MOV     #24000,(R0)+
        MOV     #24000,(R0)+
        MOV     #24000,(R0)+
        ADD     #64.,R0
        MOV     #24000,(R0)+
        MOV     #24000,(R0)+
        MOV     #24000,(R0)+
        MOV     #24000,(R0)+
        DEC     R1
        BNE     2$
        MOV     #160.,R1             ; rows 196-199
3$:     MOV     #24000,(R0)+
        DEC     R1
        BNE     3$
        RTS     PC
        ; --- KGET: wait for a key and return its make code in R0 (the MS7004's
        ;     auto-repeat 0254 and ALL-UP 0263 are not keys). -------------------
KGET:   MOVB    @#177442,R0
        BITB    #2,R0
        BEQ     KGET
        MOVB    @#177440,R0
        BIC     #177400,R0
        CMP     R0,#254
        BEQ     KGET
        CMP     R0,#263
        BEQ     KGET
        RTS     PC
"""


def settings():
    """SETTNG: the settings screen ($8C54) - any choice returns to the demo;
    SETCTL: the controls menu ($8CDB); SETKEY: the key redefinition ($8D99)."""
    return settings_main() + settings_controls() + settings_keys()


def settings_main():
    """SETTNG: the settings screen ($8C54)."""
    return f"""        ; --- SETTNG: "0" in the demo - $8C54's settings screen.  "1" / "2": the
        ;     controls of player 1 / 2; "3" / "4": the sound on / off; "E": back.
        ;     As in the original, one choice and the screen is over (the demo
        ;     restarts).  Keys: 1 0300, 2 0305, 3 0313, 4 0320, 5 0325, E 0327. -----
SETTNG: JSR     PC,CLS
        MOV     #VRAM+{at(0, 45)}.,R0
        MOV     #STXT1,R1
        JSR     PC,PRTXT
        MOV     #VRAM+{at(20, 54)}.,R0
        MOV     #STXT2,R1
        JSR     PC,PRTXT
        MOV     #VRAM+{at(20, 63)}.,R0
        MOV     #STXT3,R1
        JSR     PC,PRTXT
        MOV     #VRAM+{at(20, 72)}.,R0
        MOV     #STXT4,R1
        JSR     PC,PRTXT
        MOV     #VRAM+{at(5, 92)}.,R0
        MOV     #STXT5,R1
        JSR     PC,PRTXT
        MOV     #VRAM+{at(7, 101)}.,R0
        MOV     #STXT6,R1
        JSR     PC,PRTXT
1$:     JSR     PC,KGET
        CMP     R0,#300              ; "1": player 1's controls
        BNE     2$
        CLR     SETPLY
        BR      SETCTL
2$:     CMP     R0,#305              ; "2": player 2's (kept for a 2UP game)
        BNE     3$
        MOV     #1,SETPLY
        BR      SETCTL
3$:     CMP     R0,#327              ; "E": back to the demo
        BEQ     9$
        CMP     R0,#313              ; "3": sound on
        BNE     4$
        MOV     #1,SNDENA
        RTS     PC
4$:     CMP     R0,#320              ; "4": sound off
        BNE     1$
        CLR     SNDENA
9$:     RTS     PC
"""


def settings_controls():
    """SETCTL: the controls menu ($8CDB)."""
    return f"""        ; --- SETCTL: $8CDB - the controls menu.  "1": the default keys; "4":
        ;     redefine them; "5": the Kempston joystick - the one on the MS7007
        ;     port (JOY1 / JOY2, read by KCTRL); "2" / "3" (the Sinclair
        ;     joysticks the MS-0515 has not) fall back to the default keys. ----
SETCTL: JSR     PC,CLS
        MOV     #VRAM+{at(1, 45)}.,R0
        MOV     #CTXT1,R1
        JSR     PC,PRTXT
        MOV     #VRAM+{at(1, 54)}.,R0
        MOV     #CTXT2,R1
        JSR     PC,PRTXT
        MOV     #VRAM+{at(1, 63)}.,R0
        MOV     #CTXT3,R1
        JSR     PC,PRTXT
        MOV     #VRAM+{at(1, 72)}.,R0
        MOV     #CTXT4,R1
        JSR     PC,PRTXT
        MOV     #VRAM+{at(1, 81)}.,R0
        MOV     #CTXT5,R1
        JSR     PC,PRTXT
        MOV     #VRAM+{at(3, 113)}.,R0
        MOV     #CTXT6,R1
        JSR     PC,PRTXT
1$:     JSR     PC,KGET
        CMP     R0,#320              ; "4": redefine the keys
        BEQ     SETKEY
        CMP     R0,#325              ; "5": the Kempston joystick
        BEQ     5$
        CMP     R0,#300
        BEQ     2$
        CMP     R0,#305
        BEQ     2$
        CMP     R0,#313
        BNE     1$
2$:     CLR     R1                   ; the keys alone
        BR      4$
5$:     MOV     #1,R1                ; the keys and the joystick
4$:     TST     SETPLY               ; the default keys (DEF1: the keypad, the
        BNE     3$                   ;   arrows, Space / VR / SU; DEF2: Q W E /
        MOV     #DEF1,KMAP1          ;   A S D / Z X C) and the joystick flag
        MOV     R1,JOY1
        RTS     PC
3$:     MOV     #DEF2,KMAP2
        MOV     R1,JOY2
        RTS     PC
"""


def settings_keys():
    """SETKEY: the key redefinition ($8D99) - the player's nine keys, which
    then replace its default table."""
    return f"""        ; --- SETKEY: $8D99 - for each of the nine controls, show its name at
        ;     (5, 92), take the next key, blank the name.  Player 1's keys then
        ;     replace the default set (KEYMODE). ------------------------------------
SETKEY: JSR     PC,CLS
        MOV     #VRAM+{at(1, 45)}.,R0
        MOV     #KTXT1,R1
        JSR     PC,PRTXT
        MOV     #VRAM+{at(1, 54)}.,R0
        MOV     #KTXT2,R1
        JSR     PC,PRTXT
        MOV     #VRAM+{at(1, 63)}.,R0
        MOV     #KTXT3,R1
        JSR     PC,PRTXT
        MOV     #KEYTAB,R5
        TST     SETPLY
        BEQ     1$
        MOV     #KEYTB2,R5
1$:     MOV     #KNAMES,R3
        MOV     #9.,R4
2$:     MOV     #VRAM+{at(5, 92)}.,R0
        MOV     (R3)+,R1
        JSR     PC,PRTXT
        JSR     PC,KGET
        MOVB    R0,(R5)              ; the pair's scancode (its bits are fixed)
        ADD     #2,R5
        MOV     #VRAM+{at(5, 92)}.,R0
        MOV     #BLNK12,R1
        JSR     PC,PRTXT
        DEC     R4
        BNE     2$
        TST     SETPLY
        BNE     9$
        MOV     #KEYTAB,KMAP1
        RTS     PC
9$:     MOV     #KEYTB2,KMAP2
        RTS     PC
"""


def data(snap):
    """The strings (the original's, $8E6B.. / $B039..), the ROM font, the
    yin-yang symbols ($928A full, $92AA half, from the snapshot), NBUF."""
    names = ["UP", "UP-RIGHT", "RIGHT", "DOWN-RIGHT", "DOWN", "DOWN-LEFT",
             "LEFT", "UP-LEFT", "FIRE-BUTTON"]
    out = "        .EVEN\n"
    out += asciz("PLAYER", "PLAYER") + asciz("DEMOS", "DEMO")
    out += asciz("NOVICE", "NOVICE ") + asciz("SFXST", "ST") + asciz("SFXND", "ND")
    out += asciz("SFXRD", "RD") + asciz("SFXTH", "TH") + asciz("DANS", " DAN")
    out += asciz("BLANK9", " " * 9) + asciz("BLNK12", " " * 12)
    out += asciz("STXT1", "CHANGE CONTROLS FOR PLAYER 1 (1)")
    out += asciz("STXT2", "PLAYER 2 (2)") + asciz("STXT3", "SOUND ON (3)")
    out += asciz("STXT4", "SOUND OFF(4)") + asciz("STXT5", "(TYPE 1,2,3 OR 4.)")
    out += asciz("STXT6", "TYPE E TO EXIT.")
    out += asciz("CTXT1", "1:USE DEFAULT KEYBOARD LAYOUT")
    out += asciz("CTXT2", "2:USE SINCLAIR JOYSTICK #1")
    out += asciz("CTXT3", "3:USE SINCLAIR JOYSTICK #2")
    out += asciz("CTXT4", "4:RECONFIGURE KEYBOARD")
    out += asciz("CTXT5", "5:USE KEMPSTON JOYSTICK")
    out += asciz("CTXT6", "TYPE 1,2,3,4 OR 5")
    out += asciz("KTXT1", "FOR EACH DIRECTION INDICATED,")
    out += asciz("KTXT2", "PRESS THE KEY YOU WISH TO USE")
    out += asciz("KTXT3", "FOR THAT DIRECTION.")
    out += "".join(asciz(f"KNAM{i}", n) for i, n in enumerate(names))
    out += "        .EVEN\nKNAMES: .WORD   " + ",".join(f"KNAM{i}" for i in range(9)) + "\n"
    out += "NBUF:   .BLKB   10.\n        .EVEN\n"
    out += "FONT:                                ; the ROM font, characters 32..127\n"
    out += byte_rows(rom_font()) + "\n"
    out += "YYFULL:\n" + byte_rows(snap[0x928A:0x928A + 32]) + "\n"
    out += "YYHALF:\n" + byte_rows(snap[0x92AA:0x92AA + 32]) + "\n"
    return out


def all_text(snap):
    """Everything the dojo block carries for the text."""
    return (printer() + numbers() + strip() + yinyang() + screen() + settings()
            + data(snap))


def stubs():
    """The no-background build has no dojo block to hold the text: the entry
    points exist and do nothing."""
    return """HUDALL:
HUDYY:
HUDSCR:
HUDTIM:
BORDER:
SETTNG: RTS     PC
"""
