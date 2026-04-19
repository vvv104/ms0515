#!/usr/bin/env python3
"""
split_double_sided.py — Split double-sided raw floppy images into
single-sided images understood by the MS0515 emulator.

Input:  819200-byte raw image (80 tracks × 2 sides × 10 sectors × 512 bytes)
        with track-interleaved layout: T0S0, T0S1, T1S0, T1S1, ...

Output: Two 409600-byte single-sided images:
    _s0.img  = DZ0 / DZ1 (lower side, bootable)  ← raw side 0 (first block)
    _s1.img  = DZ2 / DZ3 (upper side)            ← raw side 1 (second block)

Usage:
    python split_double_sided.py disk1.raw
    python split_double_sided.py disk1.raw disk2.raw disk3.raw ...

Mount with:
    ms0515 --fd0 disk1_s0.img --fd2 disk1_s1.img
"""

import sys
import os

TRACKS = 80
SECTORS = 10
SECTOR_SIZE = 512
TRACK_SIZE = SECTORS * SECTOR_SIZE          # 5120
DOUBLE_SIDED_SIZE = TRACKS * 2 * TRACK_SIZE # 819200
SINGLE_SIDED_SIZE = TRACKS * TRACK_SIZE     # 409600


def split(path):
    size = os.path.getsize(path)
    if size != DOUBLE_SIDED_SIZE:
        print(f"SKIP {path}: expected {DOUBLE_SIDED_SIZE} bytes, got {size}")
        return

    with open(path, "rb") as f:
        data = f.read()

    side0 = bytearray()
    side1 = bytearray()
    for t in range(TRACKS):
        offset = t * 2 * TRACK_SIZE
        side0.extend(data[offset : offset + TRACK_SIZE])
        side1.extend(data[offset + TRACK_SIZE : offset + 2 * TRACK_SIZE])

    base = os.path.splitext(path)[0]
    s0_path = base + "_s0.img"
    s1_path = base + "_s1.img"

    with open(s0_path, "wb") as f:
        f.write(side0)
    with open(s1_path, "wb") as f:
        f.write(side1)

    print(f"{os.path.basename(path)} -> {os.path.basename(s0_path)} (DZ0) + {os.path.basename(s1_path)} (DZ2)")


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <file.raw> [file2.raw ...]")
        sys.exit(1)

    for path in sys.argv[1:]:
        split(path)


if __name__ == "__main__":
    main()
