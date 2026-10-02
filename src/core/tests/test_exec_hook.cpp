/*
 * test_exec_hook.cpp - the CPU's execution hook: a host function called
 * each time the processor is about to execute the instruction at one
 * chosen address.  It observes; the instruction then runs as ever.
 *
 * The host uses it to see calls of a routine that leaves no other trace -
 * the ROM's character output, called with the character in R0.
 */

#include <doctest/doctest.h>

extern "C" {
#include <ms0515/core/board.h>
#include <ms0515/core/cpu.h>
}

namespace {

constexpr uint16_t CODE_BASE  = 01000;
constexpr uint16_t INITIAL_SP = 07000;
constexpr uint16_t NOP        = 0000240;
constexpr uint16_t INC_R0     = 0005200;

struct Record {
    int      count;
    uint16_t pc;
    uint16_t r0;
};
Record g_seen{};

extern "C" void test_exec_hook(ms0515_cpu_t *cpu)
{
    g_seen.count++;
    g_seen.pc = cpu->r[CPU_REG_PC];
    g_seen.r0 = cpu->r[0];
}

void prepare_board(ms0515_board_t &board)
{
    g_seen = {};
    board_init(&board);
    board.cpu.r[CPU_REG_PC] = CODE_BASE;
    board.cpu.r[CPU_REG_SP] = INITIAL_SP;
    board.cpu.psw           = 0;
}

}  // namespace

TEST_SUITE("CPU exec_hook") {

TEST_CASE("the hook fires each time its address is executed, before the instruction") {
    ms0515_board_t board{};
    prepare_board(board);

    /* 1000: INC R0 / 1002: INC R0 / 1004: BR 1000 */
    board_write_word(&board, CODE_BASE,     INC_R0);
    board_write_word(&board, CODE_BASE + 2, INC_R0);
    board_write_word(&board, CODE_BASE + 4, 0000775);

    board.cpu.exec_hook_pc[0] = CODE_BASE + 2;
    board.cpu.exec_hook_count = 1;
    board.cpu.exec_hook    = &test_exec_hook;

    cpu_step(&board.cpu);                       /* 1000 */
    CHECK(g_seen.count == 0);
    cpu_step(&board.cpu);                       /* 1002 */
    CHECK(g_seen.count == 1);
    CHECK(g_seen.pc == CODE_BASE + 2);          /* not yet advanced       */
    CHECK(g_seen.r0 == 1);                      /* the INC there not yet run */
    CHECK(board.cpu.r[0] == 2);                 /* and then it ran        */

    for (int i = 0; i < 3 * 4; ++i)             /* four more turns */
        cpu_step(&board.cpu);
    CHECK(g_seen.count == 5);
    CHECK(g_seen.r0 == 9);
}

TEST_CASE("one hook watches several addresses and is told which it is at") {
    ms0515_board_t board{};
    prepare_board(board);

    /* 1000: INC R0 / 1002: INC R0 / 1004: INC R0 / 1006: BR 1000 */
    board_write_word(&board, CODE_BASE,     INC_R0);
    board_write_word(&board, CODE_BASE + 2, INC_R0);
    board_write_word(&board, CODE_BASE + 4, INC_R0);
    board_write_word(&board, CODE_BASE + 6, 0000774);

    board.cpu.exec_hook_pc[0] = CODE_BASE;
    board.cpu.exec_hook_pc[1] = CODE_BASE + 4;
    board.cpu.exec_hook_count = 2;
    board.cpu.exec_hook       = &test_exec_hook;

    cpu_step(&board.cpu);
    CHECK(g_seen.count == 1);
    CHECK(g_seen.pc == CODE_BASE);
    cpu_step(&board.cpu);                       /* 1002: not watched */
    CHECK(g_seen.count == 1);
    cpu_step(&board.cpu);
    CHECK(g_seen.count == 2);
    CHECK(g_seen.pc == CODE_BASE + 4);
    CHECK(board.cpu.instruction_pc == CODE_BASE + 4);

    /* An address past the count is not watched. */
    board.cpu.exec_hook_pc[2] = CODE_BASE + 6;
    cpu_step(&board.cpu);
    CHECK(g_seen.count == 2);
}

TEST_CASE("no hook, or a hook taken off, leaves execution alone") {
    ms0515_board_t board{};
    prepare_board(board);
    CHECK(board.cpu.exec_hook == nullptr);

    board_write_word(&board, CODE_BASE,     NOP);
    board_write_word(&board, CODE_BASE + 2, NOP);
    board_write_word(&board, CODE_BASE + 4, 0000775);

    board.cpu.exec_hook_pc[0] = CODE_BASE;
    board.cpu.exec_hook_count = 1;
    board.cpu.exec_hook    = &test_exec_hook;
    for (int i = 0; i < 3; ++i) cpu_step(&board.cpu);
    CHECK(g_seen.count == 1);

    board.cpu.exec_hook = nullptr;
    for (int i = 0; i < 6; ++i) cpu_step(&board.cpu);
    CHECK(g_seen.count == 1);
    CHECK(board.cpu.r[CPU_REG_PC] == CODE_BASE);
}

TEST_CASE("an interrupt taken at the hooked address does not fire the hook twice") {
    ms0515_board_t board{};
    prepare_board(board);

    /* The hooked instruction, and an interrupt whose handler is one RTI. */
    constexpr uint16_t HANDLER = 02000;
    board_write_word(&board, CODE_BASE,     NOP);
    board_write_word(&board, CODE_BASE + 2, NOP);
    board_write_word(&board, HANDLER,       0000002);   /* RTI */
    board_write_word(&board, CPU_VEC_EMT,     HANDLER);
    board_write_word(&board, CPU_VEC_EMT + 2, 0);

    board.cpu.exec_hook_pc[0] = CODE_BASE;
    board.cpu.exec_hook_count = 1;
    board.cpu.exec_hook    = &test_exec_hook;

    /* With PC at the hooked address a request is pending: the step
     * services it and executes the handler's RTI instead. */
    board.cpu.irq_emt = true;
    cpu_step(&board.cpu);
    CHECK(g_seen.count == 0);
    CHECK(board.cpu.r[CPU_REG_PC] == CODE_BASE);        /* back from RTI */

    cpu_step(&board.cpu);                               /* now the NOP   */
    CHECK(g_seen.count == 1);
    CHECK(board.cpu.r[CPU_REG_PC] == CODE_BASE + 2);
}

}
