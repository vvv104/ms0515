"""Where the original lives: the MANICM_DIR environment variable, else a
checkout of Richard Dymond's SkoolKit disassembly (the repository
`skoolkid/manicminer`) next to this repository.  Nothing of it is ever
committed here; prepare_mm.py makes the snapshot in the checkout."""
import os
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[4]
MANICM_DIR = Path(os.environ.get("MANICM_DIR") or (REPO_ROOT.parent / "manicminer"))
SNAPSHOT = MANICM_DIR / "manic_miner.z80"
