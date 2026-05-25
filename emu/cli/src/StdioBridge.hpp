/*
 * StdioBridge.hpp — host-stdin → MS-7004 keyboard bridge for ms0515-cli.
 *
 * The CLI's output is taken from VRAM via the Terminal class (set up
 * in main.cpp), so this module no longer intercepts .TTYIN/.TTYOUT/
 * .PRINT — the kernel routes those through its own TT.SYS, and the
 * resulting screen draws land in VRAM where the Terminal mirror picks
 * them up.
 *
 * What's left here is the input side: drain host stdin into a KOI-8
 * byte queue, then feed those bytes into the emulated keyboard as
 * key-press / release pairs.  The kernel echoes typed characters
 * through TT.SYS like any other output, so no host-side echo is
 * needed either.
 *
 * Singleton: the trap_thunk and emu pointer have no user-data slot,
 * so this module owns process-global state.  main() calls install()
 * once; pumpInput() once per frame.
 */

#ifndef MS0515_CLI_STDIO_BRIDGE_HPP
#define MS0515_CLI_STDIO_BRIDGE_HPP

#include <ms0515/Emulator.hpp>

namespace ms0515::cli::bridge {

/* Install the (diagnostic-only) trap_thunk on `emu` and remember the
 * emu pointer for pumpInput(). */
void install(ms0515::Emulator &emu);

/* Pump host stdin → MS-7004 keypress sequence.  Reads available bytes,
 * converts UTF-8 → KOI-8R, queues them as keystrokes, then feeds one
 * keystroke into the emulated keyboard per few frames.  Input flows
 * through the keyboard hardware path (i8251 ISR fills TT.SYS buffer);
 * a direct .TTYIN hook would short-circuit that.
 *
 * Injection is gated by a "kernel is ready" flag — main.cpp watches
 * the VramMirror's idle counter and calls setInputReady(true) once
 * the kernel is parked at a prompt.  Before that, typed bytes are
 * still queued but not delivered, so a user typing during boot
 * sees their input land once the OS is ready. */
void pumpInput();

void setInputReady(bool ready);

/* Enable a histogram of EMT request counts (printed by dumpEmtCounts()
 * at exit) and a per-byte trace of .TTYOUT to stderr.  Diagnostic only;
 * the CLI's --emt-trace flag turns it on. */
void setEmtTrace(bool enabled);

/* Print the EMT histogram if tracing is enabled.  No-op otherwise. */
void dumpEmtCounts();

}  /* namespace ms0515::cli::bridge */

#endif  /* MS0515_CLI_STDIO_BRIDGE_HPP */
