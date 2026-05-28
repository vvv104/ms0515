#!/usr/bin/env python3
"""
extract_files.py - Pull every RT-11 file out of each disk image in
collection/ss/ into collection/extracted/<image-stem>/.

Detection works per-disk, trying every plausible physical-to-logical
mapping and picking the one that validates the most segment-chain links
and exposes the most PERM entries.  Four mappings are tested:

  ss-canonical      OSA-style: cyl-0-last, 2:1 sector interleave.
  ss-cyl0last-noil  cyl-0-last, no sector interleave.
  ss-cyl0first-noil cyl-0-first (no rotation), no interleave.
  ss-lbn-linear     plain (block N at byte N*512).

If a disk's directory points to file blocks beyond the SS bound (800
blocks), the filesystem is DS-spanning - we look for a sibling `_s1`
half, recombine, and retry with DS mappings.

Output per image:
  collection/extracted/<stem>/<FILENAME>     - raw RT-11 file bytes
  collection/extracted/<stem>/MANIFEST.md    - per-file table
  collection/extracted/INDEX.md              - one-row summary per image
"""

from __future__ import annotations

import hashlib
import struct
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

REPO_ROOT = Path(__file__).resolve().parents[1]
SS_DIR = REPO_ROOT / "collection" / "ss"
OUT_ROOT = REPO_ROOT / "collection" / "extracted"
ASSETS_DIR = REPO_ROOT / "src" / "assets" / "disks"
TESTS_DISKS_DIR = REPO_ROOT / "src" / "lib" / "tests" / "disks"

BLOCK = 512
SECTORS_PER_TRACK = 10
TRACKS = 80
TRACK_SIZE = SECTORS_PER_TRACK * BLOCK         # 5120
SS_SIZE = TRACKS * TRACK_SIZE                  # 409600
DS_SIZE = 2 * SS_SIZE                          # 819200
SS_BLOCKS = SS_SIZE // BLOCK                   # 800
DS_BLOCKS = DS_SIZE // BLOCK                   # 1600

INTERLEAVE = [0, 2, 4, 6, 8, 1, 3, 5, 7, 9]

E_TENT = 0o000400
E_MPTY = 0o001000
E_PERM = 0o002000
E_EOS  = 0o004000

RAD50 = ' ABCDEFGHIJKLMNOPQRSTUVWXYZ$.?0123456789'

DIR_CANDIDATE_BLOCKS = (6, 13, 8, 10, 12)


# ----- mappings: LBN -> byte offset within image -----

def ss_byte_canonical(n: int) -> int:
    return ((n // 10 + 1) % 80) * TRACK_SIZE + INTERLEAVE[n % 10] * BLOCK

def ss_byte_cyl0last_noil(n: int) -> int:
    return ((n // 10 + 1) % 80) * TRACK_SIZE + (n % 10) * BLOCK

def ss_byte_cyl0first_noil(n: int) -> int:
    return (n // 10) * TRACK_SIZE + (n % 10) * BLOCK

def ss_byte_linear(n: int) -> int:
    return n * BLOCK


# OSA uses canonical 2:1 sector interleave plus a per-track skew of +2
# sectors.  Derived from "create from inside" pattern probe (PIP writing
# from terminal to a fresh INIT'd disk):
#   sec(N) = (INT_canonical[N % 10] + 2 * track(N) - 2) mod 10
# At track 1 (where boot/home/dir live) the skew is zero so the formula
# reduces to canonical - this is why directories still parse cleanly with
# the plain `ss-canonical` mapping.  At track 2+ the rotation grows by 2
# sectors per track, giving us the file-area layout we observed.

def ss_byte_osa_skew(n: int) -> int:
    track = (n // 10 + 1) % 80
    sec = (INTERLEAVE[n % 10] + 2 * track - 2) % 10
    return track * TRACK_SIZE + sec * BLOCK


def ds_byte_canonical(n: int) -> int:
    cyl = (n // 20 + 1) % 80
    head = (n // 10) % 2
    sec = n % 10
    return (cyl * 2 + head) * TRACK_SIZE + INTERLEAVE[sec] * BLOCK

def ds_byte_cyl0last_noil(n: int) -> int:
    cyl = (n // 20 + 1) % 80
    head = (n // 10) % 2
    sec = n % 10
    return (cyl * 2 + head) * TRACK_SIZE + sec * BLOCK

def ds_byte_cyl0first_noil(n: int) -> int:
    cyl = n // 20
    head = (n // 10) % 2
    sec = n % 10
    return (cyl * 2 + head) * TRACK_SIZE + sec * BLOCK

def ds_byte_linear(n: int) -> int:
    return n * BLOCK


SS_MAPPINGS: dict[str, Callable[[int], int]] = {
    "ss-canonical":       ss_byte_canonical,
    "ss-cyl0last-noil":   ss_byte_cyl0last_noil,
    "ss-cyl0first-noil":  ss_byte_cyl0first_noil,
    "ss-lbn-linear":      ss_byte_linear,
    "ss-osa-skew":        ss_byte_osa_skew,
}

DS_MAPPINGS: dict[str, Callable[[int], int]] = {
    "ds-canonical":       ds_byte_canonical,
    "ds-cyl0last-noil":   ds_byte_cyl0last_noil,
    "ds-cyl0first-noil":  ds_byte_cyl0first_noil,
    "ds-lbn-linear":      ds_byte_linear,
}


# ----- reference boot blocks: identify a system without guessing -----

# Reference SS layouts per system.  Each system specifies both a
# directory-parsing mapping and a file-data mapping; in OSA they
# diverge (see `ss-osa-file`).
# Fields: (path, system, dir_layout, file_layout, is_ds)
# OSA, Omega, Mihin all use the same skew formula for file data - the
# "create from inside" pattern probe confirmed identical block placement
# on freshly-INIT'd disks under all three monitors.  Their DZ.SYS drivers
# implement the same canonical-2:1 + per-track skew-of-2 layout.
# rodionov (RT-15SJ) probe pending - left at ss-canonical until verified.
REFERENCE_DISKS = [
    (TESTS_DISKS_DIR / "test_osa.dsk",      "OSA",      "ss-canonical", "ss-osa-skew",  False),
    (TESTS_DISKS_DIR / "test_omega.dsk",    "Omega",    "ss-canonical", "ss-osa-skew",  False),
    (TESTS_DISKS_DIR / "test_mihin.dsk",    "Mihin",    "ss-canonical", "ss-osa-skew",  False),
    (TESTS_DISKS_DIR / "test_rod.dsk",      "rodionov", "ss-canonical", "ss-canonical", True),
    (ASSETS_DIR / "osa.dsk",                 "OSA",      "ss-canonical", "ss-osa-skew",  False),
    (ASSETS_DIR / "omega-lang.dsk",          "Omega",    "ss-canonical", "ss-osa-skew",  False),
    (ASSETS_DIR / "omega-games.dsk",         "Omega",    "ss-canonical", "ss-osa-skew",  False),
    (ASSETS_DIR / "mihin.dsk",               "Mihin",    "ss-canonical", "ss-osa-skew",  False),
    (ASSETS_DIR / "rodionov.dsk",            "rodionov", "ss-canonical", "ss-canonical", True),
]


def _boot_hash(boot_block: bytes) -> str:
    return hashlib.sha256(boot_block).hexdigest()[:12]


def build_reference_table() -> dict[str, tuple[str, str, str]]:
    """Return {boot_hash: (system_name, dir_layout, file_layout)}.
    Side 1 of DS reference disks is registered separately."""
    table: dict[str, tuple[str, str, str]] = {}
    for path, system, dir_layout, file_layout, is_ds in REFERENCE_DISKS:
        if not path.exists():
            continue
        data = path.read_bytes()
        if len(data) >= 5120 + 512:
            table.setdefault(_boot_hash(data[5120:5632]),
                             (system, dir_layout, file_layout))
        if is_ds and len(data) >= DS_SIZE:
            table.setdefault(_boot_hash(data[10240:10240 + 512]),
                             (system + "-side1", dir_layout, file_layout))
    return table


REFERENCE_BOOT_TABLE = build_reference_table()


# ----- directory parsing -----

def rad50_word(w: int) -> str:
    if w >= 64000:
        return "???"
    return (RAD50[w // 1600]
            + RAD50[(w // 40) % 40]
            + RAD50[w % 40])


def decode_filename(fn1: int, fn2: int, ext: int) -> str:
    name = (rad50_word(fn1) + rad50_word(fn2)).rstrip()
    e = rad50_word(ext).rstrip()
    return f"{name}.{e}" if e else name


@dataclass
class DirEntry:
    status: int
    name: str
    start_block: int
    length: int
    date_word: int

    @property
    def is_perm(self) -> bool:
        return bool(self.status & E_PERM)


def parse_segment(seg: bytes, max_blocks: int) -> tuple[dict, list[DirEntry]] | None:
    if len(seg) < 10:
        return None
    seg_total, seg_next, seg_high, extra, data_block = struct.unpack_from("<5H", seg, 0)
    if not (1 <= seg_total <= 31):
        return None
    if not (1 <= seg_high <= seg_total):
        return None
    if seg_next > seg_total:
        return None
    if extra > 64 or extra & 1:
        return None
    if not (1 <= data_block <= max_blocks):
        return None

    entry_size = 14 + extra
    entries: list[DirEntry] = []
    cur = data_block
    p = 10
    end = 1024
    while p + entry_size <= end:
        status, fn1, fn2, ext, length, _cj, date = struct.unpack_from("<7H", seg, p)
        if status == 0:
            return None
        entries.append(DirEntry(status, decode_filename(fn1, fn2, ext),
                                cur, length, date))
        cur += length
        p += entry_size
        if status & E_EOS:
            break
    if not entries or not any(e.is_perm for e in entries):
        return None
    header = {
        "segs_total": seg_total,
        "next_seg":   seg_next,
        "high_seg":   seg_high,
        "extra":      extra,
        "data_block": data_block,
    }
    return header, entries


def read_block(data: bytes, mapping: Callable[[int], int], lbn: int,
               max_blocks: int) -> bytes:
    if lbn < 0 or lbn >= max_blocks:
        return b"\x00" * BLOCK
    off = mapping(lbn)
    if off + BLOCK > len(data):
        return b"\x00" * BLOCK
    return data[off:off + BLOCK]


def read_segment(data: bytes, mapping, dir_lbn: int, max_blocks: int) -> bytes:
    return read_block(data, mapping, dir_lbn, max_blocks) + \
           read_block(data, mapping, dir_lbn + 1, max_blocks)


def walk_directory(data: bytes, mapping, first_dir_lbn: int,
                   max_blocks: int) -> tuple[int, list[DirEntry]] | None:
    """Return (segments_validated, accumulated_entries).  None if seg 1
    doesn't parse."""
    seen: set[int] = set()
    all_entries: list[DirEntry] = []
    segs_ok = 0
    seg_index = 1
    dir_lbn = first_dir_lbn
    while seg_index not in seen:
        seen.add(seg_index)
        seg_bytes = read_segment(data, mapping, dir_lbn, max_blocks)
        result = parse_segment(seg_bytes, max_blocks)
        if result is None:
            break
        header, entries = result
        all_entries.extend(entries)
        segs_ok += 1
        if header["next_seg"] == 0:
            break
        seg_index = header["next_seg"]
        dir_lbn = first_dir_lbn + (seg_index - 1) * 2
        if dir_lbn + 1 >= max_blocks:
            break
    if not all_entries:
        return None
    return segs_ok, all_entries


def detect(data: bytes, mappings: dict, max_blocks: int) \
        -> tuple[str, Callable, int, list[DirEntry]] | None:
    """Pick (mapping_name, mapping, dir_lbn, entries) that yields the longest
    validated segment chain and the most PERM entries."""
    best = None
    best_score = (0, 0)
    for mname, m in mappings.items():
        for dir_lbn in DIR_CANDIDATE_BLOCKS:
            walked = walk_directory(data, m, dir_lbn, max_blocks)
            if walked is None:
                continue
            segs, entries = walked
            perm = sum(1 for e in entries if e.is_perm)
            score = (segs, perm)
            if score > best_score:
                best_score = score
                best = (mname, m, dir_lbn, entries)
    return best


# ----- extraction -----

SAFE_CHARS = set(
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-$")


def safe_filename(name: str) -> str:
    out = "".join(c if c in SAFE_CHARS else "_" for c in name)
    return out or "unnamed"


def extract_file(data: bytes, mapping, e: DirEntry, max_blocks: int) -> bytes:
    chunks = [read_block(data, mapping, e.start_block + i, max_blocks)
              for i in range(e.length)]
    return b"".join(chunks)


def write_manifest(out_dir: Path, image: Path, mapping_name: str,
                   dir_lbn: int, entries: list[DirEntry],
                   written: list[tuple[DirEntry, str]],
                   skipped: list[tuple[DirEntry, str]]) -> None:
    lines = []
    lines.append(f"# `{image.name}` extraction manifest")
    lines.append("")
    lines.append(f"- Source : `{image}`")
    lines.append(f"- Layout : `{mapping_name}`")
    lines.append(f"- First directory LBN : {dir_lbn}")
    lines.append(f"- Total directory entries : {len(entries)}")
    lines.append(f"- Files written : {len(written)}")
    if skipped:
        lines.append(f"- Files skipped : {len(skipped)}")
    lines.append("")
    lines.append("## Written files\n")
    lines.append("| Filename | RT-11 name | Start LBN | Length (blocks) | Bytes |")
    lines.append("|----------|------------|----------:|----------------:|------:|")
    for e, fname in written:
        lines.append(f"| `{fname}` | `{e.name}` | {e.start_block} | "
                     f"{e.length} | {e.length * BLOCK} |")
    lines.append("")
    if skipped:
        lines.append("## Skipped entries\n")
        lines.append("| RT-11 name | Reason |")
        lines.append("|------------|--------|")
        for e, reason in skipped:
            lines.append(f"| `{e.name}` | {reason} |")
        lines.append("")
    (out_dir / "MANIFEST.md").write_text("\n".join(lines), encoding="utf-8")


def is_ds_spanning(entries: list[DirEntry]) -> bool:
    """If any file's blocks reach beyond the SS bound, it's a DS filesystem."""
    for e in entries:
        if e.is_perm and e.start_block + e.length > SS_BLOCKS:
            return True
    return False


def split_ds_interleave(s0: bytes, s1: bytes) -> bytes:
    """Recombine two SS halves into a track-interleaved DS image."""
    out = bytearray(DS_SIZE)
    for t in range(80):
        out[t * 2 * TRACK_SIZE:t * 2 * TRACK_SIZE + TRACK_SIZE] = \
            s0[t * TRACK_SIZE:(t + 1) * TRACK_SIZE]
        out[t * 2 * TRACK_SIZE + TRACK_SIZE:(t + 1) * 2 * TRACK_SIZE] = \
            s1[t * TRACK_SIZE:(t + 1) * TRACK_SIZE]
    return bytes(out)


def extract_to(out_dir: Path, image: Path, data: bytes,
               mapping_name: str, mapping, dir_lbn: int,
               entries: list[DirEntry], max_blocks: int) -> dict:
    out_dir.mkdir(parents=True, exist_ok=True)
    used: dict[str, int] = {}
    written: list[tuple[DirEntry, str]] = []
    skipped: list[tuple[DirEntry, str]] = []
    for e in entries:
        if not e.is_perm:
            continue
        if e.length == 0:
            skipped.append((e, "zero length"))
            continue
        if e.start_block + e.length > max_blocks:
            skipped.append((e, f"out-of-bounds (start={e.start_block} "
                              f"len={e.length}, max={max_blocks})"))
            continue
        fname = safe_filename(e.name)
        if fname in used:
            used[fname] += 1
            stem, dot, ext = fname.partition(".")
            fname = f"{stem}_{used[fname]}" + (f".{ext}" if dot else "")
        else:
            used[fname] = 0
        (out_dir / fname).write_bytes(
            extract_file(data, mapping, e, max_blocks))
        written.append((e, fname))
    write_manifest(out_dir, image, mapping_name, dir_lbn,
                   entries, written, skipped)
    return {"layout": mapping_name, "dir_lbn": dir_lbn,
            "files": len(written), "skipped": len(skipped)}


# DS pairs in the collection use one of two naming conventions:
#   <base>_s0 / <base>_s1   - standard
#   <base>_Head0 / <base>_Head1   - alternate (some recovery dumps)
DS_PAIR_SUFFIXES: tuple[tuple[str, str], ...] = (
    ("_s0", "_s1"),
    ("_Head0", "_Head1"),
)


def _ds_pair_base(stem: str) -> tuple[str, str] | None:
    """If `stem` matches a DS-pair s0 suffix, return (base, s1_stem); else None."""
    for s0_suf, s1_suf in DS_PAIR_SUFFIXES:
        if stem.endswith(s0_suf):
            return stem[:-len(s0_suf)], stem[:-len(s0_suf)] + s1_suf
    return None


def _ds_sibling_path(image: Path) -> Path | None:
    """Return the _s1/_Head1 sibling image (if it exists in SS_DIR)."""
    pair = _ds_pair_base(image.stem)
    if pair is None:
        return None
    _, s1_stem = pair
    sibling = image.with_name(s1_stem + image.suffix)
    return sibling if sibling.exists() else None


def _has_ds_sibling(image: Path) -> bool:
    return _ds_sibling_path(image) is not None


def identify_by_boot(data: bytes) -> tuple[str, str, str] | None:
    """Return (system_name, dir_layout, file_layout) for a recognised
    boot block, else None."""
    if len(data) < 5632:
        return None
    return REFERENCE_BOOT_TABLE.get(_boot_hash(data[5120:5632]))


def _attempt_extract(image: Path, data: bytes, system: str,
                      dir_layout: str, file_layout: str) -> dict | None:
    """Try parsing dir under `dir_layout` and walking the chain.  Returns a
    result dict ready for `process_ss` or None if no usable directory."""
    dir_mapping = SS_MAPPINGS[dir_layout]
    file_mapping = SS_MAPPINGS[file_layout]
    for dir_lbn in DIR_CANDIDATE_BLOCKS:
        walked = walk_directory(data, dir_mapping, dir_lbn, SS_BLOCKS)
        if walked is None:
            continue
        _segs, entries = walked
        if is_ds_spanning(entries):
            return {"image": image.name, "status": "ds-spanning",
                    "system": system,
                    "reason": "file blocks exceed SS bound "
                              "(DS-spanning filesystem)",
                    "layout": f"{dir_layout}+{file_layout}",
                    "dir_lbn": dir_lbn}
        info = extract_to(OUT_ROOT / image.stem, image, data,
                          f"{dir_layout}+{file_layout}", file_mapping,
                          dir_lbn, entries, SS_BLOCKS)
        return {"image": image.name, "stem": image.stem, "status": "ok",
                "system": system, **info}
    return None


def process_ss(image: Path) -> dict:
    data = image.read_bytes()
    if len(data) != SS_SIZE:
        return {"image": image.name, "status": "skipped",
                "reason": f"size {len(data)} != {SS_SIZE}"}

    ident = identify_by_boot(data)
    if ident is not None:
        system, dir_layout, file_layout = ident
        result = _attempt_extract(image, data, system,
                                   dir_layout, file_layout)
        if result is not None:
            return result
        return {"image": image.name, "status": "no-rt11",
                "system": system,
                "reason": f"system={system} but no RT-11 directory under "
                          f"{dir_layout}"}

    # No reference boot match.  Try every plausible (dir, file) combo:
    # OSA/Omega/Mihin write via ss-osa-skew (dir parses as canonical too);
    # some disks use plain canonical, others use cyl0last-noil with no
    # interleave (ARCSAV/disk4/disk5/superBAK7).  Take the first hit, but
    # for cyl0last-noil specifically, defer to Pass 2 if a _s1 sibling
    # exists - those filesystems are usually DS-spanning even when no
    # individual file crosses the SS bound (file bytes are still
    # interleaved across both sides).
    fallback_combos = [
        ("ss-canonical",     "ss-osa-skew"),
        ("ss-canonical",     "ss-canonical"),
        ("ss-cyl0last-noil", "ss-cyl0last-noil"),
    ]
    ds_spanning_result = None
    for dir_layout, file_layout in fallback_combos:
        result = _attempt_extract(image, data, f"unknown-{file_layout}",
                                   dir_layout, file_layout)
        if result is None:
            continue
        if result["status"] == "ok":
            if (file_layout == "ss-cyl0last-noil"
                    and _has_ds_sibling(image)):
                return {"image": image.name, "status": "ds-spanning",
                        "system": f"unknown-{file_layout}",
                        "reason": "cyl0last-noil dir parsed + DS sibling "
                                  "exists; deferring to DS recombination",
                        "layout": f"{dir_layout}+{file_layout}",
                        "dir_lbn": result["dir_lbn"]}
            return result
        if result["status"] == "ds-spanning" and ds_spanning_result is None:
            ds_spanning_result = result
    if ds_spanning_result is not None:
        return ds_spanning_result

    return {"image": image.name, "status": "pending-phase-b",
            "system": "-",
            "reason": "boot block unknown, no RT-11 directory under "
                      "ss-osa-skew, ss-canonical or ss-cyl0last-noil"}


def process_ds_pair(s0_image: Path, s1_image: Path, base_stem: str) -> dict:
    s0 = s0_image.read_bytes()
    s1 = s1_image.read_bytes()
    if len(s0) != SS_SIZE or len(s1) != SS_SIZE:
        return {"image": f"{s0_image.name} + {s1_image.name}",
                "status": "skipped",
                "reason": "unexpected SS half sizes"}
    data = split_ds_interleave(s0, s1)
    found = detect(data, DS_MAPPINGS, DS_BLOCKS)
    if not found:
        return {"image": f"{s0_image.name} + {s1_image.name}",
                "status": "no-rt11",
                "reason": "no RT-11 directory under any DS mapping"}
    mname, mapping, dir_lbn, entries = found
    info = extract_to(OUT_ROOT / base_stem,
                      Path(f"{base_stem} (DS recombined)"),
                      data, mname, mapping, dir_lbn, entries, DS_BLOCKS)
    return {"image": f"{s0_image.name} + {s1_image.name}",
            "stem": base_stem, "status": "ok-ds", **info}


def extract_embedded_dsk(image_path: Path, out_dir: Path) -> int:
    """Some MS-0515 .DSK files are themselves mini-disk-images stored as
    flat files in the host filesystem (e.g. PROGS.DSK).  They use
    LBN-linear layout: boot at byte 0, dir at byte 3072, file LBN N at
    byte N*512.  Walk the chain and extract PERM entries.  Returns the
    number of files written, or 0 if the file isn't a recognisable
    embedded image."""
    data = image_path.read_bytes()
    if len(data) < 4096 or data[:2] != b"\xa0\x00":
        return 0   # no OSA boot signature at byte 0 -> not embedded image

    def parse_seg(off: int):
        if off + 1024 > len(data):
            return None
        st, nx, hi, ex, db = struct.unpack_from("<5H", data, off)
        if not (1 <= st <= 31) or not (1 <= hi <= st) or nx > st:
            return None
        if ex > 64 or ex & 1 or not (1 <= db * BLOCK <= len(data)):
            return None
        entry_size = 14 + ex
        out = []
        cur = db
        p = off + 10
        end = off + 1024
        while p + entry_size <= end:
            status, fn1, fn2, ext_w, length, _, _ = \
                struct.unpack_from("<7H", data, p)
            if status == 0:
                return None
            out.append((status, decode_filename(fn1, fn2, ext_w), cur, length))
            cur += length
            p += entry_size
            if status & E_EOS:
                break
        if not out or not any(e[0] & E_PERM for e in out):
            return None
        return {"next": nx, "high": hi, "extra": ex}, out

    # Walk segment chain
    seg_index = 1
    dir_lbn = 6
    seen = set()
    entries = []
    while seg_index not in seen:
        seen.add(seg_index)
        res = parse_seg(dir_lbn * BLOCK)
        if res is None:
            break
        header, recs = res
        entries.extend(recs)
        if header["next"] == 0:
            break
        seg_index = header["next"]
        dir_lbn = 6 + (seg_index - 1) * 2

    if not entries:
        return 0

    out_dir.mkdir(parents=True, exist_ok=True)
    used: dict[str, int] = {}
    written = []
    for status, name, start, length in entries:
        if not (status & E_PERM):
            continue
        if length == 0 or start * BLOCK + length * BLOCK > len(data):
            continue
        fname = safe_filename(name)
        if fname in used:
            used[fname] += 1
            stem, dot, ext = fname.partition(".")
            fname = f"{stem}_{used[fname]}" + (f".{ext}" if dot else "")
        else:
            used[fname] = 0
        (out_dir / fname).write_bytes(
            data[start * BLOCK:(start + length) * BLOCK])
        written.append((name, fname, start, length))

    # Manifest
    lines = [
        f"# `{image_path.name}` (embedded LBN-linear disk image)",
        "",
        f"Source : `{image_path}`",
        f"Size   : {len(data)} bytes ({len(data) // BLOCK} blocks)",
        f"Files  : {len(written)} written",
        "",
        "| RT-11 name | Safe name | Start LBN | Length (blocks) | Bytes |",
        "|------------|-----------|----------:|----------------:|------:|",
    ]
    for rt_name, safe, start, length in written:
        lines.append(f"| `{rt_name}` | `{safe}` | {start} | {length} | "
                     f"{length * BLOCK} |")
    (out_dir / "MANIFEST.md").write_text("\n".join(lines), encoding="utf-8")
    return len(written)


def write_index(results: list[dict]) -> None:
    lines = []
    lines.append("# Extraction index\n")
    lines.append("`System` = identified via boot-block hash matching reference")
    lines.append("disks (`src/lib/tests/disks/` + `src/assets/disks/`).")
    lines.append("`(heuristic)` means we fell back to chain-validation guessing.\n")
    lines.append("| Image | Status | System | Layout | Dir LBN | Files | Skipped |")
    lines.append("|-------|--------|--------|--------|--------:|------:|--------:|")
    for r in sorted(results, key=lambda x: x["image"].lower()):
        sysname = r.get("system", "-")
        if r["status"] in ("ok", "ok-ds"):
            lines.append(f"| `{r['image']}` | {r['status']} | {sysname} | "
                         f"{r['layout']} | {r['dir_lbn']} | "
                         f"{r['files']} | {r['skipped']} |")
        else:
            lines.append(f"| `{r['image']}` | {r['status']} | {sysname} | - "
                         f"| - | - | - <br/>_{r.get('reason','')}_ |")
    lines.append("")
    by_status = Counter(r["status"] for r in results)
    lines.append("Totals: " + ", ".join(f"{k}={v}"
                                        for k, v in by_status.items()))
    lines.append("")
    (OUT_ROOT / "INDEX.md").write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    if not SS_DIR.exists():
        print(f"missing {SS_DIR}")
        return 1
    OUT_ROOT.mkdir(parents=True, exist_ok=True)

    # Pass 1: try each disk as standalone SS.
    images = sorted(p for p in SS_DIR.iterdir()
                    if p.is_file() and p.suffix.lower() == ".dsk")
    results = [process_ss(p) for p in images]

    # Pass 2: any image whose SS-side directory points beyond the SS
    # bound is half of a DS-spanning filesystem.  Find the _s1 sibling,
    # track-interleave them back into a 819200-byte image, and extract
    # under the best matching DS mapping.  Replace both halves' Pass-1
    # results with the joint DS result.
    images_by_stem = {p.stem: p for p in images}
    processed_ds_stems: set[str] = set()
    for idx, r in enumerate(results):
        if r["status"] != "ds-spanning":
            continue
        stem = Path(r["image"]).stem
        pair = _ds_pair_base(stem)
        if pair is None:
            continue   # only drive pass 2 from the s0/Head0 half
        base, s1_stem = pair
        if base in processed_ds_stems:
            results[idx] = {"image": r["image"], "status": "ds-dup",
                            "system": "-", "stem": stem,
                            "reason": f"DS pair {base} already processed "
                                      f"via another naming variant"}
            # Also mark our _s1/_Head1 sibling, if present in results
            for j, rj in enumerate(results):
                if Path(rj["image"]).stem == s1_stem:
                    results[j] = {"image": rj["image"], "status": "ds-dup",
                                  "system": "-", "stem": s1_stem,
                                  "reason": f"DS pair {base} already "
                                            f"processed via another "
                                            f"naming variant"}
                    break
            continue
        if s1_stem not in images_by_stem:
            r["reason"] = (r.get("reason", "") +
                           f"; no DS sibling ({s1_stem}) for recombine")
            continue
        ds_result = process_ds_pair(images_by_stem[stem],
                                     images_by_stem[s1_stem], base)
        results[idx] = ds_result
        # Mark the s1/Head1 half as consumed by its DS pair
        for j, rj in enumerate(results):
            if Path(rj["image"]).stem == s1_stem:
                results[j] = {"image": rj["image"], "status": "ds-half",
                              "system": "-", "stem": s1_stem,
                              "reason": f"recombined with {stem} -> {base}"}
                break
        processed_ds_stems.add(base)

    for r in sorted(results, key=lambda x: x["image"].lower()):
        sysname = r.get("system", "-")
        if r["status"] in ("ok", "ok-ds"):
            print(f"  {r['image']:<42}  {r['status']:<6}  "
                  f"{sysname:<14}  {r['layout']:<20}  dir@{r['dir_lbn']:>2}  "
                  f"files={r['files']:>3}  skipped={r['skipped']}")
        else:
            print(f"  {r['image']:<42}  {r['status']:<11}  "
                  f"{sysname:<14}  {r.get('reason','')}")

    # After main extraction, recursively dive into any .DSK files we
    # extracted - they may themselves be LBN-linear mini-disk-images
    # (e.g. PROGS.DSK).  Skip if user-side files already exist (idempotent).
    nested_total = 0
    for nested in sorted(OUT_ROOT.rglob("*.[Dd][Ss][Kk]")):
        if not nested.is_file():
            continue
        out_dir = nested.parent / (nested.stem + "_extracted")
        if out_dir.exists() and any(out_dir.iterdir()):
            continue
        n = extract_embedded_dsk(nested, out_dir)
        if n > 0:
            rel = nested.relative_to(OUT_ROOT).as_posix()
            print(f"  embedded:  {rel:<55} files={n}")
            nested_total += n
    if nested_total:
        print(f"\nExtracted {nested_total} files from embedded .DSK images.")
    write_index(results)
    print(f"\nWrote {OUT_ROOT / 'INDEX.md'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
