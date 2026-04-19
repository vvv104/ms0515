#!/usr/bin/env python3
"""
convert_extended_dsk.py — Convert Extended CPC DSK images to raw format.

The Extended CPC DSK format (used by SAMdisk and Mikhin's ms0515btl
emulator) wraps raw sector data in a structured container with per-track
headers and per-sector metadata.

This tool extracts the raw sector data and writes single-sided .img
files in the same physical-order format used by the MS0515 emulator.

Usage:
    python convert_extended_dsk.py disk5-final.dsk
    python convert_extended_dsk.py *.dsk
"""

import sys
import os
import struct

# MS0515 floppy geometry
TRACKS = 80
SECTORS_PER_TRACK = 10
SECTOR_SIZE = 512
SIDE_SIZE = TRACKS * SECTORS_PER_TRACK * SECTOR_SIZE  # 409600


def parse_extended_dsk(path):
    """Parse an Extended CPC DSK file and return per-side raw images."""
    with open(path, 'rb') as f:
        data = f.read()

    # Verify signature
    sig = data[:22].decode('ascii', errors='replace')
    if not sig.startswith('EXTENDED CPC DSK File'):
        print(f"  Not an Extended CPC DSK file: {sig!r}")
        return None

    creator = data[34:48].decode('ascii', errors='replace').rstrip('\x00')
    num_tracks = data[48]
    num_sides = data[49]

    print(f"  Creator: {creator}")
    print(f"  Geometry: {num_tracks} tracks, {num_sides} sides")

    # Track size table: one byte per track-side, value * 256 = track block size
    track_sizes = data[52:52 + num_tracks * num_sides]

    # Initialize output arrays (one per side)
    sides = [bytearray(SIDE_SIZE) for _ in range(num_sides)]

    # Parse each track
    offset = 256  # skip main header
    for ts_idx in range(num_tracks * num_sides):
        track_block_size = track_sizes[ts_idx] * 256
        if track_block_size == 0:
            continue  # unformatted track

        if offset + track_block_size > len(data):
            print(f"  WARNING: truncated at track-side index {ts_idx}")
            break

        # Track info header (256 bytes)
        track_info = data[offset:offset + 256]
        track_sig = track_info[:10].decode('ascii', errors='replace')
        if not track_sig.startswith('Track-Info'):
            print(f"  WARNING: bad track header at offset 0x{offset:x}")
            offset += track_block_size
            continue

        track_num = track_info[16]
        side_num = track_info[17]
        num_sectors = track_info[21]

        if side_num >= num_sides or track_num >= TRACKS:
            offset += track_block_size
            continue

        # Read sector data
        sector_data_offset = offset + 256
        for s in range(min(num_sectors, SECTORS_PER_TRACK)):
            si_off = 24 + s * 8
            sector_id = track_info[si_off + 2]   # 1-based sector ID
            actual_size = struct.unpack_from('<H', track_info, si_off + 6)[0]
            if actual_size == 0:
                actual_size = SECTOR_SIZE

            if sector_id < 1 or sector_id > SECTORS_PER_TRACK:
                sector_data_offset += actual_size
                continue

            # Sector ID 1-10 → index 0-9
            sector_idx = sector_id - 1

            # Read sector data
            src = data[sector_data_offset:sector_data_offset + min(actual_size, SECTOR_SIZE)]
            dst_off = track_num * SECTORS_PER_TRACK * SECTOR_SIZE + sector_idx * SECTOR_SIZE

            if dst_off + SECTOR_SIZE <= SIDE_SIZE:
                sides[side_num][dst_off:dst_off + len(src)] = src

            sector_data_offset += actual_size

        offset += track_block_size

    return sides, num_sides


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <file.dsk> [file2.dsk ...]")
        sys.exit(1)

    for path in sys.argv[1:]:
        print(f"\n{'='*60}")
        print(f"Converting: {os.path.basename(path)}")
        print(f"  Size: {os.path.getsize(path)} bytes")

        result = parse_extended_dsk(path)
        if result is None:
            continue

        sides, num_sides = result
        base = os.path.splitext(path)[0]

        for side in range(num_sides):
            suffix = f"_s{side}.img"
            out_path = base + suffix
            with open(out_path, 'wb') as f:
                f.write(sides[side])
            print(f"  Wrote: {os.path.basename(out_path)} ({len(sides[side])} bytes)")


if __name__ == '__main__':
    main()
