#!/usr/bin/env python3
"""
rt11_handler.py — take an RT-11 device handler (.SYS) apart.

    python tools/rt11_handler.py HANDLER.SYS [-o LISTING]

A handler is a memory image with a header the SYSMAC macros lay out, and
code that is position-independent, so every address here is the offset in
the file and the two agree.  The header (block 0):

    0    .RAD50 "HAN"            the V5 handler signature
    2    FETCH  4 RELEASE        the entry points .DRPTR declares
    6    LOAD  10 UNLOAD
   12    FORMAT 14 SHOW
   20    device class, 21 modifier bits   (.DREST)
   52    length of the code       54  blocks on the device
   56    device status            60  ERL$G + MMG$T*2 + TIM$IT*4 + RTE$M*10
  176    address of the controller's registers   (.DRDEF CSR=)
  400+   the SET options table    (.DRSET)
 1000    the code: vector, offset to the interrupt entry, priority,
         two queue words, and a NOP — what .DRBEG puts there

Disassembling straight through does not work: tables sit between the
routines and the listing loses its footing on them.  So this follows the
control flow instead, from the entry points the header names, and prints
what was never reached as data.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pdp11_disasm import Disassembler  # noqa: E402

R50 = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.?0123456789"
CODE_START = 0o1000
BODY = 0o1014                      # past .DRBEG's six words

SYSGEN_BITS = (("ERL$G", 1), ("MMG$T", 2), ("TIM$IT", 4), ("RTE$M", 8))
CLASSES = {0: "unknown", 1: "NL", 2: "TT", 3: "tape", 4: "disk", 5: "magtape",
           6: "cassette", 7: "printer", 8: "DE", 9: "DP", 10: "DL",
           11: "network", 12: "pseudo", 13: "VT"}

# What the machine's own addresses mean, so the listing reads as hardware.
NAMED = {
    0o177640: "FDC command/status", 0o177642: "FDC track",
    0o177644: "FDC sector", 0o177646: "FDC data",
    0o177716: "port C", 0o177714: "port B", 0o177712: "port A",
    0o177710: "PPI control", 0o177400: "memory dispatcher",
    0o160000: "ROM console out", 0o160004: "ROM console in",
    0o54: "RMON pointer", 0o44: "JSW", 0o46: "USR load",
}

STOP = ("RTS", "RTI", "RTT", "HALT", "JMP", "BR")
COND = ("BNE", "BEQ", "BPL", "BMI", "BCC", "BCS", "BVC", "BVS", "BGE", "BLT",
        "BGT", "BLE", "BHI", "BLOS", "BHIS", "BLO", "SOB", "BR")


def rad50(word: int) -> str:
    return "".join(R50[(word // d) % 40] for d in (1600, 40, 1))


def word_at(data: bytes, off: int) -> int:
    return data[off] | (data[off + 1] << 8) if off + 1 < len(data) else 0


def header(data: bytes) -> dict:
    """The named words of block 0."""
    h = {
        "signature": rad50(word_at(data, 0)),
        "fetch": word_at(data, 0o2), "release": word_at(data, 0o4),
        "load": word_at(data, 0o6), "unload": word_at(data, 0o10),
        "format": word_at(data, 0o12), "show": word_at(data, 0o14),
        "class": data[0o20] if len(data) > 0o20 else 0,
        "modifier": data[0o21] if len(data) > 0o21 else 0,
        "length": word_at(data, 0o52), "blocks": word_at(data, 0o54),
        "status": word_at(data, 0o56), "sysgen": word_at(data, 0o60),
        "csr": word_at(data, 0o176),
        "vector": word_at(data, CODE_START),
        "interrupt": CODE_START + 2 + word_at(data, CODE_START + 2),
        "priority": word_at(data, CODE_START + 4),
    }
    return h


def targets(text: str, after: int) -> tuple[list[int], bool]:
    """Where an instruction can go, and whether it can fall through."""
    mnemonic = text.split()[0] if text.split() else ""
    addrs = [int(m, 8) for m in re.findall(r"\b([0-7]{6})\b", text)]
    goes: list[int] = []
    if mnemonic in COND or mnemonic in ("JMP", "JSR", "CALL"):
        # The last number of a branch or a jump is where it goes; for JSR
        # the target is the second operand.
        if addrs:
            goes.append(addrs[-1])
    through = mnemonic not in ("BR", "JMP", "RTS", "RTI", "RTT", "HALT")
    if mnemonic == "JSR" and "PC," not in text and "R5," not in text:
        through = True
    return goes, through


def trace(data: bytes, entries: list[int]) -> dict[int, str]:
    """Walk the code from every entry, following where it can go."""
    seen: dict[int, str] = {}
    queue = [e for e in entries if 0 < e < len(data) - 1]
    while queue:
        pc = queue.pop()
        while 0 < pc < len(data) - 1 and pc not in seen:
            dis = Disassembler(data, 0)
            dis.pc = pc
            _, text = dis.disassemble_one()
            if text is None:
                break
            seen[pc] = text
            after = dis.pc
            goes, through = targets(text, after)
            for g in goes:
                if 0 < g < len(data) - 1 and g not in seen:
                    queue.append(g)
            if not through:
                break
            pc = after
    return seen


def annotate(text: str) -> str:
    for value, name in NAMED.items():
        text = text.replace(f"@#{value:o}", f"@#{value:o} ;{name}")
    return text


def listing(data: bytes, h: dict, seen: dict[int, str]) -> str:
    out = [f"; {h['signature']} handler, {len(data)} bytes",
           f"; code {h['length']:06o} bytes, volume {h['blocks']} blocks, "
           f"status {h['status']:06o}",
           f"; class {CLASSES.get(h['class'], h['class'])}, "
           f"modifier {h['modifier']:03o}, CSR {h['csr']:06o}",
           "; built with: " + (" ".join(n for n, b in SYSGEN_BITS
                                        if h['sysgen'] & b) or "nothing"),
           "; entry points: " + ", ".join(
               f"{n}={h[n]:06o}" for n in
               ("fetch", "release", "load", "unload", "format", "show")),
           f"; vector {h['vector']:06o}, interrupt at {h['interrupt']:06o}, "
           f"priority {h['priority']:03o}", ""]
    off = CODE_START
    while off < len(data) - 1:
        if off in seen:
            text = seen[off]
            dis = Disassembler(data, 0)
            dis.pc = off
            dis.disassemble_one()
            out.append(f"{off:06o}: {annotate(text)}")
            off = dis.pc
        else:
            run = []
            start = off
            while off < len(data) - 1 and off not in seen and len(run) < 8:
                run.append(f"{word_at(data, off):06o}")
                off += 2
            out.append(f"{start:06o}: .WORD " + ", ".join(run))
    return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("handler")
    ap.add_argument("-o", "--out")
    args = ap.parse_args()
    data = Path(args.handler).read_bytes()
    h = header(data)
    entries = [BODY, h["interrupt"]] + [h[n] for n in
                                        ("fetch", "release", "load",
                                         "unload", "format", "show")]
    seen = trace(data, entries)
    text = listing(data, h, seen)
    if args.out:
        Path(args.out).write_text(text, encoding="utf-8")
        print(f"{len(seen)} instructions reached -> {args.out}")
    else:
        sys.stdout.buffer.write(text.encode("utf-8", "replace"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
