#include <doctest/doctest.h>

extern "C" {
#include <ms0515/board.h>
}

#include <ms0515/Emulator.hpp>

/*
 * Helpers: write a small program into RAM at `base`, set PC there,
 * and step one instruction.  The Emulator wrapper provides readWord,
 * writeWord, stepInstruction, and direct cpu() access.
 */

TEST_SUITE("CPU") {

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/*
 * Write an instruction sequence at `addr` and set PC there.
 * Returns the address past the last written word, for chaining.
 */
static uint16_t emit(ms0515::Emulator &emu, uint16_t addr,
                     std::initializer_list<uint16_t> words)
{
    for (uint16_t w : words) {
        emu.writeWord(addr, w);
        addr += 2;
    }
    return addr;
}

static void run_at(ms0515::Emulator &emu, uint16_t addr,
                   std::initializer_list<uint16_t> words)
{
    emit(emu, addr, words);
    emu.cpu().r[CPU_REG_PC] = addr;
    emu.cpu().halted = false;
    emu.stepInstruction();
}

static constexpr uint16_t BASE = 0x1000;

/* ── Initial state ───────────────────────────────────────────────────────── */

TEST_CASE("initial state after reset") {
    ms0515::Emulator emu;
    emu.reset();

    const auto &cpu = emu.cpu();
    CHECK(cpu.r[CPU_REG_PC] != 0);
    CHECK(cpu.halted == false);
    CHECK(cpu.waiting == false);
}

/* ── MOV ─────────────────────────────────────────────────────────────────── */

TEST_CASE("MOV #imm, R0") {
    ms0515::Emulator emu;
    emu.reset();

    /* MOV #042, R0  →  012700, 000042 */
    run_at(emu, BASE, {012700, 000042});
    CHECK(emu.cpu().r[0] == 042);
}

TEST_CASE("MOV R0, R1") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 0x1234;

    /* MOV R0, R1 → 010001 */
    run_at(emu, BASE, {010001});
    CHECK(emu.cpu().r[1] == 0x1234);
}

TEST_CASE("MOV sets N flag for negative value") {
    ms0515::Emulator emu;
    emu.reset();

    /* MOV #0x8000, R0 */
    run_at(emu, BASE, {012700, 0x8000});
    CHECK((emu.cpu().psw & CPU_PSW_N) != 0);
    CHECK((emu.cpu().psw & CPU_PSW_Z) == 0);
}

TEST_CASE("MOV sets Z flag for zero") {
    ms0515::Emulator emu;
    emu.reset();

    /* MOV #0, R0 */
    run_at(emu, BASE, {012700, 0});
    CHECK((emu.cpu().psw & CPU_PSW_Z) != 0);
    CHECK((emu.cpu().psw & CPU_PSW_N) == 0);
}

/* ── MOVB ────────────────────────────────────────────────────────────────── */

TEST_CASE("MOVB sign-extends when dst is register") {
    ms0515::Emulator emu;
    emu.reset();

    /* MOVB #0xFF, R0 → 112700, 0x00FF */
    run_at(emu, BASE, {0112700, 0x00FF});
    /* MOVB to register sign-extends: 0xFF → 0xFFFF */
    CHECK(emu.cpu().r[0] == 0xFFFF);
}

/* ── CLR ─────────────────────────────────────────────────────────────────── */

TEST_CASE("CLR R0") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 0xBEEF;

    /* CLR R0 → 005000 */
    run_at(emu, BASE, {005000});
    CHECK(emu.cpu().r[0] == 0);
    CHECK((emu.cpu().psw & CPU_PSW_Z) != 0);
    CHECK((emu.cpu().psw & CPU_PSW_N) == 0);
    CHECK((emu.cpu().psw & CPU_PSW_V) == 0);
    CHECK((emu.cpu().psw & CPU_PSW_C) == 0);
}

/* ── COM (one's complement) ──────────────────────────────────────────────── */

TEST_CASE("COM R0") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 0x00FF;

    /* COM R0 → 005100 */
    run_at(emu, BASE, {005100});
    CHECK(emu.cpu().r[0] == 0xFF00);
    CHECK((emu.cpu().psw & CPU_PSW_C) != 0);  /* C always set */
    CHECK((emu.cpu().psw & CPU_PSW_V) == 0);  /* V always cleared */
}

/* ── INC / DEC ───────────────────────────────────────────────────────────── */

TEST_CASE("INC R0") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 5;

    /* INC R0 → 005200 */
    run_at(emu, BASE, {005200});
    CHECK(emu.cpu().r[0] == 6);
}

TEST_CASE("INC overflow sets V") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 0x7FFF;

    run_at(emu, BASE, {005200});
    CHECK(emu.cpu().r[0] == 0x8000);
    CHECK((emu.cpu().psw & CPU_PSW_V) != 0);
}

TEST_CASE("DEC R0") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 5;

    /* DEC R0 → 005300 */
    run_at(emu, BASE, {005300});
    CHECK(emu.cpu().r[0] == 4);
}

TEST_CASE("DEC zero wraps and sets N") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 0;

    run_at(emu, BASE, {005300});
    CHECK(emu.cpu().r[0] == 0xFFFF);
    CHECK((emu.cpu().psw & CPU_PSW_N) != 0);
}

/* ── NEG (negate) ────────────────────────────────────────────────────────── */

TEST_CASE("NEG R0") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 1;

    /* NEG R0 → 005400 */
    run_at(emu, BASE, {005400});
    CHECK(emu.cpu().r[0] == 0xFFFF);
    CHECK((emu.cpu().psw & CPU_PSW_N) != 0);
    CHECK((emu.cpu().psw & CPU_PSW_C) != 0);  /* C set when result != 0 */
}

TEST_CASE("NEG zero clears C") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 0;

    run_at(emu, BASE, {005400});
    CHECK(emu.cpu().r[0] == 0);
    CHECK((emu.cpu().psw & CPU_PSW_Z) != 0);
    CHECK((emu.cpu().psw & CPU_PSW_C) == 0);
}

/* ── TST ─────────────────────────────────────────────────────────────────── */

TEST_CASE("TST R0 sets flags without modifying") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 0x8000;

    /* TST R0 → 005700 */
    run_at(emu, BASE, {005700});
    CHECK(emu.cpu().r[0] == 0x8000);  /* unchanged */
    CHECK((emu.cpu().psw & CPU_PSW_N) != 0);
    CHECK((emu.cpu().psw & CPU_PSW_Z) == 0);
    CHECK((emu.cpu().psw & CPU_PSW_V) == 0);
    CHECK((emu.cpu().psw & CPU_PSW_C) == 0);
}

/* ── ADD / SUB ───────────────────────────────────────────────────────────── */

TEST_CASE("ADD R0, R1") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 100;
    emu.cpu().r[1] = 200;

    /* ADD R0, R1 → 060001 */
    run_at(emu, BASE, {060001});
    CHECK(emu.cpu().r[1] == 300);
}

TEST_CASE("ADD overflow sets V and C") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 0x8000;
    emu.cpu().r[1] = 0x8000;

    run_at(emu, BASE, {060001});
    CHECK(emu.cpu().r[1] == 0);
    CHECK((emu.cpu().psw & CPU_PSW_V) != 0);
    CHECK((emu.cpu().psw & CPU_PSW_C) != 0);
}

TEST_CASE("SUB R0, R1") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 50;
    emu.cpu().r[1] = 200;

    /* SUB R0, R1 → 160001 */
    run_at(emu, BASE, {0160001});
    CHECK(emu.cpu().r[1] == 150);
}

/* ── CMP ─────────────────────────────────────────────────────────────────── */

TEST_CASE("CMP equal values sets Z") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 42;
    emu.cpu().r[1] = 42;

    /* CMP R0, R1 → 020001 */
    run_at(emu, BASE, {020001});
    CHECK((emu.cpu().psw & CPU_PSW_Z) != 0);
    /* Operands unchanged */
    CHECK(emu.cpu().r[0] == 42);
    CHECK(emu.cpu().r[1] == 42);
}

TEST_CASE("CMP src > dst clears Z, clears N (unsigned)") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 100;
    emu.cpu().r[1] = 50;

    run_at(emu, BASE, {020001});
    CHECK((emu.cpu().psw & CPU_PSW_Z) == 0);
    CHECK((emu.cpu().psw & CPU_PSW_N) == 0);
}

/* ── BIT / BIC / BIS ────────────────────────────────────────────────────── */

TEST_CASE("BIT R0, R1 tests bits without modifying") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 0x00F0;
    emu.cpu().r[1] = 0x0030;

    /* BIT R0, R1 → 030001 */
    run_at(emu, BASE, {030001});
    /* 0x00F0 & 0x0030 = 0x0030, non-zero → Z clear */
    CHECK((emu.cpu().psw & CPU_PSW_Z) == 0);
    CHECK(emu.cpu().r[1] == 0x0030);  /* unchanged */
}

TEST_CASE("BIC R0, R1 clears bits") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 0x00FF;
    emu.cpu().r[1] = 0x1234;

    /* BIC R0, R1 → 040001 */
    run_at(emu, BASE, {040001});
    CHECK(emu.cpu().r[1] == 0x1200);
}

TEST_CASE("BIS R0, R1 sets bits") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 0x00FF;
    emu.cpu().r[1] = 0x1200;

    /* BIS R0, R1 → 050001 */
    run_at(emu, BASE, {050001});
    CHECK(emu.cpu().r[1] == 0x12FF);
}

/* ── XOR ─────────────────────────────────────────────────────────────────── */

TEST_CASE("XOR R0, R1") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 0xFF00;
    emu.cpu().r[1] = 0x0FF0;

    /* XOR R0, R1 → 074001 */
    run_at(emu, BASE, {074001});
    CHECK(emu.cpu().r[1] == 0xF0F0);
}

/* ── ASR / ASL / ROR / ROL ───────────────────────────────────────────────── */

TEST_CASE("ASR R0 (arithmetic shift right)") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 0x8004;

    /* ASR R0 → 006200 */
    run_at(emu, BASE, {006200});
    CHECK(emu.cpu().r[0] == 0xC002);  /* sign bit preserved */
    CHECK((emu.cpu().psw & CPU_PSW_C) == 0);  /* bit 0 was 0 */
}

TEST_CASE("ASL R0 (arithmetic shift left)") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 0x4001;

    /* ASL R0 → 006300 */
    run_at(emu, BASE, {006300});
    CHECK(emu.cpu().r[0] == 0x8002);
    CHECK((emu.cpu().psw & CPU_PSW_C) == 0);  /* bit 15 was 0 */
}

TEST_CASE("ROR R0 (rotate right through carry)") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 0x0001;
    emu.cpu().psw &= ~CPU_PSW_C;  /* C = 0 */

    /* ROR R0 → 006000 */
    run_at(emu, BASE, {006000});
    CHECK(emu.cpu().r[0] == 0x0000);
    CHECK((emu.cpu().psw & CPU_PSW_C) != 0);  /* old bit 0 → C */
}

TEST_CASE("ROL R0 (rotate left through carry)") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 0x8000;
    emu.cpu().psw &= ~CPU_PSW_C;  /* C = 0 */

    /* ROL R0 → 006100 */
    run_at(emu, BASE, {006100});
    CHECK(emu.cpu().r[0] == 0x0000);
    CHECK((emu.cpu().psw & CPU_PSW_C) != 0);  /* old bit 15 → C */
}

/* ── ADC / SBC ───────────────────────────────────────────────────────────── */

TEST_CASE("ADC R0 adds carry") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 10;
    emu.cpu().psw |= CPU_PSW_C;

    /* ADC R0 → 005500 */
    run_at(emu, BASE, {005500});
    CHECK(emu.cpu().r[0] == 11);
}

TEST_CASE("SBC R0 subtracts carry") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 10;
    emu.cpu().psw |= CPU_PSW_C;

    /* SBC R0 → 005600 */
    run_at(emu, BASE, {005600});
    CHECK(emu.cpu().r[0] == 9);
}

/* ── SXT (sign extend) ──────────────────────────────────────────────────── */

TEST_CASE("SXT R0 when N=0") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 0xBEEF;
    emu.cpu().psw &= ~CPU_PSW_N;

    /* SXT R0 → 006700 */
    run_at(emu, BASE, {006700});
    CHECK(emu.cpu().r[0] == 0);
}

TEST_CASE("SXT R0 when N=1") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 0;
    emu.cpu().psw |= CPU_PSW_N;

    run_at(emu, BASE, {006700});
    CHECK(emu.cpu().r[0] == 0xFFFF);
}

/* ── SWAB ────────────────────────────────────────────────────────────────── */

TEST_CASE("SWAB R0 swaps bytes") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 0x1234;

    /* SWAB R0 → 000300 */
    run_at(emu, BASE, {000300});
    CHECK(emu.cpu().r[0] == 0x3412);
}

/* ── Branch instructions ─────────────────────────────────────────────────── */

TEST_CASE("BR always branches") {
    ms0515::Emulator emu;
    emu.reset();

    /* BR +4 → 000402  (offset 2 words forward from PC after fetch) */
    run_at(emu, BASE, {000402});
    CHECK(emu.cpu().r[CPU_REG_PC] == BASE + 2 + 4);
}

TEST_CASE("BR negative offset branches backward") {
    ms0515::Emulator emu;
    emu.reset();

    /* BR -2 → offset = -2, encoded as 0xFF (-1 in signed byte * 2)
     * Actually: offset byte = 0xFF → sign-extend to -1, * 2 = -2
     * PC after fetch = BASE+2, so target = BASE+2 + (-2) = BASE */
    run_at(emu, BASE, {static_cast<uint16_t>(000400 | 0xFF)});
    CHECK(emu.cpu().r[CPU_REG_PC] == BASE);
}

TEST_CASE("BNE branches when Z=0") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().psw &= ~CPU_PSW_Z;

    /* BNE +4 → 001002 */
    run_at(emu, BASE, {001002});
    CHECK(emu.cpu().r[CPU_REG_PC] == BASE + 2 + 4);
}

TEST_CASE("BNE does not branch when Z=1") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().psw |= CPU_PSW_Z;

    run_at(emu, BASE, {001002});
    CHECK(emu.cpu().r[CPU_REG_PC] == BASE + 2);
}

TEST_CASE("BEQ branches when Z=1") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().psw |= CPU_PSW_Z;

    /* BEQ +4 → 001402 */
    run_at(emu, BASE, {001402});
    CHECK(emu.cpu().r[CPU_REG_PC] == BASE + 2 + 4);
}

TEST_CASE("BPL branches when N=0") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().psw &= ~CPU_PSW_N;

    /* BPL +4 → 100002 */
    run_at(emu, BASE, {0100002});
    CHECK(emu.cpu().r[CPU_REG_PC] == BASE + 2 + 4);
}

TEST_CASE("BMI branches when N=1") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().psw |= CPU_PSW_N;

    /* BMI +4 → 100402 */
    run_at(emu, BASE, {0100402});
    CHECK(emu.cpu().r[CPU_REG_PC] == BASE + 2 + 4);
}

TEST_CASE("BHI branches when C=0 and Z=0") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().psw &= ~(CPU_PSW_C | CPU_PSW_Z);

    /* BHI +4 → 101002 */
    run_at(emu, BASE, {0101002});
    CHECK(emu.cpu().r[CPU_REG_PC] == BASE + 2 + 4);
}

TEST_CASE("BLOS branches when C=1 or Z=1") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().psw |= CPU_PSW_C;

    /* BLOS +4 → 101402 */
    run_at(emu, BASE, {0101402});
    CHECK(emu.cpu().r[CPU_REG_PC] == BASE + 2 + 4);
}

/* ── SOB (subtract one and branch) ──────────────────────────────────────── */

TEST_CASE("SOB decrements and branches when non-zero") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 3;

    /* SOB R0, 2 → 077002 (reg=0, offset=2) */
    run_at(emu, BASE, {077002});
    CHECK(emu.cpu().r[0] == 2);
    CHECK(emu.cpu().r[CPU_REG_PC] == BASE + 2 - 4);  /* offset*2 backward */
}

TEST_CASE("SOB does not branch when result is zero") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 1;

    run_at(emu, BASE, {077002});
    CHECK(emu.cpu().r[0] == 0);
    CHECK(emu.cpu().r[CPU_REG_PC] == BASE + 2);  /* fall through */
}

/* ── JMP ─────────────────────────────────────────────────────────────────── */

TEST_CASE("JMP (R1)") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[1] = 0x2000;

    /* JMP (R1) → 000111  (opcode 0001, mode=1, reg=1 → DD=011) */
    run_at(emu, BASE, {000111});
    CHECK(emu.cpu().r[CPU_REG_PC] == 0x2000);
}

/* ── JSR / RTS ───────────────────────────────────────────────────────────── */

TEST_CASE("JSR PC,(R1) and RTS PC") {
    ms0515::Emulator emu;
    emu.reset();

    uint16_t subroutine = 0x2000;
    emu.cpu().r[1] = subroutine;
    emu.cpu().r[CPU_REG_SP] = 0x0800;

    /* JSR PC, (R1) → 004711 */
    run_at(emu, BASE, {004711});
    CHECK(emu.cpu().r[CPU_REG_PC] == subroutine);

    /* Return address should be on stack */
    uint16_t ret_addr = emu.readWord(emu.cpu().r[CPU_REG_SP]);
    CHECK(ret_addr == BASE + 2);

    /* RTS PC → 000207 */
    run_at(emu, subroutine, {000207});
    CHECK(emu.cpu().r[CPU_REG_PC] == BASE + 2);
}

/* ── HALT / WAIT ─────────────────────────────────────────────────────────── */

TEST_CASE("HALT traps to restart vector 0172004") {
    /* On K1801VM1 the HALT instruction is a trap, not a stop:
     * pushes PC + PSW, sets PC = 0172004, PSW = 0340.  Mirrors the
     * behaviour of an external HALT signal handled in cpu.c. */
    ms0515::Emulator emu;
    emu.reset();
    /* Map full RAM through the dispatcher so the stack push lands in
     * writable memory.  Then point SP into the same window. */
    emu.writeWord(0177400, 0x007F);
    emu.cpu().r[CPU_REG_SP] = 0x4000;
    emu.cpu().psw = 0;                  /* known PSW for the push */
    uint16_t old_sp = emu.cpu().r[CPU_REG_SP];

    run_at(emu, BASE, {000000});

    CHECK(emu.cpu().halted == false);   /* not stopped */
    CHECK(emu.cpu().r[CPU_REG_PC] == 0172004);
    CHECK(emu.cpu().psw == 0340);
    CHECK(emu.cpu().r[CPU_REG_SP] == (uint16_t)(old_sp - 4));  /* PC + PSW */
    /* Pushed PC is the address AFTER the HALT word — would be the
     * return point if a handler at 0172004 RTI'd back. */
    CHECK(emu.readWord(emu.cpu().r[CPU_REG_SP]) == BASE + 2);
    CHECK(emu.readWord(emu.cpu().r[CPU_REG_SP] + 2) == 0);  /* old PSW */
}

TEST_CASE("WAIT sets waiting flag") {
    ms0515::Emulator emu;
    emu.reset();

    /* WAIT → 000001 */
    run_at(emu, BASE, {000001});
    CHECK(emu.cpu().waiting == true);
}

/* ── NOP ─────────────────────────────────────────────────────────────────── */

TEST_CASE("NOP does nothing") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 0x1234;
    uint16_t old_psw = emu.cpu().psw;

    /* NOP → 000240 */
    run_at(emu, BASE, {000240});
    CHECK(emu.cpu().r[0] == 0x1234);
    CHECK(emu.cpu().r[CPU_REG_PC] == BASE + 2);
    CHECK(emu.cpu().psw == old_psw);
}

/* ── CCC / SCC (condition code control) ──────────────────────────────────── */

TEST_CASE("SCC sets all condition codes") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().psw &= ~0x0F;

    /* SCC (set all) → 000277 */
    run_at(emu, BASE, {000277});
    CHECK((emu.cpu().psw & 0x0F) == 0x0F);
}

TEST_CASE("CCC clears all condition codes") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().psw |= 0x0F;

    /* CCC (clear all) → 000257 */
    run_at(emu, BASE, {000257});
    CHECK((emu.cpu().psw & 0x0F) == 0);
}

/* ── Addressing modes ────────────────────────────────────────────────────── */

TEST_CASE("autoincrement mode: MOV (R1)+, R0") {
    ms0515::Emulator emu;
    emu.reset();

    uint16_t data_addr = 0x2000;
    emu.writeWord(data_addr, 0x5678);
    emu.cpu().r[1] = data_addr;

    /* MOV (R1)+, R0 → 012100 */
    run_at(emu, BASE, {012100});
    CHECK(emu.cpu().r[0] == 0x5678);
    CHECK(emu.cpu().r[1] == data_addr + 2);
}

TEST_CASE("autodecrement mode: MOV -(R1), R0") {
    ms0515::Emulator emu;
    emu.reset();

    uint16_t data_addr = 0x2000;
    emu.writeWord(data_addr, 0xABCD);
    emu.cpu().r[1] = data_addr + 2;

    /* MOV -(R1), R0 → 014100 */
    run_at(emu, BASE, {014100});
    CHECK(emu.cpu().r[0] == 0xABCD);
    CHECK(emu.cpu().r[1] == data_addr);
}

TEST_CASE("register deferred mode: MOV (R1), R0") {
    ms0515::Emulator emu;
    emu.reset();

    uint16_t data_addr = 0x2000;
    emu.writeWord(data_addr, 0x9999);
    emu.cpu().r[1] = data_addr;

    /* MOV (R1), R0 → 011100 */
    run_at(emu, BASE, {011100});
    CHECK(emu.cpu().r[0] == 0x9999);
    CHECK(emu.cpu().r[1] == data_addr);  /* unchanged */
}

TEST_CASE("immediate mode uses PC autoincrement: MOV #val, R0") {
    ms0515::Emulator emu;
    emu.reset();

    /* MOV #0x1234, R0 → 012700, 0x1234
     * mode 2 reg 7 = (PC)+ = immediate */
    run_at(emu, BASE, {012700, 0x1234});
    CHECK(emu.cpu().r[0] == 0x1234);
    CHECK(emu.cpu().r[CPU_REG_PC] == BASE + 4);
}

/* ── Memory write via addressing mode ────────────────────────────────────── */

TEST_CASE("MOV R0, (R1) writes to memory") {
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 0x4242;

    uint16_t data_addr = 0x2000;
    emu.cpu().r[1] = data_addr;

    /* MOV R0, (R1) → 010011 */
    run_at(emu, BASE, {010011});
    CHECK(emu.readWord(data_addr) == 0x4242);
}

/* ── Trap instructions ───────────────────────────────────────────────────── */

TEST_CASE("EMT sets irq_emt flag") {
    ms0515::Emulator emu;
    emu.reset();

    /* EMT 0 → 104000 */
    run_at(emu, BASE, {0104000});
    CHECK(emu.cpu().irq_emt == true);
}

TEST_CASE("TRAP sets irq_trap flag") {
    ms0515::Emulator emu;
    emu.reset();

    /* TRAP 0 → 104400 */
    run_at(emu, BASE, {0104400});
    CHECK(emu.cpu().irq_trap == true);
}

TEST_CASE("BPT sets irq_bpt flag") {
    ms0515::Emulator emu;
    emu.reset();

    /* BPT → 000003 */
    run_at(emu, BASE, {000003});
    CHECK(emu.cpu().irq_bpt == true);
}

TEST_CASE("IOT sets irq_iot flag") {
    ms0515::Emulator emu;
    emu.reset();

    /* IOT → 000004 */
    run_at(emu, BASE, {000004});
    CHECK(emu.cpu().irq_iot == true);
}

/* ── Cycle-count audit (K1801VM1 spec via MAME T11) ─────────────────────── */

/* Run one MOV instruction with the given (src_mode/reg, dst_mode/reg)
 * and return the cycle count cpu_step reported.  Sets up RAM mapping
 * and points R0/R1 inside RAM so deferred/indexed modes don't touch
 * I/O registers.  PC-relative modes (reg=7) get a zero index/imm
 * word written after the instruction. */
static int mov_cycles_full(int sm, int sr, int dm, int dr)
{
    ms0515::Emulator emu;
    emu.reset();
    emu.writeWord(0177400, 0x007F);          /* full RAM mapping */
    emu.cpu().r[CPU_REG_SP] = 0x4000;

    uint16_t instr = 010000u                 /* MOV opcode */
                   | (uint16_t)(sm << 9)
                   | (uint16_t)(sr << 6)
                   | (uint16_t)(dm << 3)
                   | (uint16_t) dr;
    emu.writeWord(BASE,     instr);
    emu.writeWord(BASE + 2, 0);
    emu.writeWord(BASE + 4, 0);
    emu.cpu().r[CPU_REG_PC] = BASE;
    emu.cpu().halted = false;
    /* Point R0/R1 (and R2-R5 for safety) at writable RAM. */
    for (int i = 0; i < 6; ++i)
        emu.cpu().r[i] = (uint16_t)(0x3000 + i * 0x10);
    emu.stepInstruction();
    return emu.cpu().cycles;
}

static int mov_cycles(int sm, int dm)
{
    return mov_cycles_full(sm, 0, dm, 1);
}

/* Diagnostic: print the full 12×12 MOV cycle matrix in the same
 * shape the forum-circulating "CMOV.SAV" tool prints — rows and
 * columns are mode-reg pairs.  Columns 01..71 use reg=1, columns
 * 27/37/67/77 use reg=7 (PC) which makes them #imm / @#abs /
 * label / @label respectively.  Rows likewise: 00..70 use reg=0,
 * 27/37/67/77 use reg=7.
 *
 * Skip-marked because it's a one-shot inspection — run with
 *   build/Release/tests/ms0515_tests.exe --no-skip -tc='DIAG MOV*'
 * and compare the printed grid against the same grid measured
 * inside an OS via CMOV.SAV (or against MAME). */
TEST_CASE("DIAG MOV cycle matrix (12×12 grid for forum-style comparison)" * doctest::skip()) {
    /* (mode, reg) entries that label the rows and columns.  reg=0 for
     * src rows and reg=1 for dst columns over modes 0-7; reg=7 for
     * the special PC-relative modes 2/3/6/7. */
    struct ModeReg { int mode; int reg; const char *label; };
    static const ModeReg kSrcAxis[12] = {
        {0,0,"00"}, {1,0,"10"}, {2,0,"20"}, {3,0,"30"},
        {4,0,"40"}, {5,0,"50"}, {6,0,"60"}, {7,0,"70"},
        {2,7,"27"}, {3,7,"37"}, {6,7,"67"}, {7,7,"77"},
    };
    static const ModeReg kDstAxis[12] = {
        {0,1,"01"}, {1,1,"11"}, {2,1,"21"}, {3,1,"31"},
        {4,1,"41"}, {5,1,"51"}, {6,1,"61"}, {7,1,"71"},
        {2,7,"27"}, {3,7,"37"}, {6,7,"67"}, {7,7,"77"},
    };

    std::fprintf(stderr, "\n[diag] MOV cycle matrix (rows=src, cols=dst):\n");
    std::fprintf(stderr, "MOV ");
    for (auto &c : kDstAxis)
        std::fprintf(stderr, "  %s ", c.label);
    std::fprintf(stderr, "\n");
    for (auto &r : kSrcAxis) {
        std::fprintf(stderr, " %s ", r.label);
        for (auto &c : kDstAxis) {
            int n = mov_cycles_full(r.mode, r.reg, c.mode, c.reg);
            std::fprintf(stderr, " %4d", n);
        }
        std::fprintf(stderr, "\n");
    }
}

TEST_CASE("MOV cycle counts match K1801VM1 spec (MAME T11 reference)") {
    /* Per src/devices/cpu/t11/t11ops.hxx: the MOV cost is the sum of
     * the source-operand cost plus the destination-operand
     * additional cost.  Source costs by mode:
     *   mode 0..7 = 9, 15, 15, 21, 18, 24, 24, 30
     * Destination additional costs by mode:
     *   mode 0..7 = 3, 12, 12, 18, 15, 21, 21, 27
     * MOV reg→reg = 9 + 3 = 12; MOV @R,@R = 15 + 12 = 27; etc. */
    constexpr int kSrcCost[8] = { 9, 15, 15, 21, 18, 24, 24, 30 };
    constexpr int kDstAdd[8]  = { 3, 12, 12, 18, 15, 21, 21, 27 };

    /* Spot-check the four corners of the matrix. */
    CHECK(mov_cycles(0, 0) == kSrcCost[0] + kDstAdd[0]);  /* 12 */
    CHECK(mov_cycles(1, 1) == kSrcCost[1] + kDstAdd[1]);  /* 27 */
    CHECK(mov_cycles(6, 6) == kSrcCost[6] + kDstAdd[6]);  /* 45 */
    CHECK(mov_cycles(7, 7) == kSrcCost[7] + kDstAdd[7]);  /* 57 */
}

TEST_CASE("MFPT writes K1801VM1 type code (4) to R0") {
    /* MFPT (000007) returns the CPU type identifier in R0.  The
     * K1801VM1 returns 4, matching the DEC T-11 / KDF-11 family.
     * Required by the unpatched ROM-A boot path on Omega; trapping
     * here causes the well-known pink-screen wedge. */
    ms0515::Emulator emu;
    emu.reset();
    emu.cpu().r[0] = 0xFFFF;                /* poison so we see the write */
    run_at(emu, BASE, {000007});
    CHECK(emu.cpu().r[0] == 4);
    CHECK(emu.cpu().irq_reserved == false); /* no trap */
}

/* ── Interrupt request API ───────────────────────────────────────────────── */

TEST_CASE("cpu_interrupt / cpu_clear_interrupt") {
    ms0515::Emulator emu;
    emu.reset();

    auto &cpu = emu.cpu();

    cpu_interrupt(&cpu, 3, CPU_VEC_KEYBOARD);
    CHECK(cpu.irq_virq[3] == true);
    CHECK(cpu.irq_virq_vec[3] == CPU_VEC_KEYBOARD);

    cpu_clear_interrupt(&cpu, 3);
    CHECK(cpu.irq_virq[3] == false);
}

/* ── Multi-instruction sequence ──────────────────────────────────────────── */

TEST_CASE("small loop: sum 1..5 using SOB") {
    ms0515::Emulator emu;
    emu.reset();

    /*
     * R0 = 5 (counter), R1 = 0 (accumulator)
     *
     * loop:  ADD R0, R1    ; 060001
     *        SOB R0, loop  ; 077002 (offset=2 words back)
     *
     * After SOB drops through (R0 hits 0) we land at the next address.
     * No HALT — that opcode now traps to the restart vector instead of
     * stopping the CPU, so the test runs an exact known number of
     * instructions (2 setup + 5 × 2 loop body) and inspects R1. */
    uint16_t addr = BASE;
    addr = emit(emu, addr, {012700, 5});       /* MOV #5, R0 */
    addr = emit(emu, addr, {012701, 0});       /* MOV #0, R1 */
    uint16_t loop = addr;
    addr = emit(emu, addr, {060001});          /* ADD R0, R1 */
    addr = emit(emu, addr, {077002});          /* SOB R0, loop (offset=2 words back) */
    (void)loop;

    emu.cpu().r[CPU_REG_PC] = BASE;
    emu.cpu().halted = false;

    /* 2 setup MOVs + 5 iterations × (ADD + SOB) = 12 instructions. */
    for (int i = 0; i < 12; i++)
        emu.stepInstruction();

    CHECK(emu.cpu().r[1] == 15);  /* 5+4+3+2+1 = 15 */
}

} /* TEST_SUITE */
