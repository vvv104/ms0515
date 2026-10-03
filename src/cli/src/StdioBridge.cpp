/*
 * StdioBridge.cpp — host stdin → MS-7004 keyboard bridge.
 *
 * Output is handled in main.cpp via VramMirror::setOutput(stdout); this
 * module only feeds keystrokes.
 */

#include "StdioBridge.hpp"

#include "Platform.hpp"

#include "HostKey.hpp"

#include <ms0515/KeyboardLayout.hpp>
#include <ms0515/Koi8.hpp>
#include <ms0515/Typist.hpp>

#include <array>
#include <cstdio>
#include <functional>

namespace ms0515::cli::bridge {

namespace {

/* The keys queued for the guest and their tapping (lib's Typist: the
 * character-to-key mapping, РУС/ЛАТ, hold and gap). */
ms0515::Typist g_typist;

/* Active emulator pointer — pumpInput() calls emu.keyPress(). */
ms0515::Emulator *g_emu = nullptr;

/* Keystroke injection gate.  Pressing keys before the kernel finishes
 * its boot sequence (ROM POST → OS banner → command prompt) can cause
 * Mihin in particular to abort and restart, so the bridge holds off
 * injection until main signals "kernel is ready" — typically by
 * observing VramMirror's idle counter passing a threshold. */
bool g_inputReady = false;

/* Leftover UTF-8 bytes from the previous host read that didn't form
 * a complete code-point yet. */
std::array<uint8_t, 4> g_utf8Pending{};
size_t                 g_utf8PendingLen = 0;

/* The terminal's bytes become keys here; each key goes to the sink
 * first (the commander over the machine, when it is up) and to the
 * guest otherwise. */
files::KeyParser g_parser;
std::function<bool(const files::HostKey &)> g_sink;

/* The MS-7004 key behind a special key of the host, or None: the
 * machine has no Insert, Home, End, PgUp, PgDn - those are dropped. */
ms0515::Key specialToKey(files::SpecialKey s)
{
    using Key = ms0515::Key;
    using S = files::SpecialKey;
    switch (s) {
    case S::up:    return Key::Up;
    case S::down:  return Key::Down;
    case S::left:  return Key::Left;
    case S::right: return Key::Right;
    case S::f1:  return Key::F1;   case S::f2:  return Key::F2;   case S::f3:  return Key::F3;
    case S::f4:  return Key::F4;   case S::f5:  return Key::F5;   case S::f6:  return Key::F6;
    case S::f7:  return Key::F7;   case S::f8:  return Key::F8;   case S::f9:  return Key::F9;
    case S::f10: return Key::F10;  case S::f11: return Key::F11;  case S::f12: return Key::F12;
    default:     return Key::None;
    }
}

void enqueueGuest(const files::HostKey &k)
{
    if (k.isByte()) { g_typist.type(k.byte); return; }
    /* Shift with an F-key is one of the keys a PC has no cap for: the
     * PF keys, Help, Perform, F13..F20 (shiftedFunctionKey). */
    if (k.shift) { g_typist.type(ms0515::shiftedFunctionKey(files::functionNumber(k.special))); return; }
    g_typist.type(specialToKey(k.special));
}

void dispatch(const files::HostKey &k)
{
    /* Ctrl-]  (ASCII 0x1D) is the CLI's quit escape - matching the
     * familiar telnet escape character.  RT-11 has no use for it, so
     * intercepting it here doesn't take anything away from the guest.
     * The signal is delivered via the Platform shouldQuit() flag the
     * main loop already polls. */
    if (k.isByte() && k.byte == 0x1Du) {
        cli::requestQuit();
        return;
    }
    /* The commander's F-keys are the same with Shift as without, as they
     * were before Shift was told; only the machine reads it. */
    files::HostKey plain = k;
    plain.shift = false;
    if (g_sink && g_sink(plain)) return;
    enqueueGuest(k);
}

void feedKoi8(uint8_t b)
{
    for (const auto &k : g_parser.feed(b)) dispatch(k);
}

void readBytesFromHost()
{
    std::array<uint8_t, 256> buf{};
    const size_t n = cli::readStdinNonBlocking(buf.data(), buf.size());
    if (n != 0) feedHostBytes(buf.data(), n);
}

}  /* namespace */

void feedHostBytes(const uint8_t *bytes, size_t n)
{
    if (n == 0 || n > 256) return;
    /* Prepend any leftover UTF-8 bytes from the previous read. */
    std::array<uint8_t, 256 + 4> work{};
    size_t workLen = 0;
    for (size_t i = 0; i < g_utf8PendingLen; ++i) work[workLen++] = g_utf8Pending[i];
    for (size_t i = 0; i < n; ++i) work[workLen++] = bytes[i];
    g_utf8PendingLen = 0;

    size_t off = 0;
    while (off < workLen) {
        uint8_t k = 0;
        size_t consumed = koi8::utf8ToKoi8(work.data() + off, workLen - off, &k);
        if (consumed == 0) {
            for (size_t i = 0; i < workLen - off && i < g_utf8Pending.size(); ++i) {
                g_utf8Pending[i] = work[off + i];
            }
            g_utf8PendingLen = workLen - off;
            break;
        }
        feedKoi8(k);
        off += consumed;
    }
    /* the burst is over: an ESC that ended it was the Esc key */
    for (const auto &k : g_parser.flush()) dispatch(k);
}

void typeToGuest(const std::string &koi8)
{
    for (const char c : koi8) enqueueGuest(files::HostKey::ofByte(static_cast<uint8_t>(c)));
}

size_t pendingTaps()
{
    return g_typist.pending();
}

void install(ms0515::Emulator &emu)
{
    g_emu = &emu;
}

void setInputReady(bool ready)
{
    g_inputReady = ready;
}

void setHostKeySink(std::function<bool(const files::HostKey &)> sink)
{
    g_sink = std::move(sink);
}

void pumpInput()
{
    if (g_emu == nullptr) return;

    readBytesFromHost();

    if (!g_inputReady) return;

    g_typist.pump(*g_emu);
}

}  /* namespace ms0515::cli::bridge */
