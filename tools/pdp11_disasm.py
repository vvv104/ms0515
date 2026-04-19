#!/usr/bin/env python3
"""
PDP-11 (T-11 / KR1807VM1) disassembler for MS0515 ROM images.

Produces output in the same format as the existing .lst files:
    AAAAAA: MNEMONIC  operands

All addresses and immediate values are in octal.
"""

import sys
import struct
from pathlib import Path


# Register names
REG = ['R0', 'R1', 'R2', 'R3', 'R4', 'R5', 'SP', 'PC']


class Disassembler:
    def __init__(self, data: bytes, base_addr: int):
        self.data = data
        self.base = base_addr
        self.pc = 0  # offset into data

    def _read_word(self) -> int:
        """Read next 16-bit word and advance PC."""
        if self.pc + 2 > len(self.data):
            return 0
        w = struct.unpack_from('<H', self.data, self.pc)[0]
        self.pc += 2
        return w

    def _peek_word(self, offset: int) -> int:
        if offset + 2 > len(self.data):
            return 0
        return struct.unpack_from('<H', self.data, offset)[0]

    def _cur_addr(self) -> int:
        return self.base + self.pc

    def _fmt_addr(self, addr: int) -> str:
        return f'{addr:06o}'

    def _fmt_word(self, val: int) -> str:
        return f'{val:06o}'

    def _fmt_byte(self, val: int) -> str:
        return f'{val:06o}'

    def _decode_operand(self, mode: int, reg: int, is_byte: bool = False) -> str:
        """Decode a 6-bit addressing mode field (3-bit mode + 3-bit register)."""
        if mode == 0:
            return REG[reg]
        elif mode == 1:
            return f'({REG[reg]})'
        elif mode == 2:
            if reg == 7:  # PC autoincrement = immediate
                val = self._read_word()
                return f'#{self._fmt_word(val)}'
            return f'({REG[reg]})+'
        elif mode == 3:
            if reg == 7:  # PC autoincrement deferred = absolute
                val = self._read_word()
                return f'@#{self._fmt_word(val)}'
            return f'@({REG[reg]})+'
        elif mode == 4:
            return f'-({REG[reg]})'
        elif mode == 5:
            return f'@-({REG[reg]})'
        elif mode == 6:
            offset = self._read_word()
            if reg == 7:  # PC index = relative
                # target = current PC + offset (PC already advanced past the offset word)
                target = (self._cur_addr() + offset) & 0xFFFF
                return self._fmt_addr(target)
            return f'{self._fmt_word(offset)}({REG[reg]})'
        elif mode == 7:
            offset = self._read_word()
            if reg == 7:  # PC index deferred = relative deferred
                target = (self._cur_addr() + offset) & 0xFFFF
                return f'@{self._fmt_addr(target)}'
            return f'@{self._fmt_word(offset)}({REG[reg]})'
        return '???'

    def _decode_branch_target(self, opcode: int) -> str:
        """Decode branch offset (signed 8-bit, word-aligned)."""
        offset = opcode & 0xFF
        if offset & 0x80:
            offset -= 256
        target = (self._cur_addr() + offset * 2) & 0xFFFF
        return self._fmt_addr(target)

    def disassemble_one(self) -> tuple:
        """Disassemble one instruction. Returns (address_str, mnemonic_str)."""
        addr = self._cur_addr()
        addr_str = self._fmt_addr(addr)

        if self.pc + 2 > len(self.data):
            return addr_str, None

        opcode = self._read_word()

        # Try to decode
        result = self._decode(opcode)
        return addr_str, result

    def _decode(self, op: int) -> str:
        """Decode a single opcode into a mnemonic string."""

        # ── Zero-operand instructions ──
        if op == 0o000000: return 'HALT'
        if op == 0o000001: return 'WAIT'
        if op == 0o000002: return 'RTI'
        if op == 0o000003: return 'BPT'
        if op == 0o000004: return 'IOT'
        if op == 0o000005: return 'RESET'
        if op == 0o000006: return 'RTT'
        if op == 0o000007: return 'MFPT'

        # ── RTS / RETURN ──
        if (op & 0o177770) == 0o000200:
            reg = op & 7
            if reg == 7:
                return 'RETURN'
            return f'RTS\t{REG[reg]}'

        # ── Condition code ops (000240-000277) ──
        if (op & 0o177740) == 0o000240:
            return self._decode_ccc_scc(op)

        # ── SWAB (000300) ──
        if (op & 0o177700) == 0o000300:
            dst_mode = (op >> 3) & 7
            dst_reg = op & 7
            operand = self._decode_operand(dst_mode, dst_reg)
            return f'SWAB\t{operand}'

        # ── Single-operand instructions ──
        # Top 10 bits (op >> 6) distinguish these.
        # Bit 15 set = byte variant (B suffix).
        top10 = (op >> 6) & 0o1777
        single_ops = {
            0o0050: 'CLR',   0o0051: 'COM',   0o0052: 'INC',
            0o0053: 'DEC',   0o0054: 'NEG',   0o0055: 'ADC',
            0o0056: 'SBC',   0o0057: 'TST',   0o0060: 'ROR',
            0o0061: 'ROL',   0o0062: 'ASR',   0o0063: 'ASL',
            0o0067: 'SXT',
            # Byte variants
            0o1050: 'CLRB',  0o1051: 'COMB',  0o1052: 'INCB',
            0o1053: 'DECB',  0o1054: 'NEGB',  0o1055: 'ADCB',
            0o1056: 'SBCB',  0o1057: 'TSTB',  0o1060: 'RORB',
            0o1061: 'ROLB',  0o1062: 'ASRB',  0o1063: 'ASLB',
            # MTPS / MFPS
            0o1064: 'MTPS',  0o1067: 'MFPS',
        }
        if top10 in single_ops:
            dst_mode = (op >> 3) & 7
            dst_reg = op & 7
            is_byte = (op & 0o100000) != 0
            operand = self._decode_operand(dst_mode, dst_reg, is_byte=is_byte)
            return f'{single_ops[top10]}\t{operand}'

        # ── JMP ──
        if (op & 0o177700) == 0o000100:
            dst_mode = (op >> 3) & 7
            dst_reg = op & 7
            if dst_mode == 0:
                return f'.WORD\t{self._fmt_word(op)}'  # JMP R is illegal
            operand = self._decode_operand(dst_mode, dst_reg)
            return f'JMP\t{operand}'

        # ── JSR / CALL ──
        if (op & 0o177000) == 0o004000:
            reg = (op >> 6) & 7
            dst_mode = (op >> 3) & 7
            dst_reg = op & 7
            operand = self._decode_operand(dst_mode, dst_reg)
            if reg == 7:
                return f'CALL\t{operand}'
            return f'JSR\t{REG[reg]}, {operand}'

        # ── EMT / TRAP ──
        if (op & 0o177400) == 0o104000:
            val = op & 0xFF
            return f'EMT\t{self._fmt_byte(val)}'
        if (op & 0o177400) == 0o104400:
            val = op & 0xFF
            return f'TRAP\t{self._fmt_byte(val)}'

        # ── SOB ──
        if (op & 0o177000) == 0o077000:
            reg = (op >> 6) & 7
            offset = op & 0o77
            target = (self._cur_addr() - offset * 2) & 0xFFFF
            return f'SOB\t{REG[reg]}, {self._fmt_addr(target)}'

        # ── Branch instructions ──
        branches = {
            0o000400: 'BR',   0o001000: 'BNE',  0o001400: 'BEQ',
            0o100000: 'BPL',  0o100400: 'BMI',  0o101000: 'BVC',
            0o101400: 'BVS',  0o102000: 'BCC',  0o102400: 'BCS',
            0o103000: 'BGE',  0o103400: 'BLT',  0o034000: 'BLE',
            0o003000: 'BGT',  0o101000: 'BVC',
        }
        # Better approach for branches
        br_code = op & 0o177400
        br_map = {
            0o000400: 'BR',
            0o001000: 'BNE',  0o001400: 'BEQ',
            0o002000: 'BGE',  0o002400: 'BLT',
            0o003000: 'BGT',  0o003400: 'BLE',
            0o100000: 'BPL',  0o100400: 'BMI',
            0o101000: 'BVC',  0o101400: 'BVS',
            0o102000: 'BHIS', 0o102400: 'BLO',
            0o103000: 'BHI',  0o103400: 'BLOS',
        }
        if br_code in br_map:
            target = self._decode_branch_target(op)
            return f'{br_map[br_code]}\t{target}'

        # ── Double-operand word ──
        dbl_word = {
            0o01: 'MOV',  0o02: 'CMP',  0o03: 'BIT',
            0o04: 'BIC',  0o05: 'BIS',  0o06: 'ADD',
        }
        top4 = (op >> 12) & 0xF
        if top4 in dbl_word:
            src_mode = (op >> 9) & 7
            src_reg = (op >> 6) & 7
            dst_mode = (op >> 3) & 7
            dst_reg = op & 7
            src = self._decode_operand(src_mode, src_reg)
            dst = self._decode_operand(dst_mode, dst_reg)
            return f'{dbl_word[top4]}\t{src}, {dst}'

        # ── Double-operand byte ──
        dbl_byte = {
            0o11: 'MOVB',  0o12: 'CMPB',  0o13: 'BITB',
            0o14: 'BICB',  0o15: 'BISB',
        }
        if top4 in dbl_byte:
            src_mode = (op >> 9) & 7
            src_reg = (op >> 6) & 7
            dst_mode = (op >> 3) & 7
            dst_reg = op & 7
            src = self._decode_operand(src_mode, src_reg, is_byte=True)
            dst = self._decode_operand(dst_mode, dst_reg, is_byte=True)
            return f'{dbl_byte[top4]}\t{src}, {dst}'

        # ── SUB ──
        if top4 == 0o16:
            src_mode = (op >> 9) & 7
            src_reg = (op >> 6) & 7
            dst_mode = (op >> 3) & 7
            dst_reg = op & 7
            src = self._decode_operand(src_mode, src_reg)
            dst = self._decode_operand(dst_mode, dst_reg)
            return f'SUB\t{src}, {dst}'

        # ── XOR ──
        if (op & 0o177000) == 0o074000:
            reg = (op >> 6) & 7
            dst_mode = (op >> 3) & 7
            dst_reg = op & 7
            operand = self._decode_operand(dst_mode, dst_reg)
            return f'XOR\t{REG[reg]}, {operand}'

        # ── Unknown — emit as .WORD ──
        return f'.WORD\t{self._fmt_word(op)}'

    def _decode_ccc_scc(self, op: int) -> str:
        """Decode condition code set/clear instructions."""
        is_set = (op & 0o000020) != 0
        bits = op & 0xF
        if bits == 0:
            return 'NOP'

        prefix = 'SE' if is_set else 'CL'

        # Full set/clear all
        if bits == 0xF:
            return 'SCC' if is_set else 'CCC'

        # Individual flag mnemonics
        flags = ''
        if bits & 1: flags += f'{prefix}C '
        if bits & 2: flags += f'{prefix}V '
        if bits & 4: flags += f'{prefix}Z '
        if bits & 8: flags += f'{prefix}N '

        # Single flag — use standard abbreviation
        flag_count = bin(bits).count('1')
        if flag_count == 1:
            if bits == 1: return f'{prefix}C'
            if bits == 2: return f'{prefix}V'
            if bits == 4: return f'{prefix}Z'
            if bits == 8: return f'{prefix}N'

        # Multiple flags combined
        return flags.strip()

    def disassemble_all(self) -> list:
        """Disassemble the entire ROM. Returns list of (addr, text) tuples."""
        lines = []
        while self.pc < len(self.data):
            addr_str, text = self.disassemble_one()
            if text is None:
                break
            lines.append((addr_str, text))
        return lines


def disassemble_rom(rom_path: str, base_addr: int) -> str:
    """Disassemble a ROM file and return the listing as a string."""
    data = Path(rom_path).read_bytes()
    dis = Disassembler(data, base_addr)
    lines = dis.disassemble_all()

    result = []
    for addr, text in lines:
        result.append(f'{addr}: {text}')
    return '\n'.join(result)


def main():
    if len(sys.argv) < 2:
        # Default: disassemble both MS0515 ROMs
        ref_dir = Path(__file__).parent.parent / 'reference' / 'docs'

        for name in ['ms0515-roma', 'ms0515-romb']:
            rom_path = ref_dir / f'{name}.rom'
            if not rom_path.exists():
                print(f'ROM not found: {rom_path}', file=sys.stderr)
                continue

            # ROM is 16 KB, mapped at 160000-177777 in address space
            # But only upper 8 KB (160000-177377) is normally visible
            # Full ROM starts at 140000 when extended
            base_addr = 0o140000  # Full 16 KB from 140000

            listing = disassemble_rom(str(rom_path), base_addr)
            out_path = ref_dir / f'{name}.disasm.lst'
            out_path.write_text(listing, encoding='utf-8')
            print(f'Wrote {out_path} ({len(listing)} bytes)')
    else:
        rom_path = sys.argv[1]
        base = int(sys.argv[2], 8) if len(sys.argv) > 2 else 0o140000
        print(disassemble_rom(rom_path, base))


if __name__ == '__main__':
    main()
