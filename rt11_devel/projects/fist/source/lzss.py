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

The parse is optimal, not greedy.  A literal costs nine bits and a match
seventeen *whatever its length*, so the cheapest encoding of a block is a
shortest path over its positions - and it is often worth taking a shorter
match, or a literal, to land on a better one.  Nothing about the format or
the decoder changes; only the choice of tokens does.  It is worth about two
per cent over the greedy parse, which is a block and a half of FIST.DAT.
"""
from __future__ import annotations

from collections import defaultdict

WINDOW = 4096
MIN_MATCH = 3
MAX_MATCH = 18
LITERAL_COST = 9     # eighths of a byte: the byte plus its flag bit
MATCH_COST = 17      # the pair plus its flag bit - the same for any length


def _longest(data: bytes) -> list[tuple[int, int]]:
    """The longest match at every position, as (length, where it starts)."""
    best: list[tuple[int, int]] = [(0, 0)] * len(data)
    seen: dict[bytes, list[int]] = defaultdict(list)
    for i in range(len(data)):
        if i + MIN_MATCH > len(data):
            break
        key = data[i:i + MIN_MATCH]
        blen, bpos = 0, 0
        for p in reversed(seen[key]):
            if i - p >= WINDOW:
                break          # the distance is twelve bits: 1..4095
            n = MIN_MATCH
            while n < MAX_MATCH and i + n < len(data) and data[p + n] == data[i + n]:
                n += 1
            if n > blen:
                blen, bpos = n, p
            if blen == MAX_MATCH:
                break
        best[i] = (blen, bpos)
        seen[key].append(i)
    return best


def compress(data: bytes) -> bytes:
    """The cheapest token sequence for `data`, by shortest path.

    Costs are in eighths of a byte so the flag bit counts: a literal is
    8 + 1, a match 16 + 1.  `cost[i]` is the cheapest encoding of the tail
    from i, and `take[i]` the token that starts it (0 = a literal)."""
    n = len(data)
    best = _longest(data)
    cost = [0] * (n + 1)
    take = [0] * (n + 1)
    for i in range(n - 1, -1, -1):
        c, t = cost[i + 1] + LITERAL_COST, 0
        for m in range(MIN_MATCH, best[i][0] + 1):
            if cost[i + m] + MATCH_COST < c:
                c, t = cost[i + m] + MATCH_COST, m
        cost[i], take[i] = c, t

    out, flags, chunk, nflag = bytearray(), 0, bytearray(), 0
    i = 0
    while i < n:
        m = take[i]
        if m:
            dist = i - best[i][1]
            chunk += bytes([dist & 0xFF, ((dist >> 8) << 4) | (m - MIN_MATCH)])
            i += m
        else:
            flags |= 1 << nflag
            chunk += bytes([data[i]])
            i += 1
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
