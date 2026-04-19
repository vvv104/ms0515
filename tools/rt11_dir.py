#!/usr/bin/env python3
"""
rt11_dir.py — Inspect RT-11 / MS0515-OSA floppy disk images.

Detects the image's geometry (single vs double-sided), locates an RT-11
directory, and lists the files contained within.  Optionally extracts
files by name.

Usage:
    python rt11_dir.py <image>            # list files
    python rt11_dir.py <image> -x <name>  # extract file (raw blocks)
    python rt11_dir.py <image> --scan     # scan a directory of images

Image formats recognised:
    409600 bytes  — single side  (80 trk * 10 sec * 512 = MS0515 logical unit)
    819200 bytes  — double side  (80 trk * 2 side * 10 sec * 512)

The MS0515 BIOS treats each side as an independent logical unit, so a
double-sided image really contains two RT-11 volumes laid end to end.

Filesystem layout
-----------------
RT-11 stores files as contiguous runs of 512-byte logical blocks.  Each
volume has one or more directory segments; each segment is 1024 bytes
(two blocks) and starts with a 5-word header followed by a list of
fixed-size file entries.  Filenames are encoded in RAD50, three chars
per 16-bit word.

Standard RT-11 puts the directory at block 6 (the home block at block
1 nominally points there).  MS0515 OSA disks observed in the wild keep
the directory at block 13 (skipping the bootloader on track 1) and
have no usable home block — so the tool tries several candidate start
blocks and accepts the first one that decodes as a sensible segment.
"""

import argparse
import os
import struct
import sys

BLOCK = 512
MS0515_TRACKS = 80
MS0515_SECTORS = 10
MS0515_TRACK_SIZE = MS0515_SECTORS * BLOCK
MS0515_SIDE_SIZE = MS0515_TRACKS * MS0515_TRACK_SIZE       # 409600
MS0515_DOUBLE_SIZE = 2 * MS0515_SIDE_SIZE                  # 819200

# RT-11 directory entry status bits
E_TENT  = 0o000400
E_MPTY  = 0o001000
E_PERM  = 0o002000
E_EOS   = 0o004000
E_READ  = 0o000040  # protected against deletion
E_PROT  = 0o100000  # write-protected (high bit)

RAD50_CHARS = ' ABCDEFGHIJKLMNOPQRSTUVWXYZ$.?0123456789'


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
    """Return (variant, [side_offsets])."""
    if size == MS0515_SIDE_SIZE:
        return 'single-side', [0]
    if size == MS0515_DOUBLE_SIZE:
        return 'double-side', [0, MS0515_SIDE_SIZE]
    if size % MS0515_SIDE_SIZE == 0:
        n = size // MS0515_SIDE_SIZE
        return f'{n}x sides ({size} B)', [i * MS0515_SIDE_SIZE for i in range(n)]
    return f'unknown ({size} B)', [0]


def try_parse_segment(data, base, dir_block):
    """Decode a directory segment at the given block within `data[base:]`.
    Returns (header_dict, [entries]) on success, or None on obvious garbage.
    """
    off = base + dir_block * BLOCK
    if off + 10 > len(data):
        return None
    seg_total, seg_next, seg_high, extra, data_block = \
        struct.unpack_from('<5H', data, off)

    # Sanity checks — reject obvious garbage
    if seg_total == 0 or seg_total > 31:
        return None
    if seg_high == 0 or seg_high > seg_total:
        return None
    if seg_next > seg_total:
        return None
    if extra > 64 or extra & 1:
        return None
    if data_block == 0 or data_block * BLOCK > len(data) - base:
        return None

    entry_size = 14 + extra
    entries = []
    cur_block = data_block
    p = off + 10
    end = off + 1024  # one segment = 2 blocks

    while p + entry_size <= end:
        status, fn1, fn2, ext, length, chan_job, date = \
            struct.unpack_from('<7H', data, p)

        if status == 0:
            # Genuine zero entry — looks like a corrupted/blank slot
            return None

        flags = []
        if status & E_TENT: flags.append('TENT')
        if status & E_MPTY: flags.append('EMPTY')
        if status & E_PERM: flags.append('PERM')
        if status & E_EOS:  flags.append('EOS')
        if status & E_READ: flags.append('PROT')

        entries.append({
            'status':  status,
            'flags':   flags,
            'name':    decode_filename(fn1, fn2, ext),
            'block':   cur_block,
            'length':  length,
            'date':    date,
        })

        cur_block += length
        p += entry_size
        if status & E_EOS:
            break

    if not entries:
        return None

    # An accepted segment must contain at least one PERM entry
    if not any(e['status'] & E_PERM for e in entries):
        return None

    header = {
        'segs_total': seg_total,
        'next_seg':   seg_next,
        'high_seg':   seg_high,
        'extra':      extra,
        'data_block': data_block,
    }
    return header, entries


def find_directory(data, base):
    """Try several candidate directory start blocks.  Returns
    (start_block, header, entries) or None."""
    for blk in (6, 13, 8, 10, 12):
        result = try_parse_segment(data, base, blk)
        if result:
            return (blk,) + result
    return None


def list_volume(data, base, label):
    print(f'== {label} ==')
    found = find_directory(data, base)
    if not found:
        print('  no RT-11 directory found at any standard offset')
        return None
    start_blk, header, entries = found
    print(f'  directory at block {start_blk}, '
          f'segs={header["segs_total"]} '
          f'highest={header["high_seg"]} '
          f'extra={header["extra"]} '
          f'first-data-block={header["data_block"]:o}')

    perm_files = [e for e in entries if e['status'] & E_PERM]
    print(f'  {len(perm_files)} permanent file(s):')
    for e in entries:
        if e['status'] & E_PERM:
            print(f'    {e["name"]:<14} '
                  f'blk={e["block"]:5d}  '
                  f'len={e["length"]:5d} blocks  '
                  f'({e["length"]*BLOCK} B)')
        elif e['status'] & E_MPTY:
            print(f'    <empty>        blk={e["block"]:5d}  '
                  f'len={e["length"]:5d} blocks')

    names = {e['name'] for e in perm_files}
    bootable_signs = []
    if 'SWAP.SYS' in names:
        bootable_signs.append('SWAP.SYS')
    if 'RT11SJ.SYS' in names:
        bootable_signs.append('RT11SJ.SYS')
    if 'MON8SJ.SYS' in names:
        bootable_signs.append('MON8SJ.SYS')
    if 'DZ.SYS' in names:
        bootable_signs.append('DZ.SYS')

    if 'SWAP.SYS' in names and (
            'RT11SJ.SYS' in names or 'MON8SJ.SYS' in names):
        print(f'  >>> BOOTABLE (has {", ".join(bootable_signs)})')
    elif bootable_signs:
        print(f'  partial system files: {", ".join(bootable_signs)}')
    return entries


def extract_file(data, base, entries, name, out_path):
    for e in entries:
        if e['name'].upper() == name.upper() and (e['status'] & E_PERM):
            off = base + e['block'] * BLOCK
            blob = data[off:off + e['length'] * BLOCK]
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
    print(f'  size {len(data)} B -{variant}')

    last_entries = None
    last_base = 0
    for i, base in enumerate(side_offsets):
        label = f'side {i}' if len(side_offsets) > 1 else 'volume'
        entries = list_volume(data, base, label)
        if entries:
            last_entries = entries
            last_base = base

    if extract and last_entries is not None:
        extract_file(data, last_base, last_entries,
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
    ap = argparse.ArgumentParser(description=__doc__,
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
