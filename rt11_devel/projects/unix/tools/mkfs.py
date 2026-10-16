"""mkfs.py - the system's diskette: its file system, the boot block, the kernel.

    mkfs.py [--nm NM] [--label TEXT] [--volume N] --boot BOOT.bin OUT.dsk PATH=FILE...

A double-sided MS 0515 diskette, 819200 bytes, 1600 blocks - side 0 for
0..799, side 1 after - laid out as kernel/fs.h has it: block 0 the boot
block, 1 the superblock, 2 the bitmap of the blocks taken, 3..10 the inode
table (128 inodes of 32 bytes), 11..138 the swap area, the data from 139.
Each PATH=FILE puts the host's FILE at PATH on the volume, its directories
made as they are needed; every file is laid down whole in one extent, the
next after the last.  The file at /boot/kernel is the kernel: the
superblock names its blocks for the boot block, which loads it at 01000 and
enters it there; with --nm, `_end` of the kernel's a.out (KERNEL.out given
as /boot/kernel's companion through --kernel-out) must lie below 040000.

Words go down the low byte first, a long as two words, the high word first,
as the PDP-11's compiler keeps one.
"""
import argparse
import random
import struct
import subprocess
from pathlib import Path

BLOCK = 512
BLOCKS = 1600
IMAGE = BLOCKS * BLOCK
MAGIC = 0o054125            # 'U', 'X'
VERSION = 2
BITMAP = 2
ITABLE, IBLOCKS = 3, 8
INODES = IBLOCKS * BLOCK // 32
SWAP, SWAPBLOCKS = 11, 128
DATA = SWAP + SWAPBLOCKS    # 139
ROOT = 1
KERNEL_LOAD = 0o1000
KERNEL_TOP = 0o40000
S_IFDIR, S_IFREG = 0o040000, 0o100000
DIRSIZ = 14
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


def long_(value):
    """A long as the PDP-11 keeps it: the high word first."""
    return struct.pack("<HH", (value >> 16) & 0xFFFF, value & 0xFFFF)


def blocks(n):
    return (n + BLOCK - 1) // BLOCK


class Volume:
    def __init__(self):
        self.image = bytearray(IMAGE)
        self.inodes = {}            # number -> [mode, nlink, data bytes, start]
        self.dirs = {}              # inode -> {name: inode}
        self.next_block = DATA
        self.next_inode = ROOT
        self.root = self.mkdir(None, None)

    def put(self, lbn, data):
        at = offset(lbn)
        self.image[at:at + len(data)] = data

    def inode(self, mode):
        if self.next_inode > INODES:
            raise SystemExit("out of inodes")
        n = self.next_inode
        self.next_inode += 1
        self.inodes[n] = [mode, 1, b"", 0]
        return n

    def mkdir(self, parent, name):
        n = self.inode(S_IFDIR | 0o755)
        self.dirs[n] = {".": n, "..": parent if parent else n}
        if parent:
            self.link(parent, name, n)
            self.inodes[parent][1] += 1     # its ".." links the parent
        self.inodes[n][1] = 2
        return n

    def link(self, directory, name, n):
        if len(name.encode("koi8_r")) > DIRSIZ:
            raise SystemExit(f"{name}: a name is at most {DIRSIZ} bytes")
        if name in self.dirs[directory]:
            raise SystemExit(f"{name}: there already")
        self.dirs[directory][name] = n

    def walk(self, path):
        """The directory that holds `path`, made as needed, and its last name."""
        parts = [p for p in path.split("/") if p]
        directory = self.root
        for p in parts[:-1]:
            n = self.dirs[directory].get(p)
            if n is None:
                n = self.mkdir(directory, p)
            elif n not in self.dirs:
                raise SystemExit(f"{path}: {p} is a file")
            directory = n
        return directory, parts[-1]

    def add(self, path, data):
        directory, name = self.walk(path)
        n = self.inode(S_IFREG | 0o755)
        self.inodes[n][2] = data
        self.link(directory, name, n)
        return n

    def lay(self, n):
        """Inode n's data into the next free blocks."""
        data = self.inodes[n][2]
        start = self.next_block
        count = blocks(len(data))
        if start + count > BLOCKS:
            raise SystemExit("the volume is full")
        for i in range(count):
            self.put(start + i, data[i * BLOCK:(i + 1) * BLOCK])
        self.inodes[n][3] = start
        self.next_block += count

    def finish(self, label, volume, boot, kernel_ino):
        for n, entries in self.dirs.items():       # directories' contents
            body = b""
            for name, ino in entries.items():
                body += struct.pack("<H", ino) + name.encode("koi8_r").ljust(DIRSIZ, b"\0")
            self.inodes[n][2] = body
        for n in sorted(self.inodes):
            self.lay(n)
        table = bytearray(IBLOCKS * BLOCK)
        for n, (mode, nlink, data, start) in self.inodes.items():
            count = blocks(len(data))
            ext = struct.pack("<HH", start, count) if count else b""
            raw = (struct.pack("<HBB", mode, nlink, 1 if count else 0) + long_(len(data)) + long_(0) +
                   ext.ljust(20, b"\0"))
            table[(n - 1) * 32:n * 32] = raw
        for i in range(IBLOCKS):
            self.put(ITABLE + i, table[i * BLOCK:(i + 1) * BLOCK])
        used = bytearray(BLOCKS // 8)
        for b in range(self.next_block):
            used[b // 8] |= 1 << (b % 8)
        self.put(BITMAP, used)
        kstart, kblocks = 0, 0
        if kernel_ino:
            kstart = self.inodes[kernel_ino][3]
            kblocks = blocks(len(self.inodes[kernel_ino][2]))
        sb = (struct.pack("<13H", MAGIC, VERSION, BLOCKS, kstart, kblocks, KERNEL_LOAD, KERNEL_LOAD,
                          BITMAP, ITABLE, IBLOCKS, SWAP, SWAPBLOCKS, DATA) +
              struct.pack("<H", INODES) + long_(volume) + label.encode("koi8_r")[:16].ljust(16, b"\0"))
        self.put(1, sb)
        if boot:
            self.put(0, boot)


def symbol(nm, program, name):
    for line in subprocess.run([nm, str(program)], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16)
    raise SystemExit(f"{program}: no {name}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--boot", type=Path, help="the boot block for block 0")
    ap.add_argument("--nm", help="nm of the toolchain, to check the kernel's end")
    ap.add_argument("--kernel-out", type=Path, help="the kernel's a.out, for its _end")
    ap.add_argument("--label", default="UNIX")
    ap.add_argument("--volume", type=int, help="the volume's number (at random when left out)")
    ap.add_argument("out", type=Path)
    ap.add_argument("files", nargs="*", help="PATH=FILE")
    args = ap.parse_args()

    boot = args.boot.read_bytes() if args.boot else b""
    if len(boot) > BLOCK:
        raise SystemExit(f"{args.boot}: {len(boot)} bytes, a boot block is {BLOCK}")
    if boot and boot[:2] != b"\xa0\x00":
        raise SystemExit(f"{args.boot}: a boot block begins with NOP (000240)")
    vol = Volume()
    kernel_ino = None
    for spec in args.files:
        path, _, host = spec.partition("=")
        data = Path(host).read_bytes()
        n = vol.add(path, data)
        if path.strip("/") == "boot/kernel":
            kernel_ino = n
            if KERNEL_LOAD + len(data) > KERNEL_TOP:
                raise SystemExit(f"{host}: {len(data)} bytes from 01000 pass 040000")
    if args.nm and args.kernel_out:
        end = symbol(args.nm, args.kernel_out, "_end")
        if end > KERNEL_TOP:
            raise SystemExit(f"{args.kernel_out}: the kernel ends at {end:o}, past {KERNEL_TOP:o}")
    volume = args.volume if args.volume is not None else random.getrandbits(32)
    vol.finish(args.label, volume, boot, kernel_ino)
    args.out.write_bytes(vol.image)
    print(f"{args.out.name}: {len(vol.inodes)} inodes, blocks {DATA}..{vol.next_block - 1} taken")


if __name__ == "__main__":
    main()
