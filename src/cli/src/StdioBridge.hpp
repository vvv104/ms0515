/*
 * StdioBridge.hpp — host-stdin → MS-7004 keyboard bridge for ms0515-cli.
 *
 * The CLI's output is taken from VRAM via the VramMirror (set up in
 * main.cpp), so this module doesn't intercept .TTYIN/.TTYOUT/.PRINT —
 * the kernel routes those through its own TT.SYS, the resulting screen
 * draws land in VRAM, and VramMirror's hook-driven decoder turns them
 * into ANSI-positioned UTF-8 on stdout.
 *
 * What's left here is the input side: drain host stdin into a KOI-8
 * byte queue, then feed those bytes into the emulated keyboard as
 * key-press / release pairs.  The kernel echoes typed characters
 * through TT.SYS like any other output, so no host-side echo is
 * needed either.
 *
 * Singleton: the emu pointer has no user-data slot, so this module
 * owns process-global state.  main() calls install() once; pumpInput()
 * once per frame.
 */

#ifndef MS0515_CLI_STDIO_BRIDGE_HPP
#define MS0515_CLI_STDIO_BRIDGE_HPP

#include <ms0515/Emulator.hpp>

#include "HostKey.hpp"

#include <functional>

namespace ms0515::cli::bridge {

/* Remember the emu pointer for pumpInput(). */
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

/* Every key of the host goes to `sink` first; a key it returns true for
 * never reaches the guest.  The commander over the machine installs
 * itself here.  Ctrl-] (quit) is handled before the sink. */
void setHostKeySink(std::function<bool(const files::HostKey &)> sink);

/* A burst of the host's bytes (UTF-8, ESC sequences), as if read from
 * stdin - what pumpInput() does with the terminal's input; for the tests
 * and for text the host itself puts in.  At most 256 bytes a call. */
void feedHostBytes(const uint8_t *bytes, size_t n);

/* Keystrokes queued for the guest and not yet tapped out. */
[[nodiscard]] size_t pendingTaps();

}  /* namespace ms0515::cli::bridge */

#endif  /* MS0515_CLI_STDIO_BRIDGE_HPP */
