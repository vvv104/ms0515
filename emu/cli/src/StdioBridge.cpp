/*
 * StdioBridge.cpp — singleton stdio bridge for ms0515-cli.
 */

#define _CRT_SECURE_NO_WARNINGS 1
#include "StdioBridge.hpp"

#include "Koi8.hpp"
#include "Platform.hpp"

extern "C" {
#include <ms0515/core/board.h>
#include <ms0515/core/cpu.h>
}

#include <array>
#include <cstdio>
#include <deque>
#include <string>

namespace ms0515::cli::bridge {

namespace {

/* Standard RT-11 programmed-request numbers we intercept.
 * .TTYIN is deliberately NOT in this list — the Omega monitor reads
 * its input via the MS-7004 hardware path (i8251 ISR → TT.SYS
 * buffer), and we feed that via emu.keyPress() in pumpInput().
 * Intercepting .TTYIN would short-circuit the kernel's TT driver
 * from ever seeing the buffered chars. */
constexpr uint8_t kEmtTtyout = 0341u;
constexpr uint8_t kEmtPrint  = 0351u;

/* Pending KOI-8R chars from host stdin, waiting to be tapped through
 * MS-7004 emulation as keypresses. */
std::deque<uint8_t> g_stdinQueue;

/* Active emulator pointer — pumpInput() needs to call emu.keyPress(). */
ms0515::Emulator *g_emu = nullptr;

/* True once the kernel has emitted at least one output byte via
 * .TTYOUT or .PRINT.  Used to gate keystroke injection — pressing
 * keys during the very early boot phase corrupts the boot sequence
 * (the kernel's MS-7004 ISR appears to interact badly with the
 * pre-init state).  Empirically, deferring keypress until the kernel
 * has spoken once makes typed input land in the monitor cleanly. */
bool g_kernelReady = false;

/* Optional per-EMT counter for diagnostics — toggled via the
 * MS0515_CLI_EMT_TRACE env var (any non-empty value enables). */
int g_emtCount[256] = {};
bool g_traceEmt = false;

/* Leftover UTF-8 bytes from the previous host read that didn't form
 * a complete code-point yet. */
std::array<uint8_t, 4> g_utf8Pending{};
size_t                 g_utf8PendingLen = 0;

/* Output state — KOI-7 shift mode + CR-followed-by handling.
 *
 * Bare CR (0x0D) has two meanings in the RT-11 / Mihin output stream:
 *   - When followed by LF or visible text: line separator (\r\n).
 *   - When followed by another CR or by a control sequence (ESC, BS):
 *     cursor reset only (just "\r", do NOT advance the line) — this
 *     is how the kernel positions the cursor for echo overwrite at
 *     the dot prompt.
 *
 * To decide which, we hold a pending CR until we see the next byte. */
bool g_koi7N2             = false;   /* SO has been seen, SI not yet */
bool g_pendingCr          = false;   /* last byte was CR; awaiting disambiguation */
bool g_lastWasNewline     = true;    /* last emitted byte completed a line break */
bool g_lastPrintWasPrompt = false;   /* last .PRINT emitted a single printable
                                      * char without a newline (dot prompt etc.);
                                      * suppress the NEXT empty .PRINT's auto-
                                      * newline so the cursor stays next to the
                                      * prompt char — matches RT-11 TT.SYS */
int  g_framesSinceOutput  = 0;       /* frames since the kernel last wrote a
                                      * byte — used to gate the echo timeout
                                      * (only decrement framesLeft while the
                                      * kernel is silent, so we don't time
                                      * out a typed char mid-kernel-echo). */
bool g_typingActive       = false;   /* once a keypress has been injected
                                      * and we still have queued chars or
                                      * an active tap, keep injecting back-
                                      * to-back; once nothing is left to
                                      * type we wait for the kernel to
                                      * surface the next prompt before
                                      * starting another typing burst. */

/* Echo tracker — see the larger comment in pumpInput's neighbourhood. */
struct ExpectedEcho {
    uint8_t ch;
    int     framesLeft;
};
std::deque<ExpectedEcho> g_echoQueue;
constexpr int kEchoTimeoutFrames = 8;

void writeBareCr() { cli::writeStdout("\r",   1); }
void writeCrLf()
{
    cli::writeStdout("\r\n", 2);
    g_lastWasNewline    = true;
    g_framesSinceOutput = 0;
}

/* Convert R0's low byte (KOI-8R or KOI-7 N2 depending on shift state)
 * to UTF-8 and write to stdout.  Handles ASCII control characters
 * SO/SI for KOI-7 shift, CR+LF pairing for single line breaks, and
 * a hex trace mode (MS0515_CLI_OUT_TRACE env var) for diagnostics. */
void emitGuestByte(uint8_t b)
{
    if (g_traceEmt) {
        std::fprintf(stderr, " <%03o>", b);
    }

    /* Echo-tracker match: if the kernel just sent us the byte we're
     * expecting back from a recent keystroke, consume the queue front.
     * Then we won't self-echo it on timeout.  Check before any
     * filtering so the match works regardless of what the byte is. */
    if (!g_echoQueue.empty() && g_echoQueue.front().ch == b) {
        g_echoQueue.pop_front();
    }

    /* KOI-7 mode toggles. */
    if (b == 0x0Eu) { g_koi7N2 = true;  return; }   /* SO */
    if (b == 0x0Fu) { g_koi7N2 = false; return; }   /* SI */

    /* NUL: silently consume.  Doesn't move cursor → leave g_pendingCr
     * alone, so a CR that immediately precedes a NUL-padded ESC
     * sequence is still classified as "CR for cursor positioning". */
    if (b == 0x00u) return;

    /* Resolve any pending bare CR based on the byte that follows. */
    if (g_pendingCr) {
        g_pendingCr = false;
        if (b == 0x0Au) {
            /* Classic CR + LF newline — emit once. */
            writeCrLf();
            return;
        }
        if (b == 0x0Du || b == 0x1Bu /*ESC*/ || b == 0x08u /*BS*/) {
            /* Cursor positioning sequence: the kernel uses bare CR
             * to fence ESC K (erase EOL) at the dot prompt — it
             * wants the cursor to STAY where it is (right after the
             * just-printed prompt char), not go back to column 0
             * where the ESC K would then erase the prompt itself.
             * So we DROP the pending CR entirely.  ESC K then erases
             * only what's beyond the cursor (typically nothing), the
             * prompt remains visible, and the next echoed char
             * appears next to it. */
            /* fall through to process `b`. */
        } else {
            /* CR followed by visible text — kernel-emitted line
             * separator.  Emit full newline. */
            writeCrLf();
        }
        /* Fall through and process `b` as the byte after the CR. */
    }

    if (b == 0x0Du) {
        /* Buffer this CR until we see the next byte. */
        g_pendingCr = true;
        return;
    }
    if (b == 0x0Au) {
        /* Bare LF — treat as full newline. */
        writeCrLf();
        return;
    }

    std::string utf8;
    if (g_koi7N2) {
        koi8::appendAsKoi7N2(utf8, b);
    } else {
        koi8::appendAsUtf8(utf8, b);
    }
    cli::writeStdout(utf8.data(), utf8.size());
    g_lastWasNewline    = false;
    g_framesSinceOutput = 0;
}

bool handleTtyout(ms0515_cpu_t *cpu)
{
    /* .TTYOUT — R0 low byte → terminal. */
    emitGuestByte(static_cast<uint8_t>(cpu->r[0] & 0xFFu));
    cpu->psw &= static_cast<uint16_t>(~CPU_PSW_C);
    g_kernelReady = true;
    return true;
}

bool handlePrint(ms0515_cpu_t *cpu)
{
    g_kernelReady = true;
    /* .PRINT — print ASCIZ at R0 to terminal.  Per RT-11 SSM §2.30:
     *   - byte == 0    → terminate, append CR + LF
     *   - byte & 0x80  → terminate, no CR + LF
     *   - else         → emit byte */
    uint16_t addr = cpu->r[0];
    /* Cap the walk so a bad pointer can't pin the cli forever. */
    constexpr int kMaxBytes = 16384;
    bool addNewline = true;
    int  printableCount = 0;
    for (int i = 0; i < kMaxBytes; ++i) {
        uint8_t b = board_read_byte(cpu->board, addr);
        addr = static_cast<uint16_t>(addr + 1u);
        if (b == 0) break;
        if (b & 0x80u) {
            /* Strip the high bit and emit the masked char, then stop
             * without appending a newline. */
            emitGuestByte(static_cast<uint8_t>(b & 0x7Fu));
            addNewline = false;
            break;
        }
        emitGuestByte(b);
        /* Count non-control payload chars: SSM .PRINT calls used as a
         * prompt typically have a single printable char (".", "*", ">",
         * "$", "?"); we suppress auto-CRLF for those so the cursor
         * stays right next to the prompt, matching real-RT-11 TT.SYS
         * behaviour. */
        if (b >= 0x20 && b != 0x7F) ++printableCount;
    }
    /* A "prompt" is a single printable char terminated with the
     * high-bit byte (addNewline=false) — that's how the RT-11 / Mihin
     * kernel signals "stay on this line, no auto-CRLF".  A NUL-
     * terminated single-char .PRINT (e.g. the "." that ends "161.")
     * is a message and needs the auto-CRLF for separation. */
    bool thisWasSinglePrompt = (printableCount == 1 && !addNewline);

    if (addNewline) {
        /* SSM auto-CRLF on NUL terminator.  Skip in two cases:
         *   1. The string already ended in a newline (embedded \r\n
         *      or bare CR + visible text) — don't double-space.
         *   2. The string was empty AND the previous .PRINT was a
         *      high-bit-terminated prompt char.  The kernel calls
         *      empty .PRINTs right after the dot prompt (buffer
         *      flush or some bookkeeping); suppressing their CRLF
         *      keeps the cursor on the prompt line. */
        if (g_pendingCr) g_pendingCr = false;
        bool skip = g_lastWasNewline
                 || (printableCount == 0 && g_lastPrintWasPrompt);
        if (!skip) writeCrLf();
    }

    /* Only update the prompt-flag when the .PRINT actually emitted
     * a payload char.  Empty .PRINT calls (printableCount == 0) are
     * the kernel's TT-buffer bookkeeping (we use them as a hint to
     * suppress auto-CRLF after a prompt char); they should NOT erase
     * the "we just saw a prompt" signal — otherwise the gate that
     * decides "is the kernel waiting for input?" closes one frame
     * after every dot prompt because Mihin fires an empty .PRINT
     * right after. */
    if (printableCount > 0) {
        g_lastPrintWasPrompt = thisWasSinglePrompt;
    }
    cpu->psw &= static_cast<uint16_t>(~CPU_PSW_C);
    return true;
}

extern "C" bool stdioThunk(ms0515_cpu_t *cpu, uint16_t vector)
{
    if (vector != CPU_VEC_EMT) return false;
    uint8_t req = static_cast<uint8_t>(cpu->instruction & 0xFFu);
    if (g_traceEmt) g_emtCount[req]++;
    switch (req) {
    case kEmtTtyout: return handleTtyout(cpu);
    case kEmtPrint:  return handlePrint(cpu);
    default:         return false;
    }
}

void enqueueKoi8(uint8_t b)
{
    /* Convert LF (host newline) to CR (RT-11 line ending). */
    if (b == 0x0Au) b = 0x0Du;
    g_stdinQueue.push_back(b);
}

/* Map an ASCII byte to (Key, needsShift).  Returns Key::None for
 * characters we don't have a binding for yet (caller drops them). */
struct KeyMapping {
    ms0515::Key key;
    bool        shift;
};

KeyMapping asciiToKey(uint8_t c)
{
    using Key = ms0515::Key;
    if (c >= 'a' && c <= 'z') c = static_cast<uint8_t>(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') {
        /* Letter ordering matches the Key enum's per-letter mnemonic.
         * Omega monitor accepts unshifted letter caps as uppercase
         * commands, so no shift modifier here. */
        static const Key letters[26] = {
            Key::A, Key::B, Key::C, Key::D, Key::E, Key::F, Key::G,
            Key::H, Key::I, Key::J, Key::K, Key::L, Key::M, Key::N,
            Key::O, Key::P, Key::Q, Key::R, Key::S, Key::T, Key::U,
            Key::V, Key::W, Key::X, Key::Y, Key::Z,
        };
        return {letters[c - 'A'], false};
    }
    if (c >= '1' && c <= '9') {
        static const Key digits[9] = {
            Key::Digit1, Key::Digit2, Key::Digit3, Key::Digit4,
            Key::Digit5, Key::Digit6, Key::Digit7, Key::Digit8,
            Key::Digit9,
        };
        return {digits[c - '1'], false};
    }
    switch (c) {
    case '0':  return {Key::Digit0,  false};
    case ' ':  return {Key::Space,   false};
    case '\r': return {Key::Return,  false};
    case '\n': return {Key::Return,  false};
    case 0x7Fu:
    case '\b': return {Key::Backspace, false};
    case '\t': return {Key::Tab,     false};
    case '.':  return {Key::Period,  false};
    case ',':  return {Key::Comma,   false};
    case '/':  return {Key::Slash,   false};
    case '=':  return {Key::MinusEq, true};      /* Shift+'-' = '=' */
    case '-':  return {Key::MinusEq, false};
    case ';':  return {Key::SemiPlus, false};
    case '+':  return {Key::SemiPlus, true};
    case '@':  return {Key::At,      false};
    case '_':  return {Key::Underscore, false};
    case '\\': return {Key::Backslash, false};
    case ':':  return {Key::ColonStar, false};
    case '*':  return {Key::ColonStar, true};
    default:   break;
    }
    return {Key::None, false};
}

/* Keystroke pump state-machine.  Each tap is: ShiftL down (if needed),
 * key down, hold N frames, key up, ShiftL up, gap M frames before the
 * next tap.  Numbers tuned by feel from test_keyboard_emulated.cpp. */
constexpr int kHoldFrames = 1;
constexpr int kGapFrames  = 4;

enum class TapPhase {
    Idle,
    Holding,
    Cooldown,
};
TapPhase     g_phase       = TapPhase::Idle;
int          g_phaseFrames = 0;
ms0515::Key  g_phaseKey    = ms0515::Key::None;
bool         g_phaseShift  = false;

/* Compute the byte the kernel is most likely to echo for a typed
 * character.  ASCII letters get uppercased — RT-11 monitors universally
 * uppercase commands and echo the uppercased glyph.  Everything else
 * passes through unchanged. */
uint8_t echoCharFor(uint8_t typed)
{
    if (typed >= 'a' && typed <= 'z') {
        return static_cast<uint8_t>(typed - 'a' + 'A');
    }
    return typed;
}

void selfEchoByte(uint8_t b)
{
    /* Direct write — bypass emitGuestByte's state machine so we don't
     * accidentally reset g_lastWasNewline or trigger the KOI-7-mode
     * branch for a typed ASCII char.  For Cyrillic we'd want the
     * mode-aware path, but typed input is ASCII in practice. */
    std::string utf8;
    if (g_koi7N2) {
        koi8::appendAsKoi7N2(utf8, b);
    } else {
        koi8::appendAsUtf8(utf8, b);
    }
    cli::writeStdout(utf8.data(), utf8.size());
}

}  /* namespace */

void install(ms0515::Emulator &emu)
{
    emu.setTrapThunk(stdioThunk);
    g_emu = &emu;
    const char *trace = std::getenv("MS0515_CLI_EMT_TRACE");
    g_traceEmt = (trace != nullptr && trace[0] != '\0');
}

bool serialOutByte(uint8_t byte)
{
    emitGuestByte(byte);
    g_kernelReady = true;
    return true;
}

void dumpEmtCounts()
{
    if (!g_traceEmt) return;
    std::fprintf(stderr, "\nms0515-cli: EMT counts:");
    for (int i = 0; i < 256; ++i) {
        if (g_emtCount[i] > 0) {
            std::fprintf(stderr, " 0o%03o:%d", i, g_emtCount[i]);
        }
    }
    std::fprintf(stderr, "\n");
}

namespace {

void readBytesFromHost()
{
    std::array<uint8_t, 256> buf{};
    size_t n = cli::readStdinNonBlocking(buf.data(), buf.size());
    if (n == 0) return;

    /* Prepend any leftover UTF-8 bytes from the previous read. */
    std::array<uint8_t, 256 + 4> work{};
    size_t workLen = 0;
    for (size_t i = 0; i < g_utf8PendingLen; ++i) work[workLen++] = g_utf8Pending[i];
    for (size_t i = 0; i < n; ++i) work[workLen++] = buf[i];
    g_utf8PendingLen = 0;

    /* Decode one code-point at a time. */
    size_t off = 0;
    while (off < workLen) {
        uint8_t k = 0;
        size_t consumed = koi8::utf8ToKoi8(work.data() + off, workLen - off, &k);
        if (consumed == 0) {
            /* Incomplete trailing sequence — save for next call. */
            for (size_t i = 0; i < workLen - off && i < g_utf8Pending.size(); ++i) {
                g_utf8Pending[i] = work[off + i];
            }
            g_utf8PendingLen = workLen - off;
            break;
        }
        enqueueKoi8(k);
        off += consumed;
    }
}

}  /* namespace */

void pumpInput()
{
    if (g_emu == nullptr) return;
    ++g_framesSinceOutput;

    /* Tick the echo-expectation queue, but only while the kernel is
     * silent (g_framesSinceOutput > 0).  If the kernel just emitted a
     * byte this frame, the echo we're waiting for might still arrive
     * via emitGuestByte momentarily — don't time out under it.  Once
     * the kernel has been silent for kEchoTimeoutFrames frames in a
     * row after a keystroke, self-echo: kernel writes the typed char
     * only to VRAM (OSA / Omega convention) and we have to fill in
     * the gap. */
    while (!g_echoQueue.empty() && g_framesSinceOutput > 0) {
        auto &front = g_echoQueue.front();
        if (--front.framesLeft > 0) break;
        uint8_t ch = front.ch;
        g_echoQueue.pop_front();
        selfEchoByte(ch);
    }

    /* Drain anything the kernel emitted during the previous frame to
     * the host terminal.  One fflush per frame amortises Windows-
     * console VT-parser cost across however many .TTYOUT/.PRINT
     * bytes fired this frame. */
    cli::flushStdout();
    readBytesFromHost();

    /* Track whether we're in an active typing session.  While we've
     * still got queued chars or are mid-tap, keep going uninterrupted.
     * Once nothing is left to type, fall back to waiting for the next
     * prompt before injecting again. */
    if (g_phase == TapPhase::Idle && g_stdinQueue.empty()) {
        g_typingActive = false;
    }

    /* Hold off on keypress injection until the kernel has actually
     * reached the monitor's command prompt.  Best signal we have is
     * g_lastPrintWasPrompt — set by handlePrint when the kernel emits
     * a single printable char via .PRINT with a high-bit terminator
     * (the SSM convention for "no auto-CRLF, leave cursor here").
     * Every RT-11 variant we've seen (Mihin, Omega, OSA) emits the
     * dot prompt exactly that way.  This gate cleanly opens AT the
     * prompt and re-closes the moment a non-prompt .PRINT runs (e.g.
     * a command's output), so subsequent typing sessions also wait
     * for the next prompt. */
    if (!g_kernelReady) return;
    if (!g_typingActive && !g_lastPrintWasPrompt) return;

    switch (g_phase) {
    case TapPhase::Holding:
        if (--g_phaseFrames > 0) return;
        /* Release the key (and shift if it was held). */
        g_emu->keyPress(g_phaseKey, false);
        if (g_phaseShift) g_emu->keyPress(ms0515::Key::ShiftL, false);
        g_phase       = TapPhase::Cooldown;
        g_phaseFrames = kGapFrames;
        return;

    case TapPhase::Cooldown:
        if (--g_phaseFrames > 0) return;
        g_phase = TapPhase::Idle;
        /* Fall through and try to start the next tap immediately. */
        break;

    case TapPhase::Idle:
        break;
    }

    /* Idle — start the next tap if we have one queued. */
    while (!g_stdinQueue.empty()) {
        uint8_t c = g_stdinQueue.front();
        g_stdinQueue.pop_front();
        KeyMapping km = asciiToKey(c);
        if (km.key == ms0515::Key::None) {
            /* Unknown char — drop it.  TODO: report via stderr trace. */
            continue;
        }
        if (km.shift) g_emu->keyPress(ms0515::Key::ShiftL, true);
        g_emu->keyPress(km.key, true);
        /* Track the expected kernel echo: if a matching byte arrives
         * via emitGuestByte within kEchoTimeoutFrames frames we
         * consume the queue (Mihin-style — kernel echoes via .TTYOUT,
         * we don't need to self-echo).  Otherwise we self-echo on
         * timeout (OSA / Omega-style — kernel writes only to VRAM). */
        g_echoQueue.push_back({echoCharFor(c), kEchoTimeoutFrames});
        g_phaseKey     = km.key;
        g_phaseShift   = km.shift;
        g_phase        = TapPhase::Holding;
        g_phaseFrames  = kHoldFrames;
        g_typingActive = true;
        return;
    }
}

}  /* namespace ms0515::cli::bridge */
