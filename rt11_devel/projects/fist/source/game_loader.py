"""The boot: the .SAV's start-up, the FIST.DAT loader (chunked .READW +
park / copy into the extended banks), the loading screen.

Every function returns MACRO-11 text; game_build.py assembles the game.
"""
# The .DAT travels LZSS-packed (source/lzss.py).  A piece is read into STAGE,
# expanded into BUF - the top of the primary banks 2-3, just under the dojo
# block at 0100000, plain RAM because VRAM is off under RT-11 - and copied
# from there into the parked extended banks.  STAGE is the gap between the
# backgrounds' tables (which end at 057600) and BUF; a read is whole blocks,
# so a piece's packed form plus its offset into the first block must fit it,
# which is what MAX_PACKED leaves room for.
CHUNK = 8                                    # blocks a piece expands to (4 KB)
BUF = 0o100000 - CHUNK * 512
STAGE = 0o57600                              # packed bytes are read here
STAGE_ROOM = BUF - STAGE                     # 4224 B
MAX_PACKED = 3584                            # + a 511-byte offset = 8 whole blocks


class Piece:
    """One packed piece of FIST.DAT: where it goes, how big it is raw, and
    where its bytes are (block, byte offset in it, packed length)."""

    def __init__(self, dest, raw, block, offset, packed):
        self.dest, self.raw, self.block = dest, raw, block
        self.offset, self.packed = offset, packed

    def blocks(self):
        """Whole blocks the read must cover to bring the piece in."""
        return -(-(self.offset + self.packed) // 512)


def preamble():
    """The .TITLE, the .MCALLs and the I/O / banking equates."""
    return (
        "        .TITLE  FIST\n"
        "        .MCALL  .FETCH,.LOOKUP,.READW,.CLOSE,.EXIT\n"
        "DISPAT = 177400\nSYSC   = 177604\nVRAM   = 40000\nVRAMEN = 100000\n"
        f"KBST   = 177442\nGST    = 100000\nHSPACE = 30000\nBUF    = {BUF:o}\n"
        "EXT    = 17\nPRIM   = 177\nGAME   = 3217\n")


def expand(piece, into):
    """Read a piece and expand it - the packed bytes into STAGE, the result
    at `into`.  Whole blocks are read, so the piece starts `offset` in."""
    assert piece.packed, "a stored piece would have to be block-aligned"
    assert piece.blocks() * 512 <= STAGE_ROOM, "the packed piece overruns STAGE"
    return f"""        .READW  #LKAREA,#0,#{STAGE:o},#{piece.blocks() * 256}.,#{piece.block}.
        BCC     .+6
        JMP     LDERR
        MOV     #{STAGE + piece.offset:o},R1
        MOV     #{into},R2
        MOV     #{piece.raw}.,R3
        JSR     PC,UNPK
"""


def reads(pieces):
    """Every GST piece: read, expand into BUF, copy up into the banks."""
    out = ""
    for piece in pieces:
        out += expand(piece, f"{BUF:o}")
        out += f"""        MOV     #GST+{piece.dest}.,R1
        MOV     #{piece.raw // 2}.,R2
        JSR     PC,CHUNK
"""
    return out


def unpacker():
    """The LZSS decoder, the same format FLOAD's copy decodes (see
    source/lzss.py for the format and FLOAD.MAC for the other copy):
    R1 = packed bytes, R2 = where they go, R3 = bytes to produce."""
    return """        ; UNPK: LZSS - a flag byte, then eight items, low bit first; a set
        ; bit is a literal byte, a clear one two bytes giving a distance of
        ; up to 4095 and a length of 3..18.  A match copies from what is
        ; already written, so a run comes out by itself.
UNPK:   TST     R3
        BEQ     29$
21$:    CLR     R5
        BISB    (R1)+,R5
        BIS     #400,R5                ; a sentinel above the eight flags
22$:    ASR     R5
        BCC     23$
        MOVB    (R1)+,(R2)+
        DEC     R3
        BR      28$
23$:    CLR     R0
        BISB    (R1)+,R0
        CLR     R4
        BISB    (R1)+,R4
        MOV     R4,-(SP)
        BIC     #177417,R4
        ASL     R4
        ASL     R4
        ASL     R4
        ASL     R4
        ADD     R4,R0                  ; R0 = the distance
        MOV     (SP)+,R4
        BIC     #177760,R4
        ADD     #3,R4                  ; 3..18
        MOV     R2,-(SP)
        SUB     R0,(SP)
        MOV     (SP)+,R0
24$:    MOVB    (R0)+,(R2)+
        DEC     R3
        BEQ     29$
        SOB     R4,24$
28$:    TST     R3
        BEQ     29$
        CMP     R5,#1                  ; the sentinel down to bit 0: eight done
        BNE     22$
        BR      21$
29$:    RTS     PC
"""


def title_load(scr):
    """Expand the loading screen into SCRBUF and present it before the state
    loads."""
    read = "".join(expand(p, f"SCRBUF+{p.dest}.") for p in scr)
    return f"""        ; --- the loading screen: expand it into SCRBUF (plain RAM under
        ;     RT-11), switch to the medium-res colour mode and present it -
        ;     then load the game state behind it, as the tape loader did ---
{read}        MTPS    #340
        MOVB    @#SYSC,R0
        BIC     #17,R0
        MOVB    R0,@#SYSC
        MOVB    R0,RCSHAD              ; reg C shadow: the sound driver toggles bit 6 in it
        MOV     #3377,@#DISPAT         ; VRAM on @40000, banks 4-6 primary (SCRBUF)
        MOV     #VRAM,R0
7$:     CLR     (R0)+
        CMP     R0,#VRAMEN
        BLO     7$
        JSR     PC,SPSCR
        JSR     PC,BORDER              ; the cyan border
        MOV     #3177,@#DISPAT         ; window off again for the reads (the picture stays)
        MTPS    #0
"""


def after_load(withbg):
    """After the state is loaded: with the loading screen, hold it ~3 s or
    until fire / "1"; else just set the medium video mode.  Both end at BOOT2."""
    if withbg:
        return """        ; --- state loaded: hold the loading screen ~3 s, or until fire / "1" ---
        MTPS    #340
        MOV     #1000.,R3
8$:     JSR     PC,KSCAN
        TST     KTFR
        BNE     9$
        TST     KSTART
        BNE     9$
        MOV     #1250.,R4              ; ~3 ms
81$:    SOB     R4,81$
        SOB     R3,8$
9$:     CLR     KSTART
        CLR     KTFR
        JMP     BOOT2
"""
    return """        ; --- GST loaded; set medium video ---
        MTPS    #340
        MOVB    @#SYSC,R0
        BIC     #17,R0
        MOVB    R0,@#SYSC
        MOVB    R0,RCSHAD              ; reg C shadow: the sound driver toggles bit 6 in it
        JMP     BOOT2
"""


def boot(withbg, pieces, scr):
    """BOOT: .FETCH / .LOOKUP FIST.DAT, the loading screen, the chunk reads,
    the hold.  Boot-only code: it lives in the dojo block at 0100000 when
    there is one (banks 0-1 are full) and runs there at RT-11's all-primary
    banking; the chunk copies (which hide banks 4-6) go through CHUNK in
    banks 0-1."""
    title = title_load(scr) if withbg else ""
    return f"""BOOT:   .FETCH  #HSPACE,#DATFIL
        BCC     .+6
        JMP     LDERR
        .LOOKUP #LKAREA,#0,#DATFIL
        BCC     .+6
        JMP     LDERR
{title}{reads(pieces)}        .CLOSE  #0
{after_load(withbg)}{unpacker()}"""


def start(boot_inline, dojo_boot):
    """START (the .SAV entry), CHUNK, BOOT2 (the post-load entry) up to GLOOP."""
    return f"""        .ASECT
        . = 44
        .WORD   21000                  ; JSW (file-I/O .SAV flags)
        . = 1000
        .EVEN
START:  MOV     #37776,SP              ; stack above the code, below BUF
        MOVB    @#SYSC,ORIGRC
        JMP     BOOT                   ; the loader (boot-only code, see boot_code)
{boot_inline}        ; CHUNK: copy R2 words from BUF into the parked extended banks at R1
CHUNK:  MTPS    #340
        MOVB    #EXT,@#DISPAT
        MOV     #BUF,R0
1$:     MOV     (R0)+,(R1)+
        SOB     R2,1$
        MOVB    #PRIM,@#DISPAT
        MTPS    #0
        RTS     PC
BOOT2:  MOV     #GAME,@#DISPAT         ; 03217: VRAM on, window @40000, banks 4-6 ext
        MOV     #VRAM,R0
3$:     CLR     (R0)+
        CMP     R0,#VRAMEN
        BLO     3$
        MOVB    #164,@#177526          ; timer channel 1: mode 2, binary, the full
        MOVB    #0,@#177522            ;   65536 count - the frame pace's clock
        MOVB    #0,@#177522
        JSR     PC,TSYNC
        MOV     #3003,@#DISPAT         ; the sprite cache (extended banks 10-11): empty
        MOV     #40000,R0
10$:    CLR     (R0)+
        CMP     R0,#100000
        BLO     10$
        MOV     #GAME,@#DISPAT
        ; --- $AC3E Start_1UP_Game: the match-state batch (P1 human, P2 the
        ;     computer, score 0, rank 0), then the first opponent's set-up (the
        ;     background) and a new round.  FIST.DAT is a mid-attract snapshot, so
        ;     every cell this touches is deliberately re-initialised here. ---
        MOV     #12345.,RSEED
        JSR     PC,DINIT             ; $AC05: the attract demo first ($9C2C = 0)
{dojo_boot}        JSR     PC,SETUP
        ; tell the MS7004 keyboard 0o231 (keyclick off) - the firmware treats this as the
        ; "a game is running" signal and switches auto-repeat to the fast game preset
        ; (125 ms delay vs 250 ms typing), so held-key tracking is snappier.
83$:    MOVB    @#177442,R0          ; wait for the keyboard UART transmitter
        BITB    #1,R0                ; TXRDY?
        BEQ     83$
        MOVB    #231,@#177440
"""
