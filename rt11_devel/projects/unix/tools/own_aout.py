"""own_aout.py - a linked a.out of the 0407 kind made a program that owns
the machine: magic 0401 (kernel/own.c), the rest as it is.

    own_aout.py <in a.out> <out>

The program is linked at 001000 (text, data and bss one after another,
ld -N); the kernel's loader puts the header at 000760, under the text."""
import struct
import sys
from pathlib import Path


def main():
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    data = bytearray(src.read_bytes())
    magic, text, dat, bss, _syms, entry = struct.unpack_from("<6H", data)
    if magic != 0o407:
        raise SystemExit(f"{src}: magic {magic:o}, not 0407")
    if entry < 0o1000 or entry >= 0o1000 + text:
        raise SystemExit(f"{src}: entry {entry:o} not in the text at 001000")
    if 0o760 + 16 + text + dat + bss > 0o157000:
        raise SystemExit(f"{src}: {text + dat + bss} bytes, past 157000")
    struct.pack_into("<H", data, 0, 0o401)
    dst.write_bytes(bytes(data))


if __name__ == "__main__":
    main()
