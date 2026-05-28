#!/usr/bin/env python3
"""
rt11_dir.py - Inspect RT-11 / MS-0515 floppy disk images.

Detects image geometry (single- or double-sided), identifies which OS
wrote the disk by matching its boot block against a small reference
table, picks the right LBN-to-byte mapping for that OS, lists files,
and optionally extracts one to a local file.

Usage:
    python rt11_dir.py <image>              # list files
    python rt11_dir.py <image> -x <name>    # extract file (raw blocks)
    python rt11_dir.py <image> --scan       # scan a directory of images

Image formats recognised:
    409600 bytes  - single-sided, raw physical layout
    819200 bytes  - double-sided, track-interleaved physical layout
                    (treated as two SS volumes back to back; the
                    DS-spanning filesystem variant is not handled
                    here - it needs a per-disk recombine)

LBN-to-byte mapping
-------------------
RT-11 directory parsing on a real MS-0515 disk needs the right
physical mapping; the same logical block lands at different bytes
depending on which OS wrote the disk:

  * `ss-canonical`      2:1 sector interleave, cyl-0-last
                        (rodionov; metadata of every OS)
  * `ss-osa-skew`       2:1 interleave + +2-sectors-per-track skew
                        (OSA / Omega / Mihin file data)
  * `ss-cyl0last-noil`  cyl-0-last, no sector interleave
                        (some rare driver builds)
  * `ss-cyl0first-noil` cyl-0-first (no rotation), no interleave
  * `ss-lbn-linear`     plain (block N at byte N*512)

The right mapping is chosen by matching the boot block (bytes
5120..5631) against the reference disks bundled with the emulator
(src/lib/tests/disks/ + src/assets/disks/).  Unknown boots fall
back to ss-canonical and the tool prints a warning - the listing
usually still works (metadata is at canonical positions on every
known MS-0515 variant) but extracted file content may be wrong.

See `docs/hardware/filesystem.md` for the detailed formulas.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import struct
import sys
from pathlib import Path

BLOCK = 512
SECTORS_PER_TRACK = 10
TRACKS = 80
TRACK_SIZE = SECTORS_PER_TRACK * BLOCK         # 5120
MS0515_SIDE_SIZE = TRACKS * TRACK_SIZE         # 409600
MS0515_DOUBLE_SIZE = 2 * MS0515_SIDE_SIZE      # 819200

INTERLEAVE = [0, 2, 4, 6, 8, 1, 3, 5, 7, 9]

E_TENT = 0o000400
E_MPTY = 0o001000
E_PERM = 0o002000
E_EOS  = 0o004000
E_READ = 0o000040
E_PROT = 0o100000

RAD50_CHARS = ' ABCDEFGHIJKLMNOPQRSTUVWXYZ$.?0123456789'

# Standard candidate LBNs to try for the first directory segment.
DIR_CANDIDATE_LBNS = (6, 13, 8, 10, 12)


# ----- LBN -> byte mappings (single-sided) ---------------------------

def ss_byte_canonical(n: int) -> int:
    return ((n // 10 + 1) % 80) * TRACK_SIZE + INTERLEAVE[n % 10] * BLOCK

def ss_byte_cyl0last_noil(n: int) -> int:
    return ((n // 10 + 1) % 80) * TRACK_SIZE + (n % 10) * BLOCK

def ss_byte_cyl0first_noil(n: int) -> int:
    return (n // 10) * TRACK_SIZE + (n % 10) * BLOCK

def ss_byte_linear(n: int) -> int:
    return n * BLOCK

def ss_byte_osa_skew(n: int) -> int:
    """OSA / Omega / Mihin file-area layout: canonical 2:1 interleave
    plus a +2-sectors-per-track rotation skew.  Track 1 has skew 0,
    so boot/home/dir at LBNs 0..6 land at canonical byte positions and
    parse identically to ss-canonical."""
    track = (n // 10 + 1) % 80
    sec = (INTERLEAVE[n % 10] + 2 * track - 2) % 10
    return track * TRACK_SIZE + sec * BLOCK


SS_MAPPINGS = {
    "ss-canonical":       ss_byte_canonical,
    "ss-cyl0last-noil":   ss_byte_cyl0last_noil,
    "ss-cyl0first-noil":  ss_byte_cyl0first_noil,
    "ss-lbn-linear":      ss_byte_linear,
    "ss-osa-skew":        ss_byte_osa_skew,
}


# ----- reference boot blocks: identify which OS wrote the disk -------

# Each reference disk pairs a 512-byte boot block with the layout that
# OS uses for directory parsing and for file data.  The two diverge
# for OSA / Omega / Mihin (canonical metadata + skewed file area).
# Side 1 of double-sided reference disks is registered under its own
# boot hash so DS images mounted as two SS volumes also identify.
#
# Paths are resolved relative to this script's repo root.
_REPO_ROOT = Path(__file__).resolve().parents[1]
_TESTS_DISKS = _REPO_ROOT / "src" / "lib" / "tests" / "disks"
_ASSETS_DISKS = _REPO_ROOT / "src" / "assets" / "disks"

_REFERENCE_DISKS = [
    # (path, system, dir_layout, file_layout, is_ds)
    (_TESTS_DISKS  / "test_osa.dsk",     "OSA",      "ss-canonical", "ss-osa-skew",  False),
    (_TESTS_DISKS  / "test_omega.dsk",   "Omega",    "ss-canonical", "ss-osa-skew",  False),
    (_TESTS_DISKS  / "test_mihin.dsk",   "Mihin",    "ss-canonical", "ss-osa-skew",  False),
    (_TESTS_DISKS  / "test_rod.dsk",     "rodionov", "ss-canonical", "ss-canonical", True),
    (_ASSETS_DISKS / "osa.dsk",          "OSA",      "ss-canonical", "ss-osa-skew",  False),
    (_ASSETS_DISKS / "omega-lang.dsk",   "Omega",    "ss-canonical", "ss-osa-skew",  False),
    (_ASSETS_DISKS / "omega-games.dsk",  "Omega",    "ss-canonical", "ss-osa-skew",  False),
    (_ASSETS_DISKS / "mihin.dsk",        "Mihin",    "ss-canonical", "ss-osa-skew",  False),
    (_ASSETS_DISKS / "rodionov.dsk",     "rodionov", "ss-canonical", "ss-canonical", True),
]


def _boot_hash(boot_block: bytes) -> str:
    return hashlib.sha256(boot_block).hexdigest()[:12]


def _build_reference_table():
    """Return {boot_hash: (system, dir_layout, file_layout)} mapping."""
    table = {}
    for path, system, dir_layout, file_layout, is_ds in _REFERENCE_DISKS:
        if not path.exists():
            continue
        data = path.read_bytes()
        if len(data) >= 5120 + BLOCK:
            table.setdefault(_boot_hash(data[5120:5632]),
                             (system, dir_layout, file_layout))
        if is_ds and len(data) >= MS0515_DOUBLE_SIZE:
            table.setdefault(_boot_hash(data[10240:10240 + BLOCK]),
                             (system + "-side1", dir_layout, file_layout))
    return table


REFERENCE_BOOT_TABLE = _build_reference_table()


# ----- RAD50 / directory parsing -------------------------------------

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


def classify_image(size):
    if size == MS0515_SIDE_SIZE:
        return 'single-side', [0]
    if size == MS0515_DOUBLE_SIZE:
        return 'double-side', [0, MS0515_SIDE_SIZE]
    if size % MS0515_SIDE_SIZE == 0:
        n = size // MS0515_SIDE_SIZE
        return f'{n}x sides ({size} B)', [i * MS0515_SIDE_SIZE for i in range(n)]
    return f'unknown ({size} B)', [0]


def identify_mapping(data, base):
    """Return (system, dir_layout, file_layout) by matching the boot
    block at byte 5120 of this side; defaults to canonical/canonical
    on unknown boot blocks."""
    boot_off = base + 5120
    if boot_off + BLOCK > len(data):
        return '-', 'ss-canonical', 'ss-canonical'
    h = _boot_hash(data[boot_off:boot_off + BLOCK])
    return REFERENCE_BOOT_TABLE.get(h, ('-', 'ss-canonical', 'ss-canonical'))


def read_block_via(data, base, mapping, lbn):
    """Read one 512-byte block at the given LBN through `mapping`."""
    off = base + mapping(lbn)
    if off + BLOCK > len(data):
        return b'\x00' * BLOCK
    return data[off:off + BLOCK]


def read_segment(data, base, mapping, dir_lbn):
    """A segment is two consecutive LBNs (1024 bytes)."""
    return (read_block_via(data, base, mapping, dir_lbn) +
            read_block_via(data, base, mapping, dir_lbn + 1))


def try_parse_segment(data, base, mapping, dir_lbn):
    """Decode a 1024-byte directory segment whose LBN range is
    `dir_lbn .. dir_lbn+1`, mapped via `mapping`."""
    seg = read_segment(data, base, mapping, dir_lbn)
    if len(seg) < 10:
        return None
    seg_total, seg_next, seg_high, extra, data_block = \
        struct.unpack_from('<5H', seg, 0)

    if seg_total == 0 or seg_total > 31:
        return None
    if seg_high == 0 or seg_high > seg_total:
        return None
    if seg_next > seg_total:
        return None
    if extra > 64 or extra & 1:
        return None
    if not (1 <= data_block <= MS0515_SIDE_SIZE // BLOCK):
        return None

    entry_size = 14 + extra
    entries = []
    cur_block = data_block
    p = 10
    while p + entry_size <= 1024:
        status, fn1, fn2, ext, length, _cj, date = \
            struct.unpack_from('<7H', seg, p)
        if status == 0:
            return None

        flags = []
        if status & E_TENT: flags.append('TENT')
        if status & E_MPTY: flags.append('EMPTY')
        if status & E_PERM: flags.append('PERM')
        if status & E_EOS:  flags.append('EOS')
        if status & E_READ: flags.append('PROT')

        entries.append({
            'status': status,
            'flags':  flags,
            'name':   decode_filename(fn1, fn2, ext),
            'block':  cur_block,
            'length': length,
            'date':   date,
        })
        cur_block += length
        p += entry_size
        if status & E_EOS:
            break

    if not entries or not any(e['status'] & E_PERM for e in entries):
        return None

    return ({'segs_total': seg_total, 'next_seg': seg_next,
             'high_seg': seg_high, 'extra': extra,
             'data_block': data_block}, entries)


def find_directory(data, base, mapping):
    for lbn in DIR_CANDIDATE_LBNS:
        result = try_parse_segment(data, base, mapping, lbn)
        if result:
            return (lbn, *result)
    return None


def list_volume(data, base, label):
    system, dir_layout, file_layout = identify_mapping(data, base)
    dir_mapping = SS_MAPPINGS[dir_layout]
    file_mapping = SS_MAPPINGS[file_layout]

    print(f'== {label} ==')
    print(f'  system={system}, dir-layout={dir_layout}, '
          f'file-layout={file_layout}')

    found = find_directory(data, base, dir_mapping)
    if not found:
        print('  no RT-11 directory found at any candidate LBN')
        return None, None, None
    start_lbn, header, entries = found
    print(f'  directory at LBN {start_lbn}, '
          f'segs={header["segs_total"]} '
          f'highest={header["high_seg"]} '
          f'extra={header["extra"]} '
          f'first-data-block={header["data_block"]}')

    perm_files = [e for e in entries if e['status'] & E_PERM]
    print(f'  {len(perm_files)} permanent file(s):')
    for e in entries:
        if e['status'] & E_PERM:
            print(f'    {e["name"]:<14} '
                  f'blk={e["block"]:5d}  '
                  f'len={e["length"]:5d} blocks  '
                  f'({e["length"] * BLOCK} B)')
        elif e['status'] & E_MPTY:
            print(f'    <empty>        blk={e["block"]:5d}  '
                  f'len={e["length"]:5d} blocks')

    names = {e['name'] for e in perm_files}
    signs = [s for s in ('SWAP.SYS', 'RT11SJ.SYS', 'MON8SJ.SYS', 'DZ.SYS')
             if s in names]
    if 'SWAP.SYS' in names and (
            'RT11SJ.SYS' in names or 'MON8SJ.SYS' in names):
        print(f'  >>> BOOTABLE (has {", ".join(signs)})')
    elif signs:
        print(f'  partial system files: {", ".join(signs)}')
    return entries, file_mapping, base


def extract_file(data, base, mapping, entries, name, out_path):
    for e in entries:
        if e['name'].upper() == name.upper() and (e['status'] & E_PERM):
            chunks = []
            for i in range(e['length']):
                chunks.append(read_block_via(data, base, mapping,
                                              e['block'] + i))
            blob = b''.join(chunks)
            with open(out_path, 'wb') as f:
                f.write(blob)
            print(f'wrote {out_path} ({len(blob)} B)')
            return True
    print(f'file {name!r} not found in directory')
    return False


def inspect(path, extract=None):
    with open(path, 'rb') as f:
        data = f.read()
    variant, side_offsets = classify_image(len(data))
    print(f'{path}')
    print(f'  size {len(data)} B - {variant}')

    last_entries = None
    last_mapping = None
    last_base = 0
    for i, base in enumerate(side_offsets):
        label = f'side {i}' if len(side_offsets) > 1 else 'volume'
        entries, mapping, b = list_volume(data, base, label)
        if entries:
            last_entries = entries
            last_mapping = mapping
            last_base = b

    if extract and last_entries is not None and last_mapping is not None:
        extract_file(data, last_base, last_mapping, last_entries,
                     extract, os.path.basename(extract))


def scan_dir(path):
    print(f'Scanning {path}\n')
    for fn in sorted(os.listdir(path)):
        full = os.path.join(path, fn)
        if not os.path.isfile(full):
            continue
        try:
            inspect(full)
        except Exception as exc:
            print(f'{full}: ERROR {exc}')
        print()


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('image', help='disk image file or directory (with --scan)')
    ap.add_argument('-x', '--extract', metavar='NAME',
                    help='extract a single file by name (e.g. SWAP.SYS)')
    ap.add_argument('--scan', action='store_true',
                    help='treat IMAGE as a directory and inspect every file')
    args = ap.parse_args()

    if args.scan:
        scan_dir(args.image)
    else:
        inspect(args.image, extract=args.extract)


if __name__ == '__main__':
    main()
