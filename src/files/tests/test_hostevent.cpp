/*
 * test_hostevent.cpp — a host key as the FTXUI event the commander
 * understands.
 */
#include "HostEvent.hpp"

#include <doctest/doctest.h>

using namespace ms0515::files;
using ftxui::Event;

TEST_CASE("bytes: the named keys, characters, KOI-8 letters as UTF-8, other controls as specials")
{
    CHECK(toEvent(HostKey::ofByte(0x0D)) == Event::Return);
    CHECK(toEvent(HostKey::ofByte(0x09)) == Event::Tab);
    CHECK(toEvent(HostKey::ofByte(0x1B)) == Event::Escape);
    CHECK(toEvent(HostKey::ofByte(0x08)) == Event::Backspace);
    CHECK(toEvent(HostKey::ofByte(0x7F)) == Event::Backspace);
    CHECK(toEvent(HostKey::ofByte('r')) == Event::Character("r"));
    CHECK(toEvent(HostKey::ofByte(' ')) == Event::Character(" "));
    CHECK(toEvent(HostKey::ofByte(0xC1)) == Event::Character("\xD0\xB0"));   /* KOI-8 а */
    CHECK(toEvent(HostKey::ofByte(0x03)) == Event::Special(std::string("\x03")));
}

TEST_CASE("specials: arrows, editing keys, F-keys, and Alt+F as xterm's modified sequence")
{
    CHECK(toEvent(HostKey::ofSpecial(SpecialKey::up)) == Event::ArrowUp);
    CHECK(toEvent(HostKey::ofSpecial(SpecialKey::down)) == Event::ArrowDown);
    CHECK(toEvent(HostKey::ofSpecial(SpecialKey::left)) == Event::ArrowLeft);
    CHECK(toEvent(HostKey::ofSpecial(SpecialKey::right)) == Event::ArrowRight);
    CHECK(toEvent(HostKey::ofSpecial(SpecialKey::insert)) == Event::Insert);
    CHECK(toEvent(HostKey::ofSpecial(SpecialKey::del)) == Event::Delete);
    CHECK(toEvent(HostKey::ofSpecial(SpecialKey::home)) == Event::Home);
    CHECK(toEvent(HostKey::ofSpecial(SpecialKey::end)) == Event::End);
    CHECK(toEvent(HostKey::ofSpecial(SpecialKey::pageUp)) == Event::PageUp);
    CHECK(toEvent(HostKey::ofSpecial(SpecialKey::pageDown)) == Event::PageDown);
    CHECK(toEvent(HostKey::ofSpecial(SpecialKey::backTab)) == Event::TabReverse);
    CHECK(toEvent(HostKey::ofSpecial(SpecialKey::f1)) == Event::F1);
    CHECK(toEvent(HostKey::ofSpecial(SpecialKey::f10)) == Event::F10);
    CHECK(toEvent(HostKey::ofSpecial(SpecialKey::f12)) == Event::F12);
    CHECK(toEvent(HostKey::ofSpecial(SpecialKey::f1, true)).input() == "\x1B[1;3P");
    CHECK(toEvent(HostKey::ofSpecial(SpecialKey::f2, true)).input() == "\x1B[1;3Q");
    CHECK(toEvent(HostKey::ofSpecial(SpecialKey::f5, true)).input() == "\x1B[15;3~");
}
