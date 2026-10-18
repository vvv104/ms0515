"""emu_driver - the emulator's, loaded from where the emulator is.

The driver of ms0515-cli over its stdio is the emulator's own tool
(tools/emu_driver.py in its repository); the toolset and the projects only
use it, so this stand-in loads it under the same name, from $MS0515_DIR,
else the emulator's repository this tree is part of, else ../ms0515 beside
the repository this tree is in.
"""
from __future__ import annotations

import importlib.util
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def emulator() -> Path:
    """The emulator's repository."""
    base = os.environ.get("MS0515_DIR")
    tops = [HERE.parents[1], HERE.parents[1].parent / "ms0515"]
    for root in ([Path(base)] if base else []) + tops:
        if (root / "tools" / "emu_driver.py").is_file():
            return root
    raise SystemExit("emu_driver: no emulator repository found - set MS0515_DIR")


_spec = importlib.util.spec_from_file_location("_ms0515_emu_driver", emulator() / "tools" / "emu_driver.py")
_driver = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = _driver
_spec.loader.exec_module(_driver)

EmulatorDriver = _driver.EmulatorDriver
