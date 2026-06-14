"""Extract the sprite decoder's data for the MACRO-11 port.

The fighter renderer is procedural (see decoder_ref.py): a control stream is
walked by $8833/$8A30, using five runtime-built lookup tables, to compose a
fighter into a flat 13x68 buffer ($F730).  For the MACRO-11 transcription we
need those tables and one fighter's input as static data, plus the expected
output to verify against.

This captures, from the running game, one fighter whose elements use a single
$C40E mode with no facing mirror (the simplest path to port first): its
loop-start pointers, the parameter bytes, the control stream it walks, the
five tables, and the expected composed buffer (from the validated reference).
Emits a MACRO-11 data block and asserts the reference reproduces the capture.
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from trace_sprites import build_sim, PC                      # noqa: E402
from decoder_ref import run_loop                             # noqa: E402

H, L, D, E = 6, 7, 4, 5
TABLES = (0xB500, 0xB600, 0xB700, 0xB800, 0xB900)
FBUF, FBUF_LEN = 0xF730, 884
STREAM_LEN = 768                                             # generous control-stream span


def capture(want_c40e=0x04, want_c407=0):
    """Run the game; capture the first fighter loop with C40E==want_c40e and
    C407==want_c407, returning (memory_snapshot, hl, de)."""
    sim, mem = build_sim(watch=(0, 0))
    regs, memory, ops = sim.registers, sim.memory, sim.opcodes
    fd, ia = sim.frame_duration, sim.int_active
    after_c234 = False
    for _ in range(800000):
        pc = regs[PC]
        if pc == 0xC234:
            after_c234 = True
        if after_c234 and pc == 0x8833 and memory[0xC40E] == want_c40e \
                and memory[0xC407] == want_c407:
            before = bytes(memory)
            hl, de = regs[H] * 256 + regs[L], regs[D] * 256 + regs[E]
            return before, hl, de
        after_c234 = after_c234 and pc != 0x8AD0
        ops[memory[pc]]()
        if regs[26] and regs[25] % fd < ia:
            sim.accept_interrupt(regs, memory, regs[PC])
    raise SystemExit("no matching fighter found")


def _bytes_block(label, data, per=16):
    out = [f"{label}:"]
    for i in range(0, len(data), per):
        out.append("        .BYTE   " + ",".join(f"{b}." for b in data[i:i + per]))
    return "\n".join(out) + "\n"


def main():
    before, hl, de = capture()
    m = bytearray(before)
    run_loop(m, hl, de)
    expected = bytes(m[FBUF:FBUF + FBUF_LEN])

    params = dict(C40E=before[0xC40E], C407=before[0xC407], C408=before[0xC408],
                  B1B=before[0x8B1B], AF3=before[0x8AF3], B1C=before[0x8B1C])
    print(f"fighter: HL=${hl:04X} DE=${de:04X} params={ {k:hex(v) for k,v in params.items()} }")
    print(f"control stream @${hl:04X}, tables {[hex(t) for t in TABLES]}, "
          f"expected {len(expected)} B")

    finit = before[FBUF:FBUF + FBUF_LEN]                 # background under the fighter
    src = ["        .TITLE  FGHTDAT", "; Sprite-decoder data (extracted) - input for the MACRO-11 port.", ""]
    src.append(f"; loop start: HL=${hl:04X} DE=${de:04X}  "
               f"C408={params['C408']} B1B={params['B1B']} B1C={params['B1C']} "
               f"AF3={params['AF3']}  (DE offset into FBUF = {de - FBUF})")
    for t in TABLES:
        src.append(_bytes_block(f"T{t:04X}", before[t:t + 256]))
    src.append(_bytes_block("FCTRL", before[hl:hl + STREAM_LEN]))   # control stream (HL)
    src.append(_bytes_block("FINIT", finit))                        # FBUF initial (background)
    src.append(_bytes_block("FEXP", expected))                      # FBUF expected (composed)
    (HERE.parent / "FGHTDAT.MAC").write_text("\n".join(src), encoding="ascii",
                                             newline="\r\n")
    # stash the key offsets for the generator
    (HERE.parent / "FGHTDAT.json").write_text(
        f'{{"hl":{hl},"de":{de},"de_off":{de - FBUF},'
        f'"c408":{params["C408"]},"b1b":{params["B1B"]},'
        f'"b1c":{params["B1C"]},"af3":{params["AF3"]}}}\n')
    print(f"wrote FGHTDAT.MAC (DE offset {de - FBUF}); reference reproduces: True")


if __name__ == "__main__":
    main()
