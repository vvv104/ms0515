"""The 48K tape: the game's code block as the Spectrum holds it.

A .tap is a run of blocks, each a 16-bit length, a flag byte, the data
and a checksum; a header block (flag 0, 17 bytes) names the block that
follows.  The game is the code block that loads at 0x620C.
"""
import struct

from sab2_dir import CODE_BASE, TAPE


def blocks(data):
    """(header or None, body) pairs; a header is (type, name, length, param1)."""
    i = 0
    header = None
    while i + 2 <= len(data):
        (n,) = struct.unpack_from('<H', data, i)
        blk = data[i + 2:i + 2 + n]
        i += 2 + n
        flag = blk[0]
        body = blk[1:-1]
        if flag == 0 and len(body) == 17:
            typ = body[0]
            name = body[1:11].decode('latin1').strip()
            length, p1 = struct.unpack_from('<HH', body, 11)
            header = (typ, name, length, p1)
            continue
        yield header, body
        header = None


def game_code(tape=TAPE):
    """The code block at CODE_BASE, or a SystemExit naming what is missing."""
    if not tape.exists():
        raise SystemExit(f"{tape} not found: put the 48K tape there or set SAB2_DIR")
    data = tape.read_bytes()
    for header, body in blocks(data):
        if header and header[0] == 3 and header[3] == CODE_BASE:
            return body
    raise SystemExit(f"{tape}: no code block loading at {CODE_BASE:#x}")


class Memory:
    """The Spectrum's memory as the code block fills it: rd(addr) for a byte."""

    def __init__(self, code):
        self.code = code

    def rd(self, addr):
        return self.code[addr - CODE_BASE]

    def rd16(self, addr):
        return self.rd(addr) | (self.rd(addr + 1) << 8)

    def region(self, start, end):
        return bytes(self.code[start - CODE_BASE:end - CODE_BASE])
