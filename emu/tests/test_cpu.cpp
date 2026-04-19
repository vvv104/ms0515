#include <doctest/doctest.h>

extern "C" {
#include <ms0515/board.h>
}

#include <ms0515/Emulator.hpp>

TEST_SUITE("CPU") {

TEST_CASE("initial state after reset") {
    ms0515::Emulator emu;
    emu.reset();

    const auto &cpu = emu.cpu();
    CHECK(cpu.r[CPU_REG_PC] != 0);
    CHECK(cpu.halted == false);
    CHECK(cpu.waiting == false);
}

TEST_CASE("MOV instruction") {
    ms0515::Emulator emu;
    emu.reset();

    /* MOV #042, R0  →  opcode 012700, immediate 000042 */
    /* Write via bus so memory mapping is respected */
    uint16_t addr = 0x1000;
    emu.writeWord(addr,     012700);
    emu.writeWord(addr + 2, 000042);

    emu.cpu().r[CPU_REG_PC] = addr;
    emu.cpu().halted = false;

    emu.stepInstruction();

    CHECK(emu.cpu().r[0] == 042);
}

} /* TEST_SUITE */
