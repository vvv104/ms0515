"""Generate SAPER.MAC - game logic with selectable difficulty.

All input/output paths are taken relative to this script's directory so
the project is portable.  Inputs in this directory:

    K.DAT      original Pascal SAPER sprite atlas (also shipped as SAPER.DAT)
    K.HLP      original encrypted help text
    NSEQ.BIN   captured FORLIB RAN() noise sequence used to decrypt K.HLP

Output:

    SAPER.MAC  the MACRO-11 source for assembly by MACRO.SAV + LINK.SAV
"""
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent

kdat = (HERE / 'K.DAT').read_bytes()
OUT_MAC = HERE.parent / 'SAPER.MAC'         # write into the project root
words = list(struct.unpack('<512H', kdat))

# ---------------------------------------------------------------------------
# K.HLP encrypted help file + N sequence captured from FORLIB RAN().
# Decryption happens at runtime: decoded[i] = (KHLP[i+2] - NSEQ[i]) & 0xFF,
# with KHLP[0..1] = SEED1, SEED2 = (32, 32).  Final buffer is 46 lines x 80
# chars = 3680 bytes (page 1 = lines 0..22, page 2 = lines 23..45).
KHLP_BYTES = (HERE / 'K.HLP').read_bytes()
NSEQ_BYTES = (HERE / 'NSEQ.BIN').read_bytes()
assert len(KHLP_BYTES) >= 3680 + 2
assert len(NSEQ_BYTES) >= 3680 - 2

# ---------------------------------------------------------------------------
# Crisp 8x8 cp866 bitmap font extracted from Windows' cga80866.fon (the same
# CGA 80-col Russian font DOS used).  Stored as a 256-entry KOI-8R lookup
# (FNTMAP) into a compact glyph pool (FNTGLY) covering only the byte values
# we actually render.  This keeps FNTMAP+FNTGLY+KHLP+NSEQ all below VRAM at
# 0o40000 - reads above that address return pixel data instead of the byte
# we expected.
def _extract_cp866_font(path):
    raw = open(path, 'rb').read()
    ne_off = struct.unpack_from('<I', raw, 0x3C)[0]
    rsrc_off = ne_off + struct.unpack_from('<H', raw, ne_off + 0x24)[0]
    shift = struct.unpack_from('<H', raw, rsrc_off)[0]
    pos = rsrc_off + 2
    fnt_off = None
    while True:
        type_id = struct.unpack_from('<H', raw, pos)[0]
        if type_id == 0:
            break
        count = struct.unpack_from('<H', raw, pos + 2)[0]
        pos += 8
        if (type_id & 0x7FFF) == 0x08:
            fnt_off = struct.unpack_from('<H', raw, pos)[0] << shift
            break
        pos += count * 12
    assert fnt_off is not None, 'no RT_FONT'
    bits_off = struct.unpack_from('<I', raw, fnt_off + 113)[0]
    first_ch = raw[fnt_off + 95]
    glyphs = {}
    for ci in range(first_ch, raw[fnt_off + 96] + 1):
        g = raw[fnt_off + bits_off + (ci - first_ch) * 8:][:8]
        # FNT v2.0 8x8 fixed-pitch fonts are row-major: g[r] is row r, bit 7 =
        # leftmost pixel - matching how we write bytes into VRAM.
        glyphs[ci] = list(g)
    return glyphs

_CP866 = _extract_cp866_font('C:/Windows/Fonts/cga80866.fon')

def _koi8r_to_glyph(b):
    if b < 0x20:
        return None
    try:
        cp866_byte = bytes([b]).decode('koi8-r').encode('cp866')[0]
    except (UnicodeDecodeError, UnicodeEncodeError):
        return None
    return _CP866.get(cp866_byte)

# Old in-source ASCII font + custom English help kept for reference only.
# Not emitted to MAC anymore (replaced by the KOI-8R FONT_BYTES + K.HLP path).
_LEGACY_FONT_GLYPHS = {
    ' ': [0,0,0,0,0,0,0,0],
    '!': [0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00],
    "'": [0x18,0x18,0x10,0x00,0x00,0x00,0x00,0x00],
    '(': [0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00],
    ')': [0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00],
    ',': [0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x10],
    '-': [0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00],
    '.': [0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00],
    '/': [0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00],
    '0': [0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00],
    '1': [0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00],
    '2': [0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00],
    '3': [0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00],
    '4': [0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00],
    '5': [0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00],
    '6': [0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00],
    '7': [0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00],
    '8': [0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00],
    '9': [0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00],
    ':': [0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00],
    '?': [0x3C,0x66,0x06,0x0C,0x18,0x00,0x18,0x00],
    'A': [0x18,0x3C,0x66,0x7E,0x66,0x66,0x66,0x00],
    'B': [0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00],
    'C': [0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00],
    'D': [0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00],
    'E': [0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00],
    'F': [0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00],
    'G': [0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0x00],
    'H': [0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00],
    'I': [0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00],
    'J': [0x1E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0x00],
    'K': [0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00],
    'L': [0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00],
    'M': [0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00],
    'N': [0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00],
    'O': [0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00],
    'P': [0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00],
    'Q': [0x3C,0x66,0x66,0x66,0x6A,0x6C,0x36,0x00],
    'R': [0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0x00],
    'S': [0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00],
    'T': [0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00],
    'U': [0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00],
    'V': [0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00],
    'W': [0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00],
    'X': [0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00],
    'Y': [0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00],
    'Z': [0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00],
}

# Two pages of help text.  Each line up to 40 chars (= 320 px @ 8 px/char).
# An empty string ends a page; the renderer terminates on null byte.
HELP_PAGES = [
[
    "        SAPER FOR MS-0515",
    "",
    "  F1   THIS HELP",
    "  F2   NEW GAME WITH SAME LEVEL",
    "  F3   BEGINNER      (8X8, 10 MINES)",
    "  F4   INTERMEDIATE  (16X16, 40 M.)",
    "  F5   EXPERT        (30X16, 99 M.)",
    "  F10  EXIT TO MONITOR",
    "",
    "  ARROWS   MOVE CURSOR",
    "  SPACE    OPEN CELL",
    "  ENTER    FLAG / QUESTION / CLEAR",
    "  N        NEW GAME (AFTER OVER)",
    "",
    "",
    "  UP/DOWN ARROWS - SWITCH PAGES",
    "  ANY OTHER KEY  - RESUME GAME",
],
[
    "        SAPER FOR MS-0515 - PAGE 2",
    "",
    "  COUNTERS:",
    "    LEFT  - MINES NOT YET FLAGGED",
    "    RIGHT - SECONDS ELAPSED",
    "",
    "  RULES:",
    "    HIT A MINE  - GAME LOST",
    "    OPEN ALL    - GAME WON",
    "    NON-MINE 0  - AUTO EXPANDS",
    "",
    "  FLAGS COUNT CAPPED AT MINE TOTAL.",
    "  QUESTION MARK COUNTS AS UNFLAGGED.",
    "",
    "",
    "",
    "  UP/DOWN ARROWS - SWITCH PAGES",
    "  ANY OTHER KEY  - RESUME GAME",
],
]

# (Legacy FONT_CHARS/HELP_PAGE_BYTES path removed - replaced by KOI-8R
# 256-glyph FONT_BYTES + encrypted K.HLP loaded at the end of the MAC file.)

# Inject 7-segment style digit sprites for the counter at unused K.DAT slots.
# Each digit is 16x16 px and is split into top half (slots 36..45) and
# bottom half (slots 46..55).  Displayed as 3 columns x 2 rows for the
# header counter.  Segments are 7-segment style with 2-px thick strokes.

SEGMENTS = {
    0: "top tl tr bl br bot",
    1: "tr br",
    2: "top tr mid bl bot",
    3: "top tr mid br bot",
    4: "tl tr mid br",
    5: "top tl mid br bot",
    6: "top tl mid bl br bot",
    7: "top tr br",
    8: "top tl tr mid bl br bot",
    9: "top tl tr mid br bot",
}

# Map digit 0..9 to the existing K.DAT sprite halves at indices 25..35.
# Each pair = (top_sprite, bottom_sprite).  Identified by decoding which
# segments are "dark" (= white digit strokes on the screen).
#   top  S[25] = top + TL + TR + mid
#   top  S[26] = top + TL + TR (no mid)
#   top  S[27] = top + TR + mid (no TL)
#   top  S[28] = top + TL + mid (no TR)
#   top  S[29] = TL + TR + mid (no top bar)
#   top  S[30] = top + TR (no TL, no mid)
#   top  S[31] = TR only (no top, no mid)
#   bot  S[32] = BL + BR + bot
#   bot  S[33] = BR only (no bot)
#   bot  S[34] = BL + bot
#   bot  S[35] = BR + bot
DIGIT_PAIRS = {
    0: (26, 32),   # top + TL + TR + BL + BR + bot
    1: (31, 33),   # TR + BR
    2: (27, 34),   # top + TR + mid + BL + bot
    3: (27, 35),   # top + TR + mid + BR + bot
    4: (29, 33),   # TL + TR + mid + BR
    5: (28, 35),   # top + TL + mid + BR + bot
    6: (28, 32),   # top + TL + mid + BL + BR + bot
    7: (30, 33),   # top + TR + BR
    8: (25, 32),   # all 7 segments
    9: (25, 35),   # top + TL + TR + mid + BR + bot
}

# K.DAT's S[35] has a half-texture last row (0x0055 -- right half blank),
# while all other digit-bottom sprites use full texture 0x5555.  This is an
# outlier in the data set; patch it so 3/5/9 (which use S[35]) match.
words[35 * 8 + 7] = 0x5555

# S[14] (wrong-flag X mark) lacks the cell's right border (cols 14-15) and
# bottom border (row 7) that every other cell sprite has.  Add them so the
# grid stays unbroken when the X replaces the flag at game over.
for r in range(7):
    words[14 * 8 + r] |= 0x0300          # vertical line at cols 14-15 (right edge)
words[14 * 8 + 7] = 0xFFFF                # full bottom bar


# Copy the matching K.DAT sprites into the runtime slot for each digit
# (slot 36+d for top half, slot 46+d for bottom half).
for d, (tn, bn) in DIGIT_PAIRS.items():
    for j in range(8):
        words[(36 + d) * 8 + j] = words[tn * 8 + j]
        words[(46 + d) * 8 + j] = words[bn * 8 + j]


src = r"""        .TITLE  SAPER
;
; SAPER game logic for MS-0515.
;
; Cell A[col,row] byte encoding (0..28):
;   0..8   = closed cell with N adjacent mines
;   9      = closed mine
;   10..18 = opened cell (value-10 = N adjacent mines)
;   19..28 = flagged (19 = correctly flagged mine, 20..28 = incorrectly flagged)
;
; The A[] array uses a fixed stride of 32 cells/row (5 bits) so the address
; arithmetic stays free of runtime multiplication.  Max field is 30 cols x
; 16 rows (expert), so 16 * 32 = 512 bytes is enough.
;
; Difficulty (F3/F4/F5):
;   F3 = beginner       8 x 8   / 10 mines
;   F4 = intermediate  16 x 16  / 40 mines
;   F5 = expert        30 x 16  / 99 mines
;

DPRAM    = 157700
DISPAT   = 177400
VRAM     = 40000
VRAMEN   = 100000

MINEVL = 9.
TKINIT = 59000.                 ; loop iterations per timer second (calibrated)
ROWSTR = 64.            ; row stride in A[] (38 cols max, fits 20x64 = 1280 B)

; A[] lives in SAV; KHLP and NSEQ are .READW-loaded into upper RAM
; (bank 4) on startup.  Embedding KHLP+NSEQ in the SAV pushed FNTGLY
; past 0o40000, where the VRAM virtual window (VW=01 at 0o40000-0o77777)
; overrides reads and serves pixel data instead of glyph data.  Now
; that .SETTOP at LDOK guards USR, loading them above 0o100000 is safe.
KHLP   = 100000                 ; 32768; KHLP ends at 32768 + 3680 = 36448
NSEQ   = 110000                 ; 36864; NSEQ ends at 36864 + 3680 = 40544
                                ; ^ NSEQ must start at >= KHLP+3680, otherwise
                                ;   the second .READW overwrites KHLP's tail
                                ;   and the help text turns to noise mid-page.

SCUP   = 252
SCDN   = 251
SCLT   = 247
SCRT   = 250
SCSP   = 324
SCRTN  = 275
SCN    = 334
SCF1   = 126
SCF2   = 127
SCF3   = 130
SCF4   = 131
SCF5   = 132
SCF6   = 144
SCF7   = 145
SCF8   = 146
SCF9   = 147
SCF10  = 150
SCV    = 362

START:  MOV     #340,R0
        MTPS    R0                      ; mask all IRQs while setting up

        ; --- save what we're about to overwrite (for clean F10 exit) ---
        MOV     @#177400,ORIGDP         ; original dispatcher
        MOV     @#100,ORIG10            ; timer vector PC
        MOV     @#102,ORG102            ; timer vector PSW
        MOV     @#66,ORIG66             ; monitor vector PSW
        MOV     @#132,ORG132            ; keyboard vector PSW

        ; --- load SAPER.HLP (KHLP + NSEQ) via .LOOKUP/.READW into bank 4.
        MOV     #LKAREA,R0
        MOV     #400,(R0)               ; func 1 (.LOOKUP), chan 0
        MOV     #KHFILE,2(R0)
        CLR     4(R0)
        EMT     375
        BCS     LDFAIL
        ; .READW K.HLP body: 1840 words from block 0 into KHLP
        MOV     #LKAREA,R0
        MOV     #4000,(R0)              ; func 10oct (.READW), chan 0
        CLR     2(R0)
        MOV     #KHLP,4(R0)
        MOV     #1840.,6(R0)
        CLR     10(R0)
        EMT     375
        BCS     LDFAIL
        ; .READW NSEQ body: 1840 words from block 8 into NSEQ
        MOV     #LKAREA,R0
        MOV     #4000,(R0)
        MOV     #8.,2(R0)
        MOV     #NSEQ,4(R0)
        MOV     #1840.,6(R0)
        CLR     10(R0)
        EMT     375
        BCS     LDFAIL
        ; .CLOSE chan 0
        MOV     #3000,R0                ; func 6, chan 0
        EMT     374
        BR      LDOK
LDFAIL: MOV     ORIGDP,@#DISPAT
        EMT     350

LDOK:   ; --- tell RT-11 we own memory all the way up to 0o125776 so it
        ;     swaps USR out ABOVE that.  Then put SP at the declared top.
        ;     Without this, RT-11 leaves USR resident around 0o126000
        ;     (just below the monitor boundary at 0o130200), and our
        ;     stack at 0o130000 spills straight into USR code.  After
        ;     .EXIT the next monitor command needs USR, executes our
        ;     stack residue, and traps to vector 10 (typical PC: 0o127716).
        MOV     SP,ORIGSP
        MOV     #125776,R0
        EMT     354                     ; .SETTOP -> R0 = actual top granted
        MOV     R0,SP

        ; --- save Sys Reg C (0177604) and force border=7 (white).
        ;     In hi-res mode border becomes the screen background; in lo-res
        ;     it's the frame around the active area.  Bit 7 is the speaker
        ;     gate which changes constantly, so we read the full byte but
        ;     only touch bits 0-2 (border color).
        MOVB    @#177604,R0
        MOVB    R0,ORIGRC
        BIS     #7,R0
        MOVB    R0,@#177604

        ; --- install timer ISR; block other IRQs via their vector-PSW ---
        MOV     #TISR,@#100
        MOV     #340,@#102
        MOV     #240,@#132
        MOV     #240,@#66

        ; Dispatcher: VRAM_EN + window @040000 + banks + timer-IRQ enable
        MOV     #3377,R0
        MOV     R0,@#DPRAM
        MOV     R0,@#DISPAT

        JSR     PC,CLRVRM
        MOV     #12345.,SEED

        ; Lower PSW priority to 5: only timer IRQ (prio 6) can fire.
        MOV     #240,R0
        MTPS    R0

        ; default = beginner
        MOV     #8.,R2
        MOV     #8.,R3
        MOV     #10.,R4
        JSR     PC,SETDIF
        JSR     PC,NEWGAME

LOOP:   MOV     #3377,@#177400          ; restore dispatcher (timer IRQ still on)
        ; Convert hardware ticks (50 Hz) into seconds.
TCHK:   CMP     TKR,#50.
        BLT     LPK
        SUB     #50.,TKR
        TST     TMSTRT                  ; timer paused until first SP/RTN
        BEQ     TCHK
        TST     GAMEST                  ; freeze on game over
        BNE     TCHK
        INC     SECS
        JSR     PC,SHOWTM
        BR      TCHK
LPK:    MOV     @#177442,R0
        BIT     #2,R0
        BNE     LPKD                    ; key present, go process
        JMP     LOOP                    ; nothing, retry (LOOP is too far for BR)
LPKD:   MOV     @#177440,R0
        BIC     #177400,R0

        ; F2 - new game with current difficulty
        CMP     R0,#SCF2
        BNE     LF1
        JSR     PC,NEWGAME
        JMP     LOOP
LF1:    ; F1 - show decrypted K.HLP help (2 pages, up/down switches)
        CMP     R0,#SCF1
        BNE     LF9
        JSR     PC,HLPSHO
        JMP     LOOP
LF9:    ; F9 - open game menu
        CMP     R0,#SCF9
        BNE     LF6
        JSR     PC,MNUSHO
        JMP     LOOP
LF6:    ; F6 - direct custom-game dialog
        CMP     R0,#SCF6
        BNE     LMM
        JSR     PC,CSSHO
        JMP     LOOP
LMM:    ; F7 - toggle Marker option
        CMP     R0,#SCF7
        BNE     LBB
        TST     MARKER
        BNE     1$
        MOV     #1,MARKER
        JMP     LOOP
1$:     CLR     MARKER
        JMP     LOOP
LBB:    ; F8 - best times screen
        CMP     R0,#SCF8
        BNE     LF10
        JSR     PC,BTSHO
        JMP     LOOP
LF10:   ; F10 - clean exit back to RT-11 monitor
        CMP     R0,#SCF10
        BNE     L0
        JMP     EXIT
L0:     ; F3/F4/F5 - change difficulty (always allowed)
        CMP     R0,#SCF3
        BNE     L1
        MOV     #8.,R2
        MOV     #8.,R3
        MOV     #10.,R4
        BR      DIFCHG
L1:     CMP     R0,#SCF4
        BNE     L2
        MOV     #16.,R2
        MOV     #16.,R3
        MOV     #40.,R4
        BR      DIFCHG
L2:     CMP     R0,#SCF5
        BNE     L3
        MOV     #30.,R2
        MOV     #16.,R3
        MOV     #99.,R4
DIFCHG: JSR     PC,SETDIF
        JSR     PC,NEWGAME
        JMP     LOOP

L3:     ; When game is over, only N is accepted
        TST     GAMEST
        BEQ     LLIVE
        CMP     R0,#SCN
        BEQ     L3R
        JMP     LOOP
L3R:    JSR     PC,NEWGAME
        JMP     LOOP

LLIVE:  CMP     R0,#SCUP
        BNE     N1
        CMP     CURY,YMIN
        BGT     UPMV
        JMP     LOOP
UPMV:   JSR     PC,HIDECUR
        DEC     CURY
        JSR     PC,SHOWCUR
        JMP     LOOP
N1:     CMP     R0,#SCDN
        BNE     N2
        CMP     CURY,YMAX
        BLT     DNMV
        JMP     LOOP
DNMV:   JSR     PC,HIDECUR
        INC     CURY
        JSR     PC,SHOWCUR
        JMP     LOOP
N2:     CMP     R0,#SCLT
        BNE     N3
        CMP     CURX,XMIN
        BGT     LTMV
        JMP     LOOP
LTMV:   JSR     PC,HIDECUR
        DEC     CURX
        JSR     PC,SHOWCUR
        JMP     LOOP
N3:     CMP     R0,#SCRT
        BNE     N4
        CMP     CURX,XMAX
        BLT     RTMV
        JMP     LOOP
RTMV:   JSR     PC,HIDECUR
        INC     CURX
        JSR     PC,SHOWCUR
        JMP     LOOP

N4:     CMP     R0,#SCSP
        BNE     N5
        MOV     #1,TMSTRT               ; first SPACE starts the timer
        JSR     PC,OPENCEL
        TST     GAMEST
        BEQ     N4A
        JMP     LOOP
N4A:    JSR     PC,SHOWCUR
        JMP     LOOP

N5:     CMP     R0,#SCRTN
        BNE     N6
        MOV     #1,TMSTRT               ; first ENTER starts the timer
        JSR     PC,TOGFLAG
        JSR     PC,SHOWCUR
        JMP     LOOP

N6:     CMP     R0,#SCN
        BEQ     N6A
        JMP     LOOP
N6A:    JSR     PC,NEWGAME
        JMP     LOOP

;-------------------------------------------------------------------
; TISR - timer interrupt handler.  Fires once per VBlank (50 Hz).
; All it does is bump the 16-bit tick counter, then RTI.
TISR:   INC     TKR
        RTI

;-------------------------------------------------------------------
; CLRVRM - clear the entire VRAM window.
CLRVRM: MOV     #VRAM,R0
1$:     CLR     (R0)+
        CMP     R0,#VRAMEN
        BLO     1$
        RTS     PC

;-------------------------------------------------------------------
; SETDIF - install difficulty parameters.
; Input: R2 = NCOLS, R3 = NROWS, R4 = NMINES.
; Recomputes XMIN, XMAX, YMIN (fixed = 4), YMAX.
SETDIF: MOV     R2,NCOLS
        MOV     R3,NROWS
        MOV     R4,NMINES
        ; XMIN = (38 - NCOLS) / 2 - DRAWSP already adds +1 char-col offset
        ; (INC R1) so the available char-col range is 0..38; centring the
        ; (NCOLS+2) char-col frame inside that gives a 1-cell margin on
        ; each side.
        MOV     #38.,R0
        SUB     R2,R0
        ASR     R0
        MOV     R0,XMIN
        ; XMAX = XMIN + NCOLS - 1
        ADD     R2,R0
        DEC     R0
        MOV     R0,XMAX
        ; YMIN = 4 (fixed)
        MOV     #4.,YMIN
        ; YMAX = YMIN + NROWS - 1
        MOV     #4.,R0
        ADD     R3,R0
        DEC     R0
        MOV     R0,YMAX
        ; TOTCEL = NCOLS * NROWS (loop add - no MUL on T-11)
        CLR     R0
        MOV     R3,R1
TC1:    TST     R1
        BEQ     TC2
        ADD     R2,R0
        DEC     R1
        BR      TC1
TC2:    MOV     R0,TOTCEL
        RTS     PC

;-------------------------------------------------------------------
; NEWGAME - reset state, place mines, count neighbours, draw field.
NEWGAME:
        JSR     PC,CLRVRM               ; every caller wants a clean screen
        CLR     KOK
        CLR     GAMEST
        CLR     NMARK
        CLR     SECS
        CLR     TMSTRT                  ; timer paused until first SP/RTN
        CLR     TKR                     ; hardware tick accumulator
        ; Clear A[] (full 1280 bytes - we don't bother trimming to NCOLS*NROWS)
        MOV     #A,R0
        MOV     #1280.,R1
NG1:    CLRB    (R0)+
        DEC     R1
        BNE     NG1

        ; Place NMINES mines via reject sampling against NCOLS x NROWS.
        MOV     NMINES,R5
NG2:    JSR     PC,RND
        MOV     R0,R1
        BIC     #177700,R1              ; R1 = 0..63 (low 6 bits)
        CMP     R1,NCOLS
        BGE     NG2                     ; reject col >= NCOLS
        ASR     R0
        ASR     R0
        ASR     R0
        ASR     R0
        ASR     R0
        ASR     R0
        BIC     #177740,R0              ; R0 = 0..31
        CMP     R0,NROWS
        BGE     NG2                     ; reject row >= NROWS
        MOV     R0,R2
        ASL     R2
        ASL     R2
        ASL     R2
        ASL     R2
        ASL     R2
        ASL     R2                      ; row * 64
        ADD     R1,R2                   ; +col
        ADD     #A,R2
        TSTB    (R2)
        BNE     NG2
        MOVB    #MINEVL,(R2)
        DEC     R5
        BNE     NG2

        ; Count neighbours for each non-mine cell.
        CLR     R3
NG3:    CLR     R2
NG4:    MOV     R3,R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0                      ; row * 64
        ADD     R2,R0
        ADD     #A,R0
        MOVB    (R0),R1
        CMPB    R1,#MINEVL
        BEQ     NG5
        CLR     R4
        DEC     R3
        DEC     R2
        JSR     PC,CHECKMN
        INC     R2
        JSR     PC,CHECKMN
        INC     R2
        JSR     PC,CHECKMN
        SUB     #2,R2
        INC     R3
        JSR     PC,CHECKMN
        ADD     #2,R2
        JSR     PC,CHECKMN
        SUB     #2,R2
        INC     R3
        JSR     PC,CHECKMN
        INC     R2
        JSR     PC,CHECKMN
        INC     R2
        JSR     PC,CHECKMN
        DEC     R2
        DEC     R3
        MOV     R3,R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0                      ; row * 64
        ADD     R2,R0
        ADD     #A,R0
        MOVB    R4,(R0)
NG5:    INC     R2
        CMP     R2,NCOLS
        BLT     NG4
        INC     R3
        CMP     R3,NROWS
        BLT     NG3

        ; Frame + outer outline lines + cells
        JSR     PC,FRAME
        JSR     PC,DRLINE

        MOV     YMIN,R3
NGR1:   MOV     XMIN,R2
NGR2:   MOV     #12.,R4
        JSR     PC,DRAWSP
        INC     R2
        CMP     R2,XMAX
        BLE     NGR2
        INC     R3
        CMP     R3,YMAX
        BLE     NGR1

        MOV     XMIN,CURX
        MOV     YMIN,CURY
        JSR     PC,SHOWCUR
        JSR     PC,SHOWMC
        JSR     PC,SHOWTM
        RTS     PC

;-------------------------------------------------------------------
; FRAME - decorative border around the current field.
FRAME:  ; row 0: top edge with corners
        CLR     R3
        MOV     XMIN,R2
        DEC     R2
        MOV     #18.,R4
        JSR     PC,DRAWSP
        MOV     XMIN,R2
FR1:    MOV     #19.,R4
        JSR     PC,DRAWSP
        INC     R2
        CMP     R2,XMAX
        BLE     FR1
        MOV     #20.,R4
        JSR     PC,DRAWSP
        ; rows 1..YMIN-2: side bars + S[16] fill.  YMIN is fixed at 4
        ; (kept in memory but never varies), so the limit is always 2.
        MOV     #1,R3
FR2:    CMP     R3,#2
        BGT     FR3
        MOV     XMIN,R2
        DEC     R2
        MOV     #17.,R4
        JSR     PC,DRAWSP
        MOV     XMIN,R2
FR2A:   MOV     #16.,R4
        JSR     PC,DRAWSP
        INC     R2
        CMP     R2,XMAX
        BLE     FR2A
        MOV     #17.,R4
        JSR     PC,DRAWSP
        INC     R3
        BR      FR2
        ; row YMIN-1: header bottom (corners 23, 24)
FR3:    MOV     YMIN,R3
        DEC     R3
        MOV     XMIN,R2
        DEC     R2
        MOV     #23.,R4
        JSR     PC,DRAWSP
        MOV     XMIN,R2
FR3A:   MOV     #19.,R4
        JSR     PC,DRAWSP
        INC     R2
        CMP     R2,XMAX
        BLE     FR3A
        MOV     #24.,R4
        JSR     PC,DRAWSP
        ; rows YMIN..YMAX: side bars only
        MOV     YMIN,R3
FR4:    CMP     R3,YMAX
        BGT     FR5
        MOV     XMIN,R2
        DEC     R2
        MOV     #17.,R4
        JSR     PC,DRAWSP
        MOV     XMAX,R2
        INC     R2
        MOV     #17.,R4
        JSR     PC,DRAWSP
        INC     R3
        BR      FR4
        ; row YMAX+1: bottom edge
FR5:    MOV     YMAX,R3
        INC     R3
        MOV     XMIN,R2
        DEC     R2
        MOV     #22.,R4
        JSR     PC,DRAWSP
        MOV     XMIN,R2
FR5A:   MOV     #19.,R4
        JSR     PC,DRAWSP
        INC     R2
        CMP     R2,XMAX
        BLE     FR5A
        MOV     #21.,R4
        JSR     PC,DRAWSP
        RTS     PC

;-------------------------------------------------------------------
; DRLINE - draw the Pascal-style outline lines around the frame.
;   1 horizontal line at scanline 0  (the top edge of the frame area)
;   2 vertical lines on the left side (1 pixel before frame + frame's leftmost)
;
; Cells render at byte offsets (col+1)*2 inside each scanline.  The leftmost
; pixel of the leftmost frame column is at byte XMIN*2 (bit 7).  The pixel
; just left of that is byte XMIN*2 - 1 (bit 0).  Both verticals share these
; same byte/bit positions on every scanline of the frame's height.
DRLINE: ; --- top horizontal line at scanline 0 ---
        ; Leftmost pixel (x = XMIN*16 - 1) only exists when XMIN > 0;
        ; otherwise that byte sits in user RAM, not VRAM.
        TST     XMIN
        BEQ     DRLH
        MOV     XMIN,R1
        ASL     R1
        DEC     R1
        ADD     #VRAM,R1
        BISB    #1,(R1)
DRLH:   ; Fill bytes XMIN*2 .. (XMAX+2)*2 + 1 of scanline 0 with 0xFF
        MOV     XMIN,R1
        ASL     R1
        ADD     #VRAM,R1
        MOV     XMAX,R2
        ADD     #2,R2
        ASL     R2
        INC     R2
        ADD     #VRAM,R2
1$:     MOVB    #-1,(R1)
        INC     R1
        CMP     R1,R2
        BLE     1$

        ; --- vertical lines on the left, from scanline 0 to (YMAX+2)*8-1 ---
        MOV     YMAX,R0
        ADD     #2,R0
        ASL     R0
        ASL     R0
        ASL     R0                      ; R0 = (YMAX+2) * 8
        CLR     R3                      ; scanline counter
2$:     MOV     R3,R4
        ASL     R4
        ASL     R4
        ASL     R4
        ASL     R4                      ; R4 = scanline * 16
        MOV     R4,R5
        ASL     R4
        ASL     R4                      ; R4 = scanline * 64
        ADD     R5,R4                   ; R4 = scanline * 80
        ADD     XMIN,R4
        ADD     XMIN,R4                 ; R4 += XMIN * 2  (frame leftmost byte)
        ADD     #VRAM,R4
        BISB    #200,(R4)               ; set bit 7 (= x = XMIN*16)
        TST     XMIN                    ; "before-frame" pixel needs room
        BEQ     3$
        DEC     R4
        BISB    #1,(R4)                 ; set bit 0 (= x = XMIN*16 - 1)
3$:     INC     R3
        CMP     R3,R0
        BLT     2$
        RTS     PC

;-------------------------------------------------------------------
; CHECKMN: increment R4 if A[R2,R3] is a mine.  Bounds-checks.
CHECKMN:
        TST     R2
        BLT     CME
        CMP     R2,NCOLS
        BGE     CME
        TST     R3
        BLT     CME
        CMP     R3,NROWS
        BGE     CME
        MOV     R3,R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0                      ; row * 64
        ADD     R2,R0
        ADD     #A,R0
        MOVB    (R0),R1
        CMPB    R1,#MINEVL
        BNE     CME
        INC     R4
CME:    RTS     PC

;-------------------------------------------------------------------
; OPENCEL - open the cell at (CURX, CURY).
OPENCEL:
        TST     GAMEST
        BNE     OEEND
        JSR     PC,CELLPTR
        MOVB    (R0),R1
        BIC     #177400,R1
        CMP     R1,#9.
        BGT     OEEND
        BNE     OENZ
        ; mine!
        JSR     PC,VZRYV
        MOV     CURX,R2
        MOV     CURY,R3
        MOV     #10.,R4
        JSR     PC,DRAWSP
        MOV     #2,GAMEST
        RTS     PC
OENZ:   TST     R1
        BEQ     OEREC
        ADD     #10.,R1
        MOVB    R1,(R0)
        INC     KOK
        MOV     CURX,R2
        MOV     CURY,R3
        SUB     #10.,R1
        MOV     R1,R4
        JSR     PC,DRAWSP
        BR      OCHK
OEREC:  MOV     CURX,R2
        SUB     XMIN,R2
        MOV     CURY,R3
        SUB     YMIN,R3
        JSR     PC,OTKRP
OCHK:   ; win check: KOK >= TOTCEL - NMINES
        MOV     TOTCEL,R0
        SUB     NMINES,R0
        CMP     KOK,R0
        BLT     OEEND
        JSR     PC,POBEDA
        MOV     #1,GAMEST
OEEND:  RTS     PC

;-------------------------------------------------------------------
; OTKRP - recursive flood-fill, grid coords (R2 col, R3 row).
OTKRP:  MOV     R2,-(SP)
        MOV     R3,-(SP)
        TST     R2
        BLT     OPRX
        CMP     R2,NCOLS
        BGE     OPRX
        TST     R3
        BLT     OPRX
        CMP     R3,NROWS
        BGE     OPRX
        MOV     R3,R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0                      ; row * 64
        ADD     R2,R0
        ADD     #A,R0
        MOVB    (R0),R1
        BIC     #177400,R1
        CMP     R1,#9.
        BGE     OPRX
        TST     R1
        BNE     OPRNUM
        ; A=0: open, draw S[0], recurse 8 neighbours
        MOVB    #10.,(R0)
        INC     KOK
        ADD     XMIN,R2
        ADD     YMIN,R3
        CLR     R4
        JSR     PC,DRAWSP
        SUB     XMIN,R2
        SUB     YMIN,R3
        DEC     R2
        DEC     R3
        JSR     PC,OTKRP
        INC     R2
        JSR     PC,OTKRP
        INC     R2
        JSR     PC,OTKRP
        SUB     #2,R2
        INC     R3
        JSR     PC,OTKRP
        ADD     #2,R2
        JSR     PC,OTKRP
        SUB     #2,R2
        INC     R3
        JSR     PC,OTKRP
        INC     R2
        JSR     PC,OTKRP
        INC     R2
        JSR     PC,OTKRP
        BR      OPRX
OPRNUM: ADD     #10.,R1
        MOVB    R1,(R0)
        INC     KOK
        ADD     XMIN,R2
        ADD     YMIN,R3
        MOV     R1,R4
        SUB     #10.,R4
        JSR     PC,DRAWSP
OPRX:   MOV     (SP)+,R3
        MOV     (SP)+,R2
        RTS     PC

;-------------------------------------------------------------------
; VZRYV - on loss, reveal all mines and wrong flags.
VZRYV:  CLR     R3
VZ1:    CLR     R2
VZ2:    MOV     R3,R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0                      ; row * 64
        ADD     R2,R0
        ADD     #A,R0
        MOVB    (R0),R1
        BIC     #177400,R1
        CMP     R1,#9.
        BNE     VZ3
        MOV     R2,-(SP)
        MOV     R3,-(SP)
        ADD     XMIN,R2
        ADD     YMIN,R3
        MOV     #9.,R4
        JSR     PC,DRAWSP
        MOV     (SP)+,R3
        MOV     (SP)+,R2
        BR      VZ5
VZ3:    CMP     R1,#20.
        BLT     VZ5
        CMP     R1,#28.
        BGT     VZ5
        MOV     R2,-(SP)
        MOV     R3,-(SP)
        ADD     XMIN,R2
        ADD     YMIN,R3
        MOV     #14.,R4                 ; wrong-flag X (patched with cell borders)
        JSR     PC,DRAWSP
        MOV     (SP)+,R3
        MOV     (SP)+,R2
VZ5:    INC     R2
        CMP     R2,NCOLS
        BLT     VZ2
        INC     R3
        CMP     R3,NROWS
        BLT     VZ1
        RTS     PC

;-------------------------------------------------------------------
; POBEDA - on win, replace every remaining closed mine with a flag.
POBEDA: CLR     R3
PB1:    CLR     R2
PB2:    MOV     R3,R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0                      ; row * 64
        ADD     R2,R0
        ADD     #A,R0
        MOVB    (R0),R1
        BIC     #177400,R1
        CMP     R1,#9.
        BNE     PB5
        MOV     R2,-(SP)
        MOV     R3,-(SP)
        ADD     XMIN,R2
        ADD     YMIN,R3
        MOV     #13.,R4
        JSR     PC,DRAWSP
        MOV     (SP)+,R3
        MOV     (SP)+,R2
PB5:    INC     R2
        CMP     R2,NCOLS
        BLT     PB2
        INC     R3
        CMP     R3,NROWS
        BLT     PB1
        ; Best-times update: only the 3 preset configurations qualify.
        CMP     NCOLS,#8.
        BNE     PBT1
        CMP     NROWS,#8.
        BNE     PBT1
        CMP     NMINES,#10.
        BNE     PBT1
        CMP     SECS,BTBEG
        BGE     PBEND
        MOV     SECS,BTBEG
        BR      PBEND
PBT1:   CMP     NCOLS,#16.
        BNE     PBT2
        CMP     NROWS,#16.
        BNE     PBT2
        CMP     NMINES,#40.
        BNE     PBT2
        CMP     SECS,BTINT
        BGE     PBEND
        MOV     SECS,BTINT
        BR      PBEND
PBT2:   CMP     NCOLS,#30.
        BNE     PBEND
        CMP     NROWS,#16.
        BNE     PBEND
        CMP     NMINES,#99.
        BNE     PBEND
        CMP     SECS,BTEXP
        BGE     PBEND
        MOV     SECS,BTEXP
PBEND:  RTS     PC

;-------------------------------------------------------------------
; TOGFLAG - cycle marker state at (CURX, CURY):
;   closed (0..9) -> flagged (19..28) -> question (29..38) -> closed
; The flag step is refused when NMARK == NMINES.  Question marks don't
; count toward NMARK.  Cell ranges:
;   0..9   closed (9 = mine)
;   10..18 opened
;   19..28 flagged (19 = was mine, 20..28 = was non-mine)
;   29..38 question (29 = was mine, 30..38 = was non-mine)
TOGFLAG:
        JSR     PC,CELLPTR
        MOVB    (R0),R1
        BIC     #177400,R1
        CMP     R1,#9.
        BGT     TFCHK
        ; --- CLOSED -> FLAGGED ---
        CMP     NMARK,NMINES            ; refuse if at flag limit
        BGE     TFEND
        CMP     R1,#9.
        BNE     TFCNM
        MOVB    #19.,(R0)               ; mine -> 19
        BR      TFCDR
TFCNM:  ADD     #20.,R1
        MOVB    R1,(R0)
TFCDR:  INC     NMARK
        MOV     CURX,R2
        MOV     CURY,R3
        MOV     #13.,R4                 ; flag sprite
        JSR     PC,DRAWSP
        JSR     PC,SHOWMC
        RTS     PC
TFCHK:  CMP     R1,#18.
        BLE     TFEND                   ; opened (10..18) - no-op
        CMP     R1,#28.
        BGT     TFQ                     ; 29..38 question
        ; --- FLAGGED -> (QUESTION if MARKER on, else CLOSED) ---
        TST     MARKER
        BNE     TFFQ
        ; Marker OFF: flagged (19..28) -> closed (9 if mine, 0..8 otherwise)
        CMP     R1,#19.
        BNE     TFFNM
        MOVB    #9.,(R0)
        BR      TFFCD
TFFNM:  SUB     #20.,R1                 ; 20..28 -> 0..8
        MOVB    R1,(R0)
TFFCD:  DEC     NMARK
        MOV     CURX,R2
        MOV     CURY,R3
        MOV     #12.,R4                 ; closed cell sprite
        JSR     PC,DRAWSP
        JSR     PC,SHOWMC
        RTS     PC
TFFQ:   ADD     #10.,R1                 ; 19..28 -> 29..38
        MOVB    R1,(R0)
        DEC     NMARK                   ; un-flag decrements counter
        MOV     CURX,R2
        MOV     CURY,R3
        MOV     #15.,R4                 ; '?' marker sprite
        JSR     PC,DRAWSP
        JSR     PC,SHOWMC
        RTS     PC
TFQ:    CMP     R1,#38.
        BGT     TFEND                   ; out of range
        ; --- QUESTION -> CLOSED ---
        CMP     R1,#29.
        BNE     TFQNM
        MOVB    #9.,(R0)                ; mine question -> mine closed
        BR      TFQDR
TFQNM:  SUB     #30.,R1                 ; 30..38 -> 0..8
        MOVB    R1,(R0)
TFQDR:  MOV     CURX,R2
        MOV     CURY,R3
        MOV     #12.,R4                 ; closed sprite
        JSR     PC,DRAWSP
TFEND:  RTS     PC

;-------------------------------------------------------------------
; CELLPTR - return R0 -> A[CURX-XMIN, CURY-YMIN] (stride 32).
CELLPTR:
        MOV     CURY,R0
        SUB     YMIN,R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0                      ; row * 64
        MOV     CURX,R1
        SUB     XMIN,R1
        ADD     R1,R0
        ADD     #A,R0
        RTS     PC

;-------------------------------------------------------------------
SHOWCUR: JSR    PC,HIDECUR
        MOV     CURX,R2
        MOV     CURY,R3
        MOV     #11.,R4
        JSR     PC,DRAWCS
        RTS     PC

HIDECUR: JSR    PC,CELLPTR
        MOVB    (R0),R1
        BIC     #177400,R1
        CMP     R1,#9.
        BLE     HCL                     ; 0..9 closed
        CMP     R1,#18.
        BLE     HCO                     ; 10..18 opened
        CMP     R1,#28.
        BLE     HCF                     ; 19..28 flagged
        MOV     #15.,R4                 ; 29..38 question
        BR      HC4
HCL:    MOV     #12.,R4
        BR      HC4
HCF:    MOV     #13.,R4
        BR      HC4
HCO:    MOV     R1,R4
        SUB     #10.,R4
HC4:    MOV     CURX,R2
        MOV     CURY,R3
        JSR     PC,DRAWSP
        RTS     PC

;-------------------------------------------------------------------
; SHOWMC - draw (NMINES - NMARK) clamped to 0..999 as three 16x16 digits
;          in the header (cols XMIN..XMIN+2, rows 1-2).
; Each digit is composed of a top sprite (36+digit) and bottom sprite (46+digit).
SHOWMC: MOV     NMINES,R0
        SUB     NMARK,R0
        TST     R0
        BGE     1$
        CLR     R0
1$:     CMP     R0,#999.
        BLE     2$
        MOV     #999.,R0
2$:     ; split into hundreds (R3), tens (R4), ones (R0)
        CLR     R3
3$:     CMP     R0,#100.
        BLT     4$
        SUB     #100.,R0
        INC     R3
        BR      3$
4$:     CLR     R4
5$:     CMP     R0,#10.
        BLT     6$
        SUB     #10.,R0
        INC     R4
        BR      5$
6$:     ; stack: hundreds, tens, ones
        MOV     R3,-(SP)
        MOV     R4,-(SP)
        MOV     R0,-(SP)
        ; draw hundreds at col XMIN
        MOV     XMIN,R2
        MOV     4(SP),R4                ; hundreds
        JSR     PC,DRAWDG
        ; draw tens at col XMIN+1
        MOV     XMIN,R2
        INC     R2
        MOV     2(SP),R4                ; tens
        JSR     PC,DRAWDG
        ; draw ones at col XMIN+2
        MOV     XMIN,R2
        ADD     #2,R2
        MOV     (SP),R4                 ; ones
        JSR     PC,DRAWDG
        ADD     #6,SP
        RTS     PC

;-------------------------------------------------------------------
; SHOWTM - draw SECS (clamped to 0..999) as three digits at the right
;          edge of the header, ending at column XMAX.
SHOWTM: MOV     SECS,R0
        CMP     R0,#999.
        BLE     T1
        MOV     #999.,R0
T1:     CLR     R3
T2:     CMP     R0,#100.
        BLT     T3
        SUB     #100.,R0
        INC     R3
        BR      T2
T3:     CLR     R4
T4:     CMP     R0,#10.
        BLT     T5
        SUB     #10.,R0
        INC     R4
        BR      T4
T5:     MOV     R3,-(SP)
        MOV     R4,-(SP)
        MOV     R0,-(SP)
        ; hundreds at col XMAX-2
        MOV     XMAX,R2
        SUB     #2,R2
        MOV     4(SP),R4
        JSR     PC,DRAWDG
        ; tens at XMAX-1
        MOV     XMAX,R2
        DEC     R2
        MOV     2(SP),R4
        JSR     PC,DRAWDG
        ; ones at XMAX
        MOV     XMAX,R2
        MOV     (SP),R4
        JSR     PC,DRAWDG
        ADD     #6,SP
        RTS     PC

;-------------------------------------------------------------------
; DRAWDG - draw a 16x16 digit (R4 = digit 0..9) at column R2, rows 1-2.
DRAWDG: MOV     R2,-(SP)
        MOV     R4,-(SP)
        MOV     #1,R3
        ADD     #36.,R4                 ; top-half sprite
        JSR     PC,DRAWSP
        MOV     (SP)+,R4                ; restore digit
        MOV     (SP)+,R2                ; restore col
        MOV     #2,R3
        ADD     #46.,R4                 ; bottom-half sprite
        JSR     PC,DRAWSP
        RTS     PC

;-------------------------------------------------------------------
; DRWPGE - draw one 23-line x 80-col help page to VRAM, decrypting on the
;          fly:   plain[i] = (KHLP[i] - NSEQ[i-2]) & 0xFF   for i >= 2
;                 plain[0] = KHLP[0] = 32 (seed - shown as space)
;                 plain[1] = KHLP[1] = 32
; Input: R0 = page number (0 = chars 0..1839, 1 = chars 1840..3679).
DRWPGE: TST     R0
        BEQ     DPP1
        MOV     #1840.,R5
        BR      DPST
DPP1:   CLR     R5                      ; R5 = char index in K.HLP
DPST:   CLR     R3                      ; row 0..22
DPR:    CLR     R2                      ; col 0..79
DPC:    MOV     R5,R4
        ADD     #KHLP,R4
        MOVB    (R4),R0
        BIC     #177400,R0
        CMP     R5,#2
        BLT     DPSEED                  ; first 2 chars are seed (32 = space)
        MOV     R5,R4
        SUB     #2,R4
        ADD     #NSEQ,R4
        MOVB    (R4),R1
        BIC     #177400,R1
        SUB     R1,R0
        BIC     #177400,R0
DPSEED: MOV     R5,-(SP)
        MOV     R2,-(SP)
        MOV     R3,-(SP)
        JSR     PC,CHARDR
        MOV     (SP)+,R3
        MOV     (SP)+,R2
        MOV     (SP)+,R5
        INC     R5
        INC     R2
        CMP     R2,#80.
        BLT     DPC
        INC     R3
        CMP     R3,#23.
        BLT     DPR
        RTS     PC

;-------------------------------------------------------------------
; CHARDR - draw 8x8 glyph for byte R0 at column R2 (0..79), row R3 (0..24).
;   index = FNTMAP[R0]                       (8-bit compact index)
;   glyph = FNTGLY + index * 8
;   VRAM  = VRAM + R3 * 640 + R2             (R3 * 640 = (R3<<7) + (R3<<9))
; Trashes R0, R1, R4, R5.
CHARDR: MOV     #3377,@#177400
        ADD     #FNTMAP,R0              ; R0 = &FNTMAP[byte]
        MOVB    (R0),R0                 ; R0 = compact glyph index
        BIC     #177400,R0
        ASL     R0
        ASL     R0
        ASL     R0
        ADD     #FNTGLY,R0              ; R0 = glyph src
        MOV     R3,R1
        ASL     R1
        ASL     R1
        ASL     R1
        ASL     R1
        ASL     R1
        ASL     R1
        ASL     R1                      ; R1 = R3 * 128
        MOV     R1,R4
        ASL     R4
        ASL     R4                      ; R4 = R3 * 512
        ADD     R4,R1                   ; R1 = R3 * 640
        ADD     R2,R1
        ADD     #VRAM,R1                ; R1 = dst
        MOV     #8.,R5
CDL:    MOVB    (R0)+,(R1)
        ADD     #80.,R1
        DEC     R5
        BNE     CDL
        RTS     PC

;-------------------------------------------------------------------
; REDRAW - rebuild the game screen from A[] after returning from help.
REDRAW: JSR     PC,FRAME
        JSR     PC,DRLINE
        CLR     R3
RDR1:   CLR     R2
RDR2:   MOV     R3,R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0                      ; row * 64
        ADD     R2,R0
        ADD     #A,R0
        MOVB    (R0),R1
        BIC     #177400,R1
        CMP     R1,#9.
        BLE     RDCLD
        CMP     R1,#18.
        BLE     RDOPN
        CMP     R1,#28.
        BLE     RDFLG
        MOV     #15.,R4                 ; question marker
        BR      RDDRW
RDCLD:  MOV     #12.,R4                 ; closed cell
        BR      RDDRW
RDOPN:  SUB     #10.,R1
        MOV     R1,R4                   ; opened: sprite (value)
        BR      RDDRW
RDFLG:  MOV     #13.,R4                 ; flag
RDDRW:  MOV     R2,-(SP)
        MOV     R3,-(SP)
        ADD     XMIN,R2
        ADD     YMIN,R3
        JSR     PC,DRAWSP
        MOV     (SP)+,R3
        MOV     (SP)+,R2
        INC     R2
        CMP     R2,NCOLS
        BLT     RDR2
        INC     R3
        CMP     R3,NROWS
        BLT     RDR1
        ; Replay end-of-game overlays from the last frame.
        CMP     GAMEST,#2
        BNE     RDCKW
        JSR     PC,VZRYV
        BR      RDFIN
RDCKW:  CMP     GAMEST,#1
        BNE     RDFIN
        JSR     PC,POBEDA
RDFIN:  JSR     PC,SHOWMC
        JSR     PC,SHOWTM
        JSR     PC,SHOWCUR
        RTS     PC

;-------------------------------------------------------------------
; PUTSTR - draw null-terminated string at (R2 col, R3 row).
;   Input:  R0 = string addr, R2 = col, R3 = row.
;   Preserves caller's R5 (CHARDR clobbers it on every char).
PUTSTR: MOV     R5,-(SP)                ; save caller's R5
PSL:    MOVB    (R0)+,R4
        BIC     #177400,R4
        BEQ     PSEND
        MOV     R0,-(SP)
        MOV     R2,-(SP)
        MOV     R3,-(SP)
        MOV     R4,R0
        JSR     PC,CHARDR
        MOV     (SP)+,R3
        MOV     (SP)+,R2
        MOV     (SP)+,R0
        INC     R2
        BR      PSL
PSEND:  MOV     (SP)+,R5
        RTS     PC

;-------------------------------------------------------------------
; PDONE - helper for PUTDEC: draw R5 as char at (R2,R3), then INC R2.
PDONE:  MOV     R5,R0
        MOV     R4,-(SP)
        MOV     R2,-(SP)
        MOV     R3,-(SP)
        JSR     PC,CHARDR
        MOV     (SP)+,R3
        MOV     (SP)+,R2
        MOV     (SP)+,R4
        INC     R2
        RTS     PC

;-------------------------------------------------------------------
; PUTDEC - draw R0 (0..999) as 3 digits at (R2, R3).  Leading zeros = ' '.
;   Preserves caller's R5.
PUTDEC: MOV     R5,-(SP)                ; save caller's R5
        MOV     R0,R4                   ; value
        ; hundreds
        CLR     R5
PD1:    CMP     R4,#100.
        BLT     PD2
        SUB     #100.,R4
        INC     R5
        BR      PD1
PD2:    TST     R5
        BEQ     PDH0
        ADD     #'0,R5
        BR      PDH1
PDH0:   MOV     #' ,R5
PDH1:   JSR     PC,PDONE
        ; tens
        CLR     R5
PD3:    CMP     R4,#10.
        BLT     PD4
        SUB     #10.,R4
        INC     R5
        BR      PD3
PD4:    ADD     #'0,R5
        JSR     PC,PDONE
        ; ones
        MOV     R4,R5
        ADD     #'0,R5
        JSR     PC,PDONE
        MOV     (SP)+,R5
        RTS     PC

;-------------------------------------------------------------------
; KWAIT - block until a key is pressed; return scan code in R0.
;         Keeps TKR clamped to 0 so SECS does not advance during the wait.
KWAIT:  MOV     #3377,@#177400
        CLR     TKR
        MOV     @#177442,R0
        BIT     #2,R0
        BEQ     KWAIT
        MOV     @#177440,R0
        BIC     #177400,R0
        RTS     PC

;-------------------------------------------------------------------
; EXIT - clean shutdown back to RT-11 monitor.  Never returns.
EXIT:   MOV     #340,R1
        MTPS    R1                      ; mask IRQs
        JSR     PC,CLRVRM
        MOV     ORIG10,@#100
        MOV     ORG102,@#102
        MOV     ORIG66,@#66
        MOV     ORG132,@#132
        ; Restore Sys Reg C border bits to the value we saw at startup,
        ; preserving the speaker-gate bit 7 that may have flipped since.
        MOVB    ORIGRC,R1
        BIC     #177770,R1              ; keep only bits 0-2 (saved border)
        BICB    #7,@#177604             ; clear current border bits
        BISB    R1,@#177604             ; install saved border bits
        MOV     ORIGDP,R1
        MOV     R1,@#DPRAM
        MOV     R1,@#DISPAT
        MOV     ORIGSP,SP               ; restore RT-11's SP before .EXIT
        EMT     350

;-------------------------------------------------------------------
; HLPSHO - draw decrypted K.HLP help (2 pages, UP/DN switches).  Any
;         other key restores the game and returns.  Callable from both
;         the main loop (F1) and the menu dispatcher (slot 0).
HLPSHO: CLR     HELPPG
HLPS1:  JSR     PC,CLRVRM
        MOV     HELPPG,R0
        JSR     PC,DRWPGE
HLPS2:  MOV     #3377,@#177400          ; keep dispatcher live
        CLR     TKR                     ; freeze SECS while help is up
        MOV     @#177442,R0
        BIT     #2,R0
        BEQ     HLPS2
        MOV     @#177440,R0
        BIC     #177400,R0
        CMP     R0,#SCDN
        BNE     HLPS3
        MOV     #1,HELPPG               ; page 2
        BR      HLPS1
HLPS3:  CMP     R0,#SCUP
        BNE     HLPS4
        CLR     HELPPG                  ; page 1
        BR      HLPS1
HLPS4:  JSR     PC,CLRVRM
        JSR     PC,REDRAW
        CLR     TKR
        RTS     PC

;-------------------------------------------------------------------
; BTSHO - draw best times table; wait for key.
BTSHO:  JSR     PC,CLRVRM
        MOV     #BTHDR,R0
        MOV     #34.,R2
        MOV     #4,R3
        JSR     PC,PUTSTR
        MOV     #BTLBEG,R0
        MOV     #25.,R2
        MOV     #8.,R3
        JSR     PC,PUTSTR
        MOV     BTBEG,R0
        JSR     PC,BTPVAL
        MOV     #BTLINT,R0
        MOV     #25.,R2
        MOV     #10.,R3
        JSR     PC,PUTSTR
        MOV     BTINT,R0
        JSR     PC,BTPVAL
        MOV     #BTLEXP,R0
        MOV     #25.,R2
        MOV     #12.,R3
        JSR     PC,PUTSTR
        MOV     BTEXP,R0
        JSR     PC,BTPVAL
        MOV     #FTRMSG,R0
        MOV     #25.,R2
        MOV     #20.,R3
        JSR     PC,PUTSTR
        JSR     PC,KWAIT
        JSR     PC,CLRVRM
        JSR     PC,REDRAW
        CLR     TKR
        RTS     PC

;-------------------------------------------------------------------
; BTPVAL - draw R0 either as 3-digit time + " sec" or as "---" placeholder.
BTPVAL: CMP     R0,#999.
        BNE     BTPV1
        MOV     #BTNONE,R0
        JSR     PC,PUTSTR
        RTS     PC
BTPV1:  JSR     PC,PUTDEC
        MOV     #BTSECS,R0
        JSR     PC,PUTSTR
        RTS     PC

;-------------------------------------------------------------------
; CSSHO - custom-game dialog.  Up/Down: switch field; Left/Right: adjust;
;         Enter: validate, apply, NEWGAME, exit; F9 (or any other): cancel.
;   Field 0 = COLS (8..38), 1 = ROWS (8..20), 2 = MINES (1..759 capped).
CSSHO:  MOV     NCOLS,CSCOLS
        MOV     NROWS,CSROWS
        MOV     NMINES,CSMINES
        CLR     CSFIELD
        JSR     PC,CLRVRM               ; clear once on entry; CSDRW reuses
CSDRW:  MOV     #CSHDR,R0
        MOV     #33.,R2
        MOV     #3,R3
        JSR     PC,PUTSTR
        ; --- COLS line ---
        MOV     #4,R5                   ; row 4 base for field rows
        MOV     R5,R3
        ADD     #2,R3                   ; row 6
        MOV     CSFIELD,R0
        BNE     CSC1
        MOV     #CSARR,R0
        BR      CSC1B
CSC1:   MOV     #CSBLK,R0
CSC1B:  MOV     #28.,R2
        JSR     PC,PUTSTR
        MOV     #CSLCOL,R0
        JSR     PC,PUTSTR
        MOV     CSCOLS,R0
        JSR     PC,PUTDEC
        ; --- ROWS line ---
        MOV     #8.,R3                  ; row 8
        CMP     CSFIELD,#1
        BNE     CSC2
        MOV     #CSARR,R0
        BR      CSC2B
CSC2:   MOV     #CSBLK,R0
CSC2B:  MOV     #28.,R2
        JSR     PC,PUTSTR
        MOV     #CSLROW,R0
        JSR     PC,PUTSTR
        MOV     CSROWS,R0
        JSR     PC,PUTDEC
        ; --- MINES line ---
        MOV     #10.,R3                 ; row 10
        CMP     CSFIELD,#2
        BNE     CSC3
        MOV     #CSARR,R0
        BR      CSC3B
CSC3:   MOV     #CSBLK,R0
CSC3B:  MOV     #28.,R2
        JSR     PC,PUTSTR
        MOV     #CSLMIN,R0
        JSR     PC,PUTSTR
        MOV     CSMINES,R0
        JSR     PC,PUTDEC
        MOV     #CSFTR,R0
        MOV     #14.,R2
        MOV     #20.,R3
        JSR     PC,PUTSTR
CSKEY:  JSR     PC,KWAIT
        CMP     R0,#SCUP
        BNE     CSK1
        TST     CSFIELD
        BEQ     CSDRW
        DEC     CSFIELD
        JMP     CSDRW
CSK1:   CMP     R0,#SCDN
        BNE     CSK2
        CMP     CSFIELD,#2
        BGE     CSDRW
        INC     CSFIELD
        JMP     CSDRW
CSK2:   CMP     R0,#SCLT
        BNE     CSK3
        JSR     PC,CSDEC
        JSR     PC,CSCLMP
        JMP     CSDRW
CSK3:   CMP     R0,#SCRT
        BNE     CSK4
        JSR     PC,CSINC
        JSR     PC,CSCLMP
        JMP     CSDRW
CSK4:   CMP     R0,#SCRTN
        BNE     CSK5
        ; Apply: SETDIF + NEWGAME (clears screen internally), exit.
        MOV     CSCOLS,R2
        MOV     CSROWS,R3
        MOV     CSMINES,R4
        JSR     PC,SETDIF
        JSR     PC,NEWGAME
        CLR     TKR
        RTS     PC
CSK5:   CMP     R0,#SCF9
        BEQ     CSCAN
        JMP     CSKEY                   ; ignore other keys
CSCAN:  JSR     PC,CLRVRM
        JSR     PC,REDRAW
        CLR     TKR
        RTS     PC

;-------------------------------------------------------------------
; CSDEC - decrement value at current CSFIELD by 1 (no clamp here).
CSDEC:  TST     CSFIELD
        BNE     CSD1
        DEC     CSCOLS
        RTS     PC
CSD1:   CMP     CSFIELD,#1
        BNE     CSD2
        DEC     CSROWS
        RTS     PC
CSD2:   DEC     CSMINES
        RTS     PC

;-------------------------------------------------------------------
; CSINC - increment value at current CSFIELD by 1.
CSINC:  TST     CSFIELD
        BNE     CSI1
        INC     CSCOLS
        RTS     PC
CSI1:   CMP     CSFIELD,#1
        BNE     CSI2
        INC     CSROWS
        RTS     PC
CSI2:   INC     CSMINES
        RTS     PC

;-------------------------------------------------------------------
; CSCLMP - clamp CSCOLS, CSROWS, CSMINES to valid ranges.
;   A[] is 1280 bytes with row stride 64, so (NROWS-1)*64 + (NCOLS-1) must
;   stay < 1280.  NCOLS<=38 and NROWS<=20 always fits (19*64+37 = 1253).
;   38 cols * 16 px = 608 px + 16 px frame, fits 640 px screen width.
;   20 rows: (YMAX+2)*8 = 25*8 = 200 scanlines = exactly screen height.
CSCLMP: CMP     CSCOLS,#8.
        BGE     CSCL1
        MOV     #8.,CSCOLS
CSCL1:  CMP     CSCOLS,#38.
        BLE     CSCL2
        MOV     #38.,CSCOLS
CSCL2:  CMP     CSROWS,#8.
        BGE     CSCL3
        MOV     #8.,CSROWS
CSCL3:  CMP     CSROWS,#20.
        BLE     CSCL4
        MOV     #20.,CSROWS
CSCL4:  ; compute max mines = COLS*ROWS - 1, cap at 759.
        CLR     R0
        MOV     CSROWS,R1
CCMUL:  TST     R1
        BEQ     CCMD
        ADD     CSCOLS,R0
        DEC     R1
        BR      CCMUL
CCMD:   DEC     R0
        CMP     R0,#759.
        BLE     CSCL5
        MOV     #759.,R0
CSCL5:  CMP     CSMINES,#1
        BGE     CSCL6
        MOV     #1,CSMINES
CSCL6:  CMP     CSMINES,R0
        BLE     CSCL7
        MOV     R0,CSMINES
CSCL7:  RTS     PC

;-------------------------------------------------------------------
; MNUSHO - main game menu.  Returns when user picks "Resume" or presses F9.
MNUSHO: CLR     MNUSEL
        CLR     MNCLOS
        JSR     PC,CLRVRM               ; clear once on entry from game
MNDR:   JSR     PC,MNDRW                ; redraw in place (no CLRVRM)
MNKEY:  JSR     PC,KWAIT
        CMP     R0,#SCUP
        BNE     MNK1
        TST     MNUSEL
        BEQ     MNKEY
        DEC     MNUSEL
        JMP     MNDR
MNK1:   CMP     R0,#SCDN
        BNE     MNK2
        CMP     MNUSEL,#9.
        BGE     MNKEY
        INC     MNUSEL
        JMP     MNDR
MNK2:   CMP     R0,#SCRTN
        BNE     MNK3
        MOV     MNUSEL,R0
        JSR     PC,MAINVK
        TST     MNCLOS
        BNE     MNEXIT
        ; Sub-screens (HLPSHO/CSSHO/BTSHO) wiped the menu - clear and redraw.
        JSR     PC,CLRVRM
        JMP     MNDR
MNK3:   ; Hotkey dispatch (direct invoke by slot index in new layout).
        CMP     R0,#SCF1
        BNE     MNH1
        CLR     MNUSEL                  ; F1 -> Help (slot 0)
        BR      MNHIN
MNH1:   CMP     R0,#SCF2
        BNE     MNH2
        MOV     #1,MNUSEL               ; F2 -> New
        BR      MNHIN
MNH2:   CMP     R0,#SCF3
        BNE     MNH3
        MOV     #2,MNUSEL               ; F3 -> Beginner
        BR      MNHIN
MNH3:   CMP     R0,#SCF4
        BNE     MNH4
        MOV     #3,MNUSEL               ; F4 -> Intermediate
        BR      MNHIN
MNH4:   CMP     R0,#SCF5
        BNE     MNH5
        MOV     #4,MNUSEL               ; F5 -> Expert
        BR      MNHIN
MNH5:   CMP     R0,#SCF6
        BNE     MNH6
        MOV     #5,MNUSEL               ; F6 -> Custom
        BR      MNHIN
MNH6:   CMP     R0,#SCF7
        BNE     MNH7
        MOV     #6,MNUSEL               ; F7 -> Marker toggle
        BR      MNHIN
MNH7:   CMP     R0,#SCF8
        BNE     MNH8
        MOV     #7,MNUSEL               ; F8 -> Best times
        BR      MNHIN
MNH8:   CMP     R0,#SCF9
        BNE     MNH9
        MOV     #8.,MNUSEL              ; F9 -> Resume
        BR      MNHIN
MNH9:   CMP     R0,#SCF10
        BNE     MNKEY
        MOV     #9.,MNUSEL              ; F10 -> Exit
MNHIN:  MOV     MNUSEL,R0
        JSR     PC,MAINVK
        TST     MNCLOS
        BNE     MNEXIT
        JMP     MNDR
MNEXIT: CLR     MNCLOS
        TST     MNDONE                  ; selection already painted the screen?
        BNE     MNXSK
        JSR     PC,CLRVRM
        JSR     PC,REDRAW
        CLR     TKR
        RTS     PC
MNXSK:  CLR     MNDONE
        RTS     PC

;-------------------------------------------------------------------
; MNDRW - paint the menu in place.  Caller must CLRVRM beforehand on first
; entry (or after a sub-screen wiped the menu).  On selection moves we just
; overwrite the arrow column with " > " / "   " - no full clear needed, so
; the visible content barely changes and there is no flicker.
MNDRW:  MOV     #MNTLBL,R0
        MOV     #38.,R2
        MOV     #2,R3
        JSR     PC,PUTSTR
        MOV     #MNHL,R0
        MOV     #38.,R2
        MOV     #3,R3
        JSR     PC,PUTSTR
        CLR     R5                      ; item index
MDR1:   ; row = 4 + idx (items sit directly under the "====" underline so
        ;                the last slot stays inside the visible screen)
        MOV     R5,R3
        ADD     #4,R3
        MOV     #28.,R2
        ; arrow / blank prefix
        CMP     R5,MNUSEL
        BNE     MDR1B
        MOV     #MNARR,R0
        BR      MDR1C
MDR1B:  MOV     #MNBLK,R0
MDR1C:  JSR     PC,PUTSTR
        ; Special-case Marker item (slot 6 in MNUTAB -> M6LBL): patch in place.
        CMP     R5,#6
        BNE     MDR1D
        ; M6LBL bytes [8..11] = "VKL " (on) or "VYKL" (off) in KOI-8R:
        ;   V=0367  Y=0371  K=0353  L=0354  space=040
        TST     MARKER
        BNE     MDR1M
        MOVB    #367,M6LBL+8.
        MOVB    #371,M6LBL+9.
        MOVB    #353,M6LBL+10.
        MOVB    #354,M6LBL+11.
        BR      MDR1D
MDR1M:  MOVB    #367,M6LBL+8.
        MOVB    #353,M6LBL+9.
        MOVB    #354,M6LBL+10.
        MOVB    #40,M6LBL+11.
MDR1D:  ; label from MNUTAB
        MOV     R5,R0
        ASL     R0
        ADD     #MNUTAB,R0
        MOV     (R0),R0
        JSR     PC,PUTSTR
        INC     R5
        CMP     R5,#10.
        BLT     MDR1
        ; Footer
        MOV     #MNFTR,R0
        MOV     #14.,R2
        MOV     #20.,R3
        JSR     PC,PUTSTR
        RTS     PC

;-------------------------------------------------------------------
; MAINVK - invoke menu action by index R0 (0..9).  Sets MNCLOS when the
;         menu should exit afterwards; otherwise stays open for redraw.
; Shared exits: MAINOK closes the menu without further drawing (caller
; already painted the screen); MAINW starts a fresh game then closes.
MAINOK: MOV     #1,MNDONE
        MOV     #1,MNCLOS
        RTS     PC
MAINW:  JSR     PC,NEWGAME              ; NEWGAME does its own CLRVRM
        BR      MAINOK

; Items 1..4 jump to MAINW after setting difficulty (or no-op for "New").
; Sub-screens (Custom / Best / Version) restore the screen themselves so
; they tail-call MAINOK.  Resume leaves MNDONE=0 so MNEXIT does the redraw.
MAINVK: TST     R0
        BNE     MAI1
        JSR     PC,HLPSHO               ; 0 = Help (F1)
        BR      MAINOK
MAI1:   CMP     R0,#1
        BNE     MAI2
        BR      MAINW                   ; 1 = New, same difficulty
MAI2:   CMP     R0,#2
        BNE     MAI3
        MOV     #8.,R2                  ; 2 = Beginner
        MOV     #8.,R3
        MOV     #10.,R4
        JSR     PC,SETDIF
        BR      MAINW
MAI3:   CMP     R0,#3
        BNE     MAI4
        MOV     #16.,R2                 ; 3 = Intermediate
        MOV     #16.,R3
        MOV     #40.,R4
        JSR     PC,SETDIF
        BR      MAINW
MAI4:   CMP     R0,#4
        BNE     MAI5
        MOV     #30.,R2                 ; 4 = Expert
        MOV     #16.,R3
        MOV     #99.,R4
        JSR     PC,SETDIF
        BR      MAINW
MAI5:   CMP     R0,#5
        BNE     MAI6
        JSR     PC,CSSHO                ; 5 = Custom (sub-dialog)
        BR      MAINOK
MAI6:   CMP     R0,#6
        BNE     MAI7
        ; 6 = Marker toggle (stay in menu)
        TST     MARKER
        BNE     MAI6A
        MOV     #1,MARKER
        RTS     PC
MAI6A:  CLR     MARKER
        RTS     PC
MAI7:   CMP     R0,#7
        BNE     MAI8
        JSR     PC,BTSHO                ; 7 = Best times
        BR      MAINOK
MAI8:   CMP     R0,#8.
        BNE     MAI9
        ; 8 = Resume (close menu, redraw game)
        MOV     #1,MNCLOS
        RTS     PC
MAI9:   CMP     R0,#9.
        BNE     MAIX
        JMP     EXIT                    ; 9 = Exit
MAIX:   RTS     PC

;-------------------------------------------------------------------
RND:    MOV     SEED,R0
        ASL     R0
        ASL     R0
        ADD     SEED,R0
        INC     R0
        BIC     #100000,R0
        MOV     R0,SEED
        RTS     PC

;-------------------------------------------------------------------
; DRAWSP - paint sprite #R4 at char-grid (R2, R3) via direct copy.
DRAWSP: MOV     #3377,@#177400
        MOV     R4,R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0
        ADD     #S,R0
        MOV     R3,R1
        ASL     R1
        ASL     R1
        ASL     R1
        MOV     R1,R5
        ASL     R1
        ASL     R1
        ASL     R1
        ASL     R1
        ASL     R1
        ASL     R5
        ASL     R5
        ASL     R5
        ADD     R5,R1
        ADD     R2,R1
        INC     R1
        ASL     R1
        ADD     #VRAM,R1
        MOV     #8.,R5
DL:     MOV     (R0)+,(R1)
        ADD     #80.,R1
        DEC     R5
        BNE     DL
        RTS     PC

;-------------------------------------------------------------------
; DRAWCS - like DRAWSP but BIS each word (OR-overlay).
DRAWCS: MOV     #3377,@#177400
        MOV     R4,R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0
        ADD     #S,R0
        MOV     R3,R1
        ASL     R1
        ASL     R1
        ASL     R1
        MOV     R1,R5
        ASL     R1
        ASL     R1
        ASL     R1
        ASL     R1
        ASL     R1
        ASL     R5
        ASL     R5
        ASL     R5
        ADD     R5,R1
        ADD     R2,R1
        INC     R1
        ASL     R1
        ADD     #VRAM,R1
        MOV     #8.,R5
DLC:    BIS     (R0)+,(R1)
        ADD     #80.,R1
        DEC     R5
        BNE     DLC
        RTS     PC

;-------------------------------------------------------------------
SEED:   .WORD   1
CURX:   .WORD   0
CURY:   .WORD   0
KOK:    .WORD   0
GAMEST: .WORD   0
NMARK:  .WORD   0
SECS:   .WORD   0
TKR:    .WORD   0
TMSTRT: .WORD   0
HELPPG: .WORD   0
MARKER: .WORD   1.                      ; 1 = marker ON (3-state), 0 = OFF
MNUSEL: .WORD   0
MNCLOS: .WORD   0
MNDONE: .WORD   0                       ; 1 = MAINVK action already painted the screen
BTBEG:  .WORD   999.
BTINT:  .WORD   999.
BTEXP:  .WORD   999.
CSCOLS: .WORD   8.
CSROWS: .WORD   8.
CSMINES: .WORD  10.
CSFIELD: .WORD  0
ORIGDP: .WORD   0
ORIG10: .WORD   0
ORG102: .WORD   0
ORIG66: .WORD   0
ORG132: .WORD   0
ORIGRC: .WORD   0                       ; Sys Reg C byte at startup (border etc.)
ORIGSP: .WORD   0                       ; RT-11's stack pointer at startup
NCOLS:  .WORD   8.
NROWS:  .WORD   8.
NMINES: .WORD   10.
XMIN:   .WORD   16.
XMAX:   .WORD   23.
YMIN:   .WORD   4.
YMAX:   .WORD   11.
TOTCEL: .WORD   64.

; File-I/O work area + DBLK for the K.HLP load at startup.  AREA is sized
; for .READW (5 words); .LOOKUP only uses the first 3.
; .READW area block and the SAPER.HLP DBLK (SY:SAPER.HLP).
LKAREA: .BLKW   5
KHFILE: .RAD50  /SY /                   ; boot/system device
        .RAD50  /SAP/                   ; "SAPER" RAD50 = "SAP" + "ER "
        .RAD50  /ER /
        .RAD50  /HLP/

; A[] is the game-state grid (one byte per cell, row*64+col).  Cleared
; by NEWGAME at every restart, so it doesn't matter that .BLKB seeds it
; with zeros from the SAV load.
A:      .BLKB   1280.

S:
"""

for i in range(0, 512, 8):
    src += '   .WORD   ' + ','.join(f'{w:o}' for w in words[i:i+8]) + '\n'

# ---------------------------------------------------------------------------
# Russian menu / dialog text constants.  Each glyph is one KOI-8R byte (the
# same encoding the FONT table indexes), so we emit raw .BYTE directives.
# Track every KOI-8R byte that will ever be drawn, so we can build a
# compact glyph pool indexed via FNTMAP.
USED_BYTES = set()

def _emit_str(label, text, width=None):
    raw = text.encode('koi8-r')
    if width and len(raw) < width:
        raw = raw + b' ' * (width - len(raw))
    USED_BYTES.update(raw)
    chunks = []
    for i in range(0, len(raw), 16):
        chunks.append('   .BYTE   ' + ','.join(f'{b:o}' for b in raw[i:i+16]))
    return f'{label}:\n' + '\n'.join(chunks) + '\n   .BYTE   0\n'

src += '\n'
src += _emit_str('MNTLBL', 'МЕНЮ')
src += _emit_str('MNHL',   '====')
src += _emit_str('MNARR',  ' > ')
src += _emit_str('MNBLK',  '   ')
src += _emit_str('MNFTR',  'Вверх/Вниз: выбор    Ввод: выполнить')

# Menu items - 24 KOI-8R bytes each (3-char prefix takes us to col 31, label
# 24 chars puts hotkey end at col 55).  Marker label uses "[ВКЛ ]" padding
# patched in place at draw time (bytes 8..11 of M6LBL).
src += _emit_str('M0LBL', 'Помощь                F1', 24)
src += _emit_str('M1LBL', 'Новая игра            F2', 24)
src += _emit_str('M2LBL', 'Новичок               F3', 24)
src += _emit_str('M3LBL', 'Любитель              F4', 24)
src += _emit_str('M4LBL', 'Эксперт               F5', 24)
src += _emit_str('M5LBL', 'Свои настройки        F6', 24)
src += _emit_str('M6LBL', 'Маркер [ВКЛ ]         F7', 24)
src += _emit_str('M7LBL', 'Рекорды               F8', 24)
src += _emit_str('M8LBL', 'Выход                F10', 24)
src += _emit_str('M9LBL', 'В игру                F9', 24)

# MNUTAB slots: 0 = Help (M0), 1-7 = New..Best (M1..M7), 8 = Resume (M9, F9),
# 9 = Exit (M8, F10).  The Marker label is M6LBL at slot 6 (MNDRW patches it).
# Each M*LBL block is 25 bytes (24 text + null) so the 10 labels end on an
# ODD byte boundary -- .EVEN forces MNUTAB to start on a word-aligned slot,
# otherwise the MOV (R0),R0 in MDR1D would read at addr-1 and shift the
# whole menu down by one slot.
src += '\n.EVEN\nMNUTAB: .WORD   M0LBL,M1LBL,M2LBL,M3LBL,M4LBL\n'
src += '        .WORD   M5LBL,M6LBL,M7LBL,M9LBL,M8LBL\n\n'

src += _emit_str('BTHDR',  'РЕКОРДЫ')
src += _emit_str('BTLBEG', 'Новичок        : ')
src += _emit_str('BTLINT', 'Любитель       : ')
src += _emit_str('BTLEXP', 'Эксперт        : ')
src += _emit_str('BTSECS', ' сек')
src += _emit_str('BTNONE', '  --- ')

src += _emit_str('CSHDR',  'СВОИ НАСТРОЙКИ')
src += _emit_str('CSARR',  ' > ')
src += _emit_str('CSBLK',  '   ')
src += _emit_str('CSLCOL', 'Столбцы: ')
src += _emit_str('CSLROW', 'Строки : ')
src += _emit_str('CSLMIN', 'Мины   : ')
src += _emit_str('CSFTR',  'Вверх/Вниз: поле   Влево/Вправо: -/+   Ввод: старт   F9: отмена')

src += _emit_str('FTRMSG', 'Нажмите любую клавишу')

src += '\n        .EVEN\n'


def _emit_bytes(label, data, comment=''):
    out = [f'\n{label}:                                ; {comment} ({len(data)} bytes)']
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        out.append('   .BYTE   ' + ','.join(f'{b:o}' for b in chunk))
    return '\n'.join(out) + '\n'

# KHLP and NSEQ are absolute equates pointing to bank 4 RAM; the SAPER.HLP
# disk file is .READW-loaded into those buffers at startup (see START).
# Storing them in SAV pushed FNTGLY past the VRAM window at 0o40000 and
# the high-index glyphs rendered as pixel garbage.

# Build the compact font: 256-byte index map + only-used glyph pool.
# Pull all bytes that appear in K.HLP (decrypted) plus the runtime digits
# emitted by PUTDEC (0..9), which never went through _emit_str.
for i in range(3680):
    USED_BYTES.add(KHLP_BYTES[i] if i < 2
                   else (KHLP_BYTES[i] - NSEQ_BYTES[i - 2]) & 0xFF)
USED_BYTES.update(b'0123456789')

# Drop control chars and any byte whose glyph isn't in the CGA font.
needed = []
for b in sorted(USED_BYTES):
    if b < 0x20:
        continue
    if _koi8r_to_glyph(b) is None:
        continue
    needed.append(b)

FNTMAP_BYTES = bytearray(256)               # 0 = blank glyph slot
glyph_pool = [bytes(8)]                     # index 0 = all-zero glyph
for b in needed:
    FNTMAP_BYTES[b] = len(glyph_pool)
    glyph_pool.append(bytes(_koi8r_to_glyph(b)))
FNTGLY_BYTES = b''.join(glyph_pool)

src += _emit_bytes('FNTMAP', bytes(FNTMAP_BYTES),
                   'KOI-8R byte -> compact glyph index')
src += _emit_bytes('FNTGLY', FNTGLY_BYTES,
                   f'compact 8x8 glyph pool ({len(glyph_pool)} entries)')
print(f'  compact font: {len(needed)} chars, '
      f'FNTMAP={len(FNTMAP_BYTES)}B + FNTGLY={len(FNTGLY_BYTES)}B '
      f'= {256 + len(FNTGLY_BYTES)}B (was 2048B direct table)')
src += '\n        .END    START\n'
OUT_MAC.write_text(src, encoding='ascii')
print(f'SAPER.MAC: {len(src)} bytes, {src.count(chr(10))} lines')
