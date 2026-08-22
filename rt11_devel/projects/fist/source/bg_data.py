"""Extract a WotEF background's data tables from the runtime snapshot and
emit them as relocatable MACRO-11 source.

The Z80 background definition at $602B/$603D/$604F is four (UDG, position)
address pairs plus an attribute-data address.  On the Z80 those are
absolute addresses in $6000..$7Axx; on the MS-0515 that range is the VRAM
window, so every pointer is rewritten to a MACRO-11 label and the
referenced byte blocks are emitted verbatim (UDG bitmaps, RLE position and
attribute streams - all index/count based, so they port unchanged; only
the definition table's pointers need relocating).

Block lengths are derived by replaying the same parse the engine uses:
a position/attribute stream ends at a byte equal to its successor and
zero; a UDG block spans (max referenced index + 1) * 8 bytes.
"""
import os
from pathlib import Path

from skoolkit.snapshot import get_snapshot

WOTEF_DIR = Path(os.environ.get("WOTEF_DIR", r"C:\Users\voron\wotef"))
SNAP = WOTEF_DIR / "wotef.z80"

BG_DEF = {1: 0x602B, 2: 0x603D, 3: 0x604F}


def _u16(M, a):
    return M[a] | (M[a + 1] << 8)


def _pos_extent(M, pos):
    """Return (length, max_udg_index) for a positioning stream."""
    hl, mx = pos, 0
    while True:
        a = M[hl]; hl += 1
        if a == M[hl] and a == 0:
            return hl + 1 - pos, mx
        if (a & 0x7F) > mx:
            mx = a & 0x7F
        if a & 0x80:                       # repeat run: skip the count byte
            hl += 1


def _attr_extent(M, attr):
    hl = attr
    while True:
        a = M[hl]; hl += 1
        if a == M[hl] and a == 0:
            return hl + 1 - attr
        if a & 0x80:
            hl += 1


class BackgroundData:
    """Extracted, relocatable data for one background."""

    def __init__(self, ref):
        M = get_snapshot(str(SNAP))
        self.ref = ref
        src = BG_DEF[ref]
        self.blocks = {}                   # z80 addr -> (label, bytes)
        self.def_labels = []               # 9 labels: udg0,pos0,...,udg3,pos3,attr

        def take(addr, length, kind):
            if addr not in self.blocks:
                label = f"B{ref}{kind}{addr:04X}"
                self.blocks[addr] = (label, bytes(M[addr:addr + length]))
            return self.blocks[addr][0]

        for blk in range(4):
            udg = _u16(M, src + blk * 4)
            pos = _u16(M, src + blk * 4 + 2)
            poslen, mx = _pos_extent(M, pos)
            self.def_labels.append(take(udg, (mx + 1) * 8, "U"))
            self.def_labels.append(take(pos, poslen, "P"))
        attr = _u16(M, src + 16)
        self.def_labels.append(take(attr, _attr_extent(M, attr), "A"))

    def emit(self):
        """MACRO-11 source: the definition word table + every data block."""
        out = []
        out.append(f"; --- Background {self.ref} data (extracted, relocated) ---")
        out.append(f"BG{self.ref}DEF:")
        for i in range(0, 8, 2):
            out.append(f"        .WORD   {self.def_labels[i]},{self.def_labels[i+1]}")
        out.append(f"        .WORD   {self.def_labels[8]}")
        out.append("")
        for addr in sorted(self.blocks):
            label, data = self.blocks[addr]
            out.append(f"{label}:")
            for i in range(0, len(data), 16):
                chunk = data[i:i + 16]
                out.append("        .BYTE   " + ",".join(f"{b}." for b in chunk))
        out.append("        .EVEN")
        return "\n".join(out) + "\n"


def emit_all(refs=(1, 2, 3)):
    """MACRO-11 source for several backgrounds in one block: each definition
    table + its data blocks (a block shared by two backgrounds is emitted
    once).  Returns (source, byte_count) - the count is exact so the caller
    can assert the block fits its address window."""
    out, seen, nbytes = [], {}, 0
    for ref in refs:
        bd = BackgroundData(ref)
        out.append(f"; --- Background {ref} data (extracted, relocated) ---")
        out.append(f"BG{ref}DEF:")
        # route the definition's labels through the shared map so a block
        # already emitted under an earlier background is referenced, not copied
        remap = {}
        for addr, (label, data) in bd.blocks.items():
            if addr not in seen:
                seen[addr] = (label, data)
            remap[label] = seen[addr][0]
        dl = [remap[l] for l in bd.def_labels]
        for i in range(0, 8, 2):
            out.append(f"        .WORD   {dl[i]},{dl[i+1]}")
        out.append(f"        .WORD   {dl[8]}")
        nbytes += 18
        for addr, (label, data) in sorted(bd.blocks.items()):
            if seen[addr][0] != label:
                continue
            out.append(f"{label}:")
            for i in range(0, len(data), 16):
                chunk = data[i:i + 16]
                out.append("        .BYTE   " + ",".join(f"{b}." for b in chunk))
            nbytes += len(data)
        out.append("        .EVEN")
        nbytes += nbytes & 1
        out.append("")
    return chr(10).join(out) + chr(10), nbytes


if __name__ == "__main__":
    bd = BackgroundData(1)
    print(bd.emit()[:800])
    total = sum(len(d) for _, d in bd.blocks.values())
    print(f"; bg1 total data: {total} bytes in {len(bd.blocks)} blocks")
