/*
 * test_joystick.cpp - the joystick on the MS7007 PPI's port B (0177542).
 *
 * Five switches to ground on bits 0-4 - right, left, down, up, fire (the
 * Kempston order) - so the port reads the held lines low and the open ones
 * high; SABOT2 (1991) reads it as MOV @#177542,R0 / COM R0 / BIC #177740,R0.
 * Port A is the matrix rows' output latch (readable back), port C and the
 * control word read as open lines.
 */
#include <doctest/doctest.h>

extern "C" {
#include <ms0515/core/board.h>
}

#include <memory>

namespace {

constexpr uint16_t PPI2_A = 0177540, PPI2_B = 0177542, PPI2_C = 0177544, PPI2_CTRL = 0177546;

struct Fixture {
    std::unique_ptr<ms0515_board_t> board{new ms0515_board_t};
    Fixture() { board_init(board.get()); }
    uint8_t  rb(uint16_t a) { return board_read_byte(board.get(), a); }
    uint16_t rw(uint16_t a) { return board_read_word(board.get(), a); }
};

}  // namespace

TEST_CASE("joystick: nothing held reads all lines high") {
    Fixture f;
    CHECK(f.rb(PPI2_B) == 0xFF);
    CHECK((f.rw(PPI2_B) & 0x1F) == 0x1F);               /* what the game masks */
}

TEST_CASE("joystick: held lines read low, in the Kempston order") {
    Fixture f;
    board_set_joystick(f.board.get(), MS0515_JOY_UP | MS0515_JOY_FIRE);
    CHECK(f.rb(PPI2_B) == (uint8_t)~(0x08 | 0x10));
    CHECK(((~f.rw(PPI2_B)) & 0x1F) == 0x18);             /* COM + BIC #177740: up + fire */
    board_set_joystick(f.board.get(), MS0515_JOY_LEFT | MS0515_JOY_DOWN);
    CHECK(((~f.rw(PPI2_B)) & 0x1F) == 0x06);
    board_set_joystick(f.board.get(), 0);
    CHECK(f.rb(PPI2_B) == 0xFF);
}

TEST_CASE("joystick: only the five lines are a joystick") {
    Fixture f;
    board_set_joystick(f.board.get(), 0xFF);
    CHECK(f.rb(PPI2_B) == 0xE0);                          /* bits 5-7 stay open */
}

TEST_CASE("MS7007 PPI: port A latches, port C and the control word read open") {
    Fixture f;
    board_write_byte(f.board.get(), PPI2_CTRL, 0213);     /* the ROM's mode word */
    board_write_byte(f.board.get(), PPI2_A, 0377);
    CHECK(f.rb(PPI2_A) == 0377);
    board_write_byte(f.board.get(), PPI2_A, 0);           /* the monitor after a key */
    CHECK(f.rb(PPI2_A) == 0);
    CHECK(f.rb(PPI2_C) == 0xFF);
    CHECK(f.rb(PPI2_CTRL) == 0xFF);
    CHECK(f.board->ppi2_control == 0213);
}

TEST_CASE("joystick: the host's state survives a reset, the latch does not") {
    Fixture f;
    board_set_joystick(f.board.get(), MS0515_JOY_RIGHT);
    board_write_byte(f.board.get(), PPI2_A, 0377);
    board_reset(f.board.get());
    CHECK(f.rb(PPI2_B) == 0xFE);
    CHECK(f.rb(PPI2_A) == 0);
}
