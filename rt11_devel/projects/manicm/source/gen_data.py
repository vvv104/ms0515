"""gen_data.py - MANICM.DAT, the game's data, from the original's snapshot.

The build's pre_build hook.  Reads manic_miner.z80 in MANICM_DIR (see
mm_dir.py; prepare_mm.py makes it) and writes MANICM.DAT next to the
sources - a build artifact, never committed: the caverns, the sprites,
the tunes and the title screen are the original's and stay outside the
repository.  MANICM.MAC reads the file at run time, a region at a time,
so nothing of it is in the program image either.

The file is a sequence of 512-byte blocks; MANICM.MAC names the same
offsets (the DATxxx symbols).  Every region is the original's bytes as
they sit in the Spectrum's memory, so the disassembly's description of
each applies unchanged:

    block  0.. 1   MISC   33280 Willy's sprite graphic data (256 bytes)
                          33800 the left-right movement table (16)
                          33816 'AIR' (3), 33839 the score line (32),
                          33871 'Game' (4), 33875 'Over' (4),
                          33886 the 6031769 table (16) - packed in that
                          order from offset 256, each padded to a word;
                          the title screen's scrolling message (40192,
                          256 bytes) from offset 512
    block  2.. 2   TUNES  33902 The Blue Danube (286 bytes), then
                          34188 In the Hall of the Mountain King (64)
    block  3..11   TITLE  40448 the attributes of the title screen's
                          bottom two-thirds (512), then 40960 the title
                          screen graphic data (4096)
    block 12..51   CAVERN cavern n (0..19) at block 12 + 2n: 45056 +
                          1024n, its 1024 bytes as the disassembly lays
                          them out (512 attribute bytes, then the
                          definition the game copies to 32768)
    block 52..53   FONT   the Spectrum ROM's character set, 15616..16383:
                          96 characters of 8 rows, ' ' first - the game
                          prints with it (the routine at 37579)
"""
import struct
from pathlib import Path

from skoolkit import ROM48, read_bin_file
from skoolkit.snapshot import get_snapshot

from mm_dir import SNAPSHOT

HERE = Path(__file__).resolve().parent
OUT = HERE.parent / "MANICM.DAT"
BLOCK = 512


def region(mem, start, length):
    return bytes(mem[start:start + length])


def padded(data, size):
    if len(data) > size:
        raise SystemExit(f"region of {len(data)} bytes does not fit {size}")
    return data + bytes(size - len(data))


def even(data):
    return data + (b"\0" if len(data) & 1 else b"")


def main():
    if not SNAPSHOT.exists():
        raise SystemExit(f"{SNAPSHOT} not found: run source/prepare_mm.py (or set MANICM_DIR)")
    mem = get_snapshot(str(SNAPSHOT))

    misc = region(mem, 33280, 256)
    misc += (even(region(mem, 33800, 16)) + even(region(mem, 33816, 3))
             + even(region(mem, 33839, 32)) + even(region(mem, 33871, 4))
             + even(region(mem, 33875, 4)) + even(region(mem, 33886, 16)))
    misc = padded(misc, BLOCK) + region(mem, 40192, 256)
    tunes = padded(even(region(mem, 33902, 286)) + region(mem, 34188, 64), BLOCK)
    title = padded(region(mem, 40448, 512) + region(mem, 40960, 4096), 9 * BLOCK)
    caverns = b"".join(region(mem, 45056 + 1024 * n, 1024) for n in range(20))

    font = padded(bytes(read_bin_file(ROM48)[15616:16384]), 2 * BLOCK)

    data = padded(misc, 2 * BLOCK) + tunes + title + caverns + font
    assert len(data) == 54 * BLOCK, len(data)
    OUT.write_bytes(data)
    print(f"{OUT.name}: {len(data)} bytes ({len(data) // BLOCK} blocks) from {SNAPSHOT}")


if __name__ == "__main__":
    main()
