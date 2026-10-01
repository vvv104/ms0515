/*
 * MonitorWatch.hpp - how ms0515-run knows the program is over.
 *
 * A program ends in more ways than .EXIT: the monitor stops it for a
 * trap, the user presses CTRL/C twice, or RUN never starts it (no memory,
 * not a program).  All of them end the same way - KMON comes back and
 * prints its prompt.  The watch sits on the processor's programmed
 * requests (Emulator::setTrapThunk) and sees that one: a .PRINT of KMON's
 * dot while the monitor's own KMONIN says KMON has control.  It keeps the
 * dot off the screen, since nobody is going to type after it; everything
 * else goes to the monitor untouched.
 *
 * At that moment it also reads the monitor's own verdict on the program:
 * the user error byte, which utilities and the monitor's error paths set
 * and KMON checks to decide whether an indirect file may go on.
 *
 * The thunk is a plain function with no context, so the watch is one per
 * process - as is the machine ms0515-run runs.
 */

#ifndef MS0515_RUN_MONITORWATCH_HPP
#define MS0515_RUN_MONITORWATCH_HPP

#include <ms0515/Emulator.hpp>

namespace ms0515::run {

/* Start watching `emu`, with nothing seen yet and no error stored. */
void installMonitorWatch(ms0515::Emulator &emu);

/* KMON has come to its prompt since the watch was installed. */
[[nodiscard]] bool monitorPrompted() noexcept;

/* The severities of every error since the watch was installed: the
 * monitor's stored error byte (EXTIND, RMON+416), into which KMON gathers
 * the user error byte (USERRB, 53) each time it gets control back. */
[[nodiscard]] uint8_t endSeverity() noexcept;

/* USERRB's bits that mean the program did not end well. */
inline constexpr uint8_t kSeverityError = 004;
inline constexpr uint8_t kSeverityFatal = 010;
inline constexpr uint8_t kSeverityUnconditional = 020;

} /* namespace ms0515::run */

#endif /* MS0515_RUN_MONITORWATCH_HPP */
