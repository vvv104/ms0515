#!/usr/bin/env python3
"""
build_bootdisk.py — Assemble a bootable MS0515 disk image from recovered files.

Reads recovered files and metadata, reconstructs an RT-11 disk image with
proper 2:1 sector interleave. Can build from a recovery directory or from
an explicit file list.

Usage:
    python build_bootdisk.py <recovery_dir>/ output.img
    python build_bootdisk.py --manifest manifest.txt output.img
"""

import sys
import os
import struct
import argparse

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

# MS0515 floppy geometry
TRACKS = 80
SECTORS_PER_TRACK = 10
SECTOR = 512
DISK_SIZE = TRACKS * SECTORS_PER_TRACK * SECTOR  # 409600
TOTAL_BLOCKS = TRACKS * SECTORS_PER_TRACK  # 800 (track 0 used for blocks 790-799)

# 2:1 sector interleave
INTERLEAVE = [0, 2, 4, 6, 8, 1, 3, 5, 7, 9]

# RAD50 character set
RAD50 = ' ABCDEFGHIJKLMNOPQRSTUVWXYZ$.%0123456789'


def block_to_offset(block):
    """Convert logical block number to byte offset in disk image."""
    track = (block // 10 + 1) % 80  # wraps: blocks 790-799 → track 0
    logical = block % 10
    physical = INTERLEAVE[logical]
    sector = track * 10 + physical
    return sector * SECTOR


def write_block(image, block, data):
    """Write a 512-byte block to the disk image at the interleaved position."""
    off = block_to_offset(block)
    image[off:off + SECTOR] = data[:SECTOR]


def rad50_encode(s):
    """Encode a string (up to 3 chars) as a RAD50 word."""
    s = s.upper().ljust(3)[:3]
    val = 0
    for c in s:
        idx = RAD50.find(c)
        if idx < 0:
            idx = 0  # space for unknown chars
        val = val * 40 + idx
    return val


def filename_to_rad50(filename):
    """Convert 'NAME.EXT' to three RAD50 words (name1, name2, ext)."""
    if '.' in filename:
        name, ext = filename.rsplit('.', 1)
    else:
        name, ext = filename, ''
    name = name.upper().ljust(6)[:6]
    ext = ext.upper().ljust(3)[:3]
    return rad50_encode(name[:3]), rad50_encode(name[3:6]), rad50_encode(ext)


def build_directory(files, data_start):
    """Build RT-11 directory segment (2 blocks = 1024 bytes).

    files: list of (filename, length_blocks, status)
    data_start: first data block number
    """
    seg = bytearray(1024)

    # Directory segment header (5 words = 10 bytes)
    struct.pack_into('<H', seg, 0, 1)           # total segments
    struct.pack_into('<H', seg, 2, 0)           # next segment (0 = last)
    struct.pack_into('<H', seg, 4, 1)           # highest segment in use
    struct.pack_into('<H', seg, 6, 0)           # extra bytes per entry
    struct.pack_into('<H', seg, 8, data_start)  # data start block

    offset = 10
    entry_size = 14  # no extra bytes
    running = data_start

    for filename, length, status in files:
        if offset + entry_size > 1024 - 2:  # leave room for end marker
            print(f"WARNING: directory full, cannot add {filename}")
            break

        n1, n2, ext = filename_to_rad50(filename)
        struct.pack_into('<H', seg, offset, status)
        struct.pack_into('<H', seg, offset + 2, n1)
        struct.pack_into('<H', seg, offset + 4, n2)
        struct.pack_into('<H', seg, offset + 6, ext)
        struct.pack_into('<H', seg, offset + 8, length)
        # bytes 10-13: job/channel (0), date (0)
        struct.pack_into('<H', seg, offset + 10, 0)
        struct.pack_into('<H', seg, offset + 12, 0)

        running += length
        offset += entry_size

    # Remaining free space
    free = TOTAL_BLOCKS - running
    if free > 0:
        struct.pack_into('<H', seg, offset, 0x0200)  # empty entry
        struct.pack_into('<H', seg, offset + 2, 0)
        struct.pack_into('<H', seg, offset + 4, 0)
        struct.pack_into('<H', seg, offset + 6, 0)
        struct.pack_into('<H', seg, offset + 8, free)
        struct.pack_into('<H', seg, offset + 10, 0)
        struct.pack_into('<H', seg, offset + 12, 0)
        offset += entry_size

    # End-of-segment marker
    struct.pack_into('<H', seg, offset, 0x0800)

    return seg


def build_home_block(dir_start=6):
    """Build a minimal RT-11 home block."""
    home = bytearray(512)

    # The home block for MS0515 RT-11 has a specific layout.
    # Key fields at the end of the block:
    struct.pack_into('<H', home, 0x1D2, 1)          # number of dir segments
    struct.pack_into('<H', home, 0x1D4, dir_start)  # first directory block

    # System identifier (copy from recovered if available)
    struct.pack_into('<H', home, 0x1D6, 0x8E53)     # magic? same in ver A & B

    # Fill remaining ID fields with spaces
    for i in range(0x1D8, 0x1FC, 2):
        struct.pack_into('<H', home, i, 0x2020)

    return home


def load_manifest(path):
    """Load a manifest file listing files to include.

    Format: one file per line, optional fields separated by tab:
        FILENAME.EXT[<tab>STATUS]
    STATUS: PROT (default) or PERM
    Lines starting with # are comments.
    """
    files = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = line.split('\t')
            filename = parts[0].strip()
            status = 'PROT' if len(parts) < 2 else parts[1].strip().upper()
            files.append((filename, status))
    return files


def main():
    parser = argparse.ArgumentParser(
        description='Build bootable MS0515 disk image from recovered files')
    parser.add_argument('source', help='Source directory with recovered files')
    parser.add_argument('output', help='Output disk image path (.img)')
    parser.add_argument('--manifest', help='Manifest file (overrides auto-detect)')
    parser.add_argument('--boot', help='Boot block file (default: BOOT.BLK in source dir)')
    parser.add_argument('--home', help='Home block file (default: HOME.BLK in source dir)')
    parser.add_argument('--dir-blk', help='Use original directory blocks instead of rebuilding')
    parser.add_argument('--no-interleave', action='store_true',
                        help='Write without sector interleave (linear layout)')
    args = parser.parse_args()

    source_dir = args.source
    if not os.path.isdir(source_dir):
        print(f"Error: {source_dir} is not a directory")
        sys.exit(1)

    # Default file order for OS version A, with per-file status flags.
    # This matches the original disk2 directory byte-for-byte (verified
    # against vvv104/disk2_s0.img directory dump).
    #
    # STARTP.COM and K.COM use STATUS_PERM (0x0400); all other files use
    # STATUS_PROT (0x8400).  The previously-used DIR.SG8 and D.SAV names
    # were corruption artefacts in disk1's bit-rotted directory entries;
    # the genuine names are DIR.SAV (with no D.SAV file).
    STATUS_PROT = 0x8400
    STATUS_PERM = 0x0400
    DEFAULT_ORDER_WITH_STATUS = [
        ('SWAP.SYS',   STATUS_PROT),
        ('RT11SJ.SYS', STATUS_PROT),
        ('EX.SYS',     STATUS_PROT),
        ('DZ.SYS',     STATUS_PROT),
        ('TT.SYS',     STATUS_PROT),
        ('DV.SYS',     STATUS_PROT),
        ('LP.SYS',     STATUS_PROT),
        ('LD.SYS',     STATUS_PROT),
        ('SL.SYS',     STATUS_PROT),
        ('STARTS.COM', STATUS_PROT),
        ('STARTE.COM', STATUS_PROT),
        ('STARTP.COM', STATUS_PERM),
        ('K.COM',      STATUS_PERM),
        ('DATIME.SAV', STATUS_PROT),
        ('BLACK.SAV',  STATUS_PROT),
        ('DIR.SAV',    STATUS_PROT),
        ('DUP.SAV',    STATUS_PROT),
        ('PIP.SAV',    STATUS_PROT),
        ('VC.SAV',     STATUS_PROT),
        ('VC.HLP',     STATUS_PROT),
        ('DESS.SAV',   STATUS_PROT),
        ('KED.SAV',    STATUS_PROT),
        ('MACRO.SAV',  STATUS_PROT),
        ('LINK.SAV',   STATUS_PROT),
        ('PAS1.SAV',   STATUS_PROT),
        ('BASICO.SAV', STATUS_PROT),
        ('FORTRA.SAV', STATUS_PROT),
        ('ASC.SAV',    STATUS_PROT),
    ]
    DEFAULT_ORDER = [fn for fn, _ in DEFAULT_ORDER_WITH_STATUS]

    # Determine file list
    if args.manifest:
        manifest_files = load_manifest(args.manifest)
        file_list = []
        for fn, status in manifest_files:
            path = os.path.join(source_dir, fn)
            if not os.path.exists(path):
                print(f"WARNING: {fn} not found in {source_dir}, skipping")
                continue
            st = STATUS_PROT if status == 'PROT' else STATUS_PERM
            file_list.append((fn, path, st))
    else:
        # Auto-detect: use DEFAULT_ORDER_WITH_STATUS, then add any remaining
        # files.  Each entry in DEFAULT_ORDER_WITH_STATUS carries the exact
        # status flag used in the original distribution directory.
        file_list = []
        seen = set()
        for fn, status in DEFAULT_ORDER_WITH_STATUS:
            path = os.path.join(source_dir, fn)
            if os.path.exists(path):
                file_list.append((fn, path, status))
                seen.add(fn.upper())

        # Add any files not in default order
        for fn in sorted(os.listdir(source_dir)):
            if fn.upper() in seen:
                continue
            if fn.upper() in ('BOOT.BLK', 'HOME.BLK', 'DIR.BLK', 'SECBOOT.BLK', 'RECOVERY.MD'):
                continue
            if fn.lower().endswith('.img') or fn.lower().endswith('.md'):
                continue
            path = os.path.join(source_dir, fn)
            if os.path.isfile(path):
                file_list.append((fn, path, STATUS_PERM))
                seen.add(fn.upper())

    # Read file data and compute block sizes
    files_data = []
    for fn, path, status in file_list:
        with open(path, 'rb') as f:
            data = f.read()
        blocks = (len(data) + SECTOR - 1) // SECTOR
        # Pad to full blocks
        data = data.ljust(blocks * SECTOR, b'\x00')
        files_data.append((fn, data, blocks, status))

    # Data starts at block 8 (blocks 0-5 are system, 6-7 are directory)
    data_start = 8
    total_data_blocks = sum(b for _, _, b, _ in files_data)
    total_needed = data_start + total_data_blocks

    print(f"Building disk image: {args.output}")
    print(f"  Files: {len(files_data)}")
    print(f"  Data blocks: {total_data_blocks} / {TOTAL_BLOCKS} available")
    print(f"  Free blocks: {TOTAL_BLOCKS - total_needed}")
    print()

    if total_needed > TOTAL_BLOCKS:
        print(f"ERROR: Need {total_needed} blocks but only {TOTAL_BLOCKS} available!")
        sys.exit(1)

    # Create blank disk image
    image = bytearray(DISK_SIZE)

    # Write boot block (block 0)
    boot_path = args.boot or os.path.join(source_dir, 'BOOT.BLK')
    if os.path.exists(boot_path):
        with open(boot_path, 'rb') as f:
            boot_data = f.read()[:SECTOR]
        write_block(image, 0, boot_data.ljust(SECTOR, b'\x00'))
        print(f"  Boot block: {boot_path} ({sum(1 for b in boot_data if b)} non-zero bytes)")
    else:
        print("  WARNING: No boot block — disk will not be bootable")

    # Write home block (block 1)
    home_path = args.home or os.path.join(source_dir, 'HOME.BLK')
    if os.path.exists(home_path):
        with open(home_path, 'rb') as f:
            home_data = f.read()[:SECTOR]
        write_block(image, 1, home_data.ljust(SECTOR, b'\x00'))
        print(f"  Home block: {home_path}")
    else:
        home_data = build_home_block()
        write_block(image, 1, home_data)
        print("  Home block: generated")

    # Write secondary boot (blocks 2-5).  Block 0's primary boot loads
    # this stub by issuing READ_SECTOR at track=1 sector=5 (= block 2
    # after our interleave) and reading four sectors of monitor loader.
    secboot_path = os.path.join(source_dir, 'SECBOOT.BLK')
    if os.path.exists(secboot_path):
        with open(secboot_path, 'rb') as f:
            secboot = f.read()
        for i in range(4):
            blk = secboot[i * SECTOR:(i + 1) * SECTOR].ljust(SECTOR, b'\x00')
            write_block(image, 2 + i, blk)
        nz = sum(1 for b in secboot if b)
        print(f"  SecBoot:    {secboot_path} (blocks 2-5, {nz} non-zero bytes)")
    else:
        print("  WARNING: no SECBOOT.BLK — monitor loader stub missing, "
              "primary boot will hang reading zeros from blocks 2-5")

    # Write directory (blocks 6-7)
    if args.dir_blk:
        with open(args.dir_blk, 'rb') as f:
            dir_data = f.read()
        dir_data = dir_data.ljust(1024, b'\x00')
        write_block(image, 6, dir_data[:SECTOR])
        write_block(image, 7, dir_data[SECTOR:SECTOR * 2])
        print(f"  Directory: {args.dir_blk} (original)")
    else:
        dir_entries = [(fn, blocks, status) for fn, _, blocks, status in files_data]
        dir_data = build_directory(dir_entries, data_start)
        write_block(image, 6, dir_data[:SECTOR])
        write_block(image, 7, dir_data[SECTOR:SECTOR * 2])
        print(f"  Directory: rebuilt ({len(dir_entries)} entries)")

    # Write file data
    running = data_start
    print()
    for fn, data, blocks, status in files_data:
        for i in range(blocks):
            block = running + i
            blk_data = data[i * SECTOR:(i + 1) * SECTOR]
            write_block(image, block, blk_data)
        st = 'PROT' if status == STATUS_PROT else 'PERM'
        nz_blocks = sum(1 for i in range(blocks)
                        if any(b != 0 for b in data[i*SECTOR:(i+1)*SECTOR]))
        print(f"  {fn:<14s}  blk {running:3d}-{running+blocks-1:3d}  "
              f"({blocks:3d} blocks, {nz_blocks} non-zero)  [{st}]")
        running += blocks

    # Write output
    with open(args.output, 'wb') as f:
        f.write(image)

    print(f"\nDisk image written: {args.output} ({len(image)} bytes)")
    print(f"Total: {running - data_start} data blocks + {data_start} system blocks = {running} used / {TOTAL_BLOCKS} available")


if __name__ == '__main__':
    main()
