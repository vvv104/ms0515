#!/usr/bin/env python3
"""
make_pattern_disk.py - Take an RT-11 disk image, add a file PATT.DAT
whose every 512-byte block is filled with a unique marker

    f'BLK{block:05d}_' + b'\\xAA' * 502

then write pattern bytes at the canonical byte position for each of the
file's LBNs.  Used by the layout probe to discover OSA's true
LBN-to-physical mapping when the source disk and the destination disk
both go through the OSA disk driver.

Usage:
    python make_pattern_disk.py <input.dsk> <output.dsk> [blocks]
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

BLOCK = 512
SS_BLOCKS = 800
INTERLEAVE = [0, 2, 4, 6, 8, 1, 3, 5, 7, 9]

E_TENT = 0o000400
E_MPTY = 0o001000
E_PERM = 0o002000
E_EOS  = 0o004000


def byte_canonical(n: int) -> int:
    return ((n // 10 + 1) % 80) * 5120 + INTERLEAVE[n % 10] * BLOCK


# RAD50 alphabet (RT-11 packs 3 chars into 16 bits, base 40).
RAD50 = ' ABCDEFGHIJKLMNOPQRSTUVWXYZ$.?0123456789'


def encode_rad50(s: str) -> int:
    s = s.ljust(3)[:3]
    out = 0
    for c in s:
        out = out * 40 + RAD50.index(c.upper())
    return out


def make_filename_words(name: str) -> tuple[int, int, int]:
    """RT-11 stores filename as 3 RAD50 words: NAME[0:3], NAME[3:6], EXT."""
    base, _, ext = name.partition(".")
    base = base.ljust(6)[:6]
    ext  = ext.ljust(3)[:3]
    return (encode_rad50(base[:3]),
            encode_rad50(base[3:6]),
            encode_rad50(ext))


def make_pattern_block(block_num: int) -> bytes:
    """One 512-byte block whose first 16 bytes name the block."""
    marker = f"BLK{block_num:05d}_".encode("ascii")
    assert len(marker) == 9
    head = marker + b"\xAA" * 7   # 16-byte head, easy to spot
    body = head + (bytes([0x55, 0xAA]) * ((BLOCK - len(head)) // 2))
    body = body[:BLOCK]
    return body


def main() -> int:
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <input.dsk> <output.dsk> [blocks=70]")
        return 1
    src = Path(sys.argv[1])
    dst = Path(sys.argv[2])
    nblocks = int(sys.argv[3]) if len(sys.argv) > 3 else 70

    data = bytearray(src.read_bytes())
    if len(data) != SS_BLOCKS * BLOCK:
        print(f"input not 409600 bytes: {len(data)}")
        return 1

    # Read the first directory segment at LBN 6 (canonical byte 6656).
    seg_off = byte_canonical(6)
    seg_next = seg_off + BLOCK    # LBN 7 byte
    seg_byte_pair = data[seg_off:seg_off + BLOCK] \
                  + data[byte_canonical(7):byte_canonical(7) + BLOCK]
    seg_total, seg_next_w, seg_high, extra, data_block = \
        struct.unpack_from("<5H", seg_byte_pair, 0)
    print(f"segment header: segs_total={seg_total} next={seg_next_w} "
          f"high={seg_high} extra={extra} data_block={data_block}")
    entry_size = 14 + extra

    # Walk entries to find a long-enough EMPTY entry to host PATT.DAT.
    entries: list[dict] = []
    cur = data_block
    p = 10
    end = 1024
    target_idx = -1
    while p + entry_size <= end:
        status, fn1, fn2, ext, length, cj, date = \
            struct.unpack_from("<7H", seg_byte_pair, p)
        if status == 0:
            break
        ent = dict(p=p, status=status, fn1=fn1, fn2=fn2, ext=ext,
                   length=length, cj=cj, date=date, start=cur)
        entries.append(ent)
        cur += length
        if (status & E_MPTY) and length >= nblocks and target_idx < 0:
            target_idx = len(entries) - 1
        p += entry_size
        if status & E_EOS:
            break
    if target_idx < 0:
        print(f"no EMPTY entry with at least {nblocks} blocks free")
        return 1

    host = entries[target_idx]
    print(f"hosting PATT.DAT in empty slot at LBN {host['start']} "
          f"(was {host['length']} blocks free)")

    # Compose the new PATT.DAT entry, place it at host's slot, shift the rest
    # by entry_size and reduce the EMPTY entry's length.
    fn1, fn2, ext = make_filename_words("PATT.DAT")
    patt_start = host["start"]
    new_patt = struct.pack("<7H", E_PERM, fn1, fn2, ext, nblocks, 0, 0)
    new_empty = struct.pack(
        "<7H", host["status"], host["fn1"], host["fn2"], host["ext"],
        host["length"] - nblocks, host["cj"], host["date"])

    # Shift all entries from `target_idx + 1` onwards one slot to the right.
    new_seg = bytearray(seg_byte_pair)
    # Start writing at the target host entry offset.
    write_p = host["p"]
    new_seg[write_p:write_p + entry_size] = new_patt
    write_p += entry_size
    new_seg[write_p:write_p + entry_size] = new_empty
    write_p += entry_size
    for ent in entries[target_idx + 1:]:
        rec = struct.pack("<7H", ent["status"], ent["fn1"], ent["fn2"],
                          ent["ext"], ent["length"], ent["cj"], ent["date"])
        new_seg[write_p:write_p + entry_size] = rec
        write_p += entry_size
    # Trailing bytes of the segment must remain zero so the EOS still
    # terminates iteration cleanly.
    if write_p < end:
        new_seg[write_p:end] = b"\x00" * (end - write_p)

    # Write the modified directory back into the image (LBN 6 + LBN 7).
    data[seg_off:seg_off + BLOCK] = new_seg[:BLOCK]
    data[byte_canonical(7):byte_canonical(7) + BLOCK] = new_seg[BLOCK:]

    # Fill PATT.DAT's data blocks with unique markers.
    for i in range(nblocks):
        lbn = patt_start + i
        off = byte_canonical(lbn)
        data[off:off + BLOCK] = make_pattern_block(i)

    dst.write_bytes(bytes(data))
    print(f"wrote {dst}  ({len(data)} B), PATT.DAT @LBN {patt_start} "
          f"len {nblocks} ({nblocks * BLOCK} B)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
