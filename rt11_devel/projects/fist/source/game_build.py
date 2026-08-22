"""FIST_GL=game / gamebg - the STANDALONE game, runnable on RT-11 via 'R FIST'.

Loads the full GST from GST.DAT into the parked extended banks 4-6 (the
proven chunked .READW + park / copy loader), then runs the live per-frame
loop (keyboard -> P1, LFSR AI -> P2, sound) and draws BOTH fighters from the
live state with a flicker-free per-row compositor.

withbg (FIST_GL=gamebg): also render the dojo background.  The bg engine
(CHGBG / CREBG) renders the Spectrum-format dojo into the resident SCRBUF
at every opponent set-up and SPSCR presents it to VRAM; BUILDDB keeps a
pre-converted copy (DOJOBUF) the compositor seeds each rebuilt row with, so
the two fighters composite transparently over the dojo.

The MACRO text comes from the per-subsystem modules (game_loader, game_dojo,
game_compose, game_round, game_hud, game_sound, game_keys); this module
captures the state, sizes the buffers and assembles the image.
"""
import json
import os
import re

import fighter_data as fd
import fighter_mac as fm
import game_compose
import game_dojo
import game_hud
import game_keys
import game_loader
import game_round
import game_sound
import gamelogic_mac as gm
import gamelogic_ref as ref
import gen_fist
import setup_ref as sr
from gst_addr import GBASE, g

LDAT_BASE, LDAT_END = 0x9368, 0x9600
# per-frame present clamps: a runtime box can grow taller/wider than the
# captured one (a jump/somersault) - cap it so the blit can't over-read LOWBUF
# or run off the screen (both showed as garbage).  LOWBUF holds the largest
# clamped box.
FWMAX = 40
KTMOUT = 3                                   # game frames a control stays held after its last event:
                                             # the MS7004 game preset repeats after 125 ms then every
                                             # 50 ms, so three frames bridge the first gap (a TAP = 1-3
                                             # steps); 7 was a 1.5 s ghost hold at ~7 game-fps
CAP = 120                                    # game frames that cap a round-end wait
PAUSE = 40                                   # the $AF1A x2 pause after a time-out: held frames (~33 ms each) -> ~1.3 s


def _state():
    """A mid-attract state of the original, one logic frame + the draw set-up
    run on it: (the snapshot, the frame's randoms, the state after)."""
    fm.STAGE_LEVEL = 1

    def safe_frame(m, rs):
        tmp = bytearray(m)
        try:
            ref.frame_9745(tmp, list(rs))
        except NotImplementedError:
            return
        m[:] = tmp
    snap, randoms = gm.capture_ai(0x9745, safe_frame, 0xC440)
    mm = bytearray(snap)
    ref.frame_9745(mm, list(randoms))
    sr.c101_block1(mm)
    sr.c1a2(mm)
    sr.c101_block2(mm)
    sr.c1cc(mm)
    return snap, randoms, mm


def _gst_dat(snap, withbg):
    """Write GST.DAT: the GST data ($F730+ compose is scratch), block-padded,
    with the tape's loading screen (SCREEN$, 6912 B) behind it - the loader
    reads it straight into SCRBUF and presents it while the state loads, the
    picture the original shows while its tape loads.  Returns (state blocks,
    the screen's first block)."""
    gstdat = bytes(snap[GBASE:0xF730])
    if len(gstdat) % 512:
        gstdat = gstdat + bytes(512 - (len(gstdat) % 512))
    nblocks = len(gstdat) // 512
    scrdat = bytes(gen_fist.load_loading_screen()) if withbg else b""
    if len(scrdat) % 512:
        scrdat = scrdat + bytes(512 - (len(scrdat) % 512))
    (gm.OUT_MAC.parent / "GST.DAT").write_bytes(gstdat + scrdat)
    return nblocks, nblocks


def _equs(withbg):
    """The GST table / buffer equates (+ the dojo's with a background)."""
    equs = "\nFWHITE = 043400\n"          # bright-white attribute high byte ($47)
    for t in fd.TABLES:
        equs += f"T{t:04X}  = GST+{t - GBASE}.\n"
    equs += f"FBUF   = GST+{fd.FBUF - GBASE}.\n"
    equs += f"C40EM  = GST+{0xC40E - GBASE}.\n"
    equs += f"C407M  = GST+{0xC407 - GBASE}.\n"
    equs += "WB1C   = W+60.\n"
    if withbg:
        equs += game_dojo.EQUS
    return equs


def _driver(withbg, snap, lb_words, boot_code, bgn):
    """The driver: the boot, the frame loop and every game routine."""
    # FIST_DBGMOVE=1: a test hook - a non-zero $B156 (an unused sound scratch
    # cell) overrides P1's selected move, so a test can play every move.
    dbgmove = ("" if not os.environ.get("FIST_DBGMOVE") else
               f"        MOVB    {g(0xB156)},R1       ; FIST_DBGMOVE: forced move, if any\n"
               "        BIC     #177400,R1\n"
               "        BEQ     78$\n"
               "        MOV     R1,R0\n"
               "78$:")
    boot_inline = "" if withbg else boot_code    # no dojo block: the loader stays inline
    dojo_boot = game_dojo.boot_override(bgn) if withbg else ""
    dojo_row = game_dojo.row() if withbg else ""
    rendbg = game_dojo.rendbg() if withbg else ""
    ovl_ink = game_dojo.ovl_ink(withbg)
    cell_restore = game_dojo.cell_restore(withbg)
    font_s, yyfull_s, yyhalf_s = game_hud.data(snap)
    return ("\n" + game_loader.start(boot_inline, dojo_boot)
            + game_compose.frame_head(dbgmove)
            + game_compose.fighter(1, lb_words, g(0xC408))
            + game_compose.fighter(2, lb_words, g(0xC408))
            + game_compose.geometry()
            + game_compose.compositor(dojo_row, lb_words, ovl_ink)
            + game_compose.hud_gate()
            + game_compose.sprite_cache(lb_words)
            + game_compose.geomc(FWMAX)
            + game_round.scoring(CAP) + game_round.decision(CAP)
            + game_round.round_end(PAUSE) + game_round.outcome(withbg)
            + game_round.inits()
            + rendbg
            + game_hud.draw(cell_restore, ovl_ink) + game_hud.text()
            + game_hud.rank(font_s, yyfull_s, yyhalf_s)
            + game_sound.sound()
            + game_keys.kscan(KTMOUT) + game_keys.kctrl(KTMOUT)
            + game_keys.c98a0(game_keys.control_map()))


def _engine(randoms, snap):
    """The decoder (fighter_mac), the full logic frame (with a live RNG), the
    draw set-up chain, fighter_mac's tail and the LDAT mirror."""
    decrun = (fm.emit_decrun()
              .replace("MOV     #FCTRL,SRCP\n        "
                       "MOV     #FBUF+%DEOFF%.,DSTP\n        ", "")
              .replace("ADD     #C408V,R0", "ADD     C408W,R0"))
    tail = (fm.TAIL
            .replace("C40EM:  .BYTE   %C40E%.                ; per-fighter mode flags ($C40E)\n", "")
            .replace("C407M:  .BYTE   %C407%.                ; facing flag ($C407)\n", "")
            .replace("ORIGDP: .WORD   0\n", "").replace("ORIGRC: .WORD   0\n", ""))
    logic = gm.emit_fullframe(randoms)
    # Live RNG for the loop: replace the recorded-replay ARNG with a 16-bit Galois
    # LFSR masked to 0..127 (the Z80 R register's range, so the AI's >=$80 branches
    # stay dead) - so the AI decides fresh each frame instead of replaying 2 bytes.
    logic = logic.replace(
        "ARNG:   MOV     ARNDI,R0\n"
        "        INC     ARNDI\n"
        "        MOVB    ARND(R0),R0\n"
        "        BIC     #177400,R0\n"
        "        RTS     PC\n",
        "ARNG:   MOV     RSEED,R0\n"
        "        CLC\n"
        "        ROR     R0\n"
        "        BCC     91$\n"
        "        MOV     R1,-(SP)\n"
        "        MOV     #132000,R1\n"
        "        XOR     R1,R0\n"
        "        MOV     (SP)+,R1\n"
        "91$:    MOV     R0,RSEED\n"
        "        BIC     #177600,R0\n"
        "        RTS     PC\n")
    chain = gm.emit_setupchain() + gm.emit_c101c1a2()
    ldat = ("\n        .EVEN\n" + gm._emit_window("LDAT", snap[LDAT_BASE:LDAT_END]))
    return decrun + logic + chain + tail, ldat


def _datblk(lb_words, withbg):
    """The game's own variables (banks 0-1)."""
    bgvars = game_dojo.BGVARS if withbg else ""
    datblk = ("\n        .EVEN\nDATFIL: .RAD50  /DK GST   DAT/\n"
              "        .EVEN\nLKAREA: .BLKW   5\n"
              "        .EVEN\nC408W:  .WORD   0\nORIGRC: .WORD   0\n"
              "        .EVEN\nRSEED:  .WORD   1\n"
              "        .EVEN\nRW1:    .WORD   0\nRT1:    .WORD   0\nRL1:    .WORD   0\n"
              "RW2:    .WORD   0\nRT2:    .WORD   0\nRL2:    .WORD   0\n"
              "COL1:   .WORD   0\nTOP1:   .WORD   0\nBWID1:  .WORD   0\nW1:     .WORD   0\n"
              "COL2:   .WORD   0\nTOP2:   .WORD   0\nBWID2:  .WORD   0\nW2:     .WORD   0\n"
              "SRC1:   .WORD   0\nSRC2:   .WORD   0\nROWN:   .WORD   0\n"
              "        .EVEN\nRCSHAD: .WORD   0\nSSEED:  .WORD   52525\n"
              "        .EVEN\nLASTTP: .WORD   0\nKTUP:   .WORD   0\nKTDN:   .WORD   0\nKTLF:   .WORD   0\nKTRT:   .WORD   0\nKTFR:   .WORD   0\nKTG:    .WORD   0\nKTH:    .WORD   0\nKSTART: .WORD   0\nDEMO:   .WORD   0\n"
              "        .EVEN\nRESULT: .WORD   0\nSC1:    .WORD   0\nSC2:    .WORD   0\n"
              "        .EVEN\nWINTMR: .WORD   0\nRPHASE: .WORD   0\nRANKB:  .WORD   0\nRANKS:  .BLKB   10.\n"
              "        .EVEN\nKEY1:   .BLKB   12.\nKEY2:   .BLKB   12.\n"
              "        .EVEN\nCKEY:   .BLKB   6.\nSLOT:   .WORD   0\n"
              "        .EVEN\nHUDDRT: .WORD   1\nHUDK:   .BLKB   8.\n"
              "        .EVEN\nSCRBCD: .BLKB   3.\n        .EVEN\nSTIM:   .WORD   0\n"
                            f"        .EVEN\nLBUF1: .BLKW  {lb_words}.    ; per-fighter compose copies (one fighter each)\n"
              f"LBUF2: .BLKW  {lb_words}.\n" + bgvars)
    return datblk


def _symtab(body, bgsrc):
    """FIST_SYMTAB=1: a self-describing symbol table (marker words, count,
    then every global label's address) so a profiler can map sampled PCs to
    routines without a LINK map; the names go to symtab.json in the same
    order."""
    if not os.environ.get("FIST_SYMTAB"):
        return ""
    names = list(dict.fromkeys(re.findall(r"^([A-Z][A-Z0-9.$]*):", body + bgsrc, re.M)))
    (gm.OUT_MAC.parent / "symtab.json").write_text(json.dumps(names))
    return ("\n        .EVEN\nSYMTAB: .WORD   125252,52525," + f"{len(names)}.\n"
            + "".join(f"        .WORD   {','.join(names[i:i + 8])}\n"
                      for i in range(0, len(names), 8)))


def main_game(withbg=False):
    """Build FIST.MAC + GST.DAT for the standalone game."""
    nelem = int(os.environ.get("FGHT_NELEM", "5000"))
    bgn = int(os.environ.get("FGHT_BG", "2"))    # $AF34 at the 1UP start ($AC59)
    snap, randoms, mm = _state()
    fwid, fhgt = mm[0xC40A], mm[0xC409]
    top, left = (200 - fhgt) // 2, (40 - fwid) // 2
    # Per-fighter compose buffers: each holds ONE fighter (the original
    # FBUF_LEN = 884 B).  The decode writes each fighter into FBUF (bank 6)
    # then we copy it down to LBUF1 / LBUF2 for the compositor.  Without the
    # bg the copies are bounded by what fits below bank 7 (the extended FBUF
    # the decode writes into); with the bg, trim them to the real fighter
    # size so SCRBUF (6912 B) fits banks 0-1.
    fbuf_addr = 0o100000 + (fd.FBUF - GBASE)     # compose buffer home (extended bank 6)
    safe_words = (0o157777 - fbuf_addr + 1) // 2  # composed words that fit below bank 7
    lb_words = ((fd.FBUF_LEN + 1) // 2) if withbg else safe_words
    nblocks, scrblk = _gst_dat(snap, withbg)
    boot_code = game_loader.boot(withbg, nblocks, scrblk)
    bgsrc = game_dojo.block(bgn, boot_code) if withbg else ""
    bgdat_src = game_dojo.tables(game_loader.BUF) if withbg else ""
    engine, ldat = _engine(randoms, snap)
    body = (game_loader.preamble() + _equs(withbg)
            + _driver(withbg, snap, lb_words, boot_code, bgn)
            + engine + _datblk(lb_words, withbg) + ldat)
    # (the symbol table goes behind the dojo block: it is only read from the
    #  .SAV file, and banks 0-1 have no room to spare)
    src = body + bgsrc + _symtab(body, bgsrc) + bgdat_src + "\n        .END    START\n"
    src = (src.replace("%NELEM%", str(nelem))
              .replace("%C408%", str(snap[0xC408]))
              .replace("%C40E%", str(snap[0xC40E])).replace("%C407%", str(snap[0xC407]))
              .replace("%FWID%", str(fwid)).replace("%FHGT%", str(fhgt))
              .replace("%DSTOFF%", str((top * 40 + left) * 2)))
    src.encode("ascii")
    gm.OUT_MAC.write_text(src, encoding="ascii", newline="\r\n")
    print(f"gamelogic_mac: wrote {gm.OUT_MAC} + GST.DAT (STANDALONE GAME: load GST.DAT "
          f"-> extended banks, one $9745 frame + draw both fighters, {fwid}x{fhgt})")
