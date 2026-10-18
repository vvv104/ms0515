/*
 * test_external_keyboard.cpp - a keyboard outside the emulator on the
 * keyboard's serial line, in place of the MS 7004 model.
 *
 * The line is the one the real cable carries: the bytes the machine
 * writes to the keyboard USART's transmitter go out, the bytes that come
 * in reach its receiver at the line's pace.  A fake keyboard stands in
 * for the outside one here: it answers the reset and the ID probe the
 * way the MS 7004 does and types scancodes on demand.
 */
#include <doctest/doctest.h>

#include <ms0515/Emulator.hpp>
#include <ms0515/Terminal.hpp>
#include "EmulatorInternal.hpp"
#include "test_disk.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#ifndef ASSETS_DIR
#error "ASSETS_DIR must be defined by the build system"
#endif

namespace {

using ms0515_test::TempDisk;

/* An MS 7004 as the machine sees it down the cable: what it was sent,
 * and what it has to say. */
struct FakeKeyboard {
    std::vector<uint8_t> received;
    std::deque<uint8_t>  toSend;
    int                  taken = 0;

    void fromMachine(uint8_t byte)
    {
        received.push_back(byte);
        if (byte == 0xFD)                     /* power-up reset */
            toSend.insert(toSend.end(), {0x01, 0x00, 0x00, 0x00});
        else if (byte == 0xAB)                /* ID probe */
            toSend.insert(toSend.end(), {0x01, 0x00});
    }

    bool next(uint8_t &byte)
    {
        if (toSend.empty()) return false;
        byte = toSend.front();
        toSend.pop_front();
        ++taken;
        return true;
    }

    /* A key pressed and let go: its code, then ALL-UP. */
    void tap(uint8_t code) { toSend.insert(toSend.end(), {code, 0263}); }

    void attach(ms0515::Emulator &emu)
    {
        emu.setExternalKeyboard([this](uint8_t &b) { return next(b); },
                                [this](uint8_t b) { fromMachine(b); });
    }
};

constexpr uint8_t kScD = 0354, kScI = 0331, kScR = 0335, kScReturn = 0275;

void runFrames(ms0515::Emulator &emu, int frames)
{
    for (int i = 0; i < frames; ++i)
        (void)emu.stepFrame();
}

/* The row the dot prompt is on, the last one starting with '.'. */
int promptRow(const ms0515::Terminal::Snapshot &snap)
{
    for (int r = ms0515::Terminal::kRows - 1; r >= 0; --r) {
        const std::string row = snap.row(r);
        if (!row.empty() && row[0] == '.') return r;
    }
    return -1;
}

int bootToPrompt(ms0515::Emulator &emu)
{
    ms0515::Terminal term;
    int prev = -1, stable = 0;
    for (int frame = 0; frame < 3000; ++frame) {
        (void)emu.stepFrame();
        const int row = promptRow(term.decode(emu));
        stable = (row >= 0 && row == prev) ? stable + 1 : 0;
        if (stable >= 20) return row;
        prev = row;
    }
    return -1;
}

const ms0515_keyboard_t &usart(ms0515::Emulator &emu)
{
    return ms0515::internal::board(emu).kbd;
}

} // namespace

TEST_SUITE("External keyboard") {

TEST_CASE("the ROM talks to the keyboard down the line, and RT-11 takes its keys") {
    TempDisk disk{TESTS_DIR "/disks/test_osa.dsk"};
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(ASSETS_DIR "/rom/ms0515-roma.rom"));
    REQUIRE(emu.mountDisk(0, disk.path().string()));
    emu.reset();
    FakeKeyboard kbd;
    kbd.attach(emu);
    CHECK(emu.externalKeyboard());

    const int row = bootToPrompt(emu);
    REQUIRE(row >= 0);
    /* The ROM's self-test asks the keyboard who it is (ID probe, 0253). */
    CHECK(std::count(kbd.received.begin(), kbd.received.end(), uint8_t{0xAB}) >= 1);

    for (uint8_t code : {kScD, kScI, kScR}) {
        kbd.tap(code);
        runFrames(emu, 10);
    }
    ms0515::Terminal term;
    CHECK(term.decode(emu).row(row).rfind(".DIR", 0) == 0);
    kbd.tap(kScReturn);
    runFrames(emu, 150);
    CHECK(promptRow(term.decode(emu)) > row);
}

TEST_CASE("the host's keys and the model stay out while the keyboard is outside") {
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(ASSETS_DIR "/rom/ms0515-roma.rom"));
    emu.reset();
    FakeKeyboard kbd;
    kbd.attach(emu);
    runFrames(emu, 400);                    /* the self-test is over */
    const int before = kbd.taken;

    emu.keyPress(ms0515::Key::D, true);
    runFrames(emu, 2);
    emu.keyPress(ms0515::Key::D, false);
    runFrames(emu, 2);
    CHECK(usart(emu).fifo_count == 0);
    CHECK_FALSE(usart(emu).rx_ready);
    CHECK(kbd.taken == before);
}

TEST_CASE("the line is asked for a byte only while the USART's FIFO has room") {
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(ASSETS_DIR "/rom/ms0515-roma.rom"));
    emu.reset();
    /* A keyboard with a long burst to send, asked only when there is room:
     * no byte is pushed into a full FIFO, so none is dropped. */
    int asked = 0, full = 0, sent = 0;
    emu.setExternalKeyboard(
        [&](uint8_t &byte) {
            ++asked;
            if (usart(emu).fifo_count >= static_cast<int>(sizeof usart(emu).fifo)) ++full;
            if (sent >= 200) return false;
            byte = 0300;
            ++sent;
            return true;
        },
        [](uint8_t) {});
    runFrames(emu, 100);
    CHECK(asked > 0);
    CHECK(sent > 0);
    CHECK(full == 0);
}

TEST_CASE("the lamps and the held modifiers are the line's, not the model's") {
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(ASSETS_DIR "/rom/ms0515-roma.rom"));
    emu.reset();
    FakeKeyboard kbd;
    kbd.attach(emu);
    runFrames(emu, 400);
    auto &board = ms0515::internal::board(emu);
    auto toKeyboard = [&](uint8_t b) { kbd_write(&board.kbd, 0, b); };

    /* The machine's self-test lit the Latin lamp: ЛАТ, not РУС. */
    CHECK_FALSE(emu.ruslatOn());

    /* The machine's lamp commands: Latin off (РУС), Latin on (ЛАТ). */
    toKeyboard(0x13); toKeyboard(0xA7);     /* 0x13 standalone, then a bell */
    CHECK(emu.ruslatOn());
    toKeyboard(0x11); toKeyboard(0x90);
    CHECK_FALSE(emu.ruslatOn());

    /* ФКС's lamp: 0x13 + mask with bit 2 on, 0x11 + the same mask off. */
    CHECK_FALSE(emu.capsOn());
    toKeyboard(0x13); toKeyboard(0x84);
    CHECK(emu.capsOn());
    CHECK_FALSE(emu.ruslatOn());            /* a mask is no Latin command */
    toKeyboard(0x11); toKeyboard(0x84);
    CHECK_FALSE(emu.capsOn());

    /* The other lamps: Ожидание, Композиция (КМП's), Стоп-кадр. */
    toKeyboard(0x13); toKeyboard(0x82);
    CHECK(emu.keyboardLamps().compose);
    CHECK_FALSE(emu.keyHeld(ms0515::Key::Compose));
    CHECK_FALSE(emu.capsOn());
    toKeyboard(0x13); toKeyboard(0x89);
    CHECK(emu.keyboardLamps().wait);
    CHECK(emu.keyboardLamps().hold);
    toKeyboard(0x11); toKeyboard(0x8B);
    CHECK_FALSE(emu.keyboardLamps().wait);
    CHECK_FALSE(emu.keyboardLamps().compose);
    CHECK_FALSE(emu.keyboardLamps().hold);

    /* The register keys - ВР, СУ - are held from their code to ALL-UP. */
    kbd.toSend.insert(kbd.toSend.end(), {0256, 0257});
    runFrames(emu, 2);
    CHECK(emu.keyHeld(ms0515::Key::ShiftL));
    CHECK(emu.keyHeld(ms0515::Key::Ctrl));
    kbd.toSend.push_back(0263);
    runFrames(emu, 2);
    CHECK_FALSE(emu.keyHeld(ms0515::Key::ShiftL));
    CHECK_FALSE(emu.keyHeld(ms0515::Key::Ctrl));

    /* КМП's own code is an ordinary key's: no release follows, and it
     * holds nothing. */
    kbd.toSend.push_back(0261);
    runFrames(emu, 2);
    CHECK_FALSE(emu.keyHeld(ms0515::Key::Compose));
}

TEST_CASE("without the line the MS 7004 model is back") {
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(ASSETS_DIR "/rom/ms0515-roma.rom"));
    emu.reset();
    FakeKeyboard kbd;
    kbd.attach(emu);
    runFrames(emu, 400);
    emu.setExternalKeyboard({}, {});
    CHECK_FALSE(emu.externalKeyboard());

    auto &board = ms0515::internal::board(emu);
    board.cpu.psw |= 0340;
    emu.keyPress(ms0515::Key::D, true);
    board.cpu.psw |= 0340;
    (void)emu.stepFrame();
    CHECK((usart(emu).fifo_count > 0 || usart(emu).rx_ready));
}

} // TEST_SUITE
