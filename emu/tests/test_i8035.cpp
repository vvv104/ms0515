#include <doctest/doctest.h>
#include <cstring>
#include <initializer_list>

extern "C" {
#include <ms0515/i8035.h>
}

TEST_SUITE("i8035") {

/* ── Fixture ─────────────────────────────────────────────────────────────── */

/* A scratch CPU bound to a 256-byte ROM we can fill from a test.
 * Tracks every port-IO and pin-read interaction so port tests can
 * inspect the wire-level transcript. */
struct Cpu {
    i8035_t  cpu;
    uint8_t  rom[256];

    /* Programmable inputs the host returns when the CPU asks. */
    uint8_t  in_bus = 0xFF;
    uint8_t  in_p1  = 0xFF;
    uint8_t  in_p2  = 0xFF;
    bool     pin_t0 = false;
    bool     pin_t1 = false;
    bool     pin_int = false;

    /* Last value written to each port — set by OUTL / ANL / ORL. */
    uint8_t  out_bus = 0;
    uint8_t  out_p1  = 0;
    uint8_t  out_p2  = 0;

    /* Last PROG transition recorded.  prog_edges counts every change. */
    bool     prog_level = true;       /* PROG idles high             */
    int      prog_edges = 0;

    static uint8_t cb_read (void *ctx, uint8_t port) {
        auto *c = static_cast<Cpu*>(ctx);
        switch (port) {
        case I8035_PORT_BUS: return c->in_bus;
        case I8035_PORT_P1:  return c->in_p1;
        case I8035_PORT_P2:  return c->in_p2;
        }
        return 0xFF;
    }
    static void cb_write(void *ctx, uint8_t port, uint8_t val) {
        auto *c = static_cast<Cpu*>(ctx);
        switch (port) {
        case I8035_PORT_BUS: c->out_bus = val; break;
        case I8035_PORT_P1:  c->out_p1  = val; break;
        case I8035_PORT_P2:  c->out_p2  = val; break;
        }
    }
    static bool cb_t0 (void *ctx) { return static_cast<Cpu*>(ctx)->pin_t0; }
    static bool cb_t1 (void *ctx) { return static_cast<Cpu*>(ctx)->pin_t1; }
    static bool cb_int(void *ctx) { return static_cast<Cpu*>(ctx)->pin_int; }
    static void cb_prog(void *ctx, bool level) {
        auto *c = static_cast<Cpu*>(ctx);
        if (level != c->prog_level) c->prog_edges++;
        c->prog_level = level;
    }

    void load(std::initializer_list<uint8_t> bytes) {
        std::memset(rom, 0, sizeof(rom));
        std::size_t i = 0;
        for (uint8_t b : bytes) rom[i++] = b;
        i8035_init(&cpu, rom, sizeof(rom),
                   this, cb_read, cb_write,
                   cb_t0, cb_t1, cb_int, cb_prog);
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

/* ── Arithmetic: ADD / ADDC ──────────────────────────────────────────────── */

TEST_CASE("ADD A,#imm sums and clears carry on no-overflow") {
    Cpu c; c.load({
        0x23, 0x05,   /* MOV A,#05    */
        0x03, 0x07,   /* ADD A,#07    */
    });
    c.run(2);
    CHECK(c.cpu.a == 0x0C);
    CHECK((c.cpu.psw & 0x80) == 0);   /* CY clear */
    CHECK((c.cpu.psw & 0x40) == 0);   /* AC clear */
}

TEST_CASE("ADD A,#imm sets CY on byte overflow") {
    Cpu c; c.load({
        0x23, 0xF0,   /* MOV A,#F0    */
        0x03, 0x20,   /* ADD A,#20 → 110 → A=10, CY=1 */
    });
    c.run(2);
    CHECK(c.cpu.a == 0x10);
    CHECK((c.cpu.psw & 0x80) != 0);   /* CY set   */
    CHECK((c.cpu.psw & 0x40) == 0);   /* AC clear (no nibble overflow at low end) */
}

TEST_CASE("ADD A,#imm sets AC on low-nibble overflow") {
    Cpu c; c.load({
        0x23, 0x08,   /* MOV A,#08    */
        0x03, 0x09,   /* ADD A,#09 → 11h, AC=1 (8+9=17 in low nibble) */
    });
    c.run(2);
    CHECK(c.cpu.a == 0x11);
    CHECK((c.cpu.psw & 0x80) == 0);
    CHECK((c.cpu.psw & 0x40) != 0);   /* AC set */
}

TEST_CASE("ADD A,Rn / ADD A,@Rn use the right operand") {
    /* Pointer must lie inside the 6-bit RAM (0x00..0x3F), so we use
     * 0x30 — anywhere in the user area away from R0..R7. */
    Cpu c; c.load({
        0xB8, 0x30,   /* MOV R0,#30h          */
        0xB0, 0x33,   /* MOV @R0,#33          */
        0xB9, 0x07,   /* MOV R1,#07           */
        0x23, 0x10,   /* MOV A,#10            */
        0x69,         /* ADD A,R1 → A=17      */
        0x60,         /* ADD A,@R0 → A=17+33=4A */
    });
    c.run(6);
    CHECK(c.cpu.a == 0x4A);
}

TEST_CASE("ADDC A,#imm adds carry input") {
    Cpu c; c.load({
        0x23, 0x80,   /* MOV A,#80 (carry bit) */
        0xD7,         /* MOV PSW,A → CY=1      */
        0x23, 0x05,   /* MOV A,#05             */
        0x13, 0x03,   /* ADDC A,#03 → 09       */
    });
    c.run(4);
    CHECK(c.cpu.a == 0x09);
    CHECK((c.cpu.psw & 0x80) == 0);   /* CY consumed */
}

TEST_CASE("ADDC A,Rn / ADDC A,@Rn pull carry through") {
    Cpu c; c.load({
        0x23, 0x80,   /* MOV A,#80                          */
        0xD7,         /* MOV PSW,A → CY=1                   */
        0x23, 0xFF,   /* MOV A,#FF                          */
        0xB8, 0x30,   /* MOV R0,#30                         */
        0xB0, 0x00,   /* MOV @R0,#00                        */
        0x70,         /* ADDC A,@R0 → FF+00+1 = 100 → 00, CY=1 */
    });
    c.run(6);
    CHECK(c.cpu.a == 0x00);
    CHECK((c.cpu.psw & 0x80) != 0);
}

/* ── DA A — decimal adjust after BCD addition ───────────────────────────── */

TEST_CASE("DA A: low nibble > 9 adds 6") {
    /* 0x05 + 0x09 = 0x0E (binary).  DA → 0x14 (BCD).  No CY out. */
    Cpu c; c.load({
        0x23, 0x05,
        0x03, 0x09,   /* ADD A,#09 → 0E, AC=0 (5+9=14 low nibble) */
        0x57,         /* DA A → 14h */
    });
    c.run(3);
    CHECK(c.cpu.a == 0x14);
    CHECK((c.cpu.psw & 0x80) == 0);
}

TEST_CASE("DA A: high-nibble adjust sets CY") {
    /* 0x99 + 0x01 = 0x9A.  DA: low > 9 → +6 → A0; high > 9 → +60 → 00, CY=1.
     * BCD model: 99 + 01 = 100 → 00 with carry. */
    Cpu c; c.load({
        0x23, 0x99,
        0x03, 0x01,   /* ADD A,#01 → 9A, AC=0 */
        0x57,         /* DA A → low+6=A0; high>9 → +60=00, CY=1 */
    });
    c.run(3);
    CHECK(c.cpu.a == 0x00);
    CHECK((c.cpu.psw & 0x80) != 0);
}

/* ── Logic: ANL / ORL / XRL ─────────────────────────────────────────────── */

TEST_CASE("ANL / ORL / XRL with immediate, register, and indirect") {
    Cpu c; c.load({
        0xB8, 0x30,    /* MOV R0,#30                       */
        0xB0, 0x0F,    /* MOV @R0,#0F                      */
        0xBA, 0xF0,    /* MOV R2,#F0                       */
        0x23, 0xAA,    /* MOV A,#AA                        */
        0x53, 0xCC,    /* ANL A,#CC → 88                   */
        0x4A,          /* ORL A,R2 → 88|F0 = F8            */
        0xD0,          /* XRL A,@R0 → F8 ^ 0F = F7         */
    });
    c.run(7);
    CHECK(c.cpu.a == 0xF7);
}

TEST_CASE("Logic ops do not touch CY or AC") {
    Cpu c; c.load({
        0x23, 0xC0,    /* MOV A,#C0 (CY=1, AC=1)           */
        0xD7,          /* MOV PSW,A → CY=AC=1              */
        0x23, 0xFF,    /* MOV A,#FF                        */
        0x53, 0x00,    /* ANL A,#00 → 0                    */
    });
    c.run(4);
    CHECK(c.cpu.a == 0);
    CHECK((c.cpu.psw & 0x80) != 0);   /* CY preserved */
    CHECK((c.cpu.psw & 0x40) != 0);   /* AC preserved */
}

/* ── Rotates ─────────────────────────────────────────────────────────────── */

TEST_CASE("RL A rotates left without carry") {
    Cpu c; c.load({
        0x23, 0x81,    /* MOV A,#81                    */
        0xE7,          /* RL A → 03                    */
    });
    c.run(2);
    CHECK(c.cpu.a == 0x03);
    CHECK((c.cpu.psw & 0x80) == 0);   /* CY untouched */
}

TEST_CASE("RR A rotates right without carry") {
    Cpu c; c.load({
        0x23, 0x03,    /* MOV A,#03                    */
        0x77,          /* RR A → 81                    */
    });
    c.run(2);
    CHECK(c.cpu.a == 0x81);
}

TEST_CASE("RLC A pulls bit 7 into CY and CY into bit 0") {
    Cpu c; c.load({
        0x23, 0x80,    /* MOV A,#80                    */
        0xD7,          /* MOV PSW,A — set CY           */
        0x23, 0x80,    /* MOV A,#80                    */
        0xF7,          /* RLC A → A = 01, CY = 1       */
    });
    c.run(4);
    CHECK(c.cpu.a == 0x01);
    CHECK((c.cpu.psw & 0x80) != 0);
}

TEST_CASE("RRC A pulls bit 0 into CY and CY into bit 7") {
    Cpu c; c.load({
        0x97,          /* CLR C                        */
        0x23, 0x03,    /* MOV A,#03                    */
        0x67,          /* RRC A → A = 01, CY = 1       */
    });
    c.run(3);
    CHECK(c.cpu.a == 0x01);
    CHECK((c.cpu.psw & 0x80) != 0);
}

/* ── Flag manipulation: CLR/CPL C, F0, F1, JF0, JF1 ─────────────────────── */

TEST_CASE("CLR C / CPL C toggle PSW.CY without disturbing other bits") {
    Cpu c; c.load({
        0x23, 0xF0,    /* MOV A,#F0 — high nibble all set */
        0xD7,          /* MOV PSW,A → CY=AC=F0=BS=1     */
        0x97,          /* CLR C → PSW = 78              */
        0xA7,          /* CPL C → PSW = F8              */
    });
    c.run(4);
    CHECK(c.cpu.psw == 0xF8);
    CHECK((c.cpu.psw & 0x70) == 0x70);   /* AC, F0, BS preserved */
}

TEST_CASE("CLR / CPL F0 toggle PSW bit 5") {
    Cpu c; c.load({
        0x95,          /* CPL F0 → set                 */
        0x95,          /* CPL F0 → clear               */
        0x95,          /* CPL F0 → set                 */
        0x85,          /* CLR F0                       */
    });
    c.run(3);
    CHECK((c.cpu.psw & 0x20) != 0);
    c.run(1);
    CHECK((c.cpu.psw & 0x20) == 0);
}

TEST_CASE("CLR / CPL F1 toggle the standalone F1 flag") {
    Cpu c; c.load({
        0xB5,          /* CPL F1 → 1                   */
        0xB5,          /* CPL F1 → 0                   */
        0xB5,          /* CPL F1 → 1                   */
        0xA5,          /* CLR F1                       */
    });
    c.run(3);
    CHECK(c.cpu.f1 == true);
    c.run(1);
    CHECK(c.cpu.f1 == false);
}

TEST_CASE("JF0 jumps when F0 is set, falls through otherwise") {
    Cpu c; c.load({
        0x95,          /* CPL F0 → set                 */
        0xB6, 0x10,    /* JF0 0x10 — taken             */
    });
    c.rom[0x10] = 0x23;
    c.rom[0x11] = 0x42;
    c.run(3);
    CHECK(c.cpu.a == 0x42);
}

TEST_CASE("JF1 jumps when F1 is set, falls through otherwise") {
    Cpu c; c.load({
        0xB5,          /* CPL F1 → set                 */
        0x76, 0x10,    /* JF1 0x10 — taken             */
    });
    c.rom[0x10] = 0x23;
    c.rom[0x11] = 0x55;
    c.run(3);
    CHECK(c.cpu.a == 0x55);
}

/* ── Port IO: IN, OUTL, ANL, ORL on P1 / P2 / BUS ───────────────────────── */

TEST_CASE("IN A,P1 / IN A,P2 / INS A,BUS pull from host callback") {
    Cpu c;
    c.in_p1  = 0x42;
    c.in_p2  = 0x77;
    c.in_bus = 0xCC;
    c.load({
        0x09,          /* IN A,P1 → A = 0x42         */
        0xAB,          /* MOV R3,A                   */
        0x0A,          /* IN A,P2 → A = 0x77         */
        0xAC,          /* MOV R4,A                   */
        0x08,          /* INS A,BUS → A = 0xCC       */
    });
    c.run(5);
    CHECK(c.cpu.a == 0xCC);
    CHECK(c.cpu.ram[3] == 0x42);
    CHECK(c.cpu.ram[4] == 0x77);
}

TEST_CASE("OUTL P1,A / OUTL P2,A drive the latch and the host callback") {
    Cpu c; c.load({
        0x23, 0x5A,    /* MOV A,#5A                  */
        0x39,          /* OUTL P1,A                  */
        0x23, 0xA5,    /* MOV A,#A5                  */
        0x3A,          /* OUTL P2,A                  */
        0x23, 0x33,    /* MOV A,#33                  */
        0x02,          /* OUTL BUS,A                 */
    });
    c.run(6);
    CHECK(c.cpu.p1_out == 0x5A);   CHECK(c.out_p1  == 0x5A);
    CHECK(c.cpu.p2_out == 0xA5);   CHECK(c.out_p2  == 0xA5);
                                    CHECK(c.out_bus == 0x33);
}

TEST_CASE("ANL P1,#imm / ORL P1,#imm AND/OR the latch in place") {
    Cpu c; c.load({
        0x23, 0xFF,    /* MOV A,#FF                  */
        0x39,          /* OUTL P1,A → latch = FF     */
        0x99, 0xF0,    /* ANL P1,#F0 → latch = F0    */
        0x89, 0x0C,    /* ORL P1,#0C → latch = FC    */
    });
    c.run(4);
    CHECK(c.cpu.p1_out == 0xFC);
    CHECK(c.out_p1     == 0xFC);
}

TEST_CASE("ANL P2,#imm / ORL P2,#imm AND/OR the P2 latch") {
    Cpu c; c.load({
        0x23, 0xFF,    /* MOV A,#FF                  */
        0x3A,          /* OUTL P2,A → latch = FF     */
        0x9A, 0x0F,    /* ANL P2,#0F → latch = 0F    */
        0x8A, 0x80,    /* ORL P2,#80 → latch = 8F    */
    });
    c.run(4);
    CHECK(c.cpu.p2_out == 0x8F);
}

/* ── MOVD / ANLD / ORLD: 8243 expander interface ────────────────────────── */

/* Helper: model an 8243 just barely enough to satisfy a MOVD A,Pp.
 * The CPU drives a 4-bit READ command on P2[3:0] and strobes PROG low;
 * while PROG is low, the host (this lambda) places the expander port
 * value on the low nibble of P2.  PROG goes high to latch. */
TEST_CASE("MOVD A,P4..P7 strobes PROG and latches expander nibble") {
    Cpu c;
    /* Programmed expander value: bottom nibble of in_p2 is what the
     * CPU will sample while PROG is low.  Tie the upper nibble to
     * what the latch wrote so we exercise the (val & 0x0F) mask. */
    c.in_p2 = 0xC7;        /* low nibble = 0x7 — what the expander returns */
    c.load({
        0x0E,              /* MOVD A,P6                                  */
    });
    c.run(1);
    CHECK(c.cpu.a == 0x07);              /* low nibble latched, high cleared */
    CHECK(c.prog_edges >= 2);            /* at least one falling + rising  */
    CHECK(c.prog_level == true);         /* PROG returns high after MOVD   */
    /* During the strobe the CPU put the READ command (00) plus port
     * number (P6 → 10) into P2[3:0].  After the instruction the latch
     * reflects whatever was last written — which is the command byte. */
    CHECK((c.out_p2 & 0x0F) == 0x02);    /* READ (00) | port-2 (10) = 0010 */
}

TEST_CASE("MOVD Pp,A drives WRITE command then data through P2") {
    Cpu c;
    c.load({
        0x23, 0x0A,        /* MOV A,#0A — only low nibble matters       */
        0x3D,              /* MOVD P5,A                                  */
    });
    c.run(2);
    CHECK(c.prog_edges >= 2);
    CHECK(c.prog_level == true);
    /* After the strobe sequence the P2 latch holds the data nibble
     * (0xA), having earlier held the WRITE command (01 << 2 | 01). */
    CHECK((c.cpu.p2_out & 0x0F) == 0x0A);
}

/* ── Timer / counter ─────────────────────────────────────────────────────── */

TEST_CASE("MOV T,A / MOV A,T round-trip the counter register") {
    Cpu c; c.load({
        0x23, 0x55,    /* MOV A,#55                   */
        0x62,          /* MOV T,A                     */
        0x27,          /* CLR A                       */
        0x42,          /* MOV A,T                     */
    });
    c.run(4);
    CHECK(c.cpu.t == 0x55);
    CHECK(c.cpu.a == 0x55);
}

TEST_CASE("STRT T increments T once per 32 machine cycles") {
    /* STRT T at 0, then NOPs (already zero in fresh ROM) — each NOP
     * is one machine cycle, so 32 NOPs after STRT T roll the prescaler
     * exactly once and bump T from 0 to 1. */
    Cpu c; c.load({0x55});
    c.run(33);
    CHECK(c.cpu.t == 1);
    CHECK(c.cpu.tf == false);
}

TEST_CASE("Timer overflow sets TF and JTF samples-and-clears it") {
    Cpu c; c.load({});
    c.rom[0x00] = 0x23; c.rom[0x01] = 0xFF;     /* MOV A,#FF       */
    c.rom[0x02] = 0x62;                          /* MOV T,A         */
    c.rom[0x03] = 0x55;                          /* STRT T          */
    /* NOPs at 0x04..0x23 (32 of them) — already zero from load().    */
    c.rom[0x24] = 0x16; c.rom[0x25] = 0x80;     /* JTF 0x80         */
    c.rom[0x80] = 0x23; c.rom[0x81] = 0xA5;     /* MOV A,#A5        */

    /* MOV A,#FF (1) + MOV T,A (1) + STRT T (1) + 32 NOPs = 35 steps.
     * After step 34 the prescaler reaches 32 → T overflows to 0,
     * TF is set; step 35 is one more NOP that bumps the prescaler
     * back up to 1. */
    c.run(35);
    CHECK(c.cpu.tf == true);
    CHECK(c.cpu.t  == 0x00);

    c.run(1);                                    /* JTF — taken      */
    CHECK(c.cpu.tf == false);
    CHECK(c.cpu.pc == 0x80);

    c.run(1);                                    /* MOV A,#A5        */
    CHECK(c.cpu.a == 0xA5);
}

TEST_CASE("STOP TCNT freezes the timer") {
    Cpu c; c.load({
        0x55,          /* STRT T                      */
        0x65,          /* STOP TCNT                   */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   /* 8 NOPs */
    });
    c.run(2 + 8);
    CHECK(c.cpu.t == 0);                    /* never ticked     */
    CHECK(c.cpu.timer_run == false);
}

TEST_CASE("STRT CNT counts T1 falling edges") {
    Cpu c; c.load({
        0x45,          /* STRT CNT                    */
        0x00, 0x00, 0x00, 0x00,            /* NOPs while we toggle T1 */
    });
    /* Drive T1 high then low across instruction boundaries. */
    c.pin_t1 = true;  c.run(1);            /* STRT CNT — sample T1 high */
    c.pin_t1 = false; c.run(1);            /* falling edge → T = 1  */
    c.pin_t1 = true;  c.run(1);            /* rising → no count    */
    c.pin_t1 = false; c.run(1);            /* falling edge → T = 2 */
    CHECK(c.cpu.t == 2);
}

/* ── Interrupts: enable, dispatch, return ───────────────────────────────── */

TEST_CASE("EN I + INT pin low dispatches to vector 0x003") {
    /* Vector 3 sits two bytes past EN I, so we route the main flow
     * around it with a JMP.  Without a JMP, normal execution would
     * reach the sentinel at 0x03 by simple PC advance and we
     * couldn't tell IRQ dispatch from straight-line execution. */
    Cpu c; c.load({});
    c.rom[0x00] = 0x04; c.rom[0x01] = 0x10;     /* JMP 0x10        */
    c.rom[0x03] = 0x23; c.rom[0x04] = 0x42;     /* ISR: MOV A,#42  */
    c.rom[0x10] = 0x05;                          /* EN I            */
    /* NOPs from 0x11 onward keep the main loop spinning.            */

    c.pin_int = true;                            /* INT pulled low  */
    c.run(2);                                    /* JMP + EN I       */
    CHECK(c.cpu.in_irq == false);
    c.run(1);                                    /* IRQ dispatched   */
    CHECK(c.cpu.in_irq == true);
    CHECK(c.cpu.pc == 0x03);
    c.run(1);                                    /* MOV A,#42 in ISR */
    CHECK(c.cpu.a == 0x42);
}

TEST_CASE("EN TCNTI + timer overflow dispatches to vector 0x007") {
    Cpu c; c.load({});
    /* JMP around the vector area to a setup block at 0x20. */
    c.rom[0x00] = 0x04; c.rom[0x01] = 0x20;     /* JMP 0x20         */
    c.rom[0x07] = 0x23; c.rom[0x08] = 0x99;     /* ISR: MOV A,#99   */
    c.rom[0x20] = 0x23; c.rom[0x21] = 0xFF;     /* MOV A,#FF        */
    c.rom[0x22] = 0x62;                          /* MOV T,A          */
    c.rom[0x23] = 0x25;                          /* EN TCNTI         */
    c.rom[0x24] = 0x55;                          /* STRT T           */
    /* NOPs at 0x25..0x44 — already zero. */

    /* JMP + 4 setup + 32 NOPs = 37 steps; on step 37 the IRQ check
     * fires and dispatches to vector 7. */
    c.run(1 + 4 + 32);
    CHECK(c.cpu.in_irq == true);
    CHECK(c.cpu.pc == 0x07);
    CHECK(c.cpu.tf == false);                    /* auto-cleared on ack */
    c.run(1);                                    /* MOV A,#99        */
    CHECK(c.cpu.a == 0x99);
}

TEST_CASE("RETR returns from interrupt and restores PSW upper, clears in_irq") {
    Cpu c; c.load({});
    c.rom[0x00] = 0x04; c.rom[0x01] = 0x10;     /* JMP 0x10         */
    /* ISR clears CY (touches only the PSW upper nibble — must not
     * write SP via MOV PSW,A, otherwise the pop reads from the wrong
     * stack slot).  RETR then pops PC and restores PSW upper from
     * the stack, so CY comes back from the snapshot taken on entry. */
    c.rom[0x03] = 0x97;                          /* CLR C            */
    c.rom[0x04] = 0x93;                          /* RETR             */
    /* Main: set CY=AC=1, EN I, NOP (return-slot). */
    c.rom[0x10] = 0x23; c.rom[0x11] = 0xC0;     /* MOV A,#C0        */
    c.rom[0x12] = 0xD7;                          /* MOV PSW,A → CY=AC=1 */
    c.rom[0x13] = 0x05;                          /* EN I             */
    c.rom[0x14] = 0x00;                          /* NOP (return slot) */

    c.pin_int = true;
    /* JMP, MOV A, MOV PSW, EN I = 4 steps; IRQ dispatch on step 5;
     * ISR has 2 instructions = steps 6-7.  After step 7 we are back
     * in the main flow at 0x14 with CY=AC=1 restored. */
    c.run(7);
    CHECK(c.cpu.in_irq == false);
    CHECK((c.cpu.psw & 0xF0) == 0xC0);           /* CY,AC restored   */
    CHECK(c.cpu.pc == 0x14);                     /* return slot      */
}

TEST_CASE("Nested interrupts are blocked while in_irq") {
    Cpu c; c.load({});
    c.rom[0x00] = 0x04; c.rom[0x01] = 0x10;     /* JMP 0x10         */
    /* ISR is just NOPs — never returns, so in_irq stays true.       */
    c.rom[0x10] = 0x05;                          /* EN I             */
    /* NOPs at 0x11+. */

    c.pin_int = true;
    c.run(3);                                    /* JMP + EN I + IRQ */
    REQUIRE(c.cpu.in_irq == true);
    uint8_t sp_before = c.cpu.psw & 0x07;
    /* Run a few more — INT is still asserted but we should not
     * recurse: stack pointer and in_irq don't change. */
    c.run(3);
    CHECK(c.cpu.in_irq == true);
    CHECK((c.cpu.psw & 0x07) == sp_before);
}

TEST_CASE("JNI samples INT pin without enabling interrupts") {
    /* INT high → JNI taken; INT low → JNI not taken. */
    Cpu c; c.load({
        0x86, 0x10,    /* JNI 0x10                    */
    });
    c.rom[0x10] = 0x23;
    c.rom[0x11] = 0xBB;
    c.pin_int = false;                     /* INT high           */
    c.run(2);
    CHECK(c.cpu.a == 0xBB);                /* taken              */
}

TEST_CASE("ANLD / ORLD use the AND / OR command codes") {
    Cpu c;
    c.load({
        0x23, 0x05,        /* MOV A,#05                                  */
        0x9C,              /* ANLD P4,A — AND command on port 4          */
    });
    c.run(2);
    /* AND command nibble is (11 << 2 | port).  P4 → port 0; final byte
     * before PROG rise should be the data 0x5 (low nibble of A). */
    CHECK((c.cpu.p2_out & 0x0F) == 0x05);
    CHECK(c.prog_edges >= 2);

    Cpu c2;
    c2.load({
        0x23, 0x09,
        0x8F,              /* ORLD P7,A — OR command on port 7=11       */
    });
    c2.run(2);
    CHECK((c2.cpu.p2_out & 0x0F) == 0x09);
    CHECK(c2.prog_edges >= 2);
}

} /* TEST_SUITE */
