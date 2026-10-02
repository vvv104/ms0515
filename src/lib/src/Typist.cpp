/*
 * Typist.cpp — see Typist.hpp.
 */

#include "ms0515/Typist.hpp"

namespace ms0515 {

namespace {

/* The key that gives a character, and whether Shift is held with it. */
struct Mapping {
    Key  key;
    bool shift;
};

constexpr int kHoldFrames = 1;
constexpr int kGapFrames  = 4;

/* KOI-8R 0xC0..0xFF → MS-7004 Key.  Same Key for the lowercase
 * (0xC0..0xDF) and uppercase (0xE0..0xFF) halves; shift differentiates.
 * Layout matches `KeyboardLayout.cpp` — each Russian
 * letter sits on the physical key its name shares with a Latin
 * counterpart in YЦUKEN-style mapping (Й→J, Ц→C, ... Ъ→HardSign).
 *
 * РУС mode follows normal PC convention: bare key = lowercase,
 * Shift+key = uppercase.  Only ЛАТ mode has the OS-design quirk
 * where Shift inverts (handled in asciiToKey). */
Mapping koi8CyrillicToKey(uint8_t b)
{
        int idx = -1;
    bool shifted = false;
    if (b >= 0xC0 && b <= 0xDF) {
        idx = b - 0xC0;
        shifted = false;    /* lowercase Cyrillic → bare key */
    } else if (b >= 0xE0) {
        /* uint8_t guarantees the upper bound — `b <= 0xFF` would draw
         * a -Wtype-limits warning under -Wextra. */
        idx = b - 0xE0;
        shifted = true;     /* uppercase Cyrillic → Shift+key */
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
    return {kCyrillic[idx], shifted};
}

Mapping asciiToKey(uint8_t c)
{
        static constexpr Key kLetters[26] = {
        Key::A, Key::B, Key::C, Key::D, Key::E, Key::F, Key::G,
        Key::H, Key::I, Key::J, Key::K, Key::L, Key::M, Key::N,
        Key::O, Key::P, Key::Q, Key::R, Key::S, Key::T, Key::U,
        Key::V, Key::W, Key::X, Key::Y, Key::Z,
    };
    /* ЛАТ mode of the MS-0515 monitor inverts Shift sense: a bare
     * key press echoes UPPERCASE, Shift+key echoes lowercase.  That's
     * the OS's deliberate design (not a CAPS Lock state we could
     * toggle off), so a clipboard paste of "Hello" would arrive
     * here as bytes 0x48/0x65/… and need:
     *   uppercase host byte → bare key  → OS echoes uppercase
     *   lowercase host byte → Shift+key → OS echoes lowercase
     * Keeps paste case-faithful at the cost of changing the typing
     * convention (host without caps now lands in lowercase, matching
     * PC expectations of "what I see in stdin is what shows up"). */
    if (c >= 'A' && c <= 'Z') return {kLetters[c - 'A'], false};
    if (c >= 'a' && c <= 'z') return {kLetters[c - 'a'], true};
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

}  /* namespace */

/* Expand one KOI-8R byte into the keystroke tap(s) the guest needs to
 * see it.  Cyrillic letters require the guest to be in РУС
 * mode and Latin letters require ЛАТ mode; punctuation / digits /
 * space / control characters are mode-agnostic and don't toggle.
 * When a mode flip is needed we prepend a Key::RusLat tap. */
void Typist::type(uint8_t b)
{
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
        if (rus_) {
            queue_.push_back({Key::RusLat, false});
            rus_ = false;
        }
        queue_.push_back({kLetters[b - 1], false, /*ctrl=*/true});
        return;
    }

    /* Cyrillic first — KOI-8R 0xC0..0xFF is the Russian half.  Letters
     * always need РУС mode; lower- vs upper-case picks Shift. */
    const Mapping cyr = koi8CyrillicToKey(b);
    if (cyr.key != Key::None) {
        if (!rus_) {
            queue_.push_back({Key::RusLat, false});
            rus_ = true;
        }
        queue_.push_back({cyr.key, cyr.shift});
        return;
    }

    /* ASCII / punctuation. */
    const Mapping km = asciiToKey(b);
    if (km.key == Key::None) return;  /* no MS-7004 home — drop */

    /* Only force ЛАТ mode for Latin letters; digits and punctuation are
     * the same key on both faces, no toggle needed. */
    const bool isLatinLetter = (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z');
    if (isLatinLetter && rus_) {
        queue_.push_back({Key::RusLat, false});
        rus_ = false;
    }
    queue_.push_back({km.key, km.shift});
}

void Typist::type(Key key)
{
    if (key != Key::None) queue_.push_back({key, false});
}

std::size_t Typist::pending() const noexcept
{
    return queue_.size() + (phase_ == Phase::idle ? 0 : 1);
}

void Typist::pump(Emulator &emu)
{
    switch (phase_) {
    case Phase::holding:
        if (--frames_ > 0) return;
        emu.keyPress(held_.key, false);
        if (held_.shift) emu.keyPress(Key::ShiftL, false);
        if (held_.ctrl)  emu.keyPress(Key::Ctrl,   false);
        phase_  = Phase::cooldown;
        frames_ = kGapFrames;
        return;

    case Phase::cooldown:
        if (--frames_ > 0) return;
        phase_ = Phase::idle;
        break;

    case Phase::idle:
        break;
    }

    if (queue_.empty()) return;
    held_ = queue_.front();
    queue_.pop_front();
    if (held_.ctrl)  emu.keyPress(Key::Ctrl,   true);
    if (held_.shift) emu.keyPress(Key::ShiftL, true);
    emu.keyPress(held_.key, true);
    phase_  = Phase::holding;
    frames_ = kHoldFrames;
}

}  /* namespace ms0515 */
