/*
 * HostKey.hpp — the keys of the host's terminal, read off its byte stream.
 *
 * A terminal sends a key either as a byte (letters, digits, the
 * controls: Enter 0x0D, Backspace 0x08 / 0x7F, Tab 0x09, Esc 0x1B,
 * Ctrl+letter 0x01..0x1A, Ctrl+\ 0x1C ...) or as an ESC sequence (the
 * arrows, F1..F12, Insert, Delete, Home, End, PgUp, PgDn).  KeyParser
 * turns the stream into HostKeys, one per key, so that whoever reads the
 * terminal - the commander over the machine, the bridge into the guest -
 * deals with keys and not with escape codes.  Bytes are KOI-8 here: the
 * UTF-8 of the terminal is converted before the parser, and every
 * sequence is plain ASCII.
 */
#ifndef MS0515_FILES_HOSTKEY_HPP
#define MS0515_FILES_HOSTKEY_HPP

#include <cstdint>
#include <vector>

namespace ms0515::files {

enum class SpecialKey {
    none,
    up, down, left, right,
    f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12,
    insert, del, home, end, pageUp, pageDown,
    backTab,                         /* Shift+Tab */
};

/* 1..12 for f1..f12, 0 for anything else. */
[[nodiscard]] int functionNumber(SpecialKey key) noexcept;

struct HostKey {
    uint8_t    byte = 0;             /* when special == none */
    SpecialKey special = SpecialKey::none;
    bool       alt = false;          /* Alt held with a function key */

    [[nodiscard]] static HostKey ofByte(uint8_t b) noexcept { HostKey k; k.byte = b; return k; }
    [[nodiscard]] static HostKey ofSpecial(SpecialKey s, bool alt = false) noexcept
    { HostKey k; k.special = s; k.alt = alt; return k; }
    [[nodiscard]] bool isByte() const noexcept { return special == SpecialKey::none; }
    [[nodiscard]] bool isSpecial() const noexcept { return special != SpecialKey::none; }
    bool operator==(const HostKey &) const = default;
};

/* The byte stream to keys.  feed() returns the keys a byte completes (none
 * while a sequence is in flight); flush() at the end of a read burst turns
 * a lone ESC into the Esc key - a terminal sends a whole sequence at once,
 * so an ESC that ends a burst is the key itself.  A sequence nobody knows
 * comes out as the bytes it was made of. */
class KeyParser {
public:
    [[nodiscard]] std::vector<HostKey> feed(uint8_t b);
    [[nodiscard]] std::vector<HostKey> flush();

private:
    enum class State { none, afterEsc, afterEscEsc, csi, ss3 };
    State state_ = State::none;
    bool escAlt_ = false;            /* the sequence came with an ESC prefix: Alt */
    std::vector<uint8_t> pending_;   /* the bytes of the sequence so far */

    [[nodiscard]] std::vector<HostKey> finishCsi(uint8_t final);
    [[nodiscard]] std::vector<HostKey> giveUp(uint8_t b);
    void reset() noexcept;
};

} /* namespace ms0515::files */

#endif /* MS0515_FILES_HOSTKEY_HPP */
