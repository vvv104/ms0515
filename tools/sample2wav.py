"""sample2wav.py - turn a recording of the home-made MS-0515 sampler into a WAV.

VLAD & ALEX's «Sound Effect's Recorder» (SER4) and its players LD1/LOAD
kept sound in two shapes on the diskettes:

  * `--nibbles`: 4-bit samples, two per byte, LOW nibble first - the
    recorder's own 16K-word buffer written out as is (the file `A`,
    32768 bytes);
  * `--words` (default): one 8-bit sample per 16-bit word, in the low
    byte, the high byte zero - the players' files (`9.PRG`, `TLF.PRG`,
    `100.PR` ... `140.PR`, 16384 bytes).

The sample rate was whatever delay loop the program used; nothing in the
file says.  4000 Hz is what the owner of the disks hears as right (8000
plays them twice too fast, at a child's pitch) - pass `--rate` to try
another.

    python tools/sample2wav.py 9.PRG [-o 9.wav] [--rate 4000]
    python tools/sample2wav.py A --nibbles
"""

from __future__ import annotations

import argparse
import wave
from pathlib import Path


def decode(data: bytes, nibbles: bool) -> bytes:
    if nibbles:
        out = bytearray()
        for b in data:
            out.append((b & 0x0F) << 4)
            out.append((b >> 4) << 4)
        return bytes(out)
    return bytes(data[i] for i in range(0, len(data) - 1, 2))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("src", type=Path)
    ap.add_argument("-o", "--out", type=Path, help="WAV to write (default: beside the input)")
    ap.add_argument("--rate", type=int, default=4000, help="sample rate in Hz (default 4000)")
    ap.add_argument("--nibbles", action="store_true", help="4-bit samples, two per byte, low nibble first")
    args = ap.parse_args()
    samples = decode(args.src.read_bytes(), args.nibbles)
    out = args.out or args.src.with_suffix(".wav")
    with wave.open(str(out), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(1)          # 8-bit unsigned PCM
        w.setframerate(args.rate)
        w.writeframes(samples)
    print("%s -> %s: %d samples, %.1f s at %d Hz" % (args.src.name, out, len(samples), len(samples) / args.rate, args.rate))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
