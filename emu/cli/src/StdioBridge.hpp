/*
 * StdioBridge.hpp — wires host stdin/stdout to the guest kernel via
 * the cpu->trap_thunk hook.
 *
 * Intercepts exactly three EMTs:
 *   EMT 0o340 (.TTYIN)  — pop a byte from the stdin queue into R0
 *   EMT 0o341 (.TTYOUT) — write R0's low byte to stdout (KOI-8 → UTF-8)
 *   EMT 0o351 (.PRINT)  — walk an ASCIZ string at R0 to stdout
 *
 * Every other EMT/TRAP/IOT/BPT is left for the standard kernel handler
 * (the thunk returns false in those cases, so cpu_service_interrupt()
 * runs as usual).
 *
 * Singleton: the trap_thunk has no user-data slot, so this module
 * owns process-global state (the stdin queue + whether the bridge is
 * active).  main() calls install() once; readBytesFromHost() should be
 * called once per frame to pump host stdin into the queue.
 */

#ifndef MS0515_CLI_STDIO_BRIDGE_HPP
#define MS0515_CLI_STDIO_BRIDGE_HPP

#include <ms0515/Emulator.hpp>

namespace ms0515::cli::bridge {

/* Install the trap_thunk on `emu` and remember the emu pointer so the
 * stdin pump can drive emu.keyPress() between frames. */
void install(ms0515::Emulator &emu);

/* Pump host stdin → MS-7004 keypress sequence.  Called once per
 * frame from main.  Reads available bytes, converts UTF-8 → KOI-8R,
 * queues them as keystrokes, and feeds one keystroke into the
 * MS-7004 emulation per few frames (down → release timing).
 *
 * Input goes through the keyboard hardware path because the Omega
 * monitor reads commands from MS-7004 / i8251 (the hardware ISR fills
 * the TT.SYS buffer); our EMT 0o340 hook would only catch direct
 * user-program polling, which the monitor doesn't use. */
void pumpInput();

/* When the MS0515_CLI_EMT_TRACE env var is set, write a per-EMT
 * histogram to stderr.  No-op otherwise. */
void dumpEmtCounts();

}  /* namespace ms0515::cli::bridge */

#endif  /* MS0515_CLI_STDIO_BRIDGE_HPP */
