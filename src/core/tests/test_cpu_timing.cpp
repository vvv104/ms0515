/*
 * test_cpu_timing.cpp — instruction execution times of the KR1807VM1.
 *
 * The numbers come from the T-11 Engineering Specification (Rev E,
 * Mar 82), Appendix B "T-11 instruction execution times in microcycles",
 * 16-bit bus mode - which is how the MS 0515 wires the processor.  One
 * microcycle is three clocks of the 7.5 MHz input (400 ns), and the core
 * counts clocks, so every expectation below is written as
 * `microcycles * MICROCYCLE` with the table's own figure spelled out.
 *
 * Why this matters beyond pedantry: programs of the period time
 * themselves by counting instructions, not by watching a clock.
 * FIREBIRD (LWOW SOFT, 1990) paces every frame with
 *
 *     MOV #177777, R2
 *     SOB R2, .
 *
 * so its speed is exactly our SOB timing.  With SOB charged 9 clocks
 * instead of the specified 18 the whole game ran at double speed and its
 * projectiles were impossible to dodge.  A wrong instruction time is not
 * visible in any screenshot; only a test like this one pins it.
 */

#include <doctest/doctest.h>
#include <cstring>
#include <initializer_list>

extern "C" {
#include <ms0515/core/board.h>
#include <ms0515/core/cpu.h>
}

namespace {

/* One microcycle = 3 clocks of the 7.5 MHz input (400 ns). */
constexpr int MICROCYCLE = 3;

constexpr uint16_t CODE_BASE  = 01000;
constexpr uint16_t INITIAL_SP = 07000;
constexpr uint16_t DATA_BASE  = 06000;

/*
 * Assemble `words` at CODE_BASE, point the registers at somewhere safe to
 * dereference, and step one instruction.  Returns the clocks it cost.
 */
int time_of(std::initializer_list<uint16_t> words)
{
    static ms0515_board_t board;
    board_init(&board);

    uint16_t addr = CODE_BASE;
    for (uint16_t w : words) {
        board_write_word(&board, addr, w);
        addr = (uint16_t)(addr + 2);
    }

    ms0515_cpu_t &cpu = board.cpu;
    cpu.r[CPU_REG_PC] = CODE_BASE;
    cpu.r[CPU_REG_SP] = INITIAL_SP;
    cpu.psw           = 0;
    cpu.halted        = false;
    cpu.waiting       = false;
    for (int i = 0; i < 6; i++)
        cpu.r[i] = DATA_BASE;

    return cpu_step(&cpu);
}

}  // namespace

TEST_SUITE("CPU instruction timing (T-11 Appendix B, 16-bit mode)") {

/* ── Double operand: source mode time + destination mode time ───────────── */

/* These already matched the table and are pinned so they keep matching. */
TEST_CASE("double operand: MOV over the addressing modes") {
    CHECK(time_of({0010001}) == (3 + 1) * MICROCYCLE);   /* MOV R0,R1      */
    CHECK(time_of({0011011}) == (5 + 4) * MICROCYCLE);   /* MOV (R0),(R1)  */
    CHECK(time_of({0016061, 0, 0}) == (8 + 7) * MICROCYCLE);  /* MOV X(R0),X(R1) */
}

/* CMP and BIT produce no output, so the table gives them their own, one
 * microcycle shorter, destination column - but the instruction as a whole
 * is still one microcycle longer than the operands alone account for. */
TEST_CASE("double operand: CMP and BIT") {
    CHECK(time_of({0020001}) == (3 + 1) * MICROCYCLE);   /* CMP R0,R1 */
    CHECK(time_of({0030001}) == (3 + 1) * MICROCYCLE);   /* BIT R0,R1 */
    CHECK(time_of({0021011}) == (5 + 3) * MICROCYCLE);   /* CMP (R0),(R1) */
}

/* ── Single operand ─────────────────────────────────────────────────────── */

TEST_CASE("single operand: CLR, TST, MFPS, MTPS") {
    CHECK(time_of({0005000}) ==  4 * MICROCYCLE);   /* CLR R0    */
    CHECK(time_of({0005010}) ==  7 * MICROCYCLE);   /* CLR (R0)  */
    CHECK(time_of({0005700}) ==  4 * MICROCYCLE);   /* TST R0    */
    CHECK(time_of({0005710}) ==  6 * MICROCYCLE);   /* TST (R0)  */
    CHECK(time_of({0106700}) ==  4 * MICROCYCLE);   /* MFPS R0   */
    CHECK(time_of({0106400}) ==  8 * MICROCYCLE);   /* MTPS R0   */
    CHECK(time_of({0106410}) == 10 * MICROCYCLE);   /* MTPS (R0) */
}

/* ── Branches: the same whether taken or not ────────────────────────────── */

TEST_CASE("branches cost the same taken and not taken") {
    CHECK(time_of({0000400}) == 4 * MICROCYCLE);   /* BR  .+2, always taken */
    CHECK(time_of({0001400}) == 4 * MICROCYCLE);   /* BEQ .+2, Z clear      */
}

/* ── Jumps and subroutines ──────────────────────────────────────────────── */

TEST_CASE("SOB, JMP, JSR, RTS") {
    CHECK(time_of({0077001}) == 6 * MICROCYCLE);      /* SOB R0, .-2   */
    CHECK(time_of({0000110}) == 5 * MICROCYCLE);      /* JMP (R0)      */
    CHECK(time_of({0000120}) == 6 * MICROCYCLE);      /* JMP (R0)+     */
    CHECK(time_of({0000160, 0}) == 7 * MICROCYCLE);   /* JMP X(R0)     */
    CHECK(time_of({0004710}) == 9 * MICROCYCLE);      /* JSR PC,(R0)   */
    CHECK(time_of({0004760, 0}) == 11 * MICROCYCLE);  /* JSR PC,X(R0)  */
    CHECK(time_of({0000205}) == 7 * MICROCYCLE);      /* RTS R5        */
}

/* ── Traps and returns from them ────────────────────────────────────────── */

/* The trap instructions' figure covers the whole service - pushing PS and
 * PC and loading both from the vector - which this core does inside the
 * same step, charging nothing extra for it. */
TEST_CASE("EMT, RTI and RTT") {
    CHECK(time_of({0104000}) == 16 * MICROCYCLE);   /* EMT 0 */
    CHECK(time_of({0000002}) ==  8 * MICROCYCLE);   /* RTI   */
    CHECK(time_of({0000006}) ==  8 * MICROCYCLE);   /* RTT   */
}

/* ── Miscellaneous and condition codes ──────────────────────────────────── */

TEST_CASE("NOP, the condition-code group, MFPT, HALT, WAIT and RESET") {
    CHECK(time_of({0000240}) ==  6 * MICROCYCLE);   /* NOP   */
    CHECK(time_of({0000241}) ==  6 * MICROCYCLE);   /* CLC   */
    CHECK(time_of({0000261}) ==  6 * MICROCYCLE);   /* SEC   */
    CHECK(time_of({0000007}) ==  5 * MICROCYCLE);   /* MFPT  */
    CHECK(time_of({0000000}) == 14 * MICROCYCLE);   /* HALT  */
    CHECK(time_of({0000001}) ==  4 * MICROCYCLE);   /* WAIT  */
    CHECK(time_of({0000005}) == 39 * MICROCYCLE);   /* RESET */
}

/* ── Cross-check against the machine's own documentation ────────────────── */

/*
 * The KR1807VM1 is a clone, so the DEC appendix is not by itself proof
 * that the Soviet part keeps the same times.  The MS 0515 technical
 * description (NS4, Table 2) settles it where it is specific:
 *
 *   Базовый микроцикл, нс                              400
 *   Быстродействие, млн коротких операций/с, не менее  0,5
 *   Время выполнения одноадресных команд типа
 *   «очистка», мкс, не более                           1,6
 *
 * 1.6 us is exactly the four microcycles Appendix B gives a single-operand
 * instruction in mode 0, and 0.5 M short operations a second is met by the
 * four microcycles of a register-to-register double operand (0.625 M).
 */
TEST_CASE("the times the machine's own technical description quotes") {
    const double us_per_clock = 1.0 / 7.5;                  /* 7.5 MHz */
    CHECK(time_of({0005000}) * us_per_clock == doctest::Approx(1.6));  /* CLR R0 */
    CHECK(MICROCYCLE * us_per_clock == doctest::Approx(0.4));          /* microcycle */

    const double short_ops_per_second = 1e6 / (time_of({0060001}) * us_per_clock);
    CHECK(short_ops_per_second >= 0.5e6);                   /* ADD R0,R1 */
}

/* ── The loop that started all this ─────────────────────────────────────── */

/* FIREBIRD's frame delay, spelled out: 65535 passes of a two-word loop.
 * At the specified 18 clocks a pass this is 157 ms - just under eight
 * frames of the 50 Hz machine.  Charging SOB 9 clocks halved it, and the
 * game ran at double speed. */
TEST_CASE("a SOB delay loop takes the time the hardware would take") {
    const int pass = time_of({0077001});
    CHECK(pass == 18);
    const double seconds = 65535.0 * pass / 7500000.0;
    CHECK(seconds == doctest::Approx(0.157).epsilon(0.01));
}

}  // TEST_SUITE
