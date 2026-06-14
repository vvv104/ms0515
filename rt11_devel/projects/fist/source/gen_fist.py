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
; Display foundation: present a Spectrum 256x192 framebuffer 1:1, centred
; in the MS-0515 320x200 medium-resolution colour screen.  This stage
; shows the loading screen as a runnable proof; the game engine that
; follows will render into the same Spectrum-format framebuffer and call
; SPSCR to put it on the display.
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

        JSR     PC,CLRVRM               ; black out the whole screen
        JSR     PC,SPSCR                ; blit the Spectrum screen 1:1

        ; --- wait for any key, polling directly (IRQs masked) ---
WKEY:   MOV     @#KBST,R0
        BIT     #2,R0
        BEQ     WKEY
        MOV     @#KBDT,R0               ; consume the byte

        ; --- restore the display and exit to the monitor ---
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
; Saved hardware state (restored on exit).
ORIGDP: .WORD   0
ORIGRC: .WORD   0

"""


def main():
    screen = load_loading_screen()
    assert len(screen) == SCR_BYTES, len(screen)
    rows = spectrum_row_offsets()

    src = PROGRAM
    src += "        .EVEN\n"
    src += _emit_words("SROWS", rows)
    src += "\n        .EVEN\n"
    src += _emit_bytes("SCRBUF", screen)
    src += "\n        .EVEN\n        .END    START\n"

    # MACRO-11 chokes on any non-ASCII byte (error I), even in comments.
    src.encode("ascii")
    OUT_MAC.write_text(src, encoding="ascii", newline="\r\n")
    print(f"gen_fist: wrote {OUT_MAC} ({len(src)} chars, "
          f"screen {len(screen)} B)")


if __name__ == "__main__":
    main()
