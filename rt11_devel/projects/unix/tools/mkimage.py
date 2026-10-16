"""mkimage.py - the system's diskette image from the boot block, the kernel and init.

    mkimage.py --nm NM BOOT.bin KERNEL.bin KERNEL.out INIT.bin OUT.dsk

A double-sided MS 0515 diskette, 819200 bytes: 1600 blocks, side 0 for
0..799, side 1 after.  Block 0 is the boot block, block 1 the superblock
naming the kernel's and init's blocks, which go from block 139 on (SPEC.md
"The file system and the diskette format").  The kernel is loaded at 01000
and entered there, and must end - its bss too, `_end` as NM reads it from
KERNEL.out - below 040000: banks 0 and 1, under the process's slot.  init
is loaded by the kernel at 040000 and must fit in the slot, up to 0140000.

The superblock's words: 0 the magic, 1 the version, 2 the volume's blocks,
3 the kernel's first block, 4 its blocks, 5 its load address, 6 its entry,
7 init's first block, 8 its blocks.
"""
import argparse
import struct
import subprocess
from pathlib import Path

BLOCK = 512
BLOCKS = 1600
IMAGE = BLOCKS * BLOCK
MAGIC = 0o054125            # 'U', 'X'
VERSION = 1
DATA = 139                  # the first block of the data area
KERNEL_LOAD = 0o1000
KERNEL_TOP = 0o40000
USER_BASE = 0o40000
USER_TOP = 0o140000
INTERLEAVE = [0, 2, 4, 6, 8, 1, 3, 5, 7, 9]


def offset(lbn):
    """The byte offset of block lbn in a double-sided image: cylinder 0
    holds a side's last ten blocks, the sectors interleaved 2:1 and skewed
    by two a track, the sides' tracks one after the other
    (docs/hardware/filesystem.md, src/disk/src/Layout.cpp)."""
    side, n = divmod(lbn, 800)
    track = (n // 10 + 1) % 80
    sector = (INTERLEAVE[n % 10] + 2 * track - 2) % 10
    return side * 5120 + track * 10240 + sector * 512


def symbol(nm, program, name):
    for line in subprocess.run([nm, str(program)], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16)
    raise SystemExit(f"{program}: no {name}")


def blocks(data):
    return (len(data) + BLOCK - 1) // BLOCK


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--nm", required=True)
    ap.add_argument("boot", type=Path)
    ap.add_argument("kernel", type=Path)
    ap.add_argument("kernel_out", type=Path)
    ap.add_argument("init", type=Path)
    ap.add_argument("out", type=Path)
    args = ap.parse_args()
    boot, kernel, init = args.boot.read_bytes(), args.kernel.read_bytes(), args.init.read_bytes()
    if len(boot) > BLOCK:
        raise SystemExit(f"{args.boot}: {len(boot)} bytes, a boot block is {BLOCK}")
    if boot[:2] != b"\xa0\x00":
        raise SystemExit(f"{args.boot}: a boot block begins with NOP (000240)")
    end = symbol(args.nm, args.kernel_out, "_end")
    if end > KERNEL_TOP:
        raise SystemExit(f"{args.kernel_out}: the kernel ends at {end:o}, past {KERNEL_TOP:o}")
    if USER_BASE + len(init) > USER_TOP:
        raise SystemExit(f"{args.init}: {len(init)} bytes do not fit in the slot")
    kblocks, iblocks = blocks(kernel), blocks(init)
    image = bytearray(IMAGE)

    def put(lbn, data):
        at = offset(lbn)
        image[at:at + len(data)] = data

    def lay(first, data):
        for i in range(blocks(data)):
            put(first + i, data[i * BLOCK:(i + 1) * BLOCK])

    put(0, boot)
    put(1, struct.pack("<9H", MAGIC, VERSION, BLOCKS, DATA, kblocks, KERNEL_LOAD, KERNEL_LOAD,
                       DATA + kblocks, iblocks))
    lay(DATA, kernel)
    lay(DATA + kblocks, init)
    args.out.write_bytes(image)
    print(f"{args.out.name}: boot block {len(boot)} bytes, kernel {len(kernel)} bytes (to {end:o}), "
          f"init {len(init)} bytes")


if __name__ == "__main__":
    main()
