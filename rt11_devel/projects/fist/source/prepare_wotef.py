"""Prepare WOTEF_DIR for the FIST build from pobtastic's disassembly checkout.

    git clone https://github.com/pobtastic/wayoftheexplodingfist
    WOTEF_DIR=/path/to/wayoftheexplodingfist python prepare_wotef.py

The generator needs, in WOTEF_DIR (the checkout itself is the natural place):
  *.tzx           the original tape (the loading screen comes from it),
  wotef.z80       the game's runtime snapshot - tap2sna's output for the
                  checkout's wayoftheexplodingfist.t2s (it fetches the tape
                  from World of Spectrum itself),
  wotef_run.z80   that snapshot run forward in SkoolKit's simulator: a
                  mid-attract frame the port's oracles are captured from.
This script makes all three: the tape's zip is downloaded and the .tzx
extracted, tap2sna.py is run on the .t2s, the run snapshot is traced.
Nothing is committed anywhere - the art stays outside the repository.
"""
import io
import os
import re
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

from skoolkit import tap2sna
from skoolkit.trace import main as trace_main

from wotef_dir import WOTEF_DIR                            # noqa: E402
WOTEF_DIR = WOTEF_DIR.resolve()
T2S = WOTEF_DIR / "wayoftheexplodingfist.t2s"
SNAP = WOTEF_DIR / "wotef.z80"
RUN = WOTEF_DIR / "wotef_run.z80"
RUN_TSTATES = "40000000"                     # as sim_capture.py's default


def tape_url():
    """The tape's URL: the first line of the .t2s that is one."""
    for line in T2S.read_text(encoding="utf-8").splitlines():
        if re.match(r"https?://", line.strip()):
            return line.strip()
    raise SystemExit(f"{T2S}: no tape URL in it")


def main():
    if not T2S.exists():
        raise SystemExit(f"{T2S} not found - WOTEF_DIR must be the disassembly checkout")
    if not list(WOTEF_DIR.glob("*.tzx")):
        url = tape_url()
        print(f"tape: downloading {url}")
        data = urllib.request.urlopen(url).read()
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            for name in z.namelist():
                if name.lower().endswith(".tzx"):
                    (WOTEF_DIR / Path(name).name).write_bytes(z.read(name))
                    print(f"tape: {name}")
    if not SNAP.exists():
        print("snapshot: tap2sna.py @wayoftheexplodingfist.t2s")
        cwd = os.getcwd()
        os.chdir(WOTEF_DIR)
        try:
            tap2sna.main(["@" + T2S.name])
        finally:
            os.chdir(cwd)
        out = WOTEF_DIR / "WayOfTheExplodingFistThe.z80"
        shutil.copy(out, SNAP)
        print(f"snapshot: {SNAP}")
    if not RUN.exists():
        print(f"run snapshot: {RUN_TSTATES} T-states of the simulator")
        trace_main(["-M", RUN_TSTATES, str(SNAP), str(RUN)])
        print(f"run snapshot: {RUN}")
    print("WOTEF_DIR ready:", ", ".join(p.name for p in sorted(WOTEF_DIR.iterdir())
                                           if p.suffix in (".tzx", ".z80")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
