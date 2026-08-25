/*
 * test_sound.cpp - the speaker path behind system register C (0177604).
 *
 * NS4 tech description 4.8, Fig.17: bit 7 gates timer channel 2, bit 6
 * enables the speaker, and bit 5 ("changing this bit changes the speaker's
 * tone") is a direct software drive - games bit-bang it for noise effects.
 * Modelled as: speaker = bit6 ? !(timer ch2 OUT xor bit5) : 0 - the polarity
 * the Spectrum ports need (BIRDS alternates 0x60 and 0x80 for its sound).
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

TEST_CASE("reg C bit 5 toggles the speaker with the timer gate off") {
    Fixture f;
    f.regc(0x40);                      /* sound on, gate low (ch2 OUT high), bit 5 = 0 */
    size_t base = f.levels.size();
    f.regc(0x60);                      /* bit 5 = 1 -> level flips */
    f.regc(0x40);
    f.regc(0x60);
    f.regc(0x40);
    REQUIRE(f.levels.size() == base + 4);
    CHECK(f.levels[base] != f.levels[base + 1]);
    CHECK(f.levels[base + 1] != f.levels[base + 2]);
    CHECK(f.levels[base + 2] != f.levels[base + 3]);
}

TEST_CASE("reg C bit 5 is silent while sound is disabled") {
    Fixture f;
    f.regc(0x00);
    size_t base = f.levels.size();
    f.regc(0x20);
    f.regc(0x00);
    f.regc(0x20);
    CHECK(f.levels.size() == base);    /* no speaker transitions */
    CHECK(f.board->sound_value == 0);
}

TEST_CASE("rewriting reg C with the same sound bits is not a transition") {
    Fixture f;
    f.regc(0x60);
    size_t base = f.levels.size();
    f.regc(0x60 | 0x05);               /* border change only */
    f.regc(0x60 | 0x02);
    CHECK(f.levels.size() == base);
}

TEST_CASE("the Spectrum ports' pattern: 0x60 / 0x80 alternating is a square wave") {
    Fixture f;
    f.regc(0x80);                      /* gate on, sound off, bit 5 = 0 */
    size_t base = f.levels.size();
    for (int i = 0; i < 4; ++i) { f.regc(0x60); f.regc(0x80); }
    REQUIRE(f.levels.size() == base + 8);
    for (int i = 0; i < 8; ++i)
        CHECK(f.levels[base + i] == (i % 2 == 0 ? 1 : 0));   /* high at 0x60, low at 0x80 */
}
