/*
 * HostEvent.cpp — host keys as FTXUI events.
 */
#include "HostEvent.hpp"

#include "Viewer.hpp"

#include <fmt/format.h>

#include <string>

namespace ms0515::files {

namespace {

using ftxui::Event;

Event byteEvent(uint8_t b)
{
    switch (b) {
    case 0x0D: return Event::Return;
    case 0x09: return Event::Tab;
    case 0x1B: return Event::Escape;
    case 0x08:
    case 0x7F: return Event::Backspace;
    default:   break;
    }
    if (b < 0x20) return Event::Special(std::string(1, static_cast<char>(b)));
    bool shift = false;
    return Event::Character(decodeByte(b, Encoding::koi8r, shift));
}

Event functionEvent(int n, bool alt)
{
    if (alt) {
        static constexpr int kCode[12] = {11, 12, 13, 14, 15, 17, 18, 19, 20, 21, 23, 24};
        if (n <= 4) return Event::Special(fmt::format("\x1B[1;3{}", static_cast<char>('P' + n - 1)));
        return Event::Special(fmt::format("\x1B[{};3~", kCode[n - 1]));
    }
    static const Event kF[12] = {Event::F1, Event::F2, Event::F3, Event::F4, Event::F5, Event::F6,
                                 Event::F7, Event::F8, Event::F9, Event::F10, Event::F11, Event::F12};
    return kF[n - 1];
}

} // namespace

Event toEvent(const HostKey &key)
{
    if (key.isByte()) return byteEvent(key.byte);
    if (const int n = functionNumber(key.special)) return functionEvent(n, key.alt);
    switch (key.special) {
    case SpecialKey::up:       return Event::ArrowUp;
    case SpecialKey::down:     return Event::ArrowDown;
    case SpecialKey::left:     return Event::ArrowLeft;
    case SpecialKey::right:    return Event::ArrowRight;
    case SpecialKey::insert:   return Event::Insert;
    case SpecialKey::del:      return Event::Delete;
    case SpecialKey::home:     return Event::Home;
    case SpecialKey::end:      return Event::End;
    case SpecialKey::pageUp:   return Event::PageUp;
    case SpecialKey::pageDown: return Event::PageDown;
    case SpecialKey::backTab:  return Event::TabReverse;
    default:                   return Event::Special("");
    }
}

} /* namespace ms0515::files */
