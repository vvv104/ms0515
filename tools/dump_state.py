#!/usr/bin/env python3
"""Dump key fields from an MS0515 state file (.ms0515)."""

import struct
import sys

def read_bytes(f, n):
    d = f.read(n)
    if len(d) < n:
        raise EOFError(f"expected {n} bytes, got {len(d)}")
    return d

def u8(f):  return struct.unpack('<B', read_bytes(f, 1))[0]
def u16(f): return struct.unpack('<H', read_bytes(f, 2))[0]
def u32(f): return struct.unpack('<I', read_bytes(f, 4))[0]
def i32(f): return struct.unpack('<i', read_bytes(f, 4))[0]
def boolv(f): return u8(f) != 0

def dump_state(path):
    with open(path, 'rb') as f:
        # Header
        magic = read_bytes(f, 4)
        assert magic == b'MS05', f"bad magic: {magic}"
        version = u16(f)
        flags = u16(f)
        rom_crc = u32(f)
        reserved = u32(f)
        print(f"=== Header: version={version} rom_crc={rom_crc:#010x} ===\n")

        cpu_regs = None
        ram = None

        while True:
            chunk_id = read_bytes(f, 4)
            chunk_size = u32(f)
            chunk_start = f.tell()

            if chunk_id == b'END\x00':
                break
            elif chunk_id == b'CPU\x00':
                regs = [u16(f) for _ in range(8)]
                psw = u16(f)
                insn = u16(f)
                insn_pc = u16(f)
                halted = boolv(f)
                waiting = boolv(f)
                cpu_regs = regs
                print("=== CPU ===")
                for i in range(8):
                    name = f"R{i}" if i < 6 else ("SP" if i == 6 else "PC")
                    print(f"  {name} = {regs[i]:#08o} ({regs[i]:#06x})")
                print(f"  PSW = {psw:#08o}")
                print(f"  instruction = {insn:#08o} at PC={insn_pc:#08o}")
                print(f"  halted={halted} waiting={waiting}")
                # skip rest of chunk
                f.seek(chunk_start + chunk_size)

            elif chunk_id == b'MEM\x00':
                dispatcher = u16(f)
                rom_ext = boolv(f)
                ram = read_bytes(f, 128 * 1024)  # MEM_RAM_SIZE
                vram = read_bytes(f, 16 * 1024)
                print(f"\n=== Memory ===")
                print(f"  dispatcher = {dispatcher:#08o} ({dispatcher:#06x})")
                print(f"  rom_extended = {rom_ext}")
                # Dump interesting RAM regions
                def dump_ram_region(name, start_addr, length):
                    """Dump RAM at a logical address (assuming primary banks)."""
                    print(f"\n  --- {name} (octal {start_addr:#08o}) ---")
                    # Primary bank: physical offset = address
                    for off in range(0, length, 2):
                        addr = start_addr + off
                        phys = addr  # primary bank assumption
                        if phys + 1 < len(ram):
                            w = ram[phys] | (ram[phys + 1] << 8)
                            if w != 0:
                                print(f"    {addr:#08o}: {w:#08o}")

                # Interrupt vectors 0-0400 from RAM
                print(f"\n  --- Interrupt vectors from RAM (non-zero) ---")
                for addr in range(0, 0o400, 2):
                    w = ram[addr] | (ram[addr + 1] << 8)
                    if w != 0:
                        print(f"    {addr:#08o}: {w:#08o}")

                # Interrupt vectors from VRAM (if VRAM overlay active)
                if dispatcher & 0x80:  # VRAM enabled
                    win = (dispatcher >> 10) & 3
                    print(f"\n  --- Interrupt vectors from VRAM (window={win}) ---")
                    for addr in range(0, 0o400, 2):
                        if addr < len(vram):
                            w = vram[addr] | (vram[addr + 1] << 8)
                            if w != 0:
                                print(f"    {addr:#08o}: {w:#08o} (VRAM)")

                # Also dump irq_virq state
                print(f"\n  --- Active IRQ flags (from CPU chunk) ---")
                # We'd need to re-parse CPU chunk for irq_virq

                # Area around 0o146700-0o147100 (IRQ handler)
                dump_ram_region("IRQ handler area", 0o146700, 0o400)

                # Area around 0o157000-0o160000 (JSR target)
                dump_ram_region("JSR target area (157000-160000)", 0o157000, 0o1000)

            elif chunk_id == b'BRD\x00':
                reg_a = u8(f)
                reg_b = u8(f)
                reg_c = u8(f)
                ppi_ctrl = u8(f)
                hires = boolv(f)
                border = u8(f)
                sound_on = boolv(f)
                print(f"\n=== Board ===")
                print(f"  reg_a = {reg_a:#04x} = {reg_a:#05o} (bin={reg_a:08b})")
                print(f"    drive={reg_a & 3} motor={'ON' if not (reg_a & 4) else 'off'} "
                      f"side={0 if (reg_a & 8) else 1} "
                      f"EROM={'YES' if (reg_a & 0x80) else 'no'}")
                print(f"  reg_b = {reg_b:#04x} ({reg_b:08b})")
                print(f"  reg_c = {reg_c:#04x} ({reg_c:08b})")
                print(f"  ppi_ctrl = {ppi_ctrl:#04x}")
                print(f"  hires={hires} border={border}")
                f.seek(chunk_start + chunk_size)

            elif chunk_id == b'FDC\x00':
                selected = i32(f)
                status = u8(f)
                command = u8(f)
                track = u8(f)
                sector = u8(f)
                data = u8(f)
                drq = boolv(f)
                intrq = boolv(f)
                busy = boolv(f)
                print(f"\n=== FDC ===")
                print(f"  selected={selected} track={track} sector={sector}")
                print(f"  status={status:#04x} command={command:#04x} data={data:#04x}")
                print(f"  drq={drq} intrq={intrq} busy={busy}")
                f.seek(chunk_start + chunk_size)

            elif chunk_id == b'KBD\x00':
                rx_data = u8(f)
                tx_data = u8(f)
                status = u8(f)
                mode = u8(f)
                command = u8(f)
                rx_ready = boolv(f)
                tx_ready = boolv(f)
                print(f"\n=== Keyboard USART ===")
                print(f"  rx_data={rx_data:#04x} tx_data={tx_data:#04x}")
                print(f"  status={status:#04x} mode={mode:#04x} command={command:#04x}")
                print(f"  rx_ready={rx_ready} tx_ready={tx_ready}")
                f.seek(chunk_start + chunk_size)

            elif chunk_id == b'HIST':
                version = u32(f)
                cap     = u32(f)
                head    = u32(f)
                _pad    = u32(f)
                written_lo = u32(f)
                written_hi = u32(f)
                written = written_lo | (written_hi << 32)
                print(f"\n=== History event ring ===")
                print(f"  version={version} cap={cap} head={head} "
                      f"written={written}  "
                      f"({'wrapped' if written > cap else 'linear'})")
                # Decide oldest index: same logic as the C walker
                if written < cap:
                    count = written
                    start = 0
                else:
                    count = cap
                    start = head
                # Read all events into memory so we can walk in order
                raw = read_bytes(f, cap * 16)
                events = []
                for i in range(cap):
                    rec = raw[i*16:(i+1)*16]
                    cycle = struct.unpack('<Q', rec[0:8])[0]
                    pc    = struct.unpack('<H', rec[8:10])[0]
                    kind  = rec[10]
                    dlen  = rec[11]
                    data  = rec[12:12+dlen]
                    events.append((cycle, pc, kind, data))
                kinds = {1: 'REG_A', 2: 'DISP', 3: 'FDC',
                         4: 'TRAP',  5: 'HALT', 6: 'MEMW', 7: 'MEMR',
                         8: 'PSW'}
                fdc_regs = {0: 'cmd', 1: 'track', 2: 'sector', 3: 'data'}
                for i in range(count):
                    idx = (start + i) % cap
                    cycle, pc, kind, data = events[idx]
                    label = kinds.get(kind, f'??{kind}')
                    if kind == 1 and len(data) >= 1:          # REG_A
                        v = data[0]
                        detail = (f"val={v:03o} drive={v & 3} "
                                  f"motor={'on' if not (v & 4) else 'off'} "
                                  f"side={0 if (v & 8) else 1} "
                                  f"erom={'y' if (v & 0x80) else 'n'}")
                    elif kind == 2 and len(data) >= 2:        # DISP
                        w = data[0] | (data[1] << 8)
                        detail = f"disp={w:06o}"
                    elif kind == 3 and len(data) >= 2:        # FDC
                        detail = (f"{fdc_regs.get(data[0], '?')}="
                                  f"{data[1]:03o}")
                    elif kind == 4 and len(data) >= 1:        # TRAP
                        detail = f"vec={data[0]:03o}"
                    elif (kind == 6 or kind == 7) and len(data) >= 3:  # MEMW/MEMR
                        op = '<-' if kind == 7 else '='
                        addr = data[0] | (data[1] << 8)
                        if len(data) >= 4:
                            val = data[2] | (data[3] << 8)
                            detail = f"@{addr:06o} {op} {val:06o} (word)"
                        else:
                            detail = f"@{addr:06o} {op} {data[2]:03o} (byte)"
                    elif kind == 8 and len(data) >= 2:        # PSW priority
                        detail = f"prio {data[1]} -> {data[0]}"
                    else:
                        detail = ''
                    print(f"  cycle={cycle:<10} PC={pc:06o}  "
                          f"{label:<5}  {detail}")
                # f position is already past the chunk body
            else:
                f.seek(chunk_start + chunk_size)

if __name__ == '__main__':
    path = sys.argv[1] if len(sys.argv) > 1 else 'state.ms0515'
    dump_state(path)
