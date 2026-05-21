/*
 * test_trap_thunk.cpp — CPU trap-thunk hook for user-mode emulator.
 *
 * The `cpu->trap_thunk` pointer is the single hook that lets a user-mode
 * runtime intercept EMT/TRAP/IOT instructions before they touch the guest
 * stack and vector table.  With no thunk installed, EMT/TRAP/IOT must
 * still service normally (push PSW+PC, load new PC/PSW from the vector).
 * With a thunk installed, the thunk is called instead; the stack and the
 * PC remain at whatever the executed EMT/TRAP/IOT instruction left them,
 * so the host-side runtime can manipulate registers and PSW directly.
 */

#include <doctest/doctest.h>
#include <cstring>

extern "C" {
#include <ms0515/core/board.h>
#include <ms0515/core/cpu.h>
}

namespace {

constexpr uint16_t CODE_BASE   = 01000;  /* EMT/TRAP test instruction      */
constexpr uint16_t VECTOR_TGT  = 02000;  /* Where the vector points        */
constexpr uint16_t INITIAL_SP  = 07000;
constexpr uint16_t VECTOR_PSW  = 0340;

/* NOP (000240) is the cleanest sentinel — it does nothing, leaving PSW
 * intact so the test can assert exact PSW values across the trap service. */
constexpr uint16_t NOP_OPCODE  = 0000240;

struct ThunkRecord {
    int      count;
    uint16_t vector;
    uint16_t instruction;
    uint16_t instruction_pc;
    uint16_t r[8];
    uint16_t psw;
};

ThunkRecord g_thunk{};

extern "C" void test_thunk(ms0515_cpu_t *cpu, uint16_t vector)
{
    g_thunk.count++;
    g_thunk.vector         = vector;
    g_thunk.instruction    = cpu->instruction;
    g_thunk.instruction_pc = cpu->instruction_pc;
    std::memcpy(g_thunk.r, cpu->r, sizeof(cpu->r));
    g_thunk.psw            = cpu->psw;
}

/*
 * Bring a board up far enough to run a couple of instructions out of RAM.
 * After board_init the dispatcher is zero (all-extended banks), which
 * still maps RAM at 0..0157777 — fine for these tests.  PC is set
 * explicitly so we don't depend on the ROM boot vector.
 */
void prepare_board(ms0515_board_t &board)
{
    std::memset(&g_thunk, 0, sizeof(g_thunk));
    board_init(&board);
    board.cpu.r[CPU_REG_PC] = CODE_BASE;
    board.cpu.r[CPU_REG_SP] = INITIAL_SP;
    board.cpu.psw           = 0;
}

void write_word(ms0515_board_t &board, uint16_t addr, uint16_t value)
{
    board_write_word(&board, addr, value);
}

uint16_t read_word(ms0515_board_t &board, uint16_t addr)
{
    return board_read_word(&board, addr);
}

}  // namespace

TEST_SUITE("CPU trap_thunk") {

TEST_CASE("without thunk EMT services through the vector (push PSW+PC, load vector)") {
    ms0515_board_t board{};
    prepare_board(board);

    /* Code: EMT 7 at CODE_BASE, NOP at VECTOR_TGT. */
    write_word(board, CODE_BASE,  0104007);
    write_word(board, VECTOR_TGT, NOP_OPCODE);

    /* EMT vector: PC=VECTOR_TGT, PSW=VECTOR_PSW. */
    write_word(board, CPU_VEC_EMT,     VECTOR_TGT);
    write_word(board, CPU_VEC_EMT + 2, VECTOR_PSW);

    /* Step 1: fetch & execute EMT — sets irq_emt, advances PC past EMT. */
    cpu_step(&board.cpu);
    CHECK(board.cpu.irq_emt == true);
    CHECK(board.cpu.r[CPU_REG_PC] == CODE_BASE + 2);

    /* Step 2: service EMT (push PSW+PC, load vector), then fetch+execute
     * the NOP at the vector target. */
    cpu_step(&board.cpu);

    /* PC moved past the NOP at the vector target. */
    CHECK(board.cpu.r[CPU_REG_PC] == VECTOR_TGT + 2);
    /* SP decreased by 4 (one push for PSW, one for PC). */
    CHECK(board.cpu.r[CPU_REG_SP] == INITIAL_SP - 4);
    /* Stack contains the pushed PC (top) and PSW (below). */
    CHECK(read_word(board, INITIAL_SP - 2) == 0);              /* old PSW */
    CHECK(read_word(board, INITIAL_SP - 4) == CODE_BASE + 2);  /* old PC */
    /* PSW now reflects the vector PSW (NOP does not touch PSW). */
    CHECK(board.cpu.psw == VECTOR_PSW);
    /* No thunk was called. */
    CHECK(g_thunk.count == 0);
}

TEST_CASE("with thunk installed EMT is intercepted; stack & PC untouched, execution resumes past EMT") {
    ms0515_board_t board{};
    prepare_board(board);
    board.cpu.trap_thunk = test_thunk;
    board.cpu.r[1] = 0x1111;
    board.cpu.r[2] = 0x2222;

    /* Code: EMT 7, then NOP right after the EMT. */
    write_word(board, CODE_BASE,     0104007);
    write_word(board, CODE_BASE + 2, NOP_OPCODE);

    /* Booby-trap the vector so we can prove it was *not* followed.
     * If the standard service path is taken we will land here on a
     * distinctly different PC than CODE_BASE + 4. */
    write_word(board, CPU_VEC_EMT,     VECTOR_TGT);
    write_word(board, CPU_VEC_EMT + 2, VECTOR_PSW);
    write_word(board, VECTOR_TGT,      NOP_OPCODE);

    /* Step 1: EMT executes, irq_emt set. */
    cpu_step(&board.cpu);
    CHECK(board.cpu.irq_emt == true);

    /* Step 2: thunk is called instead of the standard service, then
     * cpu_step continues to fetch the NOP at CODE_BASE+2. */
    cpu_step(&board.cpu);

    /* Thunk fired exactly once with the EMT vector. */
    CHECK(g_thunk.count == 1);
    CHECK(g_thunk.vector == CPU_VEC_EMT);

    /* SP unchanged — thunk did not push anything. */
    CHECK(board.cpu.r[CPU_REG_SP] == INITIAL_SP);
    /* PSW unchanged — thunk did not load from vector. */
    CHECK(board.cpu.psw == 0);
    /* Control resumed past the EMT: NOP at CODE_BASE+2 ran, PC advanced. */
    CHECK(board.cpu.r[CPU_REG_PC] == CODE_BASE + 4);
    /* irq_emt cleared by the dispatch. */
    CHECK(board.cpu.irq_emt == false);
}

TEST_CASE("thunk observes opcode, instruction_pc, registers and PSW at trap time") {
    ms0515_board_t board{};
    prepare_board(board);
    board.cpu.trap_thunk = test_thunk;
    board.cpu.r[0] = 0x0a0a;
    board.cpu.r[1] = 0x0b0b;
    board.cpu.r[2] = 0x0c0c;
    board.cpu.r[3] = 0x0d0d;
    board.cpu.r[4] = 0x0e0e;
    board.cpu.r[5] = 0x0f0f;
    board.cpu.psw  = 0;

    /* EMT 042 at CODE_BASE.  Followed by a NOP so the resume path is
     * benign — we only inspect the thunk-side snapshot below. */
    write_word(board, CODE_BASE,     0104042);
    write_word(board, CODE_BASE + 2, NOP_OPCODE);

    cpu_step(&board.cpu);  /* execute EMT 042 */
    cpu_step(&board.cpu);  /* triggers thunk on entry */

    CHECK(g_thunk.count == 1);
    CHECK(g_thunk.vector == CPU_VEC_EMT);
    CHECK(g_thunk.instruction == 0104042);
    CHECK(g_thunk.instruction_pc == CODE_BASE);
    CHECK(g_thunk.r[0] == 0x0a0a);
    CHECK(g_thunk.r[1] == 0x0b0b);
    CHECK(g_thunk.r[2] == 0x0c0c);
    CHECK(g_thunk.r[3] == 0x0d0d);
    CHECK(g_thunk.r[4] == 0x0e0e);
    CHECK(g_thunk.r[5] == 0x0f0f);
    CHECK(g_thunk.r[CPU_REG_SP] == INITIAL_SP);
    CHECK(g_thunk.r[CPU_REG_PC] == CODE_BASE + 2);  /* PC advanced past EMT */
    CHECK(g_thunk.psw == 0);
}

TEST_CASE("thunk is also used for TRAP instructions") {
    ms0515_board_t board{};
    prepare_board(board);
    board.cpu.trap_thunk = test_thunk;

    /* TRAP 005 = 0104405 */
    write_word(board, CODE_BASE,     0104405);
    write_word(board, CODE_BASE + 2, NOP_OPCODE);

    /* Booby-trap the TRAP vector. */
    write_word(board, CPU_VEC_TRAP,     VECTOR_TGT);
    write_word(board, CPU_VEC_TRAP + 2, VECTOR_PSW);
    write_word(board, VECTOR_TGT,       NOP_OPCODE);

    cpu_step(&board.cpu);  /* execute TRAP */
    CHECK(board.cpu.irq_trap == true);

    cpu_step(&board.cpu);  /* thunk + resume */

    CHECK(g_thunk.count == 1);
    CHECK(g_thunk.vector == CPU_VEC_TRAP);
    CHECK(g_thunk.instruction == 0104405);
    CHECK(board.cpu.r[CPU_REG_SP] == INITIAL_SP);   /* no push */
    CHECK(board.cpu.psw == 0);                       /* no PSW load */
    CHECK(board.cpu.r[CPU_REG_PC] == CODE_BASE + 4); /* resumed past TRAP */
    CHECK(board.cpu.irq_trap == false);
}

TEST_CASE("thunk is also used for IOT instructions") {
    ms0515_board_t board{};
    prepare_board(board);
    board.cpu.trap_thunk = test_thunk;

    /* IOT = 0000004 (zero-operand opcode) */
    write_word(board, CODE_BASE,     0000004);
    write_word(board, CODE_BASE + 2, NOP_OPCODE);

    write_word(board, CPU_VEC_IOT,     VECTOR_TGT);
    write_word(board, CPU_VEC_IOT + 2, VECTOR_PSW);
    write_word(board, VECTOR_TGT,      NOP_OPCODE);

    cpu_step(&board.cpu);
    CHECK(board.cpu.irq_iot == true);

    cpu_step(&board.cpu);

    CHECK(g_thunk.count == 1);
    CHECK(g_thunk.vector == CPU_VEC_IOT);
    CHECK(board.cpu.r[CPU_REG_SP] == INITIAL_SP);
    CHECK(board.cpu.psw == 0);
    CHECK(board.cpu.r[CPU_REG_PC] == CODE_BASE + 4);
    CHECK(board.cpu.irq_iot == false);
}

}  // TEST_SUITE
