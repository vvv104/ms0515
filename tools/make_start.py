"""make_start.py - a start file for the browser build from a card.

A start file (src/web/www/start.js; README.md there, "Start files") is the
machine at the moment a game begins: one .zip with the snapshot, the
diskette the game runs from, the settings and a picture.  A card says what
that moment is - a TOML file:

    title = "Saboteur II"                 # what the page says
    rom   = "a"                           # "a" (default) or "b"

    [disk]                                # drive A, side 0
    system = "osa"                        # composed from the software collection
    media  = "ss"                         # (ms0515-disk compose --system/--media/--add)
    add    = ["sabot2-osa"]
    # image = "games.dsk"                 # ... or a ready image, relative to the card
    # name  = "sabot2.dsk"                # the image's name in the file (default: the card's)

    [run]
    script = ["R SABOT2"]                 # typed after the boot, one line at a time
    settle = 2.0                          # seconds of a still screen between the lines
    wait   = 4.0                          # seconds from the last line's Return, at the
                                          # machine's pace, to the moment that is kept

    [sound]                               # what the page turns on; a game start
    speaker = true                        # wants the speaker and nothing else
    drive   = false
    kbd     = false
    speed   = 100                         # the page's speed control, per cent

The diskette is composed (or copied), ms0515-cli boots it at the machine's
pace with the terminal mirrored to stdio, the startup questions are
answered, the script typed, and the CLI's quit key makes it save the
snapshot and the screen on the way out; the parts are packed as start.js
reads them.  The snapshot is taken in a scratch directory by the image's
bare name, so that no path of this machine goes into the file.

    python tools/make_start.py src/web/starts/sabot2.toml [-o sabot2.zip]

The binaries are looked up in $MS0515_PACKAGE, else in the repository's
package/ directory; the software collection in $MS0515_SOFTWARE, else in
../ms0515-software beside the repository.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import shutil
import subprocess
import sys
import tempfile
import time
import tomllib
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "rt11_devel" / "toolset"))
from decsys import collection  # noqa: E402
from emu_driver import EmulatorDriver  # noqa: E402
from run_program import CLI, DISKTOOL, PACKAGE, QUIT_HOTKEY, answer_startup, wait_quiet  # noqa: E402

SCHEMA = 1
ROMS = {"a": "ms0515-roma.rom", "b": "ms0515-romb.rom"}
SOUND = {"speaker": True, "drive": False, "kbd": False}


def load_card(path: Path) -> dict:
    card = tomllib.loads(path.read_text(encoding="utf-8"))
    if not card.get("title"):
        raise SystemExit(f"{path}: no title")
    if card.get("rom", "a") not in ROMS:
        raise SystemExit(f"{path}: rom must be \"a\" or \"b\"")
    disk = card.get("disk") or {}
    if bool(disk.get("image")) == bool(disk.get("system")):
        raise SystemExit(f"{path}: [disk] names either an image or a system to compose")
    if not (card.get("run") or {}).get("script"):
        raise SystemExit(f"{path}: [run] script is empty - nothing to start")
    return card


def make_disk(card: dict, card_path: Path, workdir: Path) -> Path:
    """The diskette, by its bare name in the scratch directory."""
    disk = card["disk"]
    name = disk.get("name") or (Path(disk["image"]).name if disk.get("image") else card_path.stem + ".dsk")
    out = workdir / name
    if disk.get("image"):
        shutil.copyfile(card_path.parent / disk["image"], out)
        return out
    cmd = [str(DISKTOOL), "compose", "--repo", str(collection()),
           "--system", disk["system"], "--media", disk.get("media", "ss")]
    if disk.get("add"):
        cmd += ["--add", ",".join(disk["add"])]
    for pick in disk.get("pick", []):
        cmd += ["--pick", pick]
    for line in disk.get("startup", []):
        cmd += ["--startup", line]
    cmd.append(str(out))
    res = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if res.returncode != 0:
        raise SystemExit("ms0515-disk compose failed:\n" + res.stdout + res.stderr)
    return out


def run_to_the_moment(card: dict, image: Path, workdir: Path) -> None:
    """Boot, type the script, keep the moment: state.ms0515 and screen.png
    in the scratch directory."""
    run = card["run"]
    cmd = [str(CLI), "--no-config", "--disk0-side0", image.name, "--realtime", "--frames", "6000000",
           "--drive-sounds", "off", "--keyboard-sounds", "off",
           "--save-state", "state.ms0515", "--screenshot", "screen.png",
           "--rom", str(PACKAGE / "assets" / "rom" / ROMS[card.get("rom", "a")])]
    emu = EmulatorDriver(cmd, cwd=workdir, encoding="utf-8")
    emu.start()
    try:
        settle = float(run.get("settle", 2.0))
        answer_startup(emu, settle)
        *lines, last = run["script"]
        for line in lines:
            emu.send(line + "\r")
            wait_quiet(emu, settle, float(run.get("timeout", 60.0)))
        # The last line is the game: its screens are graphics, which the
        # terminal mirror does not show, so the moment is a count of
        # seconds from the Return, at the machine's own pace.
        emu.send(last + "\r")
        time.sleep(float(run.get("wait", 6.0)))
        emu.send(QUIT_HOTKEY)
        emu._proc.wait(timeout=30)       # noqa: SLF001 - the driver has no public waiter
    finally:
        emu.kill()
    for part in ("state.ms0515", "screen.png"):
        if not (workdir / part).exists():
            raise SystemExit(f"the CLI left no {part}: did the guest take the quit key?")


def pack(card: dict, card_path: Path, image: Path, workdir: Path, out: Path) -> None:
    sound = {**SOUND, **{k: bool(v) for k, v in (card.get("sound") or {}).items() if k in SOUND}}
    meta = {
        "schema": SCHEMA,
        "title": card["title"],
        "rom": card.get("rom", "a"),
        "disks": {"fd": [image.name, "", "", ""], "hd": ""},
        "sound": sound,
        "speed": int((card.get("sound") or {}).get("speed", 100)),
        "made": {"by": "tools/make_start.py", "card": card_path.name,
                 "command": " / ".join(card["run"]["script"]),
                 "date": dt.date.today().isoformat()},
    }
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("start.json", json.dumps(meta, indent=2) + "\n")
        z.write(workdir / "state.ms0515", "state.ms0515")
        z.write(image, "disks/" + image.name)
        z.write(workdir / "screen.png", "screen.png")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("card", type=Path, help="the card (TOML)")
    ap.add_argument("-o", "--out", type=Path, help="the start file (default: the card's name with .zip)")
    args = ap.parse_args()
    if not CLI.exists() or not DISKTOOL.exists():
        print("ms0515-cli / ms0515-disk not found in %s (set MS0515_PACKAGE)" % PACKAGE, file=sys.stderr)
        return 2
    card = load_card(args.card)
    out = args.out or args.card.with_suffix(".zip")
    with tempfile.TemporaryDirectory(prefix="ms0515-start-") as tmp:
        workdir = Path(tmp)
        image = make_disk(card, args.card.resolve(), workdir)
        run_to_the_moment(card, image, workdir)
        pack(card, args.card, image, workdir, out)
    print(f"{out}: {out.stat().st_size} bytes - {card['title']}, {image.name}, "
          f"after {' / '.join(card['run']['script'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
