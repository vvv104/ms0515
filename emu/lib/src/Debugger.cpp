/*
 * Debugger.cpp — Implementation of the interactive debugger.
 *
 * Run-loop strategy: we step the CPU one instruction at a time and check
 * the new PC against the breakpoint set after each step.  This is slower
 * than letting the core run a whole frame, but gives the debugger exact
 * control over where execution halts.
 */

#include "ms0515/Debugger.hpp"
#include "ms0515/Emulator.hpp"

#include <format>

namespace ms0515 {

Debugger::Debugger(Emulator &emu)
    : emu_(emu)
{
}

/* ── Breakpoints ────────────────────────────────────────────────────────── */

void Debugger::addBreakpoint(uint16_t address)
{
    breakpoints_.insert(address);
}

void Debugger::removeBreakpoint(uint16_t address)
{
    breakpoints_.erase(address);
}

void Debugger::clearBreakpoints()
{
    breakpoints_.clear();
}

bool Debugger::hasBreakpoint(uint16_t address) const
{
    return breakpoints_.contains(address);
}

/* ── Internal helpers ───────────────────────────────────────────────────── */

bool Debugger::checkBreakpoint()
{
    uint16_t pc = emu_.cpu().r[CPU_REG_PC];
    if (breakpoints_.contains(pc)) {
        lastStop_     = StopReason::Breakpoint;
        lastStopAddr_ = pc;
        return true;
    }
    return false;
}

/* ── Execution control ──────────────────────────────────────────────────── */

StopReason Debugger::stepInstruction()
{
    if (emu_.cpu().halted) {
        lastStop_     = StopReason::Halted;
        lastStopAddr_ = emu_.cpu().r[CPU_REG_PC];
        return lastStop_;
    }

    emu_.stepInstruction();

    if (emu_.cpu().halted) {
        lastStop_     = StopReason::Halted;
        lastStopAddr_ = emu_.cpu().r[CPU_REG_PC];
        return lastStop_;
    }

    lastStop_     = StopReason::Step;
    lastStopAddr_ = emu_.cpu().r[CPU_REG_PC];
    return lastStop_;
}

StopReason Debugger::stepOver()
{
    uint16_t pc      = emu_.cpu().r[CPU_REG_PC];
    auto     decoded = Disassembler::decode(pc, emu_);
    bool     isCall  = (decoded.mnemonic == "JSR")  ||
                       (decoded.mnemonic == "CALL") ||
                       (decoded.mnemonic == "EMT")  ||
                       (decoded.mnemonic == "TRAP");

    if (!isCall)
        return stepInstruction();

    uint16_t returnAddr = static_cast<uint16_t>(pc + decoded.length);
    bool     hadBp      = hasBreakpoint(returnAddr);
    if (!hadBp)
        addBreakpoint(returnAddr);

    StopReason reason = run(/*maxInstructions=*/0);

    if (!hadBp)
        removeBreakpoint(returnAddr);
    return reason;
}

StopReason Debugger::run(int maxInstructions)
{
    stopRequested_ = false;

    int executed = 0;
    while (true) {
        if (stopRequested_) {
            lastStop_     = StopReason::Manual;
            lastStopAddr_ = emu_.cpu().r[CPU_REG_PC];
            return lastStop_;
        }
        if (emu_.cpu().halted) {
            lastStop_     = StopReason::Halted;
            lastStopAddr_ = emu_.cpu().r[CPU_REG_PC];
            return lastStop_;
        }

        emu_.stepInstruction();
        ++executed;

        if (checkBreakpoint())
            return lastStop_;

        if (maxInstructions > 0 && executed >= maxInstructions) {
            lastStop_     = StopReason::CycleLimit;
            lastStopAddr_ = emu_.cpu().r[CPU_REG_PC];
            return lastStop_;
        }
    }
}

void Debugger::reset()
{
    emu_.reset();
    stopRequested_ = false;
    lastStop_      = StopReason::None;
    lastStopAddr_  = 0;
}

/* ── Inspection ─────────────────────────────────────────────────────────── */

std::vector<DisassembledInstruction> Debugger::disassemble(uint16_t address,
                                                           int count) const
{
    std::vector<DisassembledInstruction> out;
    out.reserve(static_cast<std::size_t>(count));
    uint16_t addr = address;
    for (int i = 0; i < count; ++i) {
        auto inst = Disassembler::decode(addr, emu_);
        addr = static_cast<uint16_t>(addr + inst.length);
        out.push_back(std::move(inst));
    }
    return out;
}

std::vector<DisassembledInstruction> Debugger::disassembleAtPc(int count) const
{
    return disassemble(emu_.cpu().r[CPU_REG_PC], count);
}

std::string Debugger::formatRegisters() const
{
    const auto &cpu = emu_.cpu();
    return std::format(
        "R0={:06o} R1={:06o} R2={:06o} R3={:06o}\n"
        "R4={:06o} R5={:06o} SP={:06o} PC={:06o}\n"
        "PSW={:06o}  [{}{}{}{}{}]  pri={}\n",
        cpu.r[0], cpu.r[1], cpu.r[2], cpu.r[3],
        cpu.r[4], cpu.r[5], cpu.r[6], cpu.r[7],
        cpu.psw,
        (cpu.psw & CPU_PSW_N) ? 'N' : '-',
        (cpu.psw & CPU_PSW_Z) ? 'Z' : '-',
        (cpu.psw & CPU_PSW_V) ? 'V' : '-',
        (cpu.psw & CPU_PSW_C) ? 'C' : '-',
        (cpu.psw & CPU_PSW_T) ? 'T' : '-',
        (cpu.psw >> 5) & 7);
}

} /* namespace ms0515 */
