/*
 * test_frame_rate.cpp - the frame rate the board runs at, and what the
 * software reads of it.
 *
 * The rate is the jumpers E3/E4 (NS4 TO Table 6): both fitted 50 Hz, E4
 * alone 60 Hz (260 lines of 64 us, 16.7 ms), E3 alone 72 Hz.  The
 * software reads them back as РВК1/РВК0, bits 4-3 of System Register B
 * (Table 10: 00 = 50, 01 = 72, 1x = 60).  The machines ran at 50 Hz: the
 * timer interrupt is the frame sync itself (D60.1 is clocked by КСИ), the
 * RT-11 kits of two bureaus count it as a 50-cycle clock, and the
 * machines kept time in banks - at 60 Hz their clocks would have gained
 * 4.8 hours a day.  So the board starts at 50 Hz: a frame of 7 500 000 /
 * 50 = 150 000 CPU cycles (docs/hardware/board.md).
 */

#include <doctest/doctest.h>

extern "C" {
#include <ms0515/core/board.h>
}

namespace {

constexpr uint16_t REG_B = 0177602;
constexpr uint16_t NOP   = 0000240;

/* A board running NOPs at 01000, interrupts off. */
void prepare(ms0515_board_t &board)
{
    board_init(&board);
    board.mem.dispatcher = 0;
    for (uint16_t i = 0; i < 32; ++i)
        board_write_word(&board, (uint16_t)(01000 + i * 2), NOP);
    board_write_word(&board, 01100, 0000137);   /* JMP @#01000 */
    board_write_word(&board, 01102, 01000);
    board.cpu.r[CPU_REG_PC] = 01000;
    board.cpu.r[CPU_REG_SP] = 07000;
    board.cpu.psw           = 0340;
}

} /* namespace */

TEST_CASE("the board starts at 50 Hz, as the machines kept time")
{
    ms0515_board_t board;
    prepare(board);

    CHECK(board.dip_refresh == 0);
    CHECK(((board_read_byte(&board, REG_B) >> 3) & 0x03) == 0x00);   /* РВК1 = РВК0 = 0 */
    CHECK(board_frame_cycles(&board) == 150000);

    /* A frame is that many cycles, give or take the last instruction. */
    const uint64_t before = board.total_cycles;
    CHECK(board_step_frame(&board));
    const uint64_t ran = board.total_cycles - before;
    CHECK(ran >= 150000);
    CHECK(ran <  150000 + 16);
}

TEST_CASE("the jumpers' other settings keep their rates")
{
    ms0515_board_t board;
    prepare(board);

    board.dip_refresh = 0;                                          /* E3 + E4 */
    CHECK(board_frame_cycles(&board) == 150000);
    CHECK(((board_read_byte(&board, REG_B) >> 3) & 0x03) == 0x00);

    board.dip_refresh = 1;                                          /* E3 alone */
    CHECK(board_frame_cycles(&board) == 104167);
    CHECK(((board_read_byte(&board, REG_B) >> 3) & 0x03) == 0x01);

    board.dip_refresh = 2;                                          /* E4 alone */
    CHECK(board_frame_cycles(&board) == 125000);
    CHECK(((board_read_byte(&board, REG_B) >> 3) & 0x03) == 0x02);
}
