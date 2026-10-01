/*
 * ConsoleText.cpp - see ConsoleText.hpp.
 */

#include "ConsoleText.hpp"

#include <ms0515/VramMirror.hpp>

namespace ms0515::run {

namespace {

constexpr uint8_t kBell = 007, kBackspace = 010, kTab = 011, kLineFeed = 012,
                  kReturn = 015, kEscape = 033;

} /* namespace */

void ConsoleText::control(uint8_t code, std::string &out) const
{
    switch (code) {
    case kLineFeed:
        out += '\n';
        break;
    case kTab:
        out += '\t';
        break;
    case kReturn:
    case kBackspace:
    case kBell:
        if (reader_ == Reader::terminal) out += static_cast<char>(code);
        break;
    default:                    /* SO, SI and the rest: the console's own */
        break;
    }
}

void ConsoleText::escape(uint8_t code, std::string &out)
{
    state_ = State::text;
    if (code == 'Y') {                      /* ESC Y row column */
        state_ = State::row;
        return;
    }
    if (reader_ != Reader::terminal)
        return;
    switch (code) {
    case 'A': case 'B': case 'C': case 'D':     /* up, down, right, left */
    case 'H':                                   /* home                  */
    case 'J':                                   /* erase to the screen's end */
    case 'K':                                   /* erase to the line's end   */
        out += "\x1B[";
        out += static_cast<char>(code);
        break;
    default:
        break;
    }
}

std::string ConsoleText::convert(std::string_view bytes)
{
    std::string out;
    for (char c : bytes) {
        const auto code = static_cast<uint8_t>(c);
        switch (state_) {
        case State::escape:
            escape(code, out);
            break;
        case State::row:
            row_ = code;
            state_ = State::column;
            break;
        case State::column:
            state_ = State::text;
            if (reader_ == Reader::terminal && row_ >= 040 && code >= 040)
                out += "\x1B[" + std::to_string(row_ - 037) + ';' +
                       std::to_string(code - 037) + 'H';
            break;
        case State::text:
            if (code == kEscape)
                state_ = State::escape;
            else if (code < 040 || code == 0177)
                control(code, out);
            else
                out += ms0515::VramMirror::utf8FromRomBCode(code);
            break;
        }
    }
    return out;
}

} /* namespace ms0515::run */
