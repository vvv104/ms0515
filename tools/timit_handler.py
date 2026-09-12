"""timit_handler.py - a handler for a monitor generated with TIM$IT, made from a plain one.

An RT-11 handler ends (.DREND in SYSMAC.SML) with pointer words the monitor
fills when it loads the handler: $INPTR and $FKPTR, and before them $TIMIT
when the monitor was generated with device time-out support (TIM$IT).  The
monitor finds the words from the handler's end, by its own layout - which is
why it refuses a handler whose SYSGEN option word (offset 060 of block 0)
differs from its own.  Flip the bit alone and the monitor writes $TIMIT over
the last word of the handler's code: in DV.SYS that is the 270 of .DRFIN's
JMP @270(R5), the return into the monitor after every request.

Mihin's OS-16SJ is such a monitor.  This makes a handler for it from a plain
one: a zero word for $TIMIT goes in before $INPTR, the handler size (052)
and what lies past the insertion - the high limit (050) and the primary
bootstrap's offset (062) - grow by 2, and the TIM$IT bit is set in 060.
Nothing else moves, provided the code never addresses the pointer words
itself: .DRAST and .FORK do (JSR R5,@X(PC), whose displacement would need
fixing), so a handler that uses them is refused.  The MS-0515 floppy
handlers poll the controller and use neither.

    python tools/timit_handler.py <collection>/software/system/handlers/omega/DV.SYS \
                                  <collection>/software/system/handlers/mihin/DV.SYS
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

BLOCK = 512
TIM_IT = 0o4                    # the bit of TIM$IT in the SYSGEN option word
JSR_R5_DEFERRED_PC = 0o004577   # JSR R5,@X(PC): .DRAST's and .FORK's call


def word(data: bytes, off: int) -> int:
    return struct.unpack_from("<H", data, off)[0]


def convert(src: bytes) -> bytes:
    if len(src) % BLOCK or len(src) < 2 * BLOCK:
        raise SystemExit("not a handler file: %d bytes" % len(src))
    size = word(src, 0o52)
    options = word(src, 0o60)
    if options & TIM_IT:
        raise SystemExit("already a TIM$IT handler (option word %06o)" % options)
    end = BLOCK + size                      # the resident part is block 1 on
    if end > len(src) or end < BLOCK + 4:
        raise SystemExit("handler size %06o does not fit the file" % size)
    for off in range(BLOCK, end - 4, 2):
        if word(src, off) == JSR_R5_DEFERRED_PC:
            raise SystemExit("the handler calls through its pointer words (JSR R5,@X(PC) at %06o): "
                             "its displacements would need fixing - not done here" % (off - BLOCK + 0o1000))
    if src[-2:] != b"\0\0":
        raise SystemExit("no room: the last word of the file is not zero")
    out = bytearray(src[:end - 4] + b"\0\0" + src[end - 4:-2])
    struct.pack_into("<H", out, 0o52, size + 2)
    struct.pack_into("<H", out, 0o60, options | TIM_IT)
    for off in (0o50, 0o62):
        if word(src, off):
            struct.pack_into("<H", out, off, word(src, off) + 2)
    return bytes(out)


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print("usage: timit_handler.py <plain handler> <TIM$IT handler>", file=sys.stderr)
        return 2
    src = Path(argv[1]).read_bytes()
    Path(argv[2]).write_bytes(convert(src))
    print("%s -> %s: $TIMIT inserted at %06o, size %06o -> %06o" % (
        argv[1], argv[2], word(src, 0o52) + 0o1000 - 4, word(src, 0o52), word(src, 0o52) + 2))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
