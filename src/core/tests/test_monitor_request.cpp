/*
 * test_monitor_request.cpp - dispatcher bit 8, the monitor's interrupt
 * request (vector 064).
 *
 * NS4 4.3: "1 sets the request from the monitor, 0 resets it".  So a write
 * of 1 makes the interrupt pending, and a write of 0 takes a pending
 * request back - it must never raise one.  RT-11's terminal service
 * leans on that: it clears bit 8 as it enters, prints a character with
 * the priority briefly lowered, sets bit 8 for the next character, and
 * returns.  Raising a request on the clear let the service re-enter
 * itself at that lowered priority, before its RTI, once per character:
 * the stack ran down into the vector page and `?MON-F-Stack overflow`
 * stopped any long TYPE on OSA and Omega alike.
 */

#include <doctest/doctest.h>

extern "C" {
#include <ms0515/core/board.h>
#include <ms0515/core/cpu.h>
}

namespace {

constexpr uint16_t CODE_BASE  = 01000;
constexpr uint16_t ISR_BASE   = 02000;
constexpr uint16_t INITIAL_SP = 07000;
constexpr uint16_t NOP        = 0000240;
constexpr uint16_t DISPATCHER = 0177400;
constexpr uint16_t BIT8       = 0000400;

void write_word(ms0515_board_t &board, uint16_t addr, uint16_t value)
{
    board_write_word(&board, addr, value);
}

/* A board running NOPs at CODE_BASE; vector 064 points at ISR_BASE. */
void prepare(ms0515_board_t &board, int priority)
{
    board_init(&board);
    board.mem.dispatcher = 0;
    for (uint16_t i = 0; i < 16; ++i) write_word(board, (uint16_t)(CODE_BASE + i * 2), NOP);
    for (uint16_t i = 0; i < 4; ++i) write_word(board, (uint16_t)(ISR_BASE + i * 2), NOP);
    write_word(board, 064, ISR_BASE);
    write_word(board, 066, 0340);
    board.cpu.r[CPU_REG_PC] = CODE_BASE;
    board.cpu.r[CPU_REG_SP] = INITIAL_SP;
    board.cpu.psw           = (uint16_t)(priority << 5);
}

/* The requests as the processor's next read cycle latches them (the
 * NS4 board: a request is seen through the latch, not the moment it is
 * written - core/tests/test_irq_order.cpp). */
void latch(ms0515_board_t &board)
{
    cpu_sample_requests(&board.cpu);
}

/* One instruction: did the processor enter the service routine on it? */
bool taken(ms0515_board_t &board)
{
    const uint16_t sp = board.cpu.r[CPU_REG_SP];
    cpu_step(&board.cpu);
    const uint16_t pc = board.cpu.r[CPU_REG_PC];
    return pc >= ISR_BASE && pc < ISR_BASE + 8 && board.cpu.r[CPU_REG_SP] == sp - 4;
}

}  /* namespace */

TEST_SUITE("core/monitor request") {

TEST_CASE("bit 8 written 1 requests the monitor interrupt; taken once") {
    ms0515_board_t board;
    prepare(board, 0);
    write_word(board, DISPATCHER, BIT8);
    latch(board);
    CHECK(taken(board));
    board.cpu.psw = 0;
    CHECK_FALSE(taken(board));                       /* one request, one service */
}

TEST_CASE("bit 8 written 0 takes a pending request back, and raises none of its own") {
    ms0515_board_t board;
    prepare(board, 7);                               /* held off: it stays pending */
    write_word(board, DISPATCHER, BIT8);
    latch(board);
    write_word(board, DISPATCHER, 0);                /* reset: NS4 4.3 */
    latch(board);
    board.cpu.psw = 0;
    CHECK_FALSE(taken(board));

    prepare(board, 0);
    write_word(board, DISPATCHER, 0);                /* a write of 0 on 0: nothing */
    latch(board);
    CHECK_FALSE(taken(board));
}

TEST_CASE("the service routine's own clear must not re-enter it: clear, lower the priority, no interrupt") {
    ms0515_board_t board;
    prepare(board, 0);
    write_word(board, DISPATCHER, BIT8);             /* the main line asks for a character */
    latch(board);
    REQUIRE(taken(board));                           /* the service is entered, at 7 */
    REQUIRE(board.cpu.psw == 0340);
    write_word(board, DISPATCHER, 0);                /* the service acknowledges: bit 8 off */
    latch(board);
    board.cpu.psw = 0;                               /* it lowers the priority to print */
    CHECK_FALSE(taken(board));                       /* nothing pending: no re-entry */
    write_word(board, DISPATCHER, BIT8);             /* it asks for the next character */
    latch(board);
    CHECK(taken(board));                             /* and that one is taken */
}

TEST_CASE("bit 8 written 1 while already 1 is still one request") {
    ms0515_board_t board;
    prepare(board, 7);
    write_word(board, DISPATCHER, BIT8);
    latch(board);
    write_word(board, DISPATCHER, BIT8);
    latch(board);
    board.cpu.psw = 0;
    CHECK(taken(board));
    board.cpu.psw = 0;
    CHECK_FALSE(taken(board));
}

}
