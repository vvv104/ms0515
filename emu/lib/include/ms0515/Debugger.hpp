/*
 * Debugger.hpp — Interactive debugger for the MS0515 emulator.
 *
 * Provides:
 *   - Address breakpoints (PC equality)
 *   - Single-step (one instruction)
 *   - Step-over (skip past JSR/CALL by inserting a temporary breakpoint)
 *   - Run-until-break (executes instructions until a breakpoint hits or
 *     a configurable cycle budget is exhausted)
 *   - Register/PSW inspection
 *   - Disassembly around the current PC
 *
 * The debugger does not own the Emulator — it holds a reference and
 * drives execution through Emulator::stepInstruction().
 */

#ifndef MS0515_DEBUGGER_HPP
#define MS0515_DEBUGGER_HPP

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "ms0515/Disassembler.hpp"

namespace ms0515 {

class Emulator;

enum class StopReason {
    None,           /* not stopped (still running)                          */
    Breakpoint,     /* PC matches a breakpoint                              */
    Watchpoint,     /* a watched memory location was accessed (future)      */
    Step,           /* requested single-step finished                       */
    Halted,         /* CPU executed HALT                                    */
    CycleLimit,     /* run budget reached without hitting a breakpoint     */
    Manual,         /* host called requestStop()                            */
};

class Debugger {
public:
    explicit Debugger(Emulator &emu);

    Emulator       &emulator()       noexcept { return emu_; }
    const Emulator &emulator() const noexcept { return emu_; }

    /* ── Breakpoints ────────────────────────────────────────────────────── */
    void addBreakpoint(uint16_t address);
    void removeBreakpoint(uint16_t address);
    void clearBreakpoints();
    [[nodiscard]] bool hasBreakpoint(uint16_t address) const;
    [[nodiscard]] const std::unordered_set<uint16_t> &breakpoints() const noexcept
    { return breakpoints_; }

    /* ── Execution control ──────────────────────────────────────────────── */

    StopReason stepInstruction();
    StopReason stepOver();
    StopReason run(int maxInstructions = 0);

    void requestStop() noexcept { stopRequested_ = true; }

    void reset();

    /* ── Inspection ─────────────────────────────────────────────────────── */

    [[nodiscard]] StopReason lastStopReason()  const noexcept { return lastStop_; }
    [[nodiscard]] uint16_t   lastStopAddress() const noexcept { return lastStopAddr_; }

    [[nodiscard]] std::vector<DisassembledInstruction>
    disassemble(uint16_t address, int count) const;

    [[nodiscard]] std::vector<DisassembledInstruction>
    disassembleAtPc(int count) const;

    [[nodiscard]] std::string formatRegisters() const;

private:
    bool checkBreakpoint();

    Emulator &emu_;
    std::unordered_set<uint16_t> breakpoints_;
    bool        stopRequested_ = false;
    StopReason  lastStop_      = StopReason::None;
    uint16_t    lastStopAddr_  = 0;
};

} /* namespace ms0515 */

#endif /* MS0515_DEBUGGER_HPP */
