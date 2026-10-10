"""aout2sav.py - the linked a.out as an RT-11 .SAV.

    aout2sav.py <a.out> <X.SAV> [--tools <prefix>] [--stack <bytes>]

The program is linked by GCC's ld with -N -Ttext 0x200: text, data and
bss one after another from 01000, the first address RT-11 leaves to a
program.  objcopy makes the memory image of text and data; the bss is
zeroed after them up to _end, then the stack's room, and the whole is
padded to a block.  Block 0 in front of it is RT-11's job header - word
040 the start address, 042 the first stack address (the stack grows down
from it), 044 the job status word with bit 14 set, so that lower case
typed at the program passes as typed (the monitor folds it to upper
case otherwise), 050 the high limit, and from 0360 a bitmap of the
file's blocks to load, a bit a block from the top of byte 0360.

The stack has its own room above the bss (--stack, 1024 bytes when not
said): LINK's default, 01000 and down, is the vector area's few hundred
bytes, too little for C.  --tools names the toolchain's prefix
(pdp11-aout-) when its programs are not on the PATH."""
import argparse
import struct
import subprocess
from pathlib import Path

ORIGIN = 0o1000
BLOCK = 512


def symbols(nm, aout):
    out = subprocess.run([nm, str(aout)], check=True, capture_output=True, text=True).stdout
    table = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            table[parts[2]] = int(parts[0], 16)
    return table


JSW_LOWERCASE = 0o40000       # bit 14: lower case typed passes as typed


def header(start, stack, high, blocks):
    block0 = bytearray(BLOCK)
    struct.pack_into("<HHH", block0, 0o40, start, stack, JSW_LOWERCASE)
    struct.pack_into("<H", block0, 0o50, high)
    for b in range(blocks):
        block0[0o360 + b // 8] |= 0x80 >> (b % 8)
    return bytes(block0)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("aout", type=Path)
    ap.add_argument("sav", type=Path)
    ap.add_argument("--tools", default="pdp11-aout-", help="the toolchain's prefix")
    ap.add_argument("--stack", type=int, default=1024, help="the stack's bytes above the bss")
    args = ap.parse_args()

    flat = args.sav.with_suffix(".bin")
    subprocess.run([args.tools + "objcopy", "-O", "binary", str(args.aout), str(flat)], check=True)
    image = bytearray(flat.read_bytes())
    flat.unlink()
    table = symbols(args.tools + "nm", args.aout)
    start, end = table["_start"], table["_end"]
    if end > ORIGIN + len(image):
        image += bytes(end - ORIGIN - len(image))
    stack = (ORIGIN + len(image) + args.stack + 1) & ~1
    image += bytes(stack - ORIGIN - len(image))
    image += bytes(-len(image) % BLOCK)
    blocks = 1 + len(image) // BLOCK
    args.sav.write_bytes(header(start, stack, stack - 1, blocks) + bytes(image))
    print(f"{args.sav.name}: {blocks} blocks, start {start:o}, stack {stack:o}, high {stack - 1:o}")


if __name__ == "__main__":
    main()
