"""Trace the WotEF fighter-sprite draw routine (reverse-engineering aid).

The disassembly does not analyse the sprite engine.  This drives SkoolKit's
pure-Python Z80 simulator from a captured fighter-animation state, with a
memory that logs every write into the Spectrum screen ($4000-$5AFF), and
runs a few frames.  During that window the static title is already drawn,
so the screen writes are the animated sprites - revealing which code (PC)
blits them and into which screen region.

    python trace_sprites.py            # uses WOTEF_DIR/wotef_run.z80
"""
import os
from collections import Counter
from pathlib import Path

from skoolkit import ROM48, read_bin_file
from skoolkit.simulator import Simulator
from skoolkit.simutils import FRAME_DURATIONS, INT_ACTIVE
from skoolkit.snapshot import Snapshot

from wotef_dir import WOTEF_DIR                            # noqa: E402
STATE = WOTEF_DIR / "wotef_run.z80"            # a captured fighter-animation frame

SCR_LO, SCR_HI = 0x4000, 0x5B00                 # pixels + attributes
PC, H, L, D, E = 24, 6, 7, 4, 5                 # register-list indices
DRAW = 0xC3E4                                    # the sprite-draw routine
W_ADDR, H_ADDR = 0x40A + 0xC000, 0x409 + 0xC000  # width / height cells
INSTRUCTIONS = 120000                           # ~5 frames


class LoggingMem(list):
    sim = None
    writes = None
    lo = SCR_LO
    hi = SCR_HI

    def __setitem__(self, i, v):
        if type(i) is int and self.lo <= i < self.hi:
            self.writes.append((self.sim.registers[PC], i, v))
        list.__setitem__(self, i, v)


def build_sim(watch=(SCR_LO, SCR_HI)):
    snap = Snapshot.get(str(STATE))
    ram = list(snap.ram(-1))                     # 48K RAM
    mem = LoggingMem([0] * 16384 + ram)
    mem[:0] = mem[:0]                            # no-op; keep type a LoggingMem
    rom = read_bin_file(ROM48)
    list.__setitem__(mem, slice(0, len(rom)), list(rom))
    regs = {
        "A": snap.a, "F": snap.f, "BC": snap.bc, "DE": snap.de, "HL": snap.hl,
        "IX": snap.ix, "IY": snap.iy, "SP": snap.sp, "I": snap.i, "R": snap.r,
        "^A": snap.a2, "^F": snap.f2, "^BC": snap.bc2, "^DE": snap.de2,
        "^HL": snap.hl2, "PC": snap.pc, "MEMPTR": snap.memptr,
    }
    state = {"im": snap.im, "iff": snap.iff1, "tstates": snap.tstates}
    config = {"frame_duration": FRAME_DURATIONS[False],
              "int_active": INT_ACTIVE[False]}
    sim = Simulator(mem, regs, state, config)
    mem.sim = sim
    mem.writes = []
    mem.lo, mem.hi = watch
    return sim, mem


def main():
    sim, mem = build_sim()
    regs, memory, ops = sim.registers, sim.memory, sim.opcodes
    fd, ia = sim.frame_duration, sim.int_active
    calls = []                                   # (src, dest, width, height)
    for _ in range(INSTRUCTIONS):
        pc = regs[PC]
        if pc == DRAW:
            calls.append((regs[H] * 256 + regs[L], regs[D] * 256 + regs[E],
                          memory[W_ADDR], memory[H_ADDR]))
        ops[memory[pc]]()
        if regs[26] and regs[25] % fd < ia:
            sim.accept_interrupt(regs, memory, regs[PC])

    addrs = [a for _, a, _ in mem.writes]
    print(f"screen writes: {len(mem.writes)} into "
          f"${min(addrs):04X}..${max(addrs):04X}")
    print(f"draw-routine ($C3E4) calls: {len(calls)}")
    print("draw calls (src, dest, WxH):")
    for s, dest, w, h in calls:
        print(f"  src=${s:04X}  dest=${dest:04X}  {w}x{h}")

    # Render the distinct sprite/buffer bitmaps from the CAPTURED state
    # ($F730 is filled at runtime, so read it from wotef_run.z80).
    from skoolkit.snapshot import get_snapshot
    from PIL import Image
    M = get_snapshot(str(STATE))
    sprites = sorted({(s, w, h) for s, _, w, h in calls if w and h})
    per = min(len(sprites), 8)
    cellw = max(w for s, w, h in sprites) * 8 + 6
    cellh = max(h for s, w, h in sprites) + 6
    rows = (len(sprites) + per - 1) // per
    img = Image.new("L", (per * cellw, rows * cellh), 40)
    px = img.load()
    for idx, (s, w, h) in enumerate(sprites):
        ox, oy = (idx % per) * cellw, (idx // per) * cellh
        for r in range(h):
            for cb in range(w):
                b = M[(s + r * w + cb) & 0xFFFF]
                for bit in range(8):
                    if (b >> (7 - bit)) & 1:
                        px[ox + cb * 8 + bit, oy + r] = 255
    out = Path(__file__).resolve().parent.parent / "fighter_sprites.png"
    img.resize((img.width * 4, img.height * 4), Image.NEAREST).save(out)
    print(f"wrote {out} ({len(sprites)} distinct blits)")


if __name__ == "__main__":
    main()
