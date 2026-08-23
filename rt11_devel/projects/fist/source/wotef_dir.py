"""Where the original lives: the WOTEF_DIR environment variable, else a
checkout of pobtastic's disassembly (the repository
`wayoftheexplodingfist`) next to this repository.  Nothing of it is ever
committed here; prepare_wotef.py fills the checkout (the tape, the runtime
snapshot, a mid-attract frame of it)."""
import os
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[4]
WOTEF_DIR = Path(os.environ.get("WOTEF_DIR") or (REPO_ROOT.parent / "wayoftheexplodingfist"))
