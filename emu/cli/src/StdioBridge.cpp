/*
 * StdioBridge.cpp — host stdin → MS-7004 keyboard bridge.
 *
 * Output is handled in main.cpp via Terminal::setOutput(stdout).  This
 * module only feeds keystrokes; the diagnostic EMT trap_thunk is here
 * because it touches the same Emulator and the same singleton flags.
 */

#include "StdioBridge.hpp"

#include "Koi8.hpp"
#include "Platform.hpp"

extern "C" {
#include <ms0515/core/cpu.h>
}

#include <array>
#include <cstdio>
#include <deque>

namespace ms0515::cli::bridge {

namespace {

/* Map an ASCII / KOI-8 byte to a single MS-7004 keypress with any
 * modifiers held during the tap.  Ctrl (СУ) is paired with letter
 * keys to produce RT-11 control codes (СУ/C, СУ/U, …).  Default-zero
 * `ctrl` keeps the existing two-field aggregate initialisers correct. */
struct KeyMapping {
    ms0515::Key key;
    bool        shift;
    bool        ctrl  = false;
};

/* Pending taps queued from host stdin.  Each entry maps to exactly one
 * MS-7004 key-down / key-up pair the pump will dispatch.  Cyrillic
 * input expands to two taps when the bridge needs to toggle the OS
 * РУС/ЛАТ mode first. */
std::deque<KeyMapping> g_tapQueue;

/* Tracked state of the guest's РУС/ЛАТ toggle.  Starts at LAT (false)
 * — that's what the four shipped OSes power up in.  Flipped every time
 * we inject a Key::RusLat tap; if the guest's actual mode diverges from
 * our model (e.g. user typed РУС/ЛАТ via some other path), the bridge
 * stays out of sync and may flip the wrong direction. */
bool g_assumedRusMode = false;

/* Host arrow / F-keys arrive as ANSI escape sequences:
 *   arrows : ESC [ A/B/C/D
 *   F1..F4 : ESC O P/Q/R/S    (xterm SS3 form, some POSIX terminals)
 *   F1..F12: ESC [ N ~        (linux-console form, what Platform_win32
 *                              synthesises and what most modern terminals
 *                              — Windows Terminal, xterm with rmkx —
 *                              send by default).
 * The state machine threads each ESC-prefixed burst through a small
 * pipeline and emits the matching MS-7004 Key tap.  Any sequence the
 * MS-0515 keyboard has no key for (Home, End, PgUp, …) is silently
 * dropped — better than letting a tilde-terminator slip through as
 * literal user input. */
enum class EscState : uint8_t {
    None,             /* not inside an ESC sequence */
    AfterEsc,         /* saw ESC, waiting for '[' / 'O' / other */
    AfterCsi,         /* saw ESC [ */
    CollectingCsiNum, /* saw ESC [ <digit>, accumulating until '~' */
    AfterSs3,         /* saw ESC O, waiting for P/Q/R/S */
};
EscState g_escState  = EscState::None;
int      g_csiAccum  = 0;       /* digits accumulated under CollectingCsiNum */

/* Active emulator pointer — pumpInput() calls emu.keyPress(). */
ms0515::Emulator *g_emu = nullptr;

/* Keystroke injection gate.  Pressing keys before the kernel finishes
 * its boot sequence (ROM POST → OS banner → command prompt) can cause
 * Mihin in particular to abort and restart, so the bridge holds off
 * injection until main signals "kernel is ready" — typically by
 * observing VramMirror's idle counter passing a threshold. */
bool g_inputReady = false;

/* Optional EMT histogram for diagnostics — enabled by --emt-trace. */
int g_emtCount[256] = {};
bool g_traceEmt = false;

/* Leftover UTF-8 bytes from the previous host read that didn't form
 * a complete code-point yet. */
std::array<uint8_t, 4> g_utf8Pending{};
size_t                 g_utf8PendingLen = 0;

extern "C" bool stdioThunk(ms0515_cpu_t *cpu, uint16_t vector)
{
    /* Diagnostic only — count the EMT and optionally trace .TTYOUT
     * bytes, then return false so the kernel's standard service path
     * runs and TT.SYS routes the byte to VRAM / serial as configured.
     * Returning true here is what previously hid OSA / Omega output
     * from the CLI ([[project-cli-vram-mirror]]). */
    if (vector != CPU_VEC_EMT || !g_traceEmt) return false;
    uint8_t req = static_cast<uint8_t>(cpu->instruction & 0xFFu);
    g_emtCount[req]++;
    if (req == 0341u /*.TTYOUT*/) {
        std::fprintf(stderr, " <%03o>", static_cast<uint8_t>(cpu->r[0] & 0xFFu));
    }
    return false;
}

/* Forward declarations — full bodies appear immediately below. */
KeyMapping asciiToKey(uint8_t c);
KeyMapping koi8CyrillicToKey(uint8_t b);

/* Map an ANSI CSI final byte to the matching MS-7004 arrow Key. */
ms0515::Key csiArrow(uint8_t b)
{
    using Key = ms0515::Key;
    switch (b) {
    case 'A': return Key::Up;
    case 'B': return Key::Down;
    case 'C': return Key::Right;
    case 'D': return Key::Left;
    default:  return Key::None;
    }
}

/* "ESC [ N ~" — linux-console / xterm-rmkx style F-key sequence.
 * The numbers match the convention every modern terminal agrees on;
 * gaps in the sequence (16, 22, 25, …) are historical from VT-220
 * keypad codes that aren't used for F-keys. */
ms0515::Key csiNumToKey(int n)
{
    using Key = ms0515::Key;
    switch (n) {
    case 11: return Key::F1;
    case 12: return Key::F2;
    case 13: return Key::F3;
    case 14: return Key::F4;
    case 15: return Key::F5;
    case 17: return Key::F6;
    case 18: return Key::F7;
    case 19: return Key::F8;
    case 20: return Key::F9;
    case 21: return Key::F10;
    case 23: return Key::F11;
    case 24: return Key::F12;
    default: return Key::None;
    }
}

/* "ESC O P/Q/R/S" — xterm SS3 form for F1..F4.  F5+ aren't reachable
 * through this prefix; terminals that use SS3 for the low four switch
 * to the CSI ~ form for the rest, which csiNumToKey() handles. */
ms0515::Key ss3ToKey(uint8_t b)
{
    using Key = ms0515::Key;
    switch (b) {
    case 'P': return Key::F1;
    case 'Q': return Key::F2;
    case 'R': return Key::F3;
    case 'S': return Key::F4;
    default:  return Key::None;
    }
}

void enqueueLetterByte(uint8_t b);

/* Run incoming KOI-8 bytes through the ESC-sequence state machine.
 * Anything outside an ESC sequence falls through to enqueueLetterByte
 * for the normal Latin / Cyrillic / punctuation classification. */
void enqueueKoi8(uint8_t b)
{
    using Key = ms0515::Key;

    /* Ctrl-]  (ASCII 0x1D) is the CLI's quit escape — matching the
     * familiar telnet escape character.  RT-11 has no use for it, so
     * intercepting it here doesn't take anything away from the guest.
     * The signal is delivered via the Platform shouldQuit() flag the
     * main loop already polls. */
    if (b == 0x1Du) {
        cli::requestQuit();
        return;
    }

    switch (g_escState) {
    case EscState::None:
        if (b == 0x1Bu) {
            g_escState = EscState::AfterEsc;
            return;
        }
        enqueueLetterByte(b);
        return;

    case EscState::AfterEsc:
        if (b == '[') { g_escState = EscState::AfterCsi; return; }
        if (b == 'O') { g_escState = EscState::AfterSs3; return; }
        /* ESC followed by something else — flush both as plain bytes
         * and reset.  The guest may use bare ESC for some monitors
         * (e.g. as command-cancel), so we don't drop it. */
        g_escState = EscState::None;
        enqueueLetterByte(0x1Bu);
        enqueueLetterByte(b);
        return;

    case EscState::AfterCsi:
        if (Key arrow = csiArrow(b); arrow != Key::None) {
            g_escState = EscState::None;
            g_tapQueue.push_back({arrow, false});
            return;
        }
        if (b >= '0' && b <= '9') {
            g_csiAccum = b - '0';
            g_escState = EscState::CollectingCsiNum;
            return;
        }
        /* Unknown CSI final byte — flush the prefix and the byte. */
        g_escState = EscState::None;
        enqueueLetterByte(0x1Bu);
        enqueueLetterByte('[');
        enqueueLetterByte(b);
        return;

    case EscState::CollectingCsiNum:
        if (b >= '0' && b <= '9') {
            g_csiAccum = g_csiAccum * 10 + (b - '0');
            return;
        }
        if (b == '~') {
            const Key fkey = csiNumToKey(g_csiAccum);
            g_escState = EscState::None;
            if (fkey != Key::None) g_tapQueue.push_back({fkey, false});
            /* Otherwise this is a Home / End / Ins / Del / PgUp / PgDn
             * style sequence the MS-0515 keyboard has no equivalent
             * for — silently drop it. */
            return;
        }
        /* Sequence broke off mid-number — flush what we'd swallowed. */
        g_escState = EscState::None;
        enqueueLetterByte(0x1Bu);
        enqueueLetterByte('[');
        if (g_csiAccum >= 10) {
            enqueueLetterByte(static_cast<uint8_t>('0' + g_csiAccum / 10));
            enqueueLetterByte(static_cast<uint8_t>('0' + g_csiAccum % 10));
        } else {
            enqueueLetterByte(static_cast<uint8_t>('0' + g_csiAccum));
        }
        enqueueLetterByte(b);
        return;

    case EscState::AfterSs3:
        g_escState = EscState::None;
        if (Key fkey = ss3ToKey(b); fkey != Key::None) {
            g_tapQueue.push_back({fkey, false});
            return;
        }
        /* Unknown SS3 final byte — flush. */
        enqueueLetterByte(0x1Bu);
        enqueueLetterByte('O');
        enqueueLetterByte(b);
        return;
    }
}

/* Expand one host-stdin KOI-8R byte into the keystroke tap(s) the guest
 * needs to see it.  Cyrillic letters require the guest to be in РУС
 * mode and Latin letters require ЛАТ mode; punctuation / digits /
 * space / control characters are mode-agnostic and don't toggle.
 * When a mode flip is needed we prepend a Key::RusLat tap. */
void enqueueLetterByte(uint8_t b)
{
    using Key = ms0515::Key;
    /* Convert LF (host newline) to CR (RT-11 line ending). */
    if (b == 0x0Au) b = 0x0Du;

    /* Control codes 0x01..0x1A arrive when the host sends Ctrl+letter
     * (terminal raw-mode convention: 0x01 = Ctrl-A, …, 0x1A = Ctrl-Z).
     * RT-11 calls this the СУ ("Система Управления") modifier — СУ/C
     * interrupts a program, СУ/U cancels the input line, etc.  We hold
     * Key::Ctrl during the letter tap.  Ctrl needs ЛАТ mode like a
     * plain letter would.
     *
     * Three letters in that range are NOT Ctrl combinations on a host
     * terminal: 0x08 is Backspace (the user's dedicated key, not
     * Ctrl-H), 0x09 is Tab (Ctrl-I), 0x0D is Return (Ctrl-M).  Route
     * those to their own physical-key mappings below. */
    if (b >= 0x01u && b <= 0x1Au
        && b != 0x08u /*BS*/
        && b != 0x09u /*TAB*/
        && b != 0x0Du /*CR*/) {
        static constexpr Key kLetters[26] = {
            Key::A, Key::B, Key::C, Key::D, Key::E, Key::F, Key::G,
            Key::H, Key::I, Key::J, Key::K, Key::L, Key::M, Key::N,
            Key::O, Key::P, Key::Q, Key::R, Key::S, Key::T, Key::U,
            Key::V, Key::W, Key::X, Key::Y, Key::Z,
        };
        if (g_assumedRusMode) {
            g_tapQueue.push_back({Key::RusLat, false});
            g_assumedRusMode = false;
        }
        g_tapQueue.push_back({kLetters[b - 1], false, /*ctrl=*/true});
        return;
    }

    /* Cyrillic first — KOI-8R 0xC0..0xFF is the Russian half.  Letters
     * always need РУС mode; lower- vs upper-case picks Shift. */
    KeyMapping cyr = koi8CyrillicToKey(b);
    if (cyr.key != Key::None) {
        if (!g_assumedRusMode) {
            g_tapQueue.push_back({Key::RusLat, false});
            g_assumedRusMode = true;
        }
        g_tapQueue.push_back(cyr);
        return;
    }

    /* ASCII / punctuation. */
    KeyMapping km = asciiToKey(b);
    if (km.key == Key::None) return;  /* no MS-7004 home — drop */

    /* Only force ЛАТ mode for Latin letters; digits and punctuation are
     * the same key on both faces, no toggle needed. */
    const bool isLatinLetter = (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z');
    if (isLatinLetter && g_assumedRusMode) {
        g_tapQueue.push_back({Key::RusLat, false});
        g_assumedRusMode = false;
    }
    g_tapQueue.push_back(km);
}

/* KOI-8R 0xC0..0xFF → MS-7004 Key.  Same Key for the lowercase
 * (0xC0..0xDF) and uppercase (0xE0..0xFF) halves; shift differentiates.
 * Layout matches `emu/lib/src/KeyboardLayout.cpp` — each Russian
 * letter sits on the physical key its name shares with a Latin
 * counterpart in YЦUKEN-style mapping (Й→J, Ц→C, ... Ъ→HardSign). */
KeyMapping koi8CyrillicToKey(uint8_t b)
{
    using Key = ms0515::Key;
    int idx = -1;
    bool upper = false;
    if (b >= 0xC0 && b <= 0xDF) {
        idx = b - 0xC0;
        upper = false;
    } else if (b >= 0xE0 && b <= 0xFF) {
        idx = b - 0xE0;
        upper = true;
    } else {
        return {Key::None, false};
    }
    /* Index 0..31 corresponds to KOI-8 columns starting at 0xC0/0xE0:
     * 0=ю/Ю 1=а/А 2=б/Б 3=ц/Ц 4=д/Д 5=е/Е 6=ф/Ф 7=г/Г
     * 8=х/Х 9=и/И A=й/Й B=к/К C=л/Л D=м/М E=н/Н F=о/О
     * 10=п/П 11=я/Я 12=р/Р 13=с/С 14=т/Т 15=у/У 16=ж/Ж 17=в/В
     * 18=ь/Ь 19=ы/Ы 1A=з/З 1B=ш/Ш 1C=э/Э 1D=щ/Щ 1E=ч/Ч 1F=ъ/Ъ */
    static constexpr Key kCyrillic[32] = {
        Key::At,        Key::A,         Key::B,         Key::C,
        Key::D,         Key::E,         Key::F,         Key::G,
        Key::H,         Key::I,         Key::J,         Key::K,
        Key::L,         Key::M,         Key::N,         Key::O,
        Key::P,         Key::Q,         Key::R,         Key::S,
        Key::T,         Key::U,         Key::V,         Key::W,
        Key::X,         Key::Y,         Key::Z,         Key::LBracket,
        Key::Backslash, Key::RBracket,  Key::Che,       Key::HardSign,
    };
    return {kCyrillic[idx], upper};
}

KeyMapping asciiToKey(uint8_t c)
{
    using Key = ms0515::Key;
    static constexpr Key kLetters[26] = {
        Key::A, Key::B, Key::C, Key::D, Key::E, Key::F, Key::G,
        Key::H, Key::I, Key::J, Key::K, Key::L, Key::M, Key::N,
        Key::O, Key::P, Key::Q, Key::R, Key::S, Key::T, Key::U,
        Key::V, Key::W, Key::X, Key::Y, Key::Z,
    };
    if (c >= 'a' && c <= 'z') return {kLetters[c - 'a'], false};
    if (c >= 'A' && c <= 'Z') return {kLetters[c - 'A'], true};
    if (c >= '1' && c <= '9') {
        static const Key digits[9] = {
            Key::Digit1, Key::Digit2, Key::Digit3, Key::Digit4,
            Key::Digit5, Key::Digit6, Key::Digit7, Key::Digit8,
            Key::Digit9,
        };
        return {digits[c - '1'], false};
    }
    /* Punctuation, whitespace and shift-equivalents.  Mapping mirrors
     * the MS-7004 physical layout in `KeyboardLayout.cpp` — unshifted
     * symbol on the primary face of each key, shifted symbol on the
     * secondary face.  Symbols that don't appear on any MS-7004 key
     * ('$', '^', '`') fall through to Key::None and are silently
     * dropped; the guest has no way to receive them. */
    switch (c) {
    case '0':  return {Key::Digit0,    false};
    case ' ':  return {Key::Space,     false};
    case '\r': return {Key::Return,    false};
    case '\n': return {Key::Return,    false};
    case 0x7Fu:
    case '\b': return {Key::Backspace, false};
    case '\t': return {Key::Tab,       false};

    /* Digit-row shifted symbols.  MS-7004 puts ¤ (currency sign,
     * "жучок") on Shift+4 — the closest available glyph to '$', so
     * host '$' maps there.  '^' lives on Shift+Че (which paints ¬
     * but the keyboard sends ASCII 0x5E, so VramMirror's font lookup
     * decodes it back to '^' on the host). */
    case '!':  return {Key::Digit1, true};
    case '"':  return {Key::Digit2, true};
    case '#':  return {Key::Digit3, true};
    case '$':  return {Key::Digit4, true};      /* renders as ¤ */
    case '%':  return {Key::Digit5, true};
    case '&':  return {Key::Digit6, true};
    case '\'': return {Key::Digit7, true};
    case '(':  return {Key::Digit8, true};
    case ')':  return {Key::Digit9, true};
    case '^':  return {Key::Che,    true};      /* Shift+Че → ¬ glyph, byte 0x5E */

    /* Digit-row right-side neighbours (-=, {|, }↖). */
    case '-':  return {Key::MinusEq,      false};
    case '=':  return {Key::MinusEq,      true};
    case '{':  return {Key::LBracePipe,   false};
    case '|':  return {Key::LBracePipe,   true};
    case '}':  return {Key::RBraceLeftUp, false};

    /* Letter-row punctuation. */
    case ';':  return {Key::SemiPlus,  false};
    case '+':  return {Key::SemiPlus,  true};
    case '[':  return {Key::LBracket,  false};
    case ']':  return {Key::RBracket,  false};
    case ':':  return {Key::ColonStar, false};
    case '*':  return {Key::ColonStar, true};
    case '~':  return {Key::Tilde,     false};
    case '\\': return {Key::Backslash, false};
    case '@':  return {Key::At,        false};
    case '.':  return {Key::Period,    false};
    case '>':  return {Key::Period,    true};
    case ',':  return {Key::Comma,     false};
    case '<':  return {Key::Comma,     true};
    case '/':  return {Key::Slash,     false};
    case '?':  return {Key::Slash,     true};
    case '_':  return {Key::Underscore, false};
    default:   break;
    }
    return {Key::None, false};
}

constexpr int kHoldFrames = 1;
constexpr int kGapFrames  = 4;

enum class TapPhase { Idle, Holding, Cooldown };
TapPhase     g_phase       = TapPhase::Idle;
int          g_phaseFrames = 0;
ms0515::Key  g_phaseKey    = ms0515::Key::None;
bool         g_phaseShift  = false;
bool         g_phaseCtrl   = false;

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
        enqueueKoi8(k);
        off += consumed;
    }
}

}  /* namespace */

void install(ms0515::Emulator &emu)
{
    emu.setTrapThunk(stdioThunk);
    g_emu = &emu;
}

void setEmtTrace(bool enabled)
{
    g_traceEmt = enabled;
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

void setInputReady(bool ready)
{
    g_inputReady = ready;
}

void pumpInput()
{
    if (g_emu == nullptr) return;

    readBytesFromHost();

    if (!g_inputReady) return;

    switch (g_phase) {
    case TapPhase::Holding:
        if (--g_phaseFrames > 0) return;
        g_emu->keyPress(g_phaseKey, false);
        if (g_phaseShift) g_emu->keyPress(ms0515::Key::ShiftL, false);
        if (g_phaseCtrl)  g_emu->keyPress(ms0515::Key::Ctrl,   false);
        g_phase       = TapPhase::Cooldown;
        g_phaseFrames = kGapFrames;
        return;

    case TapPhase::Cooldown:
        if (--g_phaseFrames > 0) return;
        g_phase = TapPhase::Idle;
        break;

    case TapPhase::Idle:
        break;
    }

    while (!g_tapQueue.empty()) {
        KeyMapping km = g_tapQueue.front();
        g_tapQueue.pop_front();
        if (km.key == ms0515::Key::None) continue;
        if (km.ctrl)  g_emu->keyPress(ms0515::Key::Ctrl,   true);
        if (km.shift) g_emu->keyPress(ms0515::Key::ShiftL, true);
        g_emu->keyPress(km.key, true);
        g_phaseKey    = km.key;
        g_phaseShift  = km.shift;
        g_phaseCtrl   = km.ctrl;
        g_phase       = TapPhase::Holding;
        g_phaseFrames = kHoldFrames;
        return;
    }
}

}  /* namespace ms0515::cli::bridge */
