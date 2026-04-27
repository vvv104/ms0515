#!/usr/bin/env python3
"""
Intel MCS-48 (8035/8048) disassembler.

Coverage matches what i8035.c implements (commits 1..5 of phase 1):
all standard MCS-48 opcodes except ENT0 CLK and the documented-undefined
slots.  Output goes to stdout.

Usage: dasm8048.py rom.bin [start_addr]
       dasm8048.py rom.bin --range 0x100-0x200
"""
import sys
import argparse


# ── Single-byte opcodes that don't take an operand ────────────────────────
SIMPLE = {
    0x00: "NOP",
    0x02: "OUTL BUS,A",
    0x03: None,                # ADD A,#imm  — handled below
    0x05: "EN I",
    0x07: "DEC A",
    0x08: "INS A,BUS",
    0x09: "IN A,P1",
    0x0A: "IN A,P2",
    0x0F: "MOVD A,P7",
    0x0C: "MOVD A,P4",
    0x0D: "MOVD A,P5",
    0x0E: "MOVD A,P6",
    0x10: "INC @R0",
    0x11: "INC @R1",
    0x15: "DIS I",
    0x17: "INC A",
    0x25: "EN TCNTI",
    0x27: "CLR A",
    0x35: "DIS TCNTI",
    0x37: "CPL A",
    0x39: "OUTL P1,A",
    0x3A: "OUTL P2,A",
    0x3C: "MOVD P4,A",
    0x3D: "MOVD P5,A",
    0x3E: "MOVD P6,A",
    0x3F: "MOVD P7,A",
    0x42: "MOV A,T",
    0x45: "STRT CNT",
    0x47: "SWAP A",
    0x55: "STRT T",
    0x57: "DA A",
    0x62: "MOV T,A",
    0x65: "STOP TCNT",
    0x67: "RRC A",
    0x75: "ENT0 CLK",
    0x77: "RR A",
    0x83: "RET",
    0x85: "CLR F0",
    0x87: "CPL F0",
    0x93: "RETR",
    0x95: "CLR F1",
    0x97: "CLR C",
    0xA3: "MOVP A,@A",
    0xA5: "CPL F1",
    0xA7: "CPL C",
    0xB3: "JMPP @A",
    0xC5: "SEL RB0",
    0xC7: "MOV A,PSW",
    0xD5: "SEL RB1",
    0xD7: "MOV PSW,A",
    0xE3: "MOVP3 A,@A",
    0xE5: "SEL MB0",
    0xE7: "RL A",
    0xF5: "SEL MB1",
    0xF7: "RLC A",
    0x80: "MOVX A,@R0",
    0x81: "MOVX A,@R1",
    0x90: "MOVX @R0,A",
    0x91: "MOVX @R1,A",
}


def fmt_imm(b):
    return f"#{b:02XH}".replace("XH", "H")  # "#NNH" lowercase x→H


def fmt_addr(a):
    return f"{a:03XH}".replace("XH", "H")


def disasm_one(rom, pc):
    """Returns (mnemonic, length).  pc is the address of the opcode byte.
    rom is the full ROM blob; addresses past the end produce '???'."""
    if pc >= len(rom):
        return ("???", 0)
    op = rom[pc]
    nxt = rom[pc + 1] if pc + 1 < len(rom) else 0

    # ── ADD / ADDC immediate ─────────────────────────────────────────
    if op == 0x03:
        return (f"ADD A,#{nxt:02X}H", 2)
    if op == 0x13:
        return (f"ADDC A,#{nxt:02X}H", 2)
    if op == 0x23:
        return (f"MOV A,#{nxt:02X}H", 2)
    if op == 0x43:
        return (f"ORL A,#{nxt:02X}H", 2)
    if op == 0x53:
        return (f"ANL A,#{nxt:02X}H", 2)
    if op == 0xD3:
        return (f"XRL A,#{nxt:02X}H", 2)
    if op == 0x88:
        return (f"ORL BUS,#{nxt:02X}H", 2)
    if op == 0x89:
        return (f"ORL P1,#{nxt:02X}H", 2)
    if op == 0x8A:
        return (f"ORL P2,#{nxt:02X}H", 2)
    if op == 0x98:
        return (f"ANL BUS,#{nxt:02X}H", 2)
    if op == 0x99:
        return (f"ANL P1,#{nxt:02X}H", 2)
    if op == 0x9A:
        return (f"ANL P2,#{nxt:02X}H", 2)

    # ── ADD/ADDC/ANL/ORL/XRL A,Rn / @Rn ──────────────────────────────
    if 0x68 <= op <= 0x6F:
        return (f"ADD A,R{op & 7}", 1)
    if op in (0x60, 0x61):
        return (f"ADD A,@R{op & 1}", 1)
    if 0x78 <= op <= 0x7F:
        return (f"ADDC A,R{op & 7}", 1)
    if op in (0x70, 0x71):
        return (f"ADDC A,@R{op & 1}", 1)
    if 0x48 <= op <= 0x4F:
        return (f"ORL A,R{op & 7}", 1)
    if op in (0x40, 0x41):
        return (f"ORL A,@R{op & 1}", 1)
    if 0x58 <= op <= 0x5F:
        return (f"ANL A,R{op & 7}", 1)
    if op in (0x50, 0x51):
        return (f"ANL A,@R{op & 1}", 1)
    if 0xD8 <= op <= 0xDF:
        return (f"XRL A,R{op & 7}", 1)
    if op in (0xD0, 0xD1):
        return (f"XRL A,@R{op & 1}", 1)

    # ── MOV A,Rn / Rn,A / A,@Rn / @Rn,A ─────────────────────────────
    if 0xF8 <= op <= 0xFF:
        return (f"MOV A,R{op & 7}", 1)
    if op in (0xF0, 0xF1):
        return (f"MOV A,@R{op & 1}", 1)
    if 0xA8 <= op <= 0xAF:
        return (f"MOV R{op & 7},A", 1)
    if op in (0xA0, 0xA1):
        return (f"MOV @R{op & 1},A", 1)

    # ── MOV Rn,#imm / @Rn,#imm ──────────────────────────────────────
    if 0xB8 <= op <= 0xBF:
        return (f"MOV R{op & 7},#{nxt:02X}H", 2)
    if op in (0xB0, 0xB1):
        return (f"MOV @R{op & 1},#{nxt:02X}H", 2)

    # ── INC/DEC Rn ──────────────────────────────────────────────────
    if 0x18 <= op <= 0x1F:
        return (f"INC R{op & 7}", 1)
    if 0xC8 <= op <= 0xCF:
        return (f"DEC R{op & 7}", 1)

    # ── XCH ─────────────────────────────────────────────────────────
    if 0x28 <= op <= 0x2F:
        return (f"XCH A,R{op & 7}", 1)
    if op in (0x20, 0x21):
        return (f"XCH A,@R{op & 1}", 1)
    if op in (0x30, 0x31):
        return (f"XCHD A,@R{op & 1}", 1)

    # ── MOVD/ANLD/ORLD with port number ────────────────────────────
    if 0x8C <= op <= 0x8F:
        return (f"ORLD P{4 + (op & 3)},A", 1)
    if 0x9C <= op <= 0x9F:
        return (f"ANLD P{4 + (op & 3)},A", 1)

    # ── DJNZ Rn,addr ───────────────────────────────────────────────
    if 0xE8 <= op <= 0xEF:
        # 8-bit address inside current page
        page = pc & 0xF00
        target = page | nxt
        return (f"DJNZ R{op & 7},{target:03X}H", 2)

    # ── JBb addr (bit-test) ────────────────────────────────────────
    if op & 0x1F == 0x12:
        bit = (op >> 5) & 7
        page = pc & 0xF00
        target = page | nxt
        return (f"JB{bit} {target:03X}H", 2)

    # ── Conditional jumps with 8-bit page offset ───────────────────
    JMP_CC = {
        0x16: "JTF",
        0x26: "JNT0",
        0x36: "JT0",
        0x46: "JNT1",
        0x56: "JT1",
        0x76: "JF1",
        0x86: "JNI",
        0x96: "JNZ",
        0xB6: "JF0",
        0xC6: "JZ",
        0xE6: "JNC",
        0xF6: "JC",
    }
    if op in JMP_CC:
        page = pc & 0xF00
        target = page | nxt
        return (f"{JMP_CC[op]} {target:03X}H", 2)

    # ── Unconditional JMP / CALL with 11-bit address ───────────────
    if (op & 0x1F) == 0x04:        # JMP page
        page = ((op >> 5) & 7) << 8
        # MB bit selects upper 2 KB; we don't track MB here, just print
        target = page | nxt
        return (f"JMP {target:03X}H", 2)
    if (op & 0x1F) == 0x14:        # CALL page
        page = ((op >> 5) & 7) << 8
        target = page | nxt
        return (f"CALL {target:03X}H", 2)

    # ── Simple table fallback ──────────────────────────────────────
    if op in SIMPLE and SIMPLE[op] is not None:
        return (SIMPLE[op], 1)

    return (f"DB {op:02X}H", 1)


def disasm_range(rom, start, end):
    """Linear disassembly from `start` (inclusive) to `end` (exclusive).
    Out-of-range bytes are skipped silently."""
    pc = start
    while pc < end and pc < len(rom):
        mnem, length = disasm_one(rom, pc)
        if length == 0:
            break
        bytes_str = " ".join(f"{rom[pc + i]:02X}" for i in range(length)
                             if pc + i < len(rom))
        print(f"{pc:03X}: {bytes_str:<8} {mnem}")
        pc += length


def parse_range(s):
    """Parses 'a-b' (inclusive both ends) with hex or decimal endpoints."""
    if "-" in s:
        a, b = s.split("-", 1)
        return int(a, 0), int(b, 0) + 1
    a = int(s, 0)
    return a, a + 1


def main():
    ap = argparse.ArgumentParser(description="MCS-48 disassembler.")
    ap.add_argument("rom", help="ROM blob")
    ap.add_argument("--range", help="Address range a-b (inclusive)",
                    default=None)
    ap.add_argument("--start", type=lambda x: int(x, 0), default=0,
                    help="Start address (default 0)")
    args = ap.parse_args()

    with open(args.rom, "rb") as f:
        rom = f.read()

    if args.range:
        a, b = parse_range(args.range)
    else:
        a = args.start
        b = len(rom)
    disasm_range(rom, a, b)


if __name__ == "__main__":
    main()
