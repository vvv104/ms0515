/*
 * GdbStub.hpp — GDB Remote Serial Protocol server for the MS0515 emulator.
 *
 * Implements the minimum subset of GDB RSP needed for source-level debugging
 * of PDP-11 code with `target remote`:
 *
 *   ?              query halt reason
 *   g / G          read / write all general registers
 *   p / P          read / write a single register
 *   m / M          read / write memory
 *   s              single step
 *   c              continue
 *   Z0 / z0        insert / remove software breakpoint
 *   k              kill (disconnect)
 *   D              detach
 *   qSupported     capability negotiation
 *   qAttached      report attached state
 *
 * Register layout follows GDB's pdp11 target description:
 *   index  size  name
 *     0    2     r0
 *     1    2     r1
 *     2    2     r2
 *     3    2     r3
 *     4    2     r4
 *     5    2     r5
 *     6    2     sp
 *     7    2     pc
 *     8    2     ps
 *
 * The class is split into protocol logic (processPacket) and transport
 * (serve).  The protocol logic is pure and unit-testable; serve() runs
 * a blocking TCP loop using BSD/Winsock and is only compiled on platforms
 * where it's available.
 */

#ifndef MS0515_GDB_STUB_HPP
#define MS0515_GDB_STUB_HPP

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

namespace ms0515 {

class Debugger;

class GdbStub {
public:
    explicit GdbStub(Debugger &dbg);

    /* Process one RSP packet payload (without the surrounding $...#xx
     * framing) and return the reply payload.  Returns an empty string
     * to indicate "no reply" (the caller still sends an empty packet
     * "$#00", which is the GDB "unsupported" marker). */
    [[nodiscard]] std::string processPacket(std::string_view payload);

    [[nodiscard]] bool serve(int port);

    void stop() noexcept { stopRequested_.store(true); }

    [[nodiscard]] bool wasKilled() const noexcept { return killed_; }

private:
    /* ── Helpers used by processPacket ──────────────────────────────── */
    std::string handleHaltReason();
    std::string handleReadRegisters();
    std::string handleWriteRegisters(std::string_view body);
    std::string handleReadRegister(std::string_view body);
    std::string handleWriteRegister(std::string_view body);
    std::string handleReadMemory(std::string_view body);
    std::string handleWriteMemory(std::string_view body);
    std::string handleStep();
    std::string handleContinue();
    std::string handleInsertBreakpoint(std::string_view body);
    std::string handleRemoveBreakpoint(std::string_view body);
    std::string handleQuery(std::string_view body);

    /* Format a GDB "T" stop-reply packet from the debugger's last stop. */
    std::string makeStopReply();

    Debugger         &dbg_;
    std::atomic<bool> stopRequested_{false};
    bool              killed_ = false;
};

} /* namespace ms0515 */

#endif /* MS0515_GDB_STUB_HPP */
