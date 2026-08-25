/*
 * test_sound.cpp - the speaker path behind system register C (0177604).
 *
 * NS4 tech description 4.8, Fig.17: bit 7 gates timer channel 2, bit 6
 * enables the speaker: speaker = bit6 ? timer ch2 OUT : 0.  Bit 5 ("changing
 * this bit changes the speaker's tone") is not modelled - no program at hand
 * needs it: the Spectrum ports bit-bang their sound by alternating 0x60 and
 * 0x80 (bit 6 with the gate closed, so OUT is held high), which this pins.
 */
#include <doctest/doctest.h>

extern "C" {
#include <ms0515/core/board.h>
}

#include <memory>
#include <vector>

namespace {

constexpr uint16_t REG_C = 0177604;

struct Fixture {
    std::unique_ptr<ms0515_board_t> board{new ms0515_board_t};
    std::vector<int> levels;
    Fixture()
    {
        board_init(board.get());
        board_set_sound_callback(board.get(),
            [](void *u, int v) { static_cast<std::vector<int> *>(u)->push_back(v); },
            &levels);
    }
    void regc(uint8_t v) { board_write_byte(board.get(), REG_C, v); }
};

}  // namespace

TEST_CASE("the Spectrum ports' pattern: 0x60 / 0x80 alternating is a square wave") {
    Fixture f;
    f.regc(0x80);                      /* gate on, sound off, bit 5 = 0 */
    size_t base = f.levels.size();
    for (int i = 0; i < 4; ++i) { f.regc(0x60); f.regc(0x80); }
    REQUIRE(f.levels.size() == base + 8);
    for (int i = 0; i < 8; ++i)
        CHECK(f.levels[base + i] == (i % 2 == 0 ? 1 : 0));   /* high at 0x60, low at 0x80 */
}

TEST_CASE("bit 6 alone with the gate closed is the same square wave") {
    Fixture f;
    f.regc(0x00);
    size_t base = f.levels.size();
    for (int i = 0; i < 4; ++i) { f.regc(0x40); f.regc(0x00); }
    REQUIRE(f.levels.size() == base + 8);
    for (int i = 0; i < 8; ++i)
        CHECK(f.levels[base + i] == (i % 2 == 0 ? 1 : 0));
}

TEST_CASE("reg C bit 5 changes nothing (not modelled)") {
    Fixture f;
    f.regc(0x40);
    size_t base = f.levels.size();
    f.regc(0x60);
    f.regc(0x40);
    f.regc(0x60);
    CHECK(f.levels.size() == base);
}

TEST_CASE("the speaker is silent while sound is disabled") {
    Fixture f;
    f.regc(0x00);
    size_t base = f.levels.size();
    f.regc(0x20);
    f.regc(0x80);
    f.regc(0xA0);
    CHECK(f.levels.size() == base);    /* no speaker transitions */
    CHECK(f.board->sound_value == 0);
}
