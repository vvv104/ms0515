#!/usr/bin/env python3
"""
extract_embedded_dsk.py - Pull RT-11 files out of "disk image inside a
disk image" files like PROGS.DSK.

These are mini-disk-image files stored in the host filesystem as
ordinary data: an OS or utility on the MS-0515 builds a "virtual
floppy" inside a file.  Because there are no real physical sectors,
the layout is **LBN-linear**: LBN N lives at byte N*512.  The boot
signature `a0 00` sits at byte 0, the home block at byte 512, and the
first directory segment at byte 3072.

Usage:
    python extract_embedded_dsk.py <file.dsk> [<out-dir>]

If `out-dir` is omitted, a sibling directory `<stem>_extracted/` is
used.  Writes one file per RT-11 PERM entry plus a `MANIFEST.md`.
"""

from __future__ import annotations

import os
import struct
import sys
from pathlib import Path

BLOCK = 512

E_TENT = 0o000400
E_MPTY = 0o001000
E_PERM = 0o002000
E_EOS  = 0o004000

RAD50_CHARS = ' ABCDEFGHIJKLMNOPQRSTUVWXYZ$.?0123456789'

SAFE_CHARS = set(
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-$")


def rad50_word(w):
    if w >= 64000:
        return '???'
    return (RAD50_CHARS[w // 1600]
            + RAD50_CHARS[(w // 40) % 40]
            + RAD50_CHARS[w % 40])


def decode_filename(fn1, fn2, ext):
    name = (rad50_word(fn1) + rad50_word(fn2)).rstrip()
    e = rad50_word(ext).rstrip()
    return f"{name}.{e}" if e else name


def safe_filename(name):
    out = "".join(c if c in SAFE_CHARS else "_" for c in name)
    return out or "unnamed"


def parse_segment(data: bytes, off: int) -> tuple[dict, list] | None:
    if off + 1024 > len(data):
        return None
    seg_total, seg_next, seg_high, extra, data_block = \
        struct.unpack_from('<5H', data, off)
    if not (1 <= seg_total <= 31):
        return None
    if not (1 <= seg_high <= seg_total):
        return None
    if seg_next > seg_total:
        return None
    if extra > 64 or extra & 1:
        return None
    if not (1 <= data_block * BLOCK <= len(data)):
        return None

    entry_size = 14 + extra
    entries = []
    cur = data_block
    p = off + 10
    end = off + 1024
    while p + entry_size <= end:
        status, fn1, fn2, ext, length, _, date = \
            struct.unpack_from('<7H', data, p)
        if status == 0:
            return None
        entries.append({
            'status': status,
            'name':   decode_filename(fn1, fn2, ext),
            'start':  cur,
            'length': length,
            'date':   date,
        })
        cur += length
        p += entry_size
        if status & E_EOS:
            break
    if not entries or not any(e['status'] & E_PERM for e in entries):
        return None
    return {'segs_total': seg_total, 'next_seg': seg_next,
            'high_seg': seg_high, 'extra': extra,
            'data_block': data_block}, entries


def walk_directory(data: bytes, first_dir_lbn: int = 6) -> list:
    """Walk the directory segment chain (LBN-linear layout)."""
    all_entries = []
    seen = set()
    seg_index = 1
    dir_lbn = first_dir_lbn
    while seg_index not in seen:
        seen.add(seg_index)
        off = dir_lbn * BLOCK
        result = parse_segment(data, off)
        if result is None:
            break
        header, entries = result
        all_entries.extend(entries)
        if header['next_seg'] == 0:
            break
        seg_index = header['next_seg']
        dir_lbn = first_dir_lbn + (seg_index - 1) * 2
    return all_entries


def extract(image_path: Path, out_dir: Path) -> int:
    data = image_path.read_bytes()
    if data[:2] != b'\xa0\x00':
        print(f"warning: {image_path.name} does not start with OSA boot "
              f"signature - layout may differ", file=sys.stderr)

    entries = walk_directory(data, first_dir_lbn=6)
    if not entries:
        print(f"{image_path.name}: no RT-11 directory found at LBN 6",
              file=sys.stderr)
        return 1

    out_dir.mkdir(parents=True, exist_ok=True)
    written = []
    used = {}
    for e in entries:
        if not (e['status'] & E_PERM):
            continue
        if e['length'] == 0:
            continue
        start_byte = e['start'] * BLOCK
        end_byte = start_byte + e['length'] * BLOCK
        if end_byte > len(data):
            print(f"  skip {e['name']}: out of bounds (start={e['start']}, "
                  f"len={e['length']}, image={len(data)} B)")
            continue
        fname = safe_filename(e['name'])
        if fname in used:
            used[fname] += 1
            stem, dot, ext = fname.partition('.')
            fname = f"{stem}_{used[fname]}" + (f".{ext}" if dot else "")
        else:
            used[fname] = 0
        (out_dir / fname).write_bytes(data[start_byte:end_byte])
        written.append((e['name'], fname, e['start'], e['length']))

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

    print(f"{image_path.name}: extracted {len(written)} files to {out_dir}")
    return 0


def main() -> int:
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <file.dsk> [<out-dir>]", file=sys.stderr)
        return 1
    image = Path(sys.argv[1])
    if len(sys.argv) >= 3:
        out_dir = Path(sys.argv[2])
    else:
        out_dir = image.parent / (image.stem + "_extracted")
    return extract(image, out_dir)


if __name__ == "__main__":
    sys.exit(main())
