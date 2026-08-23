"""Run the original WotEF game in a Z80 simulator and render a frame.

Reverse-engineering aid for the fighter/sprite engine, which the
disassembly does not analyse.  Runs the runtime snapshot forward (the C
simulator in SkoolKit, with frame interrupts) for a number of T-states,
then renders the Spectrum screen ($4000) the way the MS-0515 port will
show it - giving ground-truth fighter frames to port against and a base
for tracing the sprite-draw routine.

    python sim_capture.py [tstates] [out.png]
"""
import os
import sys
from pathlib import Path

from skoolkit.snapshot import get_snapshot
from skoolkit.trace import main as trace_main

import preview

from wotef_dir import WOTEF_DIR                            # noqa: E402
SNAP = WOTEF_DIR / "wotef.z80"


def main():
    tstates = sys.argv[1] if len(sys.argv) > 1 else "40000000"
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("sim_frame.png")
    run = WOTEF_DIR / "wotef_run.z80"
    trace_main(["-M", tstates, str(SNAP), str(run)])
    mem = get_snapshot(str(run))
    screen = bytes(mem[0x4000:0x4000 + 6912])
    img = preview.render(preview.build_vram(screen))
    img.resize((640, 400), preview.Image.NEAREST).save(out)
    print(f"sim_capture: wrote {out}")


if __name__ == "__main__":
    main()
