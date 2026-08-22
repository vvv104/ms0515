"""The per-frame draw: the two fighters' decode (with the decode and
sprite caches), the geometry, the in-place row compositor, the HUD gate.

Every function returns MACRO-11 text; game_build.py assembles the game.
"""
from gst_addr import g


def frame_head(dbgmove):
    """GLOOP: the keyboard, the quit chord, the demo start, the logic frame, the sound."""
    return f"""GLOOP:  MOV     #GAME,@#DISPAT       ; (re-)park: 03217, banks 4-6 extended
        MOV     #W,R0                ; clear decoder scratch
        MOV     #32.,R1
4$:     CLR     (R0)+
        DEC     R1
        BNE     4$
        ; --- keyboard -> P1 move (AA05).  P1 is human (AA06=0), so MOVSEL leaves AA05
        ;     unless a reaction is queued (which correctly overrides player input). ---
        JSR     PC,KSCAN             ; drain the keyboard into the hold timers
        JSR     PC,KCTRL             ; R0 = the control bits (ticks the timers)
        TST     DEMO
        BNE     79$
        TST     KTG                  ; $9827: "G" and "H" held together quit the
        BEQ     79$                  ;   game -> the demo ($9C2C = 0, A = $80)
        TST     KTH
        BEQ     79$
        CLR     KTG
        CLR     KTH
        CLR     KTFR                 ; (the chord must not restart the game at once)
        CLR     KSTART
        CLR     RPHASE
        JSR     PC,DINIT
        JSR     PC,SETUP
        CLR     R0
79$:    TST     DEMO                 ; in the demo, fire ($97E3) or "1" ($97DC) starts
        BEQ     70$                  ;   a 1-player game
        BIT     #20,R0
        BNE     71$
        TST     KSTART
        BEQ     70$
71$:    CLR     KSTART
        CLR     RPHASE                ; (the demo's round-end sequence, if any, is over)
        JSR     PC,GINIT             ; $AC3E
        JSR     PC,SETUP
        CLR     R0
70$:    JSR     PC,C98A0             ; R0 = &move ($98DD table)
        MOVB    (R0),R0
{dbgmove}        MOVB    R0,{g(0xAA05)}       ; P1 selected move
        TST     RPHASE                ; a round-end sequence in progress?
        BNE     83$
        JSR     PC,ORCH              ; one logic frame (AI driven by the LFSR ARNG)
        JSR     PC,ROUNDE            ; $AD18: score the exchange, end the round
        BR      84$
83$:    JSR     PC,RNDEND            ; one frame of the round-end sequence
84$:    MOVB    {g(0xAA01)},SC1      ; stash the scores for the HUD (GST unseen at 3377)
        MOVB    {g(0xAA41)},SC2
        MOVB    {g(0xB02D)},SCRBCD   ; and P1's BCD point score, for DRWSCR at 3377
        MOVB    {g(0xB02D)}+1.,SCRBCD+1.
        MOVB    {g(0xB02D)}+2.,SCRBCD+2.
        MOVB    {g(0x9CA5)},STIM     ; and the round timer, for DRWTIM at 3377
        MOVB    {g(0xB05F)},RANKB    ; and the rank ($B05F, BCD), for DRWRNK at 3377
        TST     DEMO
        BEQ     69$
        MOVB    #377,RANKB           ; (377 = "DEMO")
69$:        ; --- sound ($9754): play the effect the hit logic queued in $B150 ---
80$:    MOVB    {g(0xB150)},R0       ; $B150 = sound code queued by $9ED2/$9D29 hit-detect
        BIC     #177400,R0
        BEQ     81$
        JSR     PC,SNDFX             ; $B15A: play it (blocking, bit-banged speaker)
        CLRB    {g(0xB150)}          ; ($B15A clears it after playing)
"""


# Per-fighter decode parameters: the local labels (block start, key miss,
# done, cache miss, the clear / segment / copy loops), the render cells the
# decode cache watches (box, sprite origin, pose record, facing, mode), the
# sprite origin (top, left), the draw set-up routine, the box letter and
# the box cells (top, left).
FIGHTERS = {
    1: dict(labels=(81, 87, 86, 63, 5, 6, 62),
            cells=(0xC41B, 0xC41C, 0xC41F, 0xC421, 0xC428, 0xC429,
                   0xC434, 0xC435, 0xC436, 0xC437, 0xC407, 0xC40E),
            origin=(0xC41C, 0xC41B), chain="C101C", box="A", boxcells=(0xC436, 0xC434)),
    2: dict(labels=(86, 89, 88, 73, 56, 7, 72),
            cells=(0xC41D, 0xC41E, 0xC420, 0xC422, 0xC42A, 0xC42B,
                   0xC438, 0xC439, 0xC43A, 0xC43B, 0xC407, 0xC40E),
            origin=(0xC41E, 0xC41D), chain="C1CC", box="B", boxcells=(0xC43A, 0xC438)),
}


def _lab(n):
    """A local label in its 8-column field."""
    return f"{n}$:".ljust(8)


def fighter(n, lb_words, c408):
    """Fighter `n`: the decode cache (KEYn), the sprite cache, else the decode
    into FBUF and its copy to LBUFn; RWn / RTn / RLn = its box."""
    f = FIGHTERS[n]
    start, miss, done, cmiss, clr, seg, copy = f["labels"]
    cells, (otop, oleft), (btop, bleft) = f["cells"], f["origin"], f["boxcells"]
    chain, box = f["chain"], f["box"]
    out = f"{start}$:    ; --- Fighter {n}: clear FBUF, decode box {box}, stash its box, copy to LBUF{n} ---\n"
    if n == 1:
        out += ("        ; Decode cache: the draw set-up ($C101 / $C1A2) reads only these render\n"
                "        ; cells (box, sprite origin, pose record, facing, mode); when none changed\n"
                "        ; since the last decode, LBUF1 / RW1 / RT1 / RL1 still hold its result.\n")
    out += "".join(f"        CMPB    {g(c)},KEY{n}+{i}.\n        BNE     {miss}$\n"
                   for i, c in enumerate(cells))
    out += f"        JMP     {done}$\n{miss}$:"
    out += "".join(f"        MOVB    {g(c)},KEY{n}+{i}.\n" for i, c in enumerate(cells))
    return out + f"""        ; sprite cache: the decoded image depends only on the pose record, the
        ; sub-cell x shift, facing and mode - look it up (16 slots in the extended
        ; banks 10-11, visible at 040000 with slots 2-3 extended and the VRAM
        ; window off: 03003) before decoding; a hit is a copy instead of a decode.
        JSR     PC,CKEY{n}
        MOV     #3003,@#DISPAT
        MOV     #LBUF{n},R1
        JSR     PC,CLOOK
        MOV     #GAME,@#DISPAT
        TST     R0
        BNE     {cmiss}$
        MOV     R2,RW{n}               ; hit: the image is in LBUF{n}, its width in R2;
        MOVB    {g(otop)},R0       ;   the (tight) box is the sprite origin
        BIC     #177400,R0
        MOV     R0,RT{n}
        MOVB    {g(oleft)},R0
        BIC     #177400,R0
        MOV     R0,RL{n}
        JMP     {done}$
{cmiss}$:
        MOV     #FBUF,R0
        MOV     #{lb_words}.,R1
{_lab(clr)}CLR     (R0)+
        DEC     R1
        BNE     {clr}$
        JSR     PC,{chain}
        JSR     PC,SETUPC
{_lab(seg)}JSR     PC,SEGSET
        MOVB    {c408},R0
        BIC     #177400,R0
        MOV     R0,C408W
        JSR     PC,DECRUN
        DECB    SEGCNT
        BNE     {seg}$
        MOVB    {g(0xC40A)},R0       ; box {box}: width, top (${btop:X}), left (${bleft:X})
        BIC     #177400,R0
        MOV     R0,RW{n}
        MOVB    {g(btop)},R0
        BIC     #177400,R0
        MOV     R0,RT{n}
        MOVB    {g(bleft)},R0
        BIC     #177400,R0
        MOV     R0,RL{n}
        MOV     #FBUF,R1
        MOV     #LBUF{n},R0
        MOV     #{lb_words}.,R2
{_lab(copy)}MOV     (R1)+,(R0)+
        DEC     R2
        BNE     {copy}$
        MOV     #3003,@#DISPAT       ; a miss: remember the decode in the cache
        MOV     #LBUF{n},R1
        MOV     RW{n},R2
        JSR     PC,CSTOR
        MOV     #GAME,@#DISPAT
"""


def geometry():
    """The on-screen geometry of both fighters (GEOMC, clamped to the screen)."""
    return f"""88$:    ; --- on-screen geometry for both fighters (each clamped to the screen) ---
        MOV     RW1,R3               ; fighter 1: raw width / top / left -> COL1/TOP1/BWID1/W1
        MOV     RT1,R4
        MOV     RL1,R5
        JSR     PC,GEOMC
        MOV     R0,COL1
        MOV     R1,TOP1
        MOV     R2,BWID1
        MOV     R3,W1
        MOV     RW2,R3               ; fighter 2
        MOV     RT2,R4
        MOV     RL2,R5
        JSR     PC,GEOMC
        MOV     R0,COL2
        MOV     R1,TOP2
        MOV     R2,BWID2
        MOV     R3,W2
        MOV     #3377,@#DISPAT       ; unpark: 03377 (RMON back, VRAM on) - present-safe
"""


def compositor(dojo_row, lb_words, ovl_ink):
    """The in-place row compositor (CLOOP): the dojo row, then each fighter."""
    return f"""        ; --- flicker-free compositor: per screen row, CLEAR then overlay each fighter ---
        ; Each fighter is drawn from its own buffer (LBUF1 / LBUF2) at its own column
        ; (COL) and top (TOP).  SRCn walks the sprite one stride (BWIDn) per row once the
        ; row reaches TOPn, and stops at the buffer end; black (zero) cells are skipped so
        ; the two sprites overlay transparently.  Clearing each row before overlay means
        ; the screen is never globally blank -> no flicker.
        ; Only clear/composite the fighters' active band [TOPCLR..200): TOPCLR =
        ; min(TOP1, TOP2, last frame's min top) so a descending fighter's old rows still
        ; get erased.  Rows above stay black from the start-up clear -> big speed win.
        MOV     TOP1,R0
        CMP     R0,TOP2
        BLE     60$
        MOV     TOP2,R0
60$:    MOV     R0,R1                ; R1 = this frame's min top
        CMP     R0,LASTTP
        BLE     61$
        MOV     LASTTP,R0            ; include last frame's top so descents don't ghost
61$:    MOV     R1,LASTTP
        MOV     R0,ROWN
        MOV     R0,R2                ; VRAM row ptr = VRAM + ROWN*80
        ASL     R2
        ASL     R2
        ASL     R2
        ASL     R2
        MOV     R2,R1
        ASL     R2
        ASL     R2
        ADD     R1,R2
        ADD     #VRAM,R2
        MOV     #LBUF1,SRC1
        MOV     #LBUF2,SRC2
        ; Each row is composed IN PLACE in VRAM: the clean dojo row is copied
        ; over it, then the fighters are overlaid - a cell is never black in
        ; between, only "dojo without the fighter" for the few microseconds
        ; between the copy and the overlay.
CLOOP:  {dojo_row}CCLR:   MOV     R2,R0                ; outside the dojo band: clear the row
        MOV     #10.,R3              ; clear 40 words, unrolled x4 (less loop overhead)
CCL1:   CLR     (R0)+
        CLR     (R0)+
        CLR     (R0)+
        CLR     (R0)+
        DEC     R3
        BNE     CCL1
CDDN:   MOV     ROWN,R0              ; --- fighter 1 ---
        CMP     R0,TOP1
        BLO     C1SK                 ; row above the sprite
        CMP     SRC1,#LBUF1+{lb_words}.*2
        BHIS    C1SK                 ; sprite exhausted
        MOV     W1,R3
        BEQ     C1AD                 ; off-screen width -> advance src only
        MOV     R2,R0                ; dst = the VRAM row + COL1*2
        MOV     COL1,R4
        ASL     R4
        ADD     R4,R0
        MOV     SRC1,R1
C1OV:   MOVB    (R1)+,R4
        BEQ     C1TR                 ; zero cell = fully transparent (dojo shows)
        BIC     #177400,R4
        BISB    R4,(R0)              ; OR the fighter pixels into the background cell
        {ovl_ink}
C1TR:   TST     (R0)+
        DEC     R3
        BNE     C1OV
C1AD:   ADD     BWID1,SRC1           ; next compose row (full stride)
C1SK:   MOV     ROWN,R0              ; --- fighter 2 ---
        CMP     R0,TOP2
        BLO     C2SK
        CMP     SRC2,#LBUF2+{lb_words}.*2
        BHIS    C2SK
        MOV     W2,R3
        BEQ     C2AD
        MOV     R2,R0
        MOV     COL2,R4
        ASL     R4
        ADD     R4,R0
        MOV     SRC2,R1
C2OV:   MOVB    (R1)+,R4
        BEQ     C2TR
        BIC     #177400,R4
        BISB    R4,(R0)              ; OR the fighter pixels into the background cell
        {ovl_ink}
C2TR:   TST     (R0)+
        DEC     R3
        BNE     C2OV
C2AD:   ADD     BWID2,SRC2
C2SK:   ADD     #80.,R2              ; next screen row
        INC     ROWN
        CMP     ROWN,#196.           ; the band ends with the dojo (the floor is row 194)
        BHIS    58$                  ; done -> next frame
        JMP     CLOOP                ; (JMP: CLOOP is out of branch range)
"""


def hud_gate():
    """The status strip redraw gate, the frame's end, and LDERR."""
    return f"""        ; --- status strip: redrawn only when a shown value changed (or the dojo
        ;     was re-presented, HUDDRT) - it is ~7% of a frame otherwise ---
58$:    TST     HUDDRT
        BNE     59$
        CMPB    SC1,HUDK
        BNE     59$
        CMPB    SC2,HUDK+1.
        BNE     59$
        CMPB    SCRBCD,HUDK+2.
        BNE     59$
        CMPB    SCRBCD+1.,HUDK+3.
        BNE     59$
        CMPB    SCRBCD+2.,HUDK+4.
        BNE     59$
        CMPB    STIM,HUDK+5.
        BNE     59$
        CMPB    RANKB,HUDK+6.
        BNE     59$
        JMP     GLOOP
59$:    MOVB    SC1,HUDK
        MOVB    SC2,HUDK+1.
        MOVB    SCRBCD,HUDK+2.
        MOVB    SCRBCD+1.,HUDK+3.
        MOVB    SCRBCD+2.,HUDK+4.
        MOVB    STIM,HUDK+5.
        MOVB    RANKB,HUDK+6.
        CLR     HUDDRT
        JSR     PC,HUD               ; draw the yin-yang score bar (top border)
        JSR     PC,DRWSCR            ; draw the numeric score across the top strip
        JSR     PC,DRWTIM            ; draw the round timer beside it
        JSR     PC,DRWRNK            ; draw the rank ("NOVICE" / "1ST DAN" ...) at the left
        JMP     GLOOP                ; next frame (no busy-wait; the work itself paces it)
LDERR:  MOV     #2177,@#DISPAT         ; unpark: banks primary, VRAM off (RMON back)
        MOVB    ORIGRC,@#SYSC
        MTPS    #0
        .EXIT
"""


def sprite_cache(lb_words):
    """CKEY1 / CKEY2 / CSLOT / CLOOK / CSTOR and the slot table."""
    slotab = ",".join(f"{0o40000 + k * 892}." for k in range(16))
    return f"""        ; --- sprite cache.  Slot = 5-byte key (pose record lo/hi, x & 3, $C41F,
        ;     $C421 - facing / mode come from the last two), pad, width, pad, the
        ;     884-byte image: 892 bytes, 16 slots direct-mapped by a hash of the
        ;     key.  CKEY1/CKEY2 build the key for a fighter and pick the slot;
        ;     CLOOK / CSTOR run with the cache mapped (03003). ------------------
CKEY1:  MOVB    {g(0xc428)},CKEY
        MOVB    {g(0xc429)},CKEY+1.
        MOVB    {g(0xc41b)},R0
        BIC     #177774,R0
        MOVB    R0,CKEY+2.
        MOVB    {g(0xc41f)},CKEY+3.
        MOVB    {g(0xc421)},CKEY+4.
        BR      CSLOT
CKEY2:  MOVB    {g(0xc42a)},CKEY
        MOVB    {g(0xc42b)},CKEY+1.
        MOVB    {g(0xc41d)},R0
        BIC     #177774,R0
        MOVB    R0,CKEY+2.
        MOVB    {g(0xc420)},CKEY+3.
        MOVB    {g(0xc422)},CKEY+4.
CSLOT:  MOVB    CKEY,R0              ; hash: lo ^ lo>>3 ^ hi ^ sub ^ facing<<2, & 15
        BIC     #177400,R0
        MOV     R0,R1
        ASR     R1
        ASR     R1
        ASR     R1
        XOR     R1,R0
        MOVB    CKEY+1.,R1
        BIC     #177400,R1
        XOR     R1,R0
        MOVB    CKEY+2.,R1
        BIC     #177400,R1
        XOR     R1,R0
        MOVB    CKEY+3.,R1
        BIC     #177400,R1
        ASL     R1
        ASL     R1
        XOR     R1,R0
        BIC     #177760,R0
        ASL     R0
        MOV     SLOTAB(R0),SLOT
        RTS     PC
        ; CLOOK (at 03003): the slot holds CKEY? -> copy its image to (R1), R2 =
        ; width, R0 = 0; else R0 = 1.
CLOOK:  MOV     SLOT,R3
        CMPB    CKEY,(R3)
        BNE     9$
        CMPB    CKEY+1.,1(R3)
        BNE     9$
        CMPB    CKEY+2.,2(R3)
        BNE     9$
        CMPB    CKEY+3.,3(R3)
        BNE     9$
        CMPB    CKEY+4.,4(R3)
        BNE     9$
        MOVB    6(R3),R2
        BIC     #177400,R2
        ADD     #8.,R3
        MOV     #{lb_words}.,R4
1$:     MOV     (R3)+,(R1)+
        DEC     R4
        BNE     1$
        CLR     R0
        RTS     PC
9$:     MOV     #1,R0
        RTS     PC
        ; CSTOR (at 03003): store CKEY, the width R2 and the image at (R1).
CSTOR:  MOV     SLOT,R3
        MOVB    CKEY,(R3)
        MOVB    CKEY+1.,1(R3)
        MOVB    CKEY+2.,2(R3)
        MOVB    CKEY+3.,3(R3)
        MOVB    CKEY+4.,4(R3)
        MOVB    R2,6(R3)
        ADD     #8.,R3
        MOV     #{lb_words}.,R4
1$:     MOV     (R1)+,(R3)+
        DEC     R4
        BNE     1$
        RTS     PC
        .EVEN
SLOTAB: .WORD   {slotab}
"""


def geomc(fwmax):
    """GEOMC: clamp one fighter's raw box to the screen."""
    return f"""        ; --- GEOMC: clamp one fighter's raw box to the screen ----------------------
        ; in:  R3 = raw width ($C40A), R4 = raw top, R5 = raw left
        ; out: R0 = COL (cell), R1 = TOP (screen row), R2 = BWID (stride), R3 = W (cells)
GEOMC:  MOV     R5,R0                ; col = (left >> 2) + 4
        ASR     R0
        ASR     R0
        ADD     #4,R0
        MOV     R3,R2                ; BWID = min(raw width, fwmax)
        CMP     R2,#{fwmax}.
        BLE     1$
        MOV     #{fwmax}.,R2
1$:     MOV     #36.,R5              ; W = min(BWID, 36 - col): clip to the picture
        SUB     R0,R5
        MOV     R2,R3
        CMP     R3,R5
        BLE     2$
        MOV     R5,R3
2$:     TST     R3
        BGT     3$
        CLR     R3                   ; off the right edge / wrapped -> skip
3$:     MOV     R4,R1                ; top: a high jump wraps above row 0
        CMP     R1,#150.
        BLE     4$
        CLR     R1
4$:     ADD     #4,R1                ; +4 = top centring margin
        RTS     PC
"""
