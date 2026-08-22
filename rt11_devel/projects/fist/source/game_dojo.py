"""The dojo background in the game: the bg engine (gen_fist) rendering
into SCRBUF, BUILDDB's pre-converted DOJOBUF, RENDBG, the bg tables.

Every function returns MACRO-11 text; game_build.py assembles the game.
"""
import os

import gen_fist
from bg_data import emit_all
from gst_addr import g

EQUS = ("LMARG  = 8.\nTMARG  = 4.\nLSTRID = 80.\n"
        "SVBASE = 40000\nSVATTR = 54000\nSVTOP  = 40200\n")
BGVARS = ("        .EVEN\nBGREF:  .WORD   0                ; background reference ($5F00), 1..3\n"
          "BGTAB:  .WORD   BG1DEF,BG2DEF,BG3DEF\n")


def ovl_ink(withbg):
    """The fighter ink in the overlay: white on the plain black-background game
    (else the fighter is black-on-black), BLACK over the dojo (black figures
    on the light dojo paper, as on the Spectrum) - keep the dojo paper colour
    either way."""
    return ("BICB    #7,1(R0)             ; black ink, keep the dojo paper colour"
            if withbg else
            "BISB    #107,1(R0)           ; white bright ink on the black background")


def cell_restore(withbg):
    """HUD cells: restore the clean dojo cell (DOJOBUF is the picture in VRAM
    row layout, rows offset by the 4-row top margin) before the glyph goes
    on, so a changed digit / a cleared yin-yang leaves no trace.  No dojo ->
    clear."""
    return ("MOV     DOJOBUF-VRAM-320.(R0),(R0) ; restore the clean dojo cell row"
            if withbg else "CLR     (R0)                 ; clear the cell row")


def boot_override(bgn):
    """FGHT_BG: override the opening background ($AF34)."""
    if "FGHT_BG" not in os.environ:
        return ""
    return f"        MOVB    #{bgn}.,{g(0xAF34)}     ; FGHT_BG: opening background override\n"


def row():
    """Per-row: copy the clean dojo row for ROWN (DOJOBUF, VRAM row layout)
    straight over the VRAM row, then the fighter overlays write over it,
    zero cells transparent -> the dojo shows through.
    NB: R2 holds the persistent VRAM row pointer across the whole CLOOP
    iteration (set before CLOOP, advanced at C2SK) - so this must touch
    only R0/R1/R3/R4/R5 and leave R2 alone."""
    return ("""        MOV     ROWN,R4              ; dojo row: y = ROWN - TMARG
        SUB     #TMARG,R4
        BLT     CCLR                 ; above the dojo band -> clear the row
        CMP     R4,#192.
        BGE     CCLR                 ; below the dojo band -> clear the row
        MOV     R4,R0                ; src = DOJOBUF + y*80 + 8 (pre-converted, VRAM row layout)
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0
        MOV     R0,R1
        ASL     R0
        ASL     R0
        ADD     R1,R0
        ADD     #DOJOBUF+8.,R0
        MOV     R2,R1                ; straight into the VRAM row, past the 4-cell margin
        ADD     #8.,R1
"""
            # copy the 32 clean dojo cells fully unrolled (no loop overhead per word)
            + "".join("        MOV     (R0)+,(R1)+\n" for _ in range(32))
            + """        BR      CDDN                 ; the 32 picture cells are rewritten in place;
                                     ; the margins stay black (fighters are clamped to the picture)
""")


def engine(bgn):
    """The bg engine from gen_fist.PROGRAM (CHGBG onwards), adapted: main_game
    owns ORIGRC (datblk) and its own exit, so the engine's copies go (ORIGDP
    is only used by the demo's EXITP, which we don't include); BGREF / BGTAB
    (banks 0-1, always mapped) replace Change_Background's built-in
    definition ($5F3C..$5F52: BGREF 1..3 picks the definition table)."""
    eng_start = gen_fist.PROGRAM.index(
        ";-------------------------------------------------------------------\n; CHGBG")
    eng = gen_fist.PROGRAM[eng_start:]
    eng = (eng.replace("ORIGDP: .WORD   0\n", "")
           .replace("ORIGRC: .WORD   0\n", "")
           .replace("BGREF:  .WORD   0                       ; selected background (1..3)\n", "")
           .replace("%BGN%", str(bgn)))
    assert eng.count("        MOV     #%BGDEF%,R1") == 1
    return eng.replace(
        "        MOV     #%BGDEF%,R1            ; definition for the built-in background\n",
        "        MOV     BGREF,R1               ; background reference 1..3 ($5F00)\n"
        "        ASL     R1\n"
        "        MOV     BGTAB-2(R1),R1         ; -> its definition table\n")


# BUILDDB: pre-convert the whole dojo (SCRBUF Spectrum planes -> VRAM word
# format) into DOJOBUF once at boot.  Same per-cell convert as the CLOOP, but
# for all 192 dojo rows, stored in the VRAM row layout (40 words/row, the
# 4-cell margins zero) so DOJOBUF + (vram - VRAM - 4*80) is the clean cell
# under any picture cell - the HUD restores cells from it.  Runs at 3377.
# No row table: SPSCR / BUILDDB compute the Spectrum row offset (ROWOFF), and
# the per-frame compositor reads DOJOBUF - banks 0-1 are full, every byte
# there counts.
BUILDDB = """
BUILDDB: CLR     R2                   ; dojo y = 0..191
BDB1:    MOV     R2,R0                ; pix = SCRBUF + ROWOFF(y)
        JSR     PC,ROWOFF
        ADD     #SCRBUF,R1
        MOV     R2,R5                ; attr = SCRBUF+6144 + (y>>3)*32
        ASR     R5
        ASR     R5
        ASR     R5
        ASL     R5
        ASL     R5
        ASL     R5
        ASL     R5
        ASL     R5
        ADD     #SCRBUF+6144.,R5
        MOV     R2,R0                ; dst = DOJOBUF + y*80 (VRAM row layout, 40 words)
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0
        MOV     R0,R3
        ASL     R0
        ASL     R0
        ADD     R3,R0
        ADD     #DOJOBUF,R0
        CLR     (R0)+                ; 4-cell left margin
        CLR     (R0)+
        CLR     (R0)+
        CLR     (R0)+
        MOV     #32.,R3
BDB2:    MOVB    (R5)+,R4
        SWAB    R4
        BIC     #377,R4
        BISB    (R1)+,R4
        MOV     R4,(R0)+
        DEC     R3
        BNE     BDB2
        CLR     (R0)+                ; 4-cell right margin
        CLR     (R0)+
        CLR     (R0)+
        CLR     (R0)+
        INC     R2
        CMP     R2,#192.
        BLO     BDB1
        RTS     PC
"""


def block(bgn, boot_code):
    """The dojo block at 0100000 in the PRIMARY banks 4-6 (embedded in the
    .SAV), which the compositor's 3377 banking makes visible; the GST loads
    into the EXTENDED banks 12-14 at the same window (decode's 3217 banking).
    One dispatcher bit switches between them, so the 6912 B SCRBUF costs
    nothing in the tight banks 0-1.  Holds the engine, BUILDDB, the loader,
    SCRBUF and (as an address EQU, not reserved storage, so it stays out of
    the .SAV image) DOJOBUF: the dojo pre-converted to VRAM word format (40
    words x 192 rows = the picture's VRAM rows 4..195, margins zero), built
    once at boot; the compositor copies a band row from here instead of
    re-converting SCRBUF every frame (the ~62%-of-frame cost)."""
    return ("\n        .ASECT\n        . = 100000\n"
            + engine(bgn) + BUILDDB + boot_code
            + "\n        .EVEN\nSCRBUF: .BLKB   6912.\n"
            + "DOJOBUF = SCRBUF+6912.\n        .EVEN\n")


def tables(buf):
    """All three backgrounds' tables (UDGs, position + attribute streams) in
    the primary bank 2 at 040000: under the VRAM window, so only reachable
    with the window off - which is how RENDBG runs CHGBG.  RT-11 loads it as
    plain RAM (the monitor runs with VRAM off); it must end below BUF."""
    bgdat, bgdat_len = emit_all((1, 2, 3))
    assert 0o40000 + bgdat_len <= buf, f"bg data {bgdat_len} B overruns BUF {buf:o}"
    return "\n        .ASECT\n        . = 40000\n" + bgdat


def rendbg():
    """RENDBG: render the background $AF34 selects and present it."""
    return f"""        ; --- RENDBG: render the background $AF34 selects and present it ($9200 ->
        ;     Change_Background).  The bg tables sit in bank 2 (040000, under the
        ;     VRAM window) and the engine + SCRBUF in the primary banks 4-6, so the
        ;     render runs with every slot primary and the window OFF; the convert +
        ;     present then run with the window on.  Returns at GAME banking. --------
RENDBG: MOVB    {g(0xAF34)},R0
        BIC     #177400,R0
        MOV     R0,BGREF
        MOV     #3177,@#DISPAT       ; all slots primary, VRAM window off
        JSR     PC,CHGBG             ; bg tables -> SCRBUF (Spectrum format)
        MOV     #3377,@#DISPAT       ; VRAM on @40000, banks 4-6 primary
        JSR     PC,BUILDDB           ; SCRBUF -> DOJOBUF (VRAM word format)
        JSR     PC,SPSCR             ; present the dojo 1:1 centred
        MOV     #1,HUDDRT            ; the strip was wiped: redraw it next frame
        MOV     #GAME,@#DISPAT
        RTS     PC
"""
