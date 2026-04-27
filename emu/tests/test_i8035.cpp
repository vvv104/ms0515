#include <doctest/doctest.h>
#include <cstring>
#include <initializer_list>

extern "C" {
#include <ms0515/i8035.h>
}

TEST_SUITE("i8035") {

/* ── Fixture ─────────────────────────────────────────────────────────────── */

/* A scratch CPU bound to a 256-byte ROM we can fill from a test.
 * Callbacks are nullptr — port and pin tests come in a later commit
 * once port-IO opcodes are implemented. */
struct Cpu {
    i8035_t  cpu;
    uint8_t  rom[256];

    void load(std::initializer_list<uint8_t> bytes) {
        std::memset(rom, 0, sizeof(rom));
        std::size_t i = 0;
        for (uint8_t b : bytes) rom[i++] = b;
        i8035_init(&cpu, rom, sizeof(rom),
                   nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
        i8035_reset(&cpu);
    }

    /* Run N steps and return total cycles consumed. */
    int run(int steps) {
        int total = 0;
        for (int i = 0; i < steps; i++) total += i8035_step(&cpu);
        return total;
    }
};

/* ── Reset state ─────────────────────────────────────────────────────────── */

TEST_CASE("RESET sets PC=0, A=0, PSW=0x08, latches 0xFF") {
    Cpu c; c.load({});
    CHECK(c.cpu.pc == 0);
    CHECK(c.cpu.a == 0);
    CHECK(c.cpu.psw == 0x08);
    CHECK(c.cpu.p1_out == 0xFF);
    CHECK(c.cpu.p2_out == 0xFF);
    CHECK(c.cpu.f1 == false);
    CHECK(c.cpu.tf == false);
}

/* ── NOP ─────────────────────────────────────────────────────────────────── */

TEST_CASE("NOP advances PC by one and consumes one machine cycle") {
    Cpu c; c.load({0x00, 0x00});
    int cycles = c.run(2);
    CHECK(c.cpu.pc == 2);
    CHECK(cycles == 2);
}

/* ── MOV immediates ──────────────────────────────────────────────────────── */

TEST_CASE("MOV A,#imm loads accumulator (2 bytes, 2 cycles)") {
    Cpu c; c.load({0x23, 0xAB});
    int cycles = c.run(1);
    CHECK(c.cpu.a == 0xAB);
    CHECK(c.cpu.pc == 2);
    CHECK(cycles == 2);
}

TEST_CASE("MOV Rn,#imm loads each register R0..R7") {
    Cpu c; c.load({
        0xB8, 0x10,   /* MOV R0,#10h */
        0xB9, 0x21,   /* MOV R1,#21h */
        0xBA, 0x32,   /* MOV R2,#32h */
        0xBF, 0x77,   /* MOV R7,#77h */
    });
    c.run(4);
    CHECK(c.cpu.ram[0] == 0x10);
    CHECK(c.cpu.ram[1] == 0x21);
    CHECK(c.cpu.ram[2] == 0x32);
    CHECK(c.cpu.ram[7] == 0x77);
}

TEST_CASE("MOV @Rn,#imm writes to RAM via R0/R1") {
    Cpu c; c.load({
        0xB8, 0x20,   /* MOV R0,#20h — pointer */
        0xB0, 0x99,   /* MOV @R0,#99h */
    });
    c.run(2);
    CHECK(c.cpu.ram[0x20] == 0x99);
}

/* ── MOV between A and registers / RAM ──────────────────────────────────── */

TEST_CASE("MOV A,Rn / MOV Rn,A round-trip through every register") {
    Cpu c; c.load({
        0x23, 0x42,   /* MOV A,#42h            */
        0xAB,         /* MOV R3,A              */
        0x23, 0x00,   /* MOV A,#0   (clobber)  */
        0xFB,         /* MOV A,R3              */
    });
    c.run(4);
    CHECK(c.cpu.a == 0x42);
    CHECK(c.cpu.ram[3] == 0x42);
}

TEST_CASE("MOV @Rn,A / MOV A,@Rn round-trip through RAM") {
    Cpu c; c.load({
        0xB8, 0x30,   /* MOV R0,#30h           */
        0x23, 0x55,   /* MOV A,#55h            */
        0xA0,         /* MOV @R0,A — ram[0x30]=0x55 */
        0x27,         /* CLR A                 */
        0xF0,         /* MOV A,@R0             */
    });
    c.run(5);
    CHECK(c.cpu.a == 0x55);
    CHECK(c.cpu.ram[0x30] == 0x55);
}

/* ── Accumulator-only ops ────────────────────────────────────────────────── */

TEST_CASE("CLR A / CPL A / SWAP A behave correctly") {
    Cpu c; c.load({
        0x23, 0xA5,   /* MOV A,#A5h */
        0x37,         /* CPL A → 5Ah */
        0x47,         /* SWAP → A5h */
        0x27,         /* CLR A → 00 */
    });
    c.run(4);
    CHECK(c.cpu.a == 0);
}

TEST_CASE("INC A / DEC A wrap on byte boundaries") {
    Cpu c; c.load({
        0x23, 0xFF,   /* MOV A,#FF */
        0x17,         /* INC A → 00 */
        0x07,         /* DEC A → FF */
        0x07,         /* DEC A → FE */
    });
    c.run(4);
    CHECK(c.cpu.a == 0xFE);
}

TEST_CASE("INC Rn / DEC Rn / INC @Rn modify storage") {
    Cpu c; c.load({
        0xB8, 0x10,   /* MOV R0,#10h           */
        0xB9, 0x80,   /* MOV R1,#80h           */
        0x18,         /* INC R0 → 11h          */
        0xC9,         /* DEC R1 → 7Fh          */
        0xB0, 0x00,   /* MOV @R0,#00 — ram[0x11] starts 0 */
        0x10,         /* INC @R0 → ram[0x11]=01 */
    });
    c.run(6);
    CHECK(c.cpu.ram[0] == 0x11);
    CHECK(c.cpu.ram[1] == 0x7F);
    CHECK(c.cpu.ram[0x11] == 0x01);
}

/* ── Bank select ─────────────────────────────────────────────────────────── */

TEST_CASE("SEL RB1 redirects Rn to ram[24..31]; SEL RB0 restores") {
    Cpu c; c.load({
        0x23, 0x11,   /* MOV A,#11             */
        0xAB,         /* MOV R3,A → ram[3]=11  (RB0) */
        0xD5,         /* SEL RB1               */
        0x23, 0x22,   /* MOV A,#22             */
        0xAB,         /* MOV R3,A → ram[27]=22 (RB1) */
        0xC5,         /* SEL RB0               */
        0xFB,         /* MOV A,R3 → A = 11     */
    });
    c.run(7);
    CHECK(c.cpu.a == 0x11);
    CHECK(c.cpu.ram[3]  == 0x11);
    CHECK(c.cpu.ram[27] == 0x22);
}

/* ── PSW round-trip ──────────────────────────────────────────────────────── */

TEST_CASE("MOV A,PSW reads PSW with bit 3 forced to 1; MOV PSW,A writes back") {
    Cpu c; c.load({
        0x23, 0xF0,   /* MOV A,#F0h            */
        0xD7,         /* MOV PSW,A → psw = F0|08 = F8 */
        0xC7,         /* MOV A,PSW             */
    });
    c.run(3);
    CHECK(c.cpu.psw == 0xF8);
    CHECK(c.cpu.a   == 0xF8);
}

/* ── Jumps, calls, returns ───────────────────────────────────────────────── */

TEST_CASE("JMP page0 jumps within first 256 bytes") {
    Cpu c; c.load({
        0x04, 0x10,   /* 0: JMP 0x010          */
        0x00, 0x00,   /* 2..3: padding         */
    });
    /* Place a sentinel at 0x10 so we can confirm execution went there. */
    c.rom[0x10] = 0x23;       /* MOV A,#5A             */
    c.rom[0x11] = 0x5A;
    c.run(2);
    CHECK(c.cpu.pc == 0x12);
    CHECK(c.cpu.a  == 0x5A);
}

TEST_CASE("JMP page-N selects high address bits") {
    /* Opcode 0x44 = JMP with bits 8..10 = 010 → target = 0x200 + lo.
     * Our ROM is only 256 bytes so we just verify the resulting PC,
     * not what executes there. */
    Cpu c; c.load({0x44, 0x55});
    c.run(1);
    CHECK(c.cpu.pc == 0x255);
}

TEST_CASE("CALL pushes return + RET pops it") {
    Cpu c; c.load({
        0x14, 0x10,   /* 0: CALL 0x010 (return PC = 0x002) */
        0x23, 0x99,   /* 2: MOV A,#99 (executed after RET) */
    });
    c.rom[0x10] = 0x83;       /* RET                   */

    c.run(1);                 /* execute CALL          */
    CHECK(c.cpu.pc == 0x10);
    CHECK((c.cpu.psw & 0x07) == 1);   /* SP advanced to 1 */

    c.run(1);                 /* execute RET           */
    CHECK(c.cpu.pc == 0x02);
    CHECK((c.cpu.psw & 0x07) == 0);

    c.run(1);                 /* MOV A,#99             */
    CHECK(c.cpu.a == 0x99);
}

/* ── Conditional jumps on Acc / Carry ───────────────────────────────────── */

TEST_CASE("JZ taken when A == 0, not taken otherwise") {
    Cpu c; c.load({
        0x27,         /* 0: CLR A                          */
        0xC6, 0x10,   /* 1: JZ 0x10                        */
    });
    c.rom[0x10] = 0x23;
    c.rom[0x11] = 0xAA;
    c.run(3);                 /* CLR A; JZ taken; MOV A,#AA */
    CHECK(c.cpu.a == 0xAA);
}

TEST_CASE("JNZ falls through when A == 0") {
    Cpu c; c.load({
        0x27,         /* CLR A             */
        0x96, 0x10,   /* JNZ 0x10 — not taken */
        0x23, 0x77,   /* MOV A,#77         */
    });
    c.run(3);
    CHECK(c.cpu.a == 0x77);
    CHECK(c.cpu.pc == 5);
}

TEST_CASE("JC / JNC follow PSW carry bit") {
    /* We cannot SET carry yet (no ADD / CLR C in this commit), so we
     * inject it via MOV PSW,A.  This also doubles as a test that
     * PSW writes through to the carry flag. */
    Cpu c; c.load({
        0x23, 0x80,   /* MOV A,#80 (carry bit) */
        0xD7,         /* MOV PSW,A → CY=1      */
        0xF6, 0x10,   /* JC 0x10 — taken       */
    });
    c.rom[0x10] = 0x23;
    c.rom[0x11] = 0xCC;
    c.run(4);
    CHECK(c.cpu.a == 0xCC);
}

} /* TEST_SUITE */
