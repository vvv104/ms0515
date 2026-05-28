#!/usr/bin/env python3
"""
td0_decode.py - Decode Sydex Teledisk (.TD0) images into raw physical-sector
                images plus a bad-sector map.

Supports only the un-LZSS-compressed variant (signature 'TD').  Both ARCSAV.TD0
and LANG.TD0 in our dump set are of this kind.  Sector data encoding methods
0, 1, 2 are implemented; bad-sector flags propagate into the .badmap output.

CLI:
    python td0_decode.py <file.TD0> [<file2.TD0> ...]

For each input writes alongside it:
    <stem>_td0.dsk    - track-interleaved DS or single-sided SS image
    <stem>_td0.badmap - 80*sides*sectors_per_track bytes: 0=good, 1=bad

The size-code shortcut assumes 512-byte sectors (size_code = 2); other sizes
emit a warning and the affected sector is marked bad.

Bad-sector heuristic: TD0 sector "syntax flags" bits 0x02 (CRC), 0x10 (data
mark "deleted"), 0x20 (no data block), 0x40 (no ID).
"""

from __future__ import annotations

import os
import sys
from dataclasses import dataclass
from pathlib import Path

SECTOR_SIZE = 512
SECTORS_PER_TRACK = 10
NUM_TRACKS = 80

BAD_FLAG_MASK = 0x02 | 0x20 | 0x40   # CRC error / no data / no ID


@dataclass
class Sector:
    cyl: int
    head: int
    sec_id: int            # 1-based
    size_code: int         # 0=128 .. 6=8192
    flags: int             # raw flags byte
    data: bytes            # sector payload (zero-filled if bad)
    bad: bool


@dataclass
class Td0Image:
    version: int
    sides: int
    comment: str
    sectors: list[Sector]


def _read_sector_data(buf: bytes, off: int, data_len: int,
                      payload_bytes: int) -> tuple[bytes, int]:
    """Return (decoded_payload, bytes_consumed_from_buf).
    data_len is the length field from the sector header and includes the
    encoding method byte at offset 0 inside the encoded block."""
    method = buf[off]
    body = buf[off + 1:off + data_len]    # data_len bytes total, 1 = method
    consumed = data_len

    if method == 0:
        # Raw data.
        out = body[:payload_bytes]
        if len(out) < payload_bytes:
            out = out + b"\x00" * (payload_bytes - len(out))
        return out, consumed

    if method == 1:
        # 2-byte repeated pair, body = count_lo, count_hi, pat0, pat1.
        if len(body) < 4:
            return b"\x00" * payload_bytes, consumed
        count = body[0] | (body[1] << 8)
        pat = bytes([body[2], body[3]])
        out = (pat * count)[:payload_bytes]
        if len(out) < payload_bytes:
            out = out + b"\x00" * (payload_bytes - len(out))
        return out, consumed

    if method == 2:
        # Subblock-encoded.
        out = bytearray()
        i = 0
        while i < len(body) and len(out) < payload_bytes:
            block_type = body[i]
            i += 1
            if block_type == 0:
                # 0, length, data...
                length = body[i]
                i += 1
                out += body[i:i + length]
                i += length
            elif block_type == 1:
                # 1, count_lo, count_hi, pat0, pat1
                if i + 4 > len(body):
                    break
                count = body[i] | (body[i + 1] << 8)
                pat = bytes([body[i + 2], body[i + 3]])
                i += 4
                out += pat * count
            else:
                # Unknown subblock type - bail out, mark sector zero.
                return b"\x00" * payload_bytes, consumed
        out = bytes(out[:payload_bytes])
        if len(out) < payload_bytes:
            out = out + b"\x00" * (payload_bytes - len(out))
        return out, consumed

    # Unknown method.
    return b"\x00" * payload_bytes, consumed


def decode(path: Path) -> Td0Image:
    with open(path, "rb") as f:
        buf = f.read()

    sig = buf[:2]
    if sig not in (b"TD", b"td"):
        raise ValueError(f"{path.name}: not a TD0 file (sig {sig!r})")
    if sig == b"td":
        raise NotImplementedError(
            f"{path.name}: LZSS-compressed TD0 not supported in this decoder")

    version = buf[4]
    stepping = buf[7]
    sides = buf[9]

    off = 12
    comment = ""
    if stepping & 0x80:
        # Comment block: crc(2), len(2), date(6), then len bytes of text.
        clen = buf[off + 2] | (buf[off + 3] << 8)
        comment_bytes = buf[off + 10:off + 10 + clen]
        comment = comment_bytes.replace(b"\x00", b"\n").decode(
            "cp866", errors="replace")
        off += 10 + clen

    sectors: list[Sector] = []
    while off < len(buf):
        nsec = buf[off]
        if nsec == 0xFF:
            # End-of-file marker.
            break
        cyl_h = buf[off + 1]
        head_h = buf[off + 2]
        # crc_h = buf[off + 3]
        off += 4
        for _ in range(nsec):
            cyl = buf[off]
            head = buf[off + 1]
            sec_id = buf[off + 2]
            size_code = buf[off + 3]
            flags = buf[off + 4]
            # crc = buf[off + 5]
            data_len = buf[off + 6] | (buf[off + 7] << 8)
            off += 8

            payload_bytes = 128 << size_code if size_code <= 6 else SECTOR_SIZE
            bad = bool(flags & BAD_FLAG_MASK)

            if data_len == 0:
                # Header without data (bad sector).
                payload = b"\x00" * payload_bytes
                bad = True
            else:
                payload, consumed = _read_sector_data(
                    buf, off, data_len, payload_bytes)
                off += consumed

            if size_code != 2:
                print(f"  warn: sector cyl={cyl} head={head} id={sec_id} "
                      f"size_code={size_code} not 512 bytes - marking bad")
                bad = True
                payload = (payload + b"\x00" * SECTOR_SIZE)[:SECTOR_SIZE]

            sectors.append(Sector(cyl_h, head_h, sec_id, size_code,
                                  flags, payload, bad))

    return Td0Image(version=version, sides=sides, comment=comment,
                    sectors=sectors)


def assemble(img: Td0Image, *, force_sides: int | None = None
             ) -> tuple[bytes, bytes, int]:
    """Lay sectors into a track-interleaved physical image.

    Returns (image_bytes, badmap_bytes, sides).  sides=1 -> 409600,
    sides=2 -> 819200.  badmap is 80 * sides * 10 bytes (0 good, 1 bad).
    """
    sides = force_sides if force_sides is not None else img.sides
    image = bytearray(NUM_TRACKS * sides * SECTORS_PER_TRACK * SECTOR_SIZE)
    badmap = bytearray(NUM_TRACKS * sides * SECTORS_PER_TRACK)

    # Pre-fill badmap with 1 (assume missing -> bad), then clear when written.
    for i in range(len(badmap)):
        badmap[i] = 1

    for s in img.sectors:
        if s.cyl >= NUM_TRACKS or s.head >= sides:
            continue
        if s.sec_id < 1 or s.sec_id > SECTORS_PER_TRACK:
            continue
        track_index = s.cyl * sides + s.head
        sec_index = s.sec_id - 1
        off = track_index * SECTORS_PER_TRACK * SECTOR_SIZE + sec_index * SECTOR_SIZE
        image[off:off + SECTOR_SIZE] = s.data
        badmap[track_index * SECTORS_PER_TRACK + sec_index] = 1 if s.bad else 0

    return bytes(image), bytes(badmap), sides


def main() -> int:
    args = sys.argv[1:]
    out_dir: Path | None = None
    if "--out-dir" in args:
        i = args.index("--out-dir")
        out_dir = Path(args[i + 1])
        del args[i:i + 2]
    if not args:
        print(f"Usage: {sys.argv[0]} [--out-dir DIR] <file.TD0> [...]")
        return 1
    for arg in args:
        path = Path(arg)
        print(f"\n=== {path.name} ===")
        img = decode(path)
        print(f"  version={img.version/10:.1f}  sides={img.sides}")
        if img.comment:
            preview = img.comment.strip().splitlines()
            print("  comment:")
            for line in preview[:6]:
                print(f"    {line}")
            if len(preview) > 6:
                print(f"    ... ({len(preview)} lines total)")
        image, badmap, sides = assemble(img)
        bad_count = sum(badmap)
        print(f"  decoded {len(img.sectors)} sectors, "
              f"{bad_count} flagged bad")
        target_dir = out_dir if out_dir else path.parent
        target_dir.mkdir(parents=True, exist_ok=True)
        out_image = target_dir / (path.stem + "_td0.dsk")
        out_badmap = target_dir / (path.stem + "_td0.badmap")
        out_image.write_bytes(image)
        out_badmap.write_bytes(badmap)
        print(f"  wrote {out_image} ({len(image)} bytes, "
              f"{'DS' if sides == 2 else 'SS'})")
        print(f"  wrote {out_badmap} ({len(badmap)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
