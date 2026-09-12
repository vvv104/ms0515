"""Prepare MANICM_DIR for the MANICM build from the disassembly checkout.

    git clone https://github.com/skoolkid/manicminer
    MANICM_DIR=/path/to/manicminer python prepare_mm.py

The build needs the game's runtime snapshot, manic_miner.z80, in
MANICM_DIR: tap2sna.py's output for the checkout's manic_miner.t2s, which
fetches the tape from World of Spectrum itself.  Nothing is committed
anywhere - the game's data stays outside the repository.
"""
import os

from skoolkit import tap2sna

from mm_dir import MANICM_DIR, SNAPSHOT

T2S = MANICM_DIR / "manic_miner.t2s"


def main():
    if not T2S.exists():
        raise SystemExit(f"{T2S} not found - MANICM_DIR must be the disassembly checkout")
    if SNAPSHOT.exists():
        print(f"snapshot: {SNAPSHOT} is there")
        return
    print("snapshot: tap2sna.py @manic_miner.t2s")
    cwd = os.getcwd()
    os.chdir(MANICM_DIR)
    try:
        tap2sna.main([f"@{T2S.name}"])
    finally:
        os.chdir(cwd)
    if not SNAPSHOT.exists():
        raise SystemExit("tap2sna.py made no snapshot")


if __name__ == "__main__":
    main()
