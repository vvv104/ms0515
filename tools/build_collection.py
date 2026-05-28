#!/usr/bin/env python3
"""
build_collection.py - Ingest raw disk dumps and lay them out as single-sided
emulator-ready images under collection/ss/.

Source : $MS0515_SOURCE_DIR   (recursive, set via env var or --src)
Target : <repo>/collection/ss/   + INVENTORY.md

Every output is a 409600-byte single-sided image.  Double-sided sources are
split into two halves named "<stem>_s0.dsk" + "<stem>_s1.dsk".

Source handling:
  - 409600 .dsk/.raw     -> copy as "<stem>.dsk"
  - 819200 .dsk/.raw     -> detect layout (physical or LBN-linear), convert
                            LBN-linear into physical, split into _s0/_s1
  - Extended CPC DSK     -> decode, split (or single-side if side1 empty)
  - .TD0 (uncompressed)  -> decode, split (or single-side if side1 empty)

Filename collisions are resolved by prefixing each conflicting entry with
its source path components (joined by `_`).
"""

from __future__ import annotations

import os
import shutil
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

# Pull Extended-CPC and TD0 parsers from the sibling tools.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from convert_extended_dsk import parse_extended_dsk  # noqa: E402
import td0_decode  # noqa: E402

SS_SIZE = 409600                          # 80 tracks * 10 sec * 512
DS_SIZE = SS_SIZE * 2                     # 819200
TRACK_SIZE = 10 * 512                     # 5120

_SRC_ENV = os.environ.get("MS0515_SOURCE_DIR")
SRC_ROOT = Path(_SRC_ENV) if _SRC_ENV else None   # set in main() if --src given
REPO_ROOT = Path(__file__).resolve().parents[1]
COLLECTION = REPO_ROOT / "collection"
SS_DIR = COLLECTION / "ss"


@dataclass
class Entry:
    """One single-sided image destined for collection/ss/."""
    src_paths: list[Path]           # contributing source files
    stem: str                       # base name (without _sN suffix)
    side: int | None = None         # 0/1 when this is half of a DS; None for plain SS
    payload: bytes | None = None    # synthesised bytes, or None to copy src[0]
    note: str = ""                  # free-form annotation for INVENTORY
    skipped_reason: str = ""        # if non-empty, the entry is not written

    @property
    def filename(self) -> str:
        if self.side is None:
            return f"{self.stem}.dsk"
        return f"{self.stem}_s{self.side}.dsk"


def detect_ds_layout(data: bytes) -> str:
    if len(data) != DS_SIZE:
        return "unknown"
    if data[10240:10242] == b"\xa0\x00":
        return "physical"
    if data[0:2] == b"\xa0\x00":
        return "lbn-linear"
    return "unknown"


def lbn_linear_to_physical(data: bytes) -> bytes:
    """Re-lay an LBN-linear DS image (cyl-0-last, no sector interleave) into
    physical track-interleaved layout the emulator reads."""
    if len(data) != DS_SIZE:
        raise ValueError("lbn_linear_to_physical: image must be 819200 bytes")
    out = bytearray(DS_SIZE)
    for n in range(1600):
        src = n * 512
        cyl = (n // 20 + 1) % 80
        head = (n // 10) % 2
        sec = n % 10
        dst = (cyl * 2 + head) * TRACK_SIZE + sec * 512
        out[dst:dst + 512] = data[src:src + 512]
    return bytes(out)


def split_ds(ds: bytes) -> tuple[bytes, bytes]:
    """Split a track-interleaved DS image into two single-sided halves
    (T0H0..T79H0, T0H1..T79H1)."""
    if len(ds) != DS_SIZE:
        raise ValueError("split_ds: image must be 819200 bytes")
    s0 = bytearray(SS_SIZE)
    s1 = bytearray(SS_SIZE)
    for t in range(80):
        src = t * 2 * TRACK_SIZE
        dst = t * TRACK_SIZE
        s0[dst:dst + TRACK_SIZE] = ds[src:src + TRACK_SIZE]
        s1[dst:dst + TRACK_SIZE] = ds[src + TRACK_SIZE:src + 2 * TRACK_SIZE]
    return bytes(s0), bytes(s1)


def side_has_data(side: bytes) -> bool:
    return any(b != 0 for b in side)


def gather_sources(root: Path) -> list[Path]:
    paths: list[Path] = []
    for p in root.rglob("*"):
        if not p.is_file():
            continue
        if p.suffix.lower() in (".dsk", ".raw", ".td0"):
            paths.append(p)
    return paths


def _emit_ds(path: Path, stem: str, ds: bytes, note: str) -> list[Entry]:
    """Split a DS image and return one or two Entries.  Side 1 is dropped if
    it is all-zero (treat as effectively single-sided)."""
    s0, s1 = split_ds(ds)
    entries: list[Entry] = [
        Entry(src_paths=[path], stem=stem, side=0, payload=s0, note=note),
    ]
    if side_has_data(s1):
        entries.append(Entry(src_paths=[path], stem=stem, side=1,
                             payload=s1, note=note))
    else:
        entries[0].note += " (side 1 all-zero, skipped)"
    return entries


def classify(path: Path) -> list[Entry]:
    """Convert one source file into one or more Entries."""
    size = path.stat().st_size
    stem = path.stem
    ext = path.suffix.lower()

    if ext == ".td0":
        try:
            img = td0_decode.decode(path)
        except (ValueError, NotImplementedError) as e:
            return [Entry(src_paths=[path], stem=stem,
                          skipped_reason=f"TD0 decode failed: {e}")]
        payload, badmap, sides = td0_decode.assemble(img)
        bad = sum(badmap)
        zeros = sum(1 for i in range(0, len(payload), 512)
                    if payload[i:i + 512] == b"\x00" * 512)
        note = f"decoded TD0 (bad={bad}, zero-sectors={zeros})"
        td0_stem = f"{stem}_td0"
        if sides == 2:
            return _emit_ds(path, td0_stem, payload, note)
        return [Entry(src_paths=[path], stem=td0_stem,
                      payload=payload, note=note)]

    if size == SS_SIZE:
        return [Entry(src_paths=[path], stem=stem)]

    if size == DS_SIZE:
        data = path.read_bytes()
        layout = detect_ds_layout(data)
        if layout == "lbn-linear":
            return _emit_ds(path, stem, lbn_linear_to_physical(data),
                            "LBN-linear DS converted to physical, split")
        if layout == "physical":
            return _emit_ds(path, stem, data, "raw track-interleaved DS, split")
        return _emit_ds(path, stem, data,
                        "DS with unverified boot signature, split as-is")

    # Extended CPC DSK?
    try:
        head = path.read_bytes()[:22]
    except OSError as e:
        return [Entry(src_paths=[path], stem=stem,
                      skipped_reason=f"cannot read: {e}")]

    if head.startswith(b"EXTENDED CPC DSK File") or head.startswith(b"MV - CPCEMU"):
        result = parse_extended_dsk(str(path))
        if result is None:
            return [Entry(src_paths=[path], stem=stem,
                          skipped_reason="Extended DSK parse failed")]
        sides_data, num_sides = result
        side0 = bytes(sides_data[0])
        side1 = bytes(sides_data[1]) if num_sides > 1 else None
        if side1 is not None and side_has_data(side1):
            return [
                Entry(src_paths=[path], stem=stem, side=0, payload=side0,
                      note="decoded Extended CPC DSK"),
                Entry(src_paths=[path], stem=stem, side=1, payload=side1,
                      note="decoded Extended CPC DSK"),
            ]
        return [Entry(src_paths=[path], stem=stem, payload=side0,
                      note="decoded Extended CPC DSK (side 0 only)")]

    return [Entry(src_paths=[path], stem=stem,
                  skipped_reason=f"unknown size/format ({size} bytes)")]


def _path_prefix(p: Path, stem: str) -> str:
    try:
        rel = p.relative_to(SRC_ROOT)
    except ValueError:
        return ""
    parts = list(rel.parts[:-1])
    if parts and parts[-1] == stem:
        parts.pop()
    return "_".join(parts)


def disambiguate(entries: list[Entry]) -> None:
    """Rewrite stems of entries that would land on the same output filename."""
    buckets: dict[str, list[Entry]] = defaultdict(list)
    for e in entries:
        if e.skipped_reason:
            continue
        buckets[e.filename].append(e)
    for _, group in buckets.items():
        if len(group) <= 1:
            continue
        for e in group:
            prefix = _path_prefix(e.src_paths[0], e.stem)
            if prefix:
                e.stem = f"{prefix}_{e.stem}"
        seen: dict[str, list[Entry]] = defaultdict(list)
        for e in group:
            seen[e.filename].append(e)
        for st, dups in seen.items():
            if len(dups) > 1:
                for e in dups:
                    e.stem = f"{e.stem}_{e.src_paths[0].stat().st_size}"


def write_entry(e: Entry) -> Path:
    SS_DIR.mkdir(parents=True, exist_ok=True)
    out = SS_DIR / e.filename
    if e.payload is not None:
        out.write_bytes(e.payload)
    else:
        shutil.copyfile(e.src_paths[0], out)
    actual = out.stat().st_size
    if actual != SS_SIZE:
        raise RuntimeError(f"size mismatch after write: {out} = {actual}")
    return out


def rel_src(p: Path) -> str:
    try:
        return str(p.relative_to(SRC_ROOT)).replace("\\", "/")
    except ValueError:
        return str(p)


def write_inventory(entries: list[Entry]) -> None:
    lines: list[str] = []
    lines.append("# MS-0515 disk collection inventory")
    lines.append("")
    lines.append(f"Source root: `{SRC_ROOT}`")
    lines.append("")
    lines.append("Every entry is a 409600-byte single-sided image suitable for")
    lines.append("`--disk0` / `--disk1` mounts.  Double-sided sources are split")
    lines.append("into two halves with `_s0`/`_s1` suffixes.")
    lines.append("")
    lines.append("## Images (`ss/`)")
    lines.append("")
    lines.append("| File | Source | Note |")
    lines.append("|------|--------|------|")
    written = sorted((e for e in entries if not e.skipped_reason),
                     key=lambda e: e.filename.lower())
    for e in written:
        sources = ", ".join(rel_src(p) for p in e.src_paths)
        lines.append(f"| `{e.filename}` | `{sources}` | {e.note} |")
    lines.append("")

    skipped = [e for e in entries if e.skipped_reason]
    if skipped:
        lines.append("## Skipped")
        lines.append("")
        lines.append("| Source | Reason |")
        lines.append("|--------|--------|")
        for e in sorted(skipped, key=lambda e: rel_src(e.src_paths[0]).lower()):
            lines.append(f"| `{rel_src(e.src_paths[0])}` | {e.skipped_reason} |")
        lines.append("")

    declared = {e.filename for e in entries if not e.skipped_reason}
    extras: list[tuple[str, int]] = []
    if SS_DIR.is_dir():
        for p in sorted(SS_DIR.iterdir()):
            if p.is_file() and p.name not in declared:
                extras.append((p.name, p.stat().st_size))
    if extras:
        lines.append("## Recovered / synthesised artifacts")
        lines.append("")
        lines.append("Files in `ss/` that build_collection did not write directly")
        lines.append("(produced by recovery scripts under `recover/`).")
        lines.append("")
        lines.append("| File | Size |")
        lines.append("|------|-----:|")
        for name, size in extras:
            lines.append(f"| `{name}` | {size} |")
        lines.append("")

    lines.append(f"Totals: written={len(written)}, skipped={len(skipped)}, "
                 f"recovered={len(extras)}.")
    lines.append("")
    (COLLECTION / "INVENTORY.md").write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    global SRC_ROOT
    import argparse
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", type=Path,
                    help="root directory holding raw disk dumps "
                         "(overrides $MS0515_SOURCE_DIR)")
    args = ap.parse_args()
    if args.src is not None:
        SRC_ROOT = args.src
    if SRC_ROOT is None:
        print("error: source dir not set - pass --src <dir> or set "
              "MS0515_SOURCE_DIR")
        return 1
    if not SRC_ROOT.exists():
        print(f"Source directory missing: {SRC_ROOT}")
        return 1
    COLLECTION.mkdir(exist_ok=True)

    paths = gather_sources(SRC_ROOT)
    entries: list[Entry] = []
    for p in paths:
        entries.extend(classify(p))

    disambiguate(entries)

    for e in entries:
        if e.skipped_reason:
            print(f"  SKIP  {rel_src(e.src_paths[0])}: {e.skipped_reason}")
            continue
        out = write_entry(e)
        src_str = ", ".join(rel_src(p) for p in e.src_paths)
        print(f"  SS  ss/{out.name}  <-  {src_str}")

    write_inventory(entries)
    print(f"\nWrote inventory to {COLLECTION / 'INVENTORY.md'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
