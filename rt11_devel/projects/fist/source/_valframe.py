"""Validate frame_9745 (the exact per-frame orchestrator) against the Z80 sim:
run the real game to a $9745 call, snapshot entry, run the sim through $9745
recording the $A3FF (RNG) stream and capturing the exit memory, then run
frame_9745 on the snapshot with that stream and compare the watched cells."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
import gamelogic_ref as ref
from trace_sprites import build_sim, PC
SP = 12

WATCH = (ref.FIGHTER_WATCH
         + list(range(0xC41B, 0xC43C))            # bf13 render area + bbox
         + [0xC409, 0xC40A, 0xC40F, 0xC41A]       # bf13 merged dimensions
         + [0x9CA6, 0x9CA5, 0x9C2B, 0x9C2C,       # timer
            0xAA08, 0xAA48, 0xA06F, 0xA070, 0xA071, 0xA072,
            0xB150, 0xAA3F])                       # hit
WATCH = sorted(set(WATCH))


def measure_one(skip):
    sim, mem = build_sim(watch=(0, 0))
    regs, memory, ops = sim.registers, sim.memory, sim.opcodes
    fd, ia = sim.frame_duration, sim.int_active
    seen = 0
    for _ in range(8000000):
        if regs[PC] == 0x9745:
            if memory[0x9C2C] == 2:        # skip round-end frames (not modelled)
                pass
            elif seen >= skip:
                break
            seen += 1
        cur = regs[PC]
        ops[memory[cur]]()
        if regs[26] and regs[25] % fd < ia:
            sim.accept_interrupt(regs, memory, regs[PC])
    else:
        return None
    s0 = regs[SP]
    ret = memory[s0] | (memory[s0 + 1] << 8)
    snap = bytearray(memory)
    randoms = []
    for _ in range(400000):
        cur = regs[PC]
        ops[memory[cur]]()
        if cur == 0xA3FF:
            randoms.append(regs[0])
        if regs[26] and regs[25] % fd < ia:
            sim.accept_interrupt(regs, memory, regs[PC])
        if regs[PC] == ret and regs[SP] == s0 + 2:
            break
    exit_mem = bytes(memory)
    mm = bytearray(snap)
    try:
        ref.frame_9745(mm, list(randoms))
    except NotImplementedError as e:
        return ("SKIP", str(e))
    mism = [a for a in WATCH if mm[a] != exit_mem[a]]
    return (len(WATCH) - len(mism), len(WATCH), mism, exit_mem, mm)


def main():
    total_ok = 0
    total = 0
    for k in range(6):
        r = measure_one(k)
        if r is None:
            print(f"frame #{k}: no call captured")
            continue
        if r[0] == "SKIP":
            print(f"frame #{k}: {r[1]}")
            continue
        ok, n, mism, ex, mm = r
        total_ok += ok
        total += n
        det = ""
        if mism:
            det = "  MISMATCH @ " + ",".join(
                f"{a:04X}(sim={ex[a]:02X} ref={mm[a]:02X})" for a in mism[:8])
        print(f"frame #{k}: {ok}/{n} watched cells match{det}")
    print(f"\nTOTAL: {total_ok}/{total} cells match"
          + ("  ALL GREEN" if total_ok == total and total else ""))


if __name__ == "__main__":
    main()
