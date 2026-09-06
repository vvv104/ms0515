/*
 * HostKey.cpp — the terminal's ESC sequences, decoded.
 */
#include "HostKey.hpp"

#include <cctype>

namespace ms0515::files {

namespace {

SpecialKey letterKey(uint8_t final)
{
    switch (final) {
    case 'A': return SpecialKey::up;
    case 'B': return SpecialKey::down;
    case 'C': return SpecialKey::right;
    case 'D': return SpecialKey::left;
    case 'H': return SpecialKey::home;
    case 'F': return SpecialKey::end;
    case 'P': return SpecialKey::f1;
    case 'Q': return SpecialKey::f2;
    case 'R': return SpecialKey::f3;
    case 'S': return SpecialKey::f4;
    case 'Z': return SpecialKey::backTab;
    default:  return SpecialKey::none;
    }
}

/* CSI n ~ : the editing keys and the F-keys by number. */
SpecialKey tildeKey(int code)
{
    switch (code) {
    case 1:  return SpecialKey::home;
    case 2:  return SpecialKey::insert;
    case 3:  return SpecialKey::del;
    case 4:  return SpecialKey::end;
    case 5:  return SpecialKey::pageUp;
    case 6:  return SpecialKey::pageDown;
    case 7:  return SpecialKey::home;
    case 8:  return SpecialKey::end;
    case 11: return SpecialKey::f1;
    case 12: return SpecialKey::f2;
    case 13: return SpecialKey::f3;
    case 14: return SpecialKey::f4;
    case 15: return SpecialKey::f5;
    case 17: return SpecialKey::f6;
    case 18: return SpecialKey::f7;
    case 19: return SpecialKey::f8;
    case 20: return SpecialKey::f9;
    case 21: return SpecialKey::f10;
    case 23: return SpecialKey::f11;
    case 24: return SpecialKey::f12;
    default: return SpecialKey::none;
    }
}

bool isFinalByte(uint8_t b) noexcept
{
    return b >= 0x40 && b <= 0x7E;
}

} // namespace

int functionNumber(SpecialKey key) noexcept
{
    const int n = static_cast<int>(key) - static_cast<int>(SpecialKey::f1) + 1;
    return n >= 1 && n <= 12 ? n : 0;
}

void KeyParser::reset() noexcept
{
    state_ = State::none;
    escAlt_ = false;
    pending_.clear();
}

std::vector<HostKey> KeyParser::feed(uint8_t b)
{
    switch (state_) {
    case State::none:
        if (b != 0x1B) return {HostKey::ofByte(b)};
        state_ = State::afterEsc;
        pending_ = {b};
        return {};

    case State::afterEsc:
    case State::afterEscEsc:
        if (b == '[') { state_ = State::csi; pending_.push_back(b); return {}; }
        if (b == 'O') { state_ = State::ss3; pending_.push_back(b); return {}; }
        if (b == 0x1B && state_ == State::afterEsc) { state_ = State::afterEscEsc; escAlt_ = true; pending_.push_back(b); return {}; }
        return giveUp(b);

    case State::ss3: {
        const SpecialKey key = letterKey(b);
        if (key == SpecialKey::none) return giveUp(b);
        const bool alt = escAlt_;
        reset();
        return {HostKey::ofSpecial(key, alt && functionNumber(key) != 0)};
    }

    case State::csi:
        if (std::isdigit(b) || b == ';') { pending_.push_back(b); return {}; }
        if (!isFinalByte(b)) return giveUp(b);
        return finishCsi(b);
    }
    return {};
}

/* The CSI parameters are in pending_ after "ESC [": "n", "n;m" (m the
 * xterm modifier: 1 + shift 1 + alt 2 + ctrl 4), or nothing. */
std::vector<HostKey> KeyParser::finishCsi(uint8_t final)
{
    int first = 0, modifier = 1, *cur = &first;
    bool any = false;
    for (size_t i = 2; i < pending_.size(); ++i) {
        const uint8_t c = pending_[i];
        if (c == ';') { cur = &modifier; modifier = 0; continue; }
        *cur = *cur * 10 + (c - '0');
        any = true;
    }
    SpecialKey key = SpecialKey::none;
    if (final == '~') key = tildeKey(first);
    else if (!any || first == 1) key = letterKey(final);
    if (key == SpecialKey::none) return giveUp(final);
    const bool alt = (escAlt_ || ((modifier - 1) & 2) != 0) && functionNumber(key) != 0;
    reset();
    return {HostKey::ofSpecial(key, alt)};
}

/* Not a sequence after all: the bytes as they came, then this one. */
std::vector<HostKey> KeyParser::giveUp(uint8_t b)
{
    std::vector<HostKey> out;
    for (const uint8_t p : pending_) out.push_back(HostKey::ofByte(p));
    out.push_back(HostKey::ofByte(b));
    reset();
    return out;
}

std::vector<HostKey> KeyParser::flush()
{
    if (state_ != State::afterEsc && state_ != State::afterEscEsc) return {};
    std::vector<HostKey> out;
    for (const uint8_t p : pending_) out.push_back(HostKey::ofByte(p));
    reset();
    return out;
}

} /* namespace ms0515::files */
