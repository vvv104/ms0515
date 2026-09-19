/*
 * test_irq_order.cpp - how the processor takes the board's interrupt
 * requests, as the NS4 system module is built (3.858.420 Э3, sheet 2).
 *
 * The requests are latched in D89 (К555ИР23) on the bus strobe; the
 * processor reads them in its read cycles (NS4 TO 4.5.2, the T-11 User's
 * Guide 1.5.1-1.5.2).  The latched requests go through D96 (К555ИВ3), a
 * priority encoder: of those present it puts one code on CP0-CP3 - the
 * monitor's (see cpu.c: the schematic has it last, and OSA's ^Q then
 * never works), else the keyboard's (MS 7004), the timer's, the serial
 * port's, the MS 7007's.  The code is the request's level (NS4 Table 4:
 * the timer 6, the keyboard 5, the monitor 4) and the processor takes it
 * when that level is above its priority; the PS in the vector is only what
 * the service runs at.
 *
 * In the core a request's line is its CP code: the timer 11, the serial
 * port 9 and 8, the keyboard 5, the MS 7007 3, the monitor 2.
 */

#include <doctest/doctest.h>

#include <initializer_list>
#include <utility>

extern "C" {
#include <ms0515/core/board.h>
#include <ms0515/core/cpu.h>
}

namespace {

constexpr uint16_t CODE_BASE  = 01000;
constexpr uint16_t INITIAL_SP = 07000;
constexpr uint16_t NOP        = 0000240;
constexpr int      LINE_MON   = 2;
constexpr int      LINE_KBD   = 5;
constexpr int      LINE_TIMER = 11;

void write_word(ms0515_board_t &board, uint16_t addr, uint16_t value)
{
    board_write_word(&board, addr, value);
}

/* A board running NOPs at CODE_BASE, priority 0; every vector given below
 * points at a NOP of its own at 02000 + the vector, with a PS of 7 as
 * RT-11 and Omega set them. */
void prepare(ms0515_board_t &board, std::initializer_list<uint16_t> vectors)
{
    board_init(&board);
    board.mem.dispatcher = 0;
    for (uint16_t i = 0; i < 16; ++i) write_word(board, (uint16_t)(CODE_BASE + i * 2), NOP);
    for (uint16_t v : vectors) {
        write_word(board, (uint16_t)(02000 + v), NOP);
        write_word(board, v, (uint16_t)(02000 + v));
        write_word(board, (uint16_t)(v + 2), 0340);
    }
    board.cpu.r[CPU_REG_PC] = CODE_BASE;
    board.cpu.r[CPU_REG_SP] = INITIAL_SP;
    board.cpu.psw           = 0;
}

/* Raise requests and let one instruction run, as a request raised by a
 * device between instructions is latched in the next one's read cycles. */
void raise(ms0515_board_t &board, std::initializer_list<std::pair<int, uint16_t>> reqs)
{
    for (auto [line, vec] : reqs) cpu_interrupt(&board.cpu, line, vec);
    cpu_step(&board.cpu);
}

/* The vector of the interrupt taken on the next step, 0 if none (the step
 * enters the service routine and runs its first instruction, a NOP). */
uint16_t taken(ms0515_board_t &board)
{
    const uint16_t sp = board.cpu.r[CPU_REG_SP];
    cpu_step(&board.cpu);
    if (board.cpu.r[CPU_REG_SP] != sp - 4) return 0;
    return (uint16_t)(board.cpu.r[CPU_REG_PC] - 02000 - 2);
}

/* Back from a service to the program at priority `prio`, nothing taken. */
void resume(ms0515_board_t &board, int prio)
{
    board.cpu.r[CPU_REG_SP] = INITIAL_SP;
    board.cpu.r[CPU_REG_PC] = CODE_BASE;
    board.cpu.psw = static_cast<uint16_t>(prio << 5);
}

}  /* namespace */

TEST_SUITE("core/interrupt order") {

TEST_CASE("the encoder puts the monitor first, then the keyboard, then the timer") {
    ms0515_board_t board;
    prepare(board, {064, 0100, 0130});
    raise(board, {{LINE_MON, 064}, {LINE_TIMER, 0100}, {LINE_KBD, 0130}});
    CHECK(taken(board) == 064);
    resume(board, 0);
    CHECK(taken(board) == 0130);
    resume(board, 0);
    CHECK(taken(board) == 0100);
}

TEST_CASE("one code at a time: the keyboard at priority 5 hides the timer") {
    /* The encoder shows the keyboard's code, level 5, which is not above
     * 5 - the timer's 6 would be, but the processor never sees it. */
    ms0515_board_t board;
    prepare(board, {0100, 0130});
    board.cpu.psw = 0240;
    raise(board, {{LINE_TIMER, 0100}, {LINE_KBD, 0130}});
    CHECK(taken(board) == 0);
    cpu_clear_interrupt(&board.cpu, LINE_KBD);          /* the key is read */
    CHECK(taken(board) == 0);                           /* latched still */
    CHECK(taken(board) == 0100);                        /* now the timer */
}

TEST_CASE("a request is taken by its line's level, not by its vector's PS") {
    /* The vectors carry PS 7, as the kits set them; the lines are 4, 5, 6. */
    ms0515_board_t board;
    prepare(board, {064, 0100, 0130});

    SUBCASE("the monitor interrupt (level 4) waits at priority 4") {
        board.cpu.psw = 0200;
        raise(board, {{LINE_MON, 064}});
        CHECK(taken(board) == 0);
        board.cpu.psw = 0140;
        CHECK(taken(board) == 064);
    }
    SUBCASE("the keyboard (level 5) waits at priority 5") {
        board.cpu.psw = 0240;
        raise(board, {{LINE_KBD, 0130}});
        CHECK(taken(board) == 0);
        board.cpu.psw = 0200;
        CHECK(taken(board) == 0130);
    }
    SUBCASE("the timer (level 6) waits at priority 6") {
        board.cpu.psw = 0300;
        raise(board, {{LINE_TIMER, 0100}});
        CHECK(taken(board) == 0);
        board.cpu.psw = 0240;
        CHECK(taken(board) == 0100);
    }
}

TEST_CASE("a vector's low PS does not hold its request back") {
    /* DEC's clock vector dismissing ticks: PS 0 in the vector, level 6 on
     * the line - taken at priority 5. */
    ms0515_board_t board;
    prepare(board, {0100});
    write_word(board, 0102, 0);
    board.cpu.psw = 0240;
    raise(board, {{LINE_TIMER, 0100}});
    CHECK(taken(board) == 0100);
}

TEST_CASE("a request set by an instruction is seen after the next one") {
    /* MOV #400,@#177400 raises the monitor's request with its write, after
     * its read cycles: the latch still holds nothing when it ends.  The
     * next instruction's fetch latches the request. */
    ms0515_board_t board;
    prepare(board, {064});
    write_word(board, CODE_BASE + 0, 0012737);
    write_word(board, CODE_BASE + 2, 0000400);
    write_word(board, CODE_BASE + 4, 0177400);
    cpu_step(&board.cpu);                               /* the MOV */
    CHECK(taken(board) == 0);                           /* the NOP after it */
    CHECK(taken(board) == 064);
}

TEST_CASE("WAIT reads the requests itself") {
    /* The priority-input cycle of WAIT: no instruction runs, the request
     * is seen at once. */
    ms0515_board_t board;
    prepare(board, {0130});
    write_word(board, CODE_BASE, 0000001);              /* WAIT */
    cpu_step(&board.cpu);
    REQUIRE(board.cpu.waiting);
    cpu_interrupt(&board.cpu, LINE_KBD, 0130);
    cpu_step(&board.cpu);                               /* enters, runs nothing */
    CHECK_FALSE(board.cpu.waiting);
    CHECK(board.cpu.r[CPU_REG_PC] == 02000 + 0130);
    CHECK(board.cpu.r[CPU_REG_SP] == INITIAL_SP - 4);
}

TEST_CASE("under ^S the output service goes again first, the key in its window") {
    /* OSA's and Omega's output interrupt under ^S asks for itself before
     * its RTI.  ^Q arriving then must not be taken at the RTI - OSA's key
     * service lowers the priority at once, and the monitor's request would
     * nest in it for good - but in the window at priority 0 the output
     * service opens before it asks again. */
    ms0515_board_t board;
    prepare(board, {064, 0130});
    raise(board, {{LINE_MON, 064}});
    CHECK(taken(board) == 064);
    cpu_interrupt(&board.cpu, LINE_KBD, 0130);          /* ^Q arrives */
    cpu_interrupt(&board.cpu, LINE_MON, 064);           /* the service asks again */
    cpu_step(&board.cpu);                               /* latched */
    board.cpu.psw = 0;                                  /* its RTI */
    CHECK(taken(board) == 064);                         /* the service again */
    cpu_step(&board.cpu);                               /* it has cleared bit 8: */
    board.cpu.psw = 0;                                  /* the window */
    CHECK(taken(board) == 0130);                        /* the key */
}

}
