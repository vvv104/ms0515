"""mkimage.py - the system's diskette image from the boot block and the kernel.

    mkimage.py BOOT.bin KERNEL.bin OUT.dsk

A double-sided MS 0515 diskette, 819200 bytes: 1600 blocks, side 0 for
0..799, side 1 after.  Block 0 is the boot block, block 1 the superblock
naming the kernel's blocks, which go from block 139 on (SPEC.md "The file
system and the diskette format").  The kernel is loaded at 01000 and entered
there, and must stay below 040000 for now: banks 0 and 1, under the boot
block running at 070000.
"""
import struct
import sys
from pathlib import Path

BLOCK = 512
BLOCKS = 1600
IMAGE = BLOCKS * BLOCK
MAGIC = 0o054125            # 'U', 'X'
VERSION = 1
KERNEL_BLOCK = 139          # the first block of the data area
KERNEL_LOAD = 0o1000
KERNEL_TOP = 0o40000
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


def main():
    boot_path, kernel_path, out_path = (Path(a) for a in sys.argv[1:4])
    boot = boot_path.read_bytes()
    kernel = kernel_path.read_bytes()
    if len(boot) > BLOCK:
        raise SystemExit(f"{boot_path}: {len(boot)} bytes, a boot block is {BLOCK}")
    if boot[:2] != b"\xa0\x00":
        raise SystemExit(f"{boot_path}: a boot block begins with NOP (000240)")
    if KERNEL_LOAD + len(kernel) > KERNEL_TOP:
        raise SystemExit(f"{kernel_path}: {len(kernel)} bytes from 01000 pass 040000")
    kblocks = (len(kernel) + BLOCK - 1) // BLOCK
    image = bytearray(IMAGE)

    def put(lbn, data):
        at = offset(lbn)
        image[at:at + len(data)] = data

    put(0, boot)
    put(1, struct.pack("<7H", MAGIC, VERSION, BLOCKS, KERNEL_BLOCK, kblocks, KERNEL_LOAD, KERNEL_LOAD))
    for i in range(kblocks):
        put(KERNEL_BLOCK + i, kernel[i * BLOCK:(i + 1) * BLOCK])
    out_path.write_bytes(image)
    print(f"{out_path.name}: boot block {len(boot)} bytes, kernel {len(kernel)} bytes in {kblocks} blocks")


if __name__ == "__main__":
    main()
