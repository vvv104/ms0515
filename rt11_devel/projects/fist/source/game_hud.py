"""The status strip: yin-yang score, rank, six-digit score, clock - drawn
in the Spectrum ROM font, as the original prints it.

Every function returns MACRO-11 text; game_build.py assembles the game.
"""
from gst_addr import g

# The status text is set in the Spectrum ROM font, as the original's $923A
# prints it (glyph = ROM $3D00 + (code - 32) * 8).  The ROM is an external
# resource read at build time (SkoolKit's copy), like the rest of the art;
# only the codes the strip uses are embedded: digits 0-9 (codes 0-9), space
# (10), A-Z (11-36).
FONT_ORDER = "0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ"


def byte_rows(b):
    """.BYTE rows of `b`, eight per line.  MACRO-11 numbers default to OCTAL -
    suffix each byte with '.' for decimal."""
    return "\n".join("        .BYTE   " + ",".join(f"{x}." for x in b[i:i + 8])
                     for i in range(0, len(b), 8))


def font_code(ch):
    """The font code of character `ch`."""
    return FONT_ORDER.index(ch)


def strb(label, s):
    """A 377-terminated string of font codes."""
    codes = ",".join(f"{font_code(c)}." for c in s.upper())
    return f"{label}: .BYTE   {codes},377\n        .EVEN"


def rom_font():
    """The ROM glyphs of FONT_ORDER, 8 bytes each."""
    from skoolkit import ROM48, read_bin_file
    rom = read_bin_file(ROM48)
    return [b for ch in FONT_ORDER
            for b in rom[0x3D00 + (ord(ch) - 32) * 8:0x3D00 + (ord(ch) - 31) * 8]]


def data(snap):
    """(font, yin-yang full, yin-yang half) as .BYTE rows.  The yin-yang
    symbols (2x2 UDGs) come from the snapshot ($928A full, $92AA half),
    embedded as data like the rest of the art."""
    font = rom_font()
    return (byte_rows(font), byte_rows(snap[0x928A:0x928A + 32]),
            byte_rows(snap[0x92AA:0x92AA + 32]))


def draw(cell_restore, ovl_ink):
    """DRAW1U / DRAWYY / DYYSL / HUD: the yin-yang score symbols."""
    return f"""        ; --- HUD: the yin-yang score in a top status strip (rows 0-15).  Each fighter
        ;     has two yin-yang slots that fill half then full as its score (SC1/SC2,
        ;     stashed from $AA01/$AA41) climbs 0..4.  The real 2x2-UDG symbol
        ;     (YYFULL/YYHALF, from the snapshot) is drawn white over the cleared strip.
DRAW1U: MOV     #8.,R3               ; one UDG cell: 8 pixel rows of (R1)+ pixels,
1$:     {cell_restore}
        MOVB    (R1)+,R4             ; then the glyph, transparent over the clean cell
        BIC     #177400,R4
        BEQ     2$                   ; blank glyph row -> the clean cell stays
        BISB    R4,(R0)              ; OR the symbol pixels onto the background cell
        {ovl_ink}
2$:     ADD     #80.,R0
        DEC     R3
        BNE     1$
        RTS     PC
DRAWYY: MOV     R2,R0                ; draw the 2x2 symbol (R1 = 32-byte block) at cell R2
        JSR     PC,DRAW1U            ; top-left
        MOV     R2,R0
        ADD     #2,R0
        JSR     PC,DRAW1U            ; top-right
        MOV     R2,R0
        ADD     #640.,R0             ; +8 rows
        JSR     PC,DRAW1U            ; bottom-left
        MOV     R2,R0
        ADD     #642.,R0
        JSR     PC,DRAW1U            ; bottom-right
        RTS     PC
DYYSL:  MOV     #YYNONE,R1           ; R4 = level (0 none / 1 half / >=2 full), R2 = pos
        TST     R4
        BEQ     8$                   ; empty -> the blank symbol (restores the dojo)
        MOV     #YYHALF,R1           ; half point
        CMP     R4,#2.
        BLO     8$
        MOV     #YYFULL,R1           ; full point
8$:     JSR     PC,DRAWYY
        RTS     PC
        ; No strip clear (that black-flash caused the flicker); each of the four
        ; slots is always drawn, in the top corners INSIDE the dojo (row 6, over the
        ; sky, above the fighters' redraw band), so drawing straight to VRAM is stable.
HUD:    MOVB    SC1,R4               ; P1 slot 0 = min(2, score)
        BIC     #177400,R4
        CMP     R4,#2.
        BLE     2$
        MOV     #2.,R4
2$:     MOV     #VRAM+496.,R2        ; P1 first yin-yang: Spectrum col 4 (the inner one)
        JSR     PC,DYYSL
        MOVB    SC1,R4               ; P1 slot 1 = score - 2
        BIC     #177400,R4
        SUB     #2.,R4
        BGT     3$
        CLR     R4
3$:     MOV     #VRAM+490.,R2        ; P1 second yin-yang: Spectrum col 1 (fills 3rd/4th)
        JSR     PC,DYYSL
        MOVB    SC2,R4               ; P2 slot 0
        BIC     #177400,R4
        CMP     R4,#2.
        BLE     5$
        MOV     #2.,R4
5$:     MOV     #VRAM+540.,R2        ; row 6, col 30 (top-right)
        JSR     PC,DYYSL
        MOVB    SC2,R4               ; P2 slot 1
        BIC     #177400,R4
        SUB     #2.,R4
        BGT     6$
        CLR     R4
6$:     MOV     #VRAM+546.,R2        ; row 6, col 33
        JSR     PC,DYYSL
        RTS     PC
"""


def text():
    """DRWDIG / DRWSTR / DRWSCR / BIN2 / DRWTIM: the score and the clock."""
    return f"""        ; --- DRWDIG: draw digit R4 (0..9) as an 8x8 glyph at VRAM cell R2. ---------
DRWDIG: MOV     R4,R1
        ASL     R1
        ASL     R1
        ASL     R1                   ; code * 8
        ADD     #DIGFNT,R1
        MOV     R2,R0
        JSR     PC,DRAW1U
        RTS     PC
        ; --- DRWSTR: draw the font-code string at R1 (ending 377) at VRAM cell R2. ---
DRWSTR: MOVB    (R1)+,R4
        BIC     #177400,R4           ; zero-extend the code
        CMP     R4,#255.
        BEQ     9$                   ; 377 terminator
        MOV     R1,-(SP)             ; DRWDIG clobbers R1 (glyph ptr)
        JSR     PC,DRWDIG
        MOV     (SP)+,R1
        ADD     #2,R2                ; next cell
        BR      DRWSTR
9$:     RTS     PC
        ; --- DRWSCR: draw P1's six-digit BCD score (SCRBCD, stashed from $B02D) across
        ;     the top strip (row 6, centre - clear of the corner yin-yang symbols).
        ;     The glyphs are the Spectrum ROM font's, as the original prints. --------
DRWSCR: MOV     #VRAM+520.,R2        ; row 6, cells 20-25 (centre)
        MOV     #2.,R5               ; BCD byte index 2..0 (most significant first)
1$:     MOVB    SCRBCD(R5),R0        ; a packed BCD byte = two digits
        BIC     #177400,R0
        MOV     R0,R4
        ASR     R4                   ; high nibble (R0 is 0..255, so ASR fills zeros)
        ASR     R4
        ASR     R4
        ASR     R4
        JSR     PC,DRWDIG
        ADD     #2,R2                ; next character cell
        MOVB    SCRBCD(R5),R4
        BIC     #177760,R4           ; low nibble
        JSR     PC,DRWDIG
        ADD     #2,R2
        DEC     R5
        BGE     1$
        RTS     PC
        ; --- BIN2: split R0 (0..99) into R3 = tens, R0 = ones (no EIS DIV on core). -
BIN2:   CLR     R3
2$:     CMP     R0,#10.
        BLO     3$
        SUB     #10.,R0
        INC     R3
        BR      2$
3$:     RTS     PC
        ; --- DRWTIM: draw the round timer (STIM, stashed from $9CA5) as two digits
        ;     in the top strip, to the right of the score. --------------------------
DRWTIM: MOVB    STIM,R0
        BIC     #177400,R0
        JSR     PC,BIN2              ; R3 = tens, R0 = ones
        MOV     R0,-(SP)             ; DRWDIG (via DRAW1U) clobbers R0 - save the ones
        MOV     #VRAM+534.,R2        ; row 6, cells 27-28 (between score and P2 yin-yang)
        MOV     R3,R4
        JSR     PC,DRWDIG            ; tens digit
        ADD     #2,R2
        MOV     (SP)+,R4             ; ones digit
        JSR     PC,DRWDIG
        RTS     PC
"""


def rank(font_s, yyfull_s, yyhalf_s):
    """DRWRNK (the rank text, $AEBF), the strings, the font and the symbols."""
    return f"""        ; --- DRWRNK: the rank text ($AEBF): "NOVICE" at 0, else "<n>ST|ND|RD|TH DAN"
        ;     from RANKB (the BCD $B05F), built as font codes in RANKS and padded
        ;     to 9 cells so a shorter rank overwrites a longer one. ----------------
DRWRNK: MOVB    RANKB,R0
        BIC     #177400,R0
        CMP     R0,#377
        BNE     11$
        MOV     #DEMSTR,R1           ; the attract demo
        BR      8$
11$:    TST     R0
        BNE     1$
        MOV     #NOVSTR,R1
        BR      8$
1$:     MOV     #RANKS,R1
        MOV     R0,R2                ; tens digit (BCD high nibble), if any
        ASR     R2
        ASR     R2
        ASR     R2
        ASR     R2
        BEQ     2$
        MOVB    R2,(R1)+
2$:     MOV     R0,R2                ; units digit
        BIC     #177760,R2
        MOVB    R2,(R1)+
        BIC     #177740,R0           ; $AEC5: rank & $1F picks the suffix
        MOV     #SFXTH,R2
        CMP     R0,#1
        BNE     3$
        MOV     #SFXST,R2
3$:     CMP     R0,#2
        BNE     4$
        MOV     #SFXND,R2
4$:     CMP     R0,#3
        BNE     5$
        MOV     #SFXRD,R2
5$:     MOVB    (R2)+,(R1)+          ; the two suffix letters
        MOVB    (R2)+,(R1)+
        MOVB    #10.,(R1)+           ; " DAN"
        MOVB    #{font_code('D')}.,(R1)+
        MOVB    #{font_code('A')}.,(R1)+
        MOVB    #{font_code('N')}.,(R1)+
        MOVB    #10.,(R1)+           ; pad (a 7-cell rank covers an 8-cell one)
        MOVB    #377,(R1)+
        MOV     #RANKS,R1
8$:     MOV     #VRAM+502.,R2        ; row 6, cells 11-19 (right of the P1 yin-yang)
        JSR     PC,DRWSTR
        RTS     PC
        .EVEN
{strb("NOVSTR", "NOVICE   ")}
{strb("DEMSTR", "DEMO     ")}
{strb("SFXST", "ST")}
{strb("SFXND", "ND")}
{strb("SFXRD", "RD")}
{strb("SFXTH", "TH")}
        .EVEN
DIGFNT:                              ; codes 0-9 digits, 10 space, 11-36 A-Z
{font_s}
        .EVEN
YYFULL:
{yyfull_s}
YYHALF:
{yyhalf_s}
YYNONE: .BLKB   32.                  ; the blank symbol: restores the dojo cells
"""
