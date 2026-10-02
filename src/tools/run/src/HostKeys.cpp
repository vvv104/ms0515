/*
 * HostKeys.cpp - see HostKeys.hpp.
 */

#include "HostKeys.hpp"

#include <ms0515/Koi8.hpp>

namespace ms0515::run {

namespace {

constexpr uint8_t kEscape = 033, kQuit = 035;

} /* namespace */

/* The bytes gathered so far, if they are a whole character. */
void HostKeys::character(ms0515::Typist &typist)
{
    uint8_t koi8 = 0;
    const std::size_t used = koi8::utf8ToKoi8(utf8_.data(), utf8Len_, &koi8);
    if (used == 0) {
        if (utf8Len_ == utf8_.size()) utf8Len_ = 0;     /* not UTF-8: drop */
        return;
    }
    utf8Len_ = 0;
    typist.type(koi8);
}

bool HostKeys::feed(std::span<const uint8_t> bytes, ms0515::Typist &typist)
{
    bool quit = false;
    for (const uint8_t b : bytes) {
        switch (state_) {
        case State::escape:
            /* ESC [ and ESC O open a sequence; ESC and anything else is
             * a lone Esc, which the keyboard has no key for. */
            if (b == '[' || b == 'O') { state_ = State::sequence; continue; }
            state_ = State::text;
            break;
        case State::sequence:
            if (b >= 0x40 && b <= 0x7E) {               /* its last byte */
                state_ = State::text;
                switch (b) {
                case 'A': typist.type(Key::Up);    break;
                case 'B': typist.type(Key::Down);  break;
                case 'C': typist.type(Key::Right); break;
                case 'D': typist.type(Key::Left);  break;
                default:  break;
                }
            }
            continue;
        case State::text:
            break;
        }
        if (b == kQuit) { quit = true; continue; }
        if (b == kEscape && utf8Len_ == 0) { state_ = State::escape; continue; }
        utf8_[utf8Len_++] = b;
        character(typist);
    }
    return quit;
}

} /* namespace ms0515::run */
