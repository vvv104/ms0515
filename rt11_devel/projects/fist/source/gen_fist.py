"""Generate FIST.MAC - The Way Of The Exploding Fist, MS-0515 port.

This first stage establishes the display foundation that the whole port
renders through: a Spectrum 256x192 framebuffer shown 1:1 (pixel for
pixel), centred in the MS-0515's larger 320x200 medium-resolution colour
screen.  As a runnable proof it displays the game's loading screen.

The screen pixels come from the original game tape, which is *external*
non-committed data (the SAPER K.DAT pattern - we never vendor the game's
content).  Point the generator at the tape with the WOTEF_DIR environment
variable; it defaults to the known local checkout.

    WOTEF_DIR/*.tzx     the original WotEF tape image

Output (written to the project root, treated as a build artifact):

    FIST.MAC            MACRO-11 source for MACRO.SAV + LINK.SAV

The Spectrum and MS-0515 hardware attribute models line up bit-for-bit:
a Spectrum attribute byte (FLASH BRIGHT PAPER[g,r,b] INK[g,r,b]) maps
directly onto the MS-0515 VRAM attribute high byte (F I G'R'B' G R B),
and a Spectrum pixel byte (bit 7 = leftmost) onto the VRAM low byte
(D7 = leftmost).  So the 1:1 present routine is a pure copy - no bit
reversal, no colour remap.  The only real work is de-interleaving the
Spectrum screen's thirds-major row order, which we precompute into the
SROWS table emitted below.
"""
import os
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT_MAC = HERE.parent / "FIST.MAC"          # project root; a build artifact

WOTEF_DIR = Path(os.environ.get("WOTEF_DIR", r"C:\Users\voron\wotef"))

# Spectrum display screen geometry.
SCR_PIXELS = 6144                            # 256x192 mono pixel area
SCR_ATTRS  = 768                             # 32x24 attribute cells
SCR_BYTES  = SCR_PIXELS + SCR_ATTRS          # 6912


# ── Tape extraction ──────────────────────────────────────────────────────────

def _find_tzx() -> Path:
    tapes = sorted(WOTEF_DIR.glob("*.tzx"))
    if not tapes:
        raise SystemExit(
            f"no .tzx tape found in {WOTEF_DIR}; set WOTEF_DIR to the WotEF "
            f"checkout that holds the original tape image")
    return tapes[0]


def _tzx_data_blocks(raw: bytes):
    """Yield the data payload of every standard-speed (ID 0x10) tape block.

    Only the block types this tape actually uses are handled; that is all
    the port needs and keeps the parser small (no third-party dependency).
    """
    assert raw[:7] == b"ZXTape!", "not a TZX file"
    i = 10                                    # skip 'ZXTape!' 0x1A major minor
    while i < len(raw):
        bid = raw[i]
        if bid == 0x10:                       # standard-speed data block
            length = struct.unpack_from("<H", raw, i + 3)[0]
            payload = raw[i + 5: i + 5 + length]
            yield payload
            i += 5 + length
        elif bid == 0x30:                     # text description
            i += 2 + raw[i + 1]
        else:
            raise SystemExit(f"unhandled TZX block id 0x{bid:02x} at 0x{i:x}")


def load_loading_screen() -> bytes:
    """Return the 6912-byte Spectrum loading screen from the tape.

    On tape a data block is [flag][...payload...][checksum]; the loading
    screen is the standard-speed block whose payload is the 6912-byte
    SCREEN$ (so the whole block is 6914 bytes).
    """
    tzx = _find_tzx()
    raw = tzx.read_bytes()
    for payload in _tzx_data_blocks(raw):
        if len(payload) == SCR_BYTES + 2:     # flag + 6912 + checksum
            return payload[1:1 + SCR_BYTES]
    raise SystemExit(f"no {SCR_BYTES}-byte loading screen block in {tzx.name}")


# ── Spectrum -> MS-0515 row de-interleave table ──────────────────────────────

def spectrum_row_offsets():
    """Byte offset of each Spectrum pixel row (y = 0..191) within the 6144-byte
    pixel area.  Spectrum screen layout is thirds-major:

        offset(y) = ((y & 0x07) << 8) | ((y & 0x38) << 2) | ((y & 0xC0) << 5)
    """
    return [((y & 0x07) << 8) | ((y & 0x38) << 2) | ((y & 0xC0) << 5)
            for y in range(192)]


# ── MACRO-11 emission ────────────────────────────────────────────────────────

def _emit_bytes(label, data, per_line=16):
    out = [f"{label}:"]
    for i in range(0, len(data), per_line):
        chunk = data[i:i + per_line]
        out.append("        .BYTE   " + ",".join(str(b) + "." for b in chunk))
    return "\n".join(out) + "\n"


def _emit_words(label, words, per_line=8):
    out = [f"{label}:"]
    for i in range(0, len(words), per_line):
        chunk = words[i:i + per_line]
        out.append("        .WORD   " + ",".join(str(w) + "." for w in chunk))
    return "\n".join(out) + "\n"


PROGRAM = r"""        .TITLE  FIST
;
; The Way Of The Exploding Fist - MS-0515 / RT-11 SJ V5.04 port.
;
; Faithful one-to-one port of the pobtastic SkoolKit disassembly.  This
; stage ports the background engine (Change_Background $5F22 ..
; Background_Attributes $6010) and renders background 1 into a Spectrum-
; format framebuffer, then presents it 1:1 and centred via SPSCR.
;
; Virtual screen addresses: the Z80 engine works on Spectrum screen
; addresses $4000..$5AFF.  On the MS-0515 that range is the VRAM window,
; so the framebuffer lives at SCRBUF instead and every screen-pointer
; access translates with the index displacement SCRBUF-40000(Rn): for a
; virtual address V the real byte is V + (SCRBUF - 40000) = SCRBUF + (V -
; $4000).  Data pointers (UDG/position/attribute streams) are ordinary
; relocated labels and are used directly.
;
; This source is generated by source/gen_fist.py - do not edit by hand.
;

DPRAM  = 157700                 ; dispatcher shadow (DRAM bank/window control)
DISPAT = 177400                 ; hardware dispatcher register
SYSC   = 177604                 ; System Register C (border + video mode)
VRAM   = 40000                  ; VRAM virtual window base (VEN=1, VW=01)
VRAMEN = 100000                 ; end of the VRAM window (40000..77777)
KBST   = 177442                 ; keyboard status (bit 1 = byte ready)
KBDT   = 177440                 ; keyboard data

SVBASE = 40000                  ; Spectrum virtual screen base ($4000)
SVATTR = 54000                  ; Spectrum virtual attribute base ($5800)
SVTOP  = 40200                  ; first block screen address ($4080)

; Centring: 256 px = 32 word-columns inside a 40-column line -> 4-word
; (32 px) margin each side; 192 lines inside 200 -> 4-line margin top.
LMARG  = 8.                     ; left margin in BYTES (4 words)
TMARG  = 4.                     ; top margin in LINES
LSTRID = 80.                    ; medium-res line stride in bytes (40 words)


        .EVEN
START:  MOV     #340,R0
        MTPS    R0                      ; mask all IRQs while we own the screen

        ; --- save what we overwrite, for a clean exit ---
        MOV     @#DISPAT,ORIGDP
        MOVB    @#SYSC,ORIGRC

        ; --- enter medium 320x200 colour mode, black border.
        ;     SYSC bit 3 = video mode (1 = hi-res mono, 0 = medium colour);
        ;     bits 0-2 = border colour.  Clear bits 0-3, keep 4-7 (the
        ;     speaker gate on bit 7 must be preserved).
        MOVB    @#SYSC,R0
        BIC     #17,R0
        MOVB    R0,@#SYSC

        ; --- dispatcher: VRAM_EN + window @40000 + banks (timer IRQ bit set
        ;     but PSW priority 7 keeps everything masked).
        MOV     #3377,@#DPRAM
        MOV     #3377,@#DISPAT

        JSR     PC,CLRVRM               ; black out the whole screen (border)
        MOV     #%BGN%,BGREF            ; select the background
%STAGE%
        ; --- wait for any key, polling directly (IRQs masked) ---
WKEY:   MOV     @#KBST,R0
        BIT     #2,R0
        BEQ     WKEY
        MOV     @#KBDT,R0               ; consume the byte

EXITP:  ; --- restore the display and exit to the monitor ---
        MOV     ORIGDP,@#DPRAM
        MOV     ORIGDP,@#DISPAT
        MOVB    ORIGRC,@#SYSC
        EMT     350                     ; .EXIT


;-------------------------------------------------------------------
; CLRVRM - clear the entire VRAM window to 0 (black pixels, black attrs).
CLRVRM: MOV     #VRAM,R0
1$:     CLR     (R0)+
        CMP     R0,#VRAMEN
        BLO     1$
        RTS     PC


;-------------------------------------------------------------------
; CHGBG - Change_Background ($5F22): blank the framebuffer, then render
; the background.  One background's data is built in at a time (FIST_BG);
; the full BGREF 1/2/3 dispatch arrives once the memory layout is settled.
CHGBG:  JSR     PC,CLRBUF
        MOV     #%BGDEF%,R1            ; definition for the built-in background
        JSR     PC,CREBG
        RTS     PC


;-------------------------------------------------------------------
; CLRBUF - Change_Background's screen blank: 6144 pixel bytes := 0,
; 768 attribute bytes := $3F (white ink on white paper).
CLRBUF: MOV     #SCRBUF,R0
        MOV     #3072.,R1              ; 6144 pixel bytes = 3072 words
1$:     CLR     (R0)+
        DEC     R1
        BNE     1$
        MOV     #384.,R1              ; 768 attr bytes = 384 words
        MOV     #16191.,R2            ; $3F3F : two $3F attribute bytes
2$:     MOV     R2,(R0)+
        DEC     R1
        BNE     2$
        RTS     PC


;-------------------------------------------------------------------
; CREBG - Create_Background ($5F80): 4 blocks of (UDG, position) data,
; each placed 0x80 below the last (hopping screen thirds), then the
; attribute stream.  R1 = background definition address.
CREBG:  MOV     R1,DEFPTR
        MOV     #SVTOP,SCRPOS          ; first block at $4080
        MOV     #4.,BLKCNT
CB1:    MOV     DEFPTR,R1
        MOV     (R1)+,UDGBAS           ; UDG block base (real label)
        MOV     (R1)+,R2               ; positioning stream (real label)
        MOV     R1,DEFPTR
        MOV     SCRPOS,R3              ; DE = current screen address (virtual)
        JSR     PC,DRAWPOS
        MOV     SCRPOS,R0              ; advance: +0x80, hop a third on boundary
        ADD     #200,R0
        BIT     #400,R0                ; bit 8 set -> crossed into next third
        BEQ     CB2
        ADD     #3400,R0               ; += 7 to the high byte
CB2:    MOV     R0,SCRPOS
        DEC     BLKCNT
        BNE     CB1
        MOV     DEFPTR,R1
        MOV     (R1),R2                ; attribute stream (real label)
        JSR     PC,BGATTR
        RTS     PC


;-------------------------------------------------------------------
; DRAWPOS - Background_Fetch_Next ($5FBE): walk a positioning stream,
; blitting the indexed UDGs.  R2 = stream ptr (real), R3 = screen DE
; (virtual).  bit 7 of a byte = repeat-run flag (next byte is the count);
; two equal bytes both zero terminate.
DRAWPOS:MOVB    (R2)+,R0               ; a = next position byte
        BIC     #177400,R0
        MOVB    (R2),R1                ; peek the following byte
        BIC     #177400,R1
        CMP     R0,R1
        BNE     DP2
        TST     R0
        BEQ     DPRET                  ; 00,00 -> terminator
DP2:    MOV     R0,R5                  ; keep raw byte (bit 7 = repeat flag)
        BIC     #177600,R0             ; a &= $7F (UDG index)
        ASL     R0
        ASL     R0
        ASL     R0                     ; index * 8
        ADD     UDGBAS,R0
        MOV     R0,UDGCUR              ; address of the referenced UDG
        JSR     PC,CPUDG
        JSR     PC,NXTBLK
        BIT     #200,R5
        BEQ     DRAWPOS                ; no repeat -> next byte
        ; R5 is free after the flag test; CPUDG clobbers R0/R1/R4 and NXTBLK
        ; R3, but both preserve R5 - so keep the run count there.
        MOVB    (R2)+,R5               ; repeat count
        BIC     #177400,R5
        DEC     R5
        BEQ     DRAWPOS
DP3:    JSR     PC,CPUDG
        JSR     PC,NXTBLK
        DEC     R5
        BNE     DP3
        BR      DRAWPOS
DPRET:  RTS     PC


;-------------------------------------------------------------------
; CPUDG - Copy_UDG ($5FF4): blit the 8-byte UDG at UDGCUR to virtual
; screen address R3, stepping down one Spectrum pixel row (+0x100) per
; line.  R3 is preserved (DE unchanged for the caller).
CPUDG:  MOV     R3,R0                  ; working virtual address
        MOV     UDGCUR,R1
        MOV     #8.,R4
1$:     MOVB    (R1)+,SCRBUF-40000(R0) ; M[real(de)] = M[udg]; udg++
        ADD     #400,R0                ; de += 0x100 (one pixel row down)
        DEC     R4
        BNE     1$
        RTS     PC


;-------------------------------------------------------------------
; NXTBLK - Background_Next_Screen_Block ($6007): advance R3 one cell to
; the right, hopping to the next screen third on a boundary.
NXTBLK: INC     R3
        BIT     #400,R3
        BEQ     1$
        ADD     #3400,R3
1$:     RTS     PC


;-------------------------------------------------------------------
; BGATTR - Background_Attributes ($6010): RLE-unpack the attribute stream
; at R2 (real) into the virtual attribute buffer at $5800.  bit 7 of a
; byte = repeat flag (next byte is the count); two equal zeros terminate.
BGATTR: MOV     #SVATTR,R3             ; DE = $5800 (virtual attr base)
BA1:    MOVB    (R2)+,R0
        BIC     #177400,R0
        MOVB    (R2),R1
        BIC     #177400,R1
        CMP     R0,R1
        BNE     BA2
        TST     R0
        BEQ     BARET                  ; 00,00 -> terminator
BA2:    MOV     R0,R5                  ; raw byte (bit 7 = repeat flag)
        BIC     #177600,R0             ; attribute := a & $7F
        MOVB    R0,SCRBUF-40000(R3)
        INC     R3
        BIT     #200,R5
        BEQ     BA1
        MOVB    (R2)+,R1               ; repeat count
        BIC     #177400,R1
        DEC     R1
        BEQ     BA1
BA4:    MOVB    R0,SCRBUF-40000(R3)
        INC     R3
        DEC     R1
        BNE     BA4
        BR      BA1
BARET:  RTS     PC


;-------------------------------------------------------------------
; SPSCR - present the Spectrum framebuffer SCRBUF (6144 pixels + 768
; attrs) to VRAM, 1:1 and centred.
;
; For each Spectrum pixel row y = 0..191:
;   pix  = SCRBUF + SROWS[y]                 ; 32 pixel bytes
;   attr = SCRBUF + 6144 + (y>>3)*32         ; 32 attribute bytes (per cell)
;   dst  = VRAM + (TMARG+y)*LSTRID + LMARG    ; centred destination
;   for cx = 0..31:
;     VRAM word = (attr[cx] << 8) | pix[cx]
;
SPSCR:  CLR     R5                      ; R5 = y
SPRY:   ; pix source -> R1
        MOV     R5,R0
        ASL     R0                      ; y*2, word index into SROWS
        MOV     SROWS(R0),R1
        ADD     #SCRBUF,R1              ; R1 = &pix[0]
        ; attr source -> R2 = SCRBUF + 6144 + (y>>3)*32
        MOV     R5,R2
        ASR     R2
        ASR     R2
        ASR     R2                      ; R2 = y>>3 (cell row 0..23)
        ASL     R2
        ASL     R2
        ASL     R2
        ASL     R2
        ASL     R2                      ; R2 = cellrow*32
        ADD     #SCRBUF+6144.,R2       ; R2 = &attr[0]
        ; dst -> R3 = VRAM + (TMARG+y)*LSTRID + LMARG
        MOV     R5,R3
        ADD     #TMARG,R3              ; line = TMARG + y
        MOV     R3,R0
        ASL     R0
        ASL     R0
        ASL     R0
        ASL     R0                      ; R0 = line*16
        MOV     R0,R4
        ASL     R0
        ASL     R0                      ; R0 = line*64
        ADD     R4,R0                   ; R0 = line*80
        ADD     #VRAM+LMARG,R0         ; R0 = dst byte address
        MOV     R0,R3
        ; inner loop: 32 columns
        MOV     #32.,R4
SPRX:   MOVB    (R2)+,R0               ; R0 = attr (sign-extended high byte)
        SWAB    R0                      ; attr -> high byte
        BIC     #377,R0                ; clear low byte
        BISB    (R1)+,R0               ; low byte = pixel byte
        MOV     R0,(R3)+               ; store the VRAM word
        DEC     R4
        BNE     SPRX
        INC     R5
        CMP     R5,#192.
        BLO     SPRY
        RTS     PC


;-------------------------------------------------------------------
; Saved hardware state (restored on exit) + engine work variables
; (the Z80 engine's $5F02/$5F04/$5F08 cells and its loop counters).
ORIGDP: .WORD   0
ORIGRC: .WORD   0
BGREF:  .WORD   0                       ; selected background (1..3)
SCRPOS: .WORD   0                       ; current block screen addr (virtual)
UDGCUR: .WORD   0                       ; address of the UDG being blitted
UDGBAS: .WORD   0                       ; base of the current UDG block
DEFPTR: .WORD   0                       ; walk pointer into the bg definition
BLKCNT: .WORD   0                       ; remaining blocks in CREBG

"""


# Debug stages (FIST_STAGE env): isolate a runtime hang by truncating the
# demo.  'clrbuf' clears the buffer only; 'chgbg' runs the whole engine then
# exits; 'full' (default) also presents and waits for a key.
STAGES = {
    "clrbuf": "        JSR     PC,CLRBUF\n        JMP     EXITP",
    "draw1":  "        JSR     PC,CLRBUF\n"
              "        MOV     #%BGDEF%,R1\n"
              "        MOV     (R1)+,UDGBAS\n"
              "        MOV     (R1)+,R2\n"
              "        MOV     #SVTOP,R3\n"
              "        JSR     PC,DRAWPOS\n"
              "        JMP     EXITP",
    "attr1":  "        JSR     PC,CLRBUF\n"
              "        MOV     #%BGDEF%,R1\n"
              "        MOV     16.(R1),R2\n"
              "        JSR     PC,BGATTR\n"
              "        JMP     EXITP",
    "chgbg":  "        JSR     PC,CHGBG\n        JMP     EXITP",
    "full":   "        JSR     PC,CHGBG                ; clear buffer + render\n"
              "        JSR     PC,SPSCR                ; present SCRBUF 1:1",
}


def main():
    if os.environ.get("FIST_MODE") == "fighter":
        import fighter_mac
        fighter_mac.main()
        return

    from bg_data import BackgroundData

    n = int(os.environ.get("FIST_BG", "1"))
    rows = spectrum_row_offsets()
    bg = BackgroundData(n)

    stage = os.environ.get("FIST_STAGE", "full")
    src = (PROGRAM.replace("%STAGE%", STAGES[stage])
                  .replace("%BGDEF%", f"BG{n}DEF")
                  .replace("%BGN%", str(n)))
    src += f"\n;------ background {n} data (extracted from the original) ------\n"
    src += bg.emit()
    src += "\n        .EVEN\n"
    src += _emit_words("SROWS", rows)
    src += "\n; SCRBUF - the Spectrum-format framebuffer the engine renders\n"
    src += "; into and SPSCR presents (6144 pixel bytes + 768 attribute bytes).\n"
    src += "        .EVEN\nSCRBUF: .BLKB   6912.\n"
    src += "\n        .EVEN\n        .END    START\n"

    # MACRO-11 chokes on any non-ASCII byte (error I), even in comments.
    src.encode("ascii")
    OUT_MAC.write_text(src, encoding="ascii", newline="\r\n")
    bgsize = sum(len(d) for _, d in bg.blocks.values())
    print(f"gen_fist: wrote {OUT_MAC} ({len(src)} chars, "
          f"bg{n} data {bgsize} B)")


if __name__ == "__main__":
    main()
