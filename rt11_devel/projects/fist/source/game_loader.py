"""The boot: the .SAV's start-up, the GST.DAT loader (chunked .READW +
park / copy into the extended banks), the loading screen.

Every function returns MACRO-11 text; game_build.py assembles the game.
"""
# The .DAT is read in CHUNK-block pieces into BUF, the top of the primary
# banks 2-3 just under the dojo block at 0100000 (VRAM is off under RT-11, so
# that is plain RAM), each piece copied into the parked extended banks.  The
# rest of banks 2-3 (040000..BUF) is free for data the .SAV carries itself -
# the three backgrounds' tables live there (game_dojo.tables).
CHUNK = 8                                    # blocks per .READW (4 KB)
BUF = 0o100000 - CHUNK * 512


def preamble():
    """The .TITLE, the .MCALLs and the I/O / banking equates."""
    return (
        "        .TITLE  FIST\n"
        "        .MCALL  .FETCH,.LOOKUP,.READW,.CLOSE,.EXIT\n"
        "DISPAT = 177400\nSYSC   = 177604\nVRAM   = 40000\nVRAMEN = 100000\n"
        f"KBST   = 177442\nGST    = 100000\nHSPACE = 30000\nBUF    = {BUF:o}\n"
        "EXT    = 17\nPRIM   = 177\nGAME   = 3217\n")


def chunks(nblocks):
    """(start block, blocks) of each .READW of GST.DAT."""
    return [(sb, min(CHUNK, nblocks - sb)) for sb in range(0, nblocks, CHUNK)]


def reads(nblocks):
    """The chunk reads: each .READW into BUF, then CHUNK copies it up."""
    out = ""
    for sb, nb in chunks(nblocks):
        out += f"""        .READW  #LKAREA,#0,#BUF,#{nb * 256}.,#{sb}.
        BCC     .+6
        JMP     LDERR
        MOV     #GST+{sb * 512}.,R1
        MOV     #{nb * 256}.,R2
        JSR     PC,CHUNK
"""
    return out


def title_load(scrblk):
    """Read the loading screen (block `scrblk` of GST.DAT) into SCRBUF and
    present it before the state loads."""
    return f"""        ; --- the loading screen: read it into SCRBUF (plain RAM under RT-11),
        ;     switch to the medium-res colour mode and present it - then load
        ;     the game state behind it, as the tape loader did ---
        .READW  #LKAREA,#0,#SCRBUF,#3456.,#{scrblk}.
        BCC     .+6
        JMP     LDERR
        MTPS    #340
        MOVB    @#SYSC,R0
        BIC     #17,R0
        MOVB    R0,@#SYSC
        MOVB    R0,RCSHAD              ; reg C shadow: the sound driver toggles bit 5 in it
        MOV     #3377,@#DISPAT         ; VRAM on @40000, banks 4-6 primary (SCRBUF)
        MOV     #VRAM,R0
7$:     CLR     (R0)+
        CMP     R0,#VRAMEN
        BLO     7$
        JSR     PC,SPSCR
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
        MOV     #2500.,R4              ; ~3 ms
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
        MOVB    R0,RCSHAD              ; reg C shadow: the sound driver toggles bit 5 in it
        JMP     BOOT2
"""


def boot(withbg, nblocks, scrblk):
    """BOOT: .FETCH / .LOOKUP GST.DAT, the loading screen, the chunk reads,
    the hold.  Boot-only code: it lives in the dojo block at 0100000 when
    there is one (banks 0-1 are full) and runs there at RT-11's all-primary
    banking; the chunk copies (which hide banks 4-6) go through CHUNK in
    banks 0-1."""
    title = title_load(scrblk) if withbg else ""
    return f"""BOOT:   .FETCH  #HSPACE,#DATFIL
        BCC     .+6
        JMP     LDERR
        .LOOKUP #LKAREA,#0,#DATFIL
        BCC     .+6
        JMP     LDERR
{title}{reads(nblocks)}        .CLOSE  #0
{after_load(withbg)}"""


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
        MOV     #3003,@#DISPAT         ; the sprite cache (extended banks 10-11): empty
        MOV     #40000,R0
10$:    CLR     (R0)+
        CMP     R0,#100000
        BLO     10$
        MOV     #GAME,@#DISPAT
        ; --- $AC3E Start_1UP_Game: the match-state batch (P1 human, P2 the
        ;     computer, score 0, rank 0), then the first opponent's set-up (the
        ;     background) and a new round.  GST.DAT is a mid-attract snapshot, so
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
