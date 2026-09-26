"""Where the original lives: the SAB2_DIR environment variable, else a
folder `saboteur2` next to this repository, holding the 48K tape of
Saboteur II as `SABOTEU2.TAP` (Durell, 1987: the TAP most archives carry,
a BASIC loader, a 6912-byte screen and the 40436-byte game at 25100).
Nothing of it is ever committed here."""
import os
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[4]
SAB2_DIR = Path(os.environ.get("SAB2_DIR") or (REPO_ROOT.parent / "saboteur2"))
TAPE = SAB2_DIR / "SABOTEU2.TAP"

CODE_BASE = 0x620C          # where the game's code block loads
