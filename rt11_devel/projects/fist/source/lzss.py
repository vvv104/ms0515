"""LZSS as the loader decodes it - and the reference decoder to check it.

The format is the classic one, chosen because the decoder is a couple of
dozen PDP-11 instructions:

    a flag byte, then eight items, low bit first
      flag bit 1   a literal byte
      flag bit 0   two bytes: the low eight bits of the distance, then the
                   top four bits of the distance and the length less three

Distances run to 4095 and lengths from 3 to 18, and a match copies from
what has already been written, byte by byte, so overlapping copies (a run)
work by themselves.  RLE managed five per cent on this data; this gets a
quarter.
"""
from __future__ import annotations

from collections import defaultdict

WINDOW = 4096
MIN_MATCH = 3
MAX_MATCH = 18


def compress(data: bytes) -> bytes:
    out, flags, chunk, nflag = bytearray(), 0, bytearray(), 0
    seen: dict[bytes, list[int]] = defaultdict(list)
    i = 0
    while i < len(data):
        best, best_len = 0, 0
        if i + MIN_MATCH <= len(data):
            key = data[i:i + MIN_MATCH]
            for p in reversed(seen[key]):
                if i - p > WINDOW:
                    break
                n = MIN_MATCH
                while n < MAX_MATCH and i + n < len(data) and data[p + n] == data[i + n]:
                    n += 1
                if n > best_len:
                    best, best_len = p, n
                if best_len == MAX_MATCH:
                    break
        if best_len >= MIN_MATCH:
            dist = i - best
            chunk += bytes([dist & 0xFF, ((dist >> 8) << 4) | (best_len - MIN_MATCH)])
        else:
            flags |= 1 << nflag
            chunk += bytes([data[i]])
            best_len = 1
        for k in range(i, min(i + best_len, len(data) - MIN_MATCH + 1)):
            seen[data[k:k + MIN_MATCH]].append(k)
        i += best_len
        nflag += 1
        if nflag == 8:
            out += bytes([flags]) + chunk
            flags, chunk, nflag = 0, bytearray(), 0
    if nflag:
        out += bytes([flags]) + chunk
    return bytes(out)


def decompress(blob: bytes, size: int) -> bytes:
    """What FLOAD's decoder does, in Python - the check that it can."""
    out = bytearray()
    i = 0
    while len(out) < size:
        flags = blob[i]
        i += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if flags & (1 << bit):
                out.append(blob[i])
                i += 1
            else:
                lo, hi = blob[i], blob[i + 1]
                i += 2
                dist = ((hi >> 4) << 8) | lo
                n = (hi & 0x0F) + MIN_MATCH
                start = len(out) - dist
                for k in range(n):
                    out.append(out[start + k])
    return bytes(out)
