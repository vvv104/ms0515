/*
 * i8035.c — Intel MCS-48 (8035 / 8048 family) CPU core implementation.
 *
 * See i8035.h for the architectural model.  This file implements the
 * instruction set directly from the Intel MCS-48 Programmer's Manual
 * (1976), one opcode (or one tightly-related group of opcodes) per
 * static helper, dispatched through a single 256-entry switch.
 *
 * Coverage so far (commits 1..3 of phase 1):
 *   - control flow:  JMP page0..7, CALL page0..7, RET, conditional
 *                    branches on Acc / carry / F0 / F1
 *                    (JZ / JNZ / JC / JNC / JF0 / JF1)
 *   - data movement: MOV A,#imm; MOV Rn,#imm; MOV @Rn,#imm;
 *                    MOV A,Rn; MOV Rn,A; MOV A,@Rn; MOV @Rn,A
 *   - simple ops:    NOP, CLR A, CPL A, SWAP A,
 *                    INC A, DEC A, INC Rn, DEC Rn, INC @Rn
 *   - bank / PSW:    SEL RB0, SEL RB1, MOV A,PSW, MOV PSW,A
 *   - arithmetic:    ADD / ADDC against #imm, Rn, @Rn, with proper
 *                    CY and AC flag updates; DA A
 *   - logic:         ANL / ORL / XRL against #imm, Rn, @Rn
 *   - rotates:       RL A, RR A, RLC A, RRC A
 *   - flag ops:      CLR/CPL C, CLR/CPL F0, CLR/CPL F1
 *   - port IO:       INS A,BUS; IN A,P1/P2; OUTL BUS/P1/P2,A;
 *                    ANL/ORL BUS/P1/P2,#imm
 *   - 8243 expander: MOVD A,P4..P7; MOVD P4..P7,A; ANLD/ORLD P4..P7,A
 *                    (drives PROG strobe through the host callback)
 *
 * Coverage to follow in subsequent commits:
 *   - timer / counter (STRT/STOP/MOV T,A, JTF),
 *   - interrupts (EN/DIS I, EN/DIS TCNTI, RETR, vector dispatch),
 *   - external memory (MOVX), MOVP / MOVP3, JMPP, DJNZ,
 *   - bit-test branches (JBb), XCH A,Rn / XCH A,@Rn / XCHD A,@Rn.
 *
 * Unimplemented opcodes trap via assert() in debug, and behave as NOP
 * in release — this surfaces firmware paths that exercise something
 * we don't model yet, without crashing the emulator outright.
 */

#include <ms0515/i8035.h>

#include <assert.h>
#include <string.h>

/* ── Internal helpers ─────────────────────────────────────────────────── */

#define PSW_CY  0x80
#define PSW_AC  0x40
#define PSW_F0  0x20
#define PSW_BS  0x10

/* Set or clear an arbitrary PSW bit while leaving the rest intact. */
static inline void psw_assign(i8035_t *cpu, uint8_t mask, bool on)
{
    cpu->psw = (uint8_t)(on ? (cpu->psw | mask) : (cpu->psw & ~mask));
}

/* Add `b` (and an optional carry-in) to the accumulator and update the
 * CY / AC flags.  Both ADD and ADDC route through here. */
static inline void add_to_a(i8035_t *cpu, uint8_t b, bool carry_in)
{
    uint8_t a = cpu->a;
    unsigned cin = carry_in ? 1u : 0u;
    unsigned full = (unsigned)a + (unsigned)b + cin;
    bool ac = (((a & 0x0F) + (b & 0x0F) + cin) & 0x10) != 0;
    cpu->a = (uint8_t)full;
    psw_assign(cpu, PSW_CY, full > 0xFFu);
    psw_assign(cpu, PSW_AC, ac);
}

/* Push a byte both into the corresponding output latch (for P1/P2 —
 * BUS has no latch in our model) and through the host callback so the
 * host sees the change immediately. */
static inline void port_write(i8035_t *cpu, uint8_t port, uint8_t val)
{
    if (port == I8035_PORT_P1) cpu->p1_out = val;
    if (port == I8035_PORT_P2) cpu->p2_out = val;
    if (cpu->port_write) cpu->port_write(cpu->host_ctx, port, val);
}

static inline uint8_t port_read(i8035_t *cpu, uint8_t port)
{
    return cpu->port_read ? cpu->port_read(cpu->host_ctx, port) : 0xFF;
}

static inline void prog_edge(i8035_t *cpu, bool level)
{
    if (cpu->prog) cpu->prog(cpu->host_ctx, level);
}

/* Drive a single i8243 transaction.  cmd_bits is the 2-bit opcode for
 * the expander (00=READ, 01=WRITE, 10=ORLD, 11=ANLD); port is 0..3 for
 * P4..P7.  For READ the accumulator gets the low nibble that the
 * expander placed on P2 while PROG was low; for the write-style
 * commands the data nibble of A goes onto P2 before PROG rises. */
static void expander_op(i8035_t *cpu, uint8_t cmd_bits, int port,
                        bool is_read)
{
    uint8_t cmd_nibble = (uint8_t)(((cmd_bits & 3) << 2) | (port & 3));
    port_write(cpu, I8035_PORT_P2,
               (uint8_t)((cpu->p2_out & 0xF0) | cmd_nibble));
    prog_edge(cpu, false);
    if (is_read) {
        cpu->a = (uint8_t)(port_read(cpu, I8035_PORT_P2) & 0x0F);
    } else {
        port_write(cpu, I8035_PORT_P2,
                   (uint8_t)((cpu->p2_out & 0xF0) | (cpu->a & 0x0F)));
    }
    prog_edge(cpu, true);
}

/* Index into ram[] for the currently-selected register Rn. */
static inline uint8_t reg_index(const i8035_t *cpu, int n)
{
    return (uint8_t)(((cpu->psw & 0x10) ? 24 : 0) + (n & 7));
}

static inline uint8_t get_reg(const i8035_t *cpu, int n)
{
    return cpu->ram[reg_index(cpu, n)];
}

static inline void set_reg(i8035_t *cpu, int n, uint8_t val)
{
    cpu->ram[reg_index(cpu, n)] = val;
}

/* Indirect through R0 or R1 — the 8035's two pointer registers.  The
 * pointer value is masked to 6 bits since internal RAM is 64 bytes. */
static inline uint8_t indirect_addr(const i8035_t *cpu, int rn /* 0 or 1 */)
{
    return (uint8_t)(get_reg(cpu, rn) & 0x3F);
}

/* ROM fetch.  PC is 12 bits but the ms7004 image is 2 KB, so we mask
 * to 11 bits to keep all fetches inside the loaded image; an out-of-
 * range fetch (which would mean the firmware ran off the end of its
 * own image) returns 0xFF — same as an unprogrammed EPROM. */
static inline uint8_t fetch(i8035_t *cpu)
{
    uint16_t addr = cpu->pc & 0x0FFF;
    cpu->pc = (uint16_t)((cpu->pc + 1) & 0x0FFF);
    if (cpu->rom == NULL || addr >= cpu->rom_size)
        return 0xFF;
    return cpu->rom[addr];
}

/* Stack push: stores PC[7:0] at ram[8 + 2*SP], PC[11:8] in low nibble
 * + PSW[7:4] in high nibble at ram[9 + 2*SP], then increments SP.  The
 * stack pointer is the low 3 bits of PSW. */
static void stack_push(i8035_t *cpu)
{
    uint8_t sp = (uint8_t)(cpu->psw & 0x07);
    uint8_t base = (uint8_t)(8 + sp * 2);
    cpu->ram[base + 0] = (uint8_t)(cpu->pc & 0xFF);
    cpu->ram[base + 1] = (uint8_t)(((cpu->pc >> 8) & 0x0F)
                                 | (cpu->psw & 0xF0));
    sp = (uint8_t)((sp + 1) & 0x07);
    cpu->psw = (uint8_t)((cpu->psw & 0xF8) | sp);
}

/* Stack pop.  `restore_psw` true ⇒ RETR (restores PSW upper nibble
 * from the stack as well as PC); false ⇒ RET (restores PC only). */
static void stack_pop(i8035_t *cpu, bool restore_psw)
{
    uint8_t sp = (uint8_t)((cpu->psw - 1) & 0x07);
    uint8_t base = (uint8_t)(8 + sp * 2);
    uint8_t lo = cpu->ram[base + 0];
    uint8_t hi = cpu->ram[base + 1];
    cpu->pc = (uint16_t)(lo | ((hi & 0x0F) << 8));
    if (restore_psw) {
        cpu->psw = (uint8_t)((hi & 0xF0) | (cpu->psw & 0x0F));
    }
    cpu->psw = (uint8_t)((cpu->psw & 0xF8) | sp);
}

/* ── Public API ───────────────────────────────────────────────────────── */

void i8035_init(i8035_t *cpu,
                const uint8_t *rom, uint16_t rom_size,
                void *host_ctx,
                i8035_port_read_fn  port_read,
                i8035_port_write_fn port_write,
                i8035_pin_read_fn   read_t0,
                i8035_pin_read_fn   read_t1,
                i8035_pin_read_fn   read_int,
                i8035_prog_fn       prog)
{
    memset(cpu, 0, sizeof(*cpu));
    cpu->rom        = rom;
    cpu->rom_size   = rom_size;
    cpu->host_ctx   = host_ctx;
    cpu->port_read  = port_read;
    cpu->port_write = port_write;
    cpu->read_t0    = read_t0;
    cpu->read_t1    = read_t1;
    cpu->read_int   = read_int;
    cpu->prog       = prog;
}

void i8035_reset(i8035_t *cpu)
{
    cpu->pc          = 0;
    cpu->a           = 0;
    cpu->psw         = 0x08;          /* SP=0, BS=0, F0=0, AC=CY=0, bit3=1 */
    cpu->t           = 0;
    cpu->p1_out      = 0xFF;
    cpu->p2_out      = 0xFF;
    cpu->f1          = false;
    cpu->tf          = false;
    cpu->ie          = false;
    cpu->tie         = false;
    cpu->in_irq      = false;
    cpu->timer_run   = false;
    cpu->counter_run = false;
    cpu->prescaler   = 0;
    cpu->last_t1     = false;
    cpu->mb          = false;
    memset(cpu->ram, 0, sizeof(cpu->ram));
}

int i8035_step(i8035_t *cpu)
{
    uint8_t op = fetch(cpu);
    int cycles = 1;

    /*
     * The MCS-48 encoding has lots of regularity — every "row" of the
     * 16×16 opcode table tends to be one operation with the low nibble
     * picking the operand.  Where that pattern holds we collapse the
     * cases (e.g. MOV A,Rn lives in 0xF8..0xFF).
     */
    switch (op) {

    /* ── No-op ──────────────────────────────────────────────────────── */
    case 0x00:                                  /* NOP                  */
        break;

    /* ── Unconditional jumps ────────────────────────────────────────── */
    /* JMP page-N, address in low byte of next; high 3 bits come from
     * the opcode.  All eight encodings differ only in bits [7:5]. */
    case 0x04: case 0x24: case 0x44: case 0x64:
    case 0x84: case 0xA4: case 0xC4: case 0xE4: {
        uint8_t lo = fetch(cpu);
        uint16_t hi = (uint16_t)(op & 0xE0) << 3;   /* page in bits 8..10 */
        cpu->pc = (uint16_t)((hi | lo) & 0x07FF);
        cycles = 2;
        break;
    }

    /* CALL — same encoding as JMP but pushes return PC + PSW upper
     * nibble onto the stack first. */
    case 0x14: case 0x34: case 0x54: case 0x74:
    case 0x94: case 0xB4: case 0xD4: case 0xF4: {
        uint8_t lo = fetch(cpu);
        uint16_t hi = (uint16_t)(op & 0xE0) << 3;
        stack_push(cpu);
        cpu->pc = (uint16_t)((hi | lo) & 0x07FF);
        cycles = 2;
        break;
    }

    case 0x83:                                  /* RET                  */
        stack_pop(cpu, false);
        cycles = 2;
        break;

    /* ── Conditional jumps on Acc / Carry ───────────────────────────── */
    /* All conditional branches are 2-byte instructions: opcode + an
     * 8-bit target that replaces only the low byte of PC (the high
     * bits stay where they were after the fetch — this gives a
     * page-local branch range). */
    case 0xC6: {                                /* JZ addr              */
        uint8_t lo = fetch(cpu);
        if (cpu->a == 0) cpu->pc = (uint16_t)((cpu->pc & 0xFF00) | lo);
        cycles = 2;
        break;
    }
    case 0x96: {                                /* JNZ addr             */
        uint8_t lo = fetch(cpu);
        if (cpu->a != 0) cpu->pc = (uint16_t)((cpu->pc & 0xFF00) | lo);
        cycles = 2;
        break;
    }
    case 0xF6: {                                /* JC addr              */
        uint8_t lo = fetch(cpu);
        if (cpu->psw & 0x80) cpu->pc = (uint16_t)((cpu->pc & 0xFF00) | lo);
        cycles = 2;
        break;
    }
    case 0xE6: {                                /* JNC addr             */
        uint8_t lo = fetch(cpu);
        if (!(cpu->psw & 0x80)) cpu->pc = (uint16_t)((cpu->pc & 0xFF00) | lo);
        cycles = 2;
        break;
    }

    /* ── Accumulator-only ───────────────────────────────────────────── */
    case 0x27:                                  /* CLR A                */
        cpu->a = 0;
        break;

    case 0x37:                                  /* CPL A                */
        cpu->a = (uint8_t)~cpu->a;
        break;

    case 0x47:                                  /* SWAP A — swap nibbles */
        cpu->a = (uint8_t)((cpu->a << 4) | (cpu->a >> 4));
        break;

    case 0x07:                                  /* DEC A                */
        cpu->a--;
        break;

    case 0x17:                                  /* INC A                */
        cpu->a++;
        break;

    /* ── Increment / decrement Rn and @Rn ───────────────────────────── */
    case 0x18: case 0x19: case 0x1A: case 0x1B:
    case 0x1C: case 0x1D: case 0x1E: case 0x1F: {
        int n = op & 7;                         /* INC Rn               */
        set_reg(cpu, n, (uint8_t)(get_reg(cpu, n) + 1));
        break;
    }

    case 0xC8: case 0xC9: case 0xCA: case 0xCB:
    case 0xCC: case 0xCD: case 0xCE: case 0xCF: {
        int n = op & 7;                         /* DEC Rn               */
        set_reg(cpu, n, (uint8_t)(get_reg(cpu, n) - 1));
        break;
    }

    case 0x10: case 0x11: {                     /* INC @Rn              */
        uint8_t addr = indirect_addr(cpu, op & 1);
        cpu->ram[addr]++;
        break;
    }

    /* ── MOV immediates ─────────────────────────────────────────────── */
    case 0x23: {                                /* MOV A,#data          */
        cpu->a = fetch(cpu);
        cycles = 2;
        break;
    }

    case 0xB8: case 0xB9: case 0xBA: case 0xBB:
    case 0xBC: case 0xBD: case 0xBE: case 0xBF: {
        int n = op & 7;                         /* MOV Rn,#data         */
        set_reg(cpu, n, fetch(cpu));
        cycles = 2;
        break;
    }

    case 0xB0: case 0xB1: {                     /* MOV @Rn,#data        */
        uint8_t addr = indirect_addr(cpu, op & 1);
        cpu->ram[addr] = fetch(cpu);
        cycles = 2;
        break;
    }

    /* ── MOV between A and Rn / @Rn ─────────────────────────────────── */
    case 0xF8: case 0xF9: case 0xFA: case 0xFB:
    case 0xFC: case 0xFD: case 0xFE: case 0xFF: {
        int n = op & 7;                         /* MOV A,Rn             */
        cpu->a = get_reg(cpu, n);
        break;
    }

    case 0xA8: case 0xA9: case 0xAA: case 0xAB:
    case 0xAC: case 0xAD: case 0xAE: case 0xAF: {
        int n = op & 7;                         /* MOV Rn,A             */
        set_reg(cpu, n, cpu->a);
        break;
    }

    case 0xF0: case 0xF1: {                     /* MOV A,@Rn            */
        uint8_t addr = indirect_addr(cpu, op & 1);
        cpu->a = cpu->ram[addr];
        break;
    }

    case 0xA0: case 0xA1: {                     /* MOV @Rn,A            */
        uint8_t addr = indirect_addr(cpu, op & 1);
        cpu->ram[addr] = cpu->a;
        break;
    }

    /* ── PSW / bank select ──────────────────────────────────────────── */
    case 0xC5:                                  /* SEL RB0              */
        cpu->psw &= (uint8_t)~0x10;
        break;

    case 0xD5:                                  /* SEL RB1              */
        cpu->psw |= 0x10;
        break;

    case 0xC7:                                  /* MOV A,PSW            */
        cpu->a = (uint8_t)(cpu->psw | 0x08);    /* bit 3 always reads 1 */
        break;

    case 0xD7:                                  /* MOV PSW,A            */
        cpu->psw = (uint8_t)(cpu->a | 0x08);
        break;

    /* ── Arithmetic: ADD / ADDC ─────────────────────────────────────── */
    case 0x03: {                                /* ADD A,#data          */
        add_to_a(cpu, fetch(cpu), false);
        cycles = 2;
        break;
    }

    case 0x68: case 0x69: case 0x6A: case 0x6B:
    case 0x6C: case 0x6D: case 0x6E: case 0x6F: {
        add_to_a(cpu, get_reg(cpu, op & 7), false);     /* ADD A,Rn   */
        break;
    }

    case 0x60: case 0x61: {                     /* ADD A,@Rn            */
        add_to_a(cpu, cpu->ram[indirect_addr(cpu, op & 1)], false);
        break;
    }

    case 0x13: {                                /* ADDC A,#data         */
        add_to_a(cpu, fetch(cpu), (cpu->psw & PSW_CY) != 0);
        cycles = 2;
        break;
    }

    case 0x78: case 0x79: case 0x7A: case 0x7B:
    case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
        add_to_a(cpu, get_reg(cpu, op & 7),
                 (cpu->psw & PSW_CY) != 0);              /* ADDC A,Rn  */
        break;
    }

    case 0x70: case 0x71: {                     /* ADDC A,@Rn           */
        add_to_a(cpu, cpu->ram[indirect_addr(cpu, op & 1)],
                 (cpu->psw & PSW_CY) != 0);
        break;
    }

    case 0x57: {                                /* DA A — decimal adjust */
        /* Apply low-nibble correction first, then high-nibble; either
         * step may cause a carry-out that latches into PSW.CY. */
        uint16_t a = cpu->a;
        if ((a & 0x0F) > 9 || (cpu->psw & PSW_AC))
            a += 0x06;
        if (a > 0x9F || (cpu->psw & PSW_CY)) {
            a += 0x60;
            cpu->psw |= PSW_CY;
        }
        cpu->a = (uint8_t)a;
        break;
    }

    /* ── Logic: ANL / ORL / XRL ─────────────────────────────────────── */
    case 0x53: {                                /* ANL A,#data          */
        cpu->a &= fetch(cpu);
        cycles = 2;
        break;
    }

    case 0x58: case 0x59: case 0x5A: case 0x5B:
    case 0x5C: case 0x5D: case 0x5E: case 0x5F: {
        cpu->a &= get_reg(cpu, op & 7);                  /* ANL A,Rn   */
        break;
    }

    case 0x50: case 0x51: {                     /* ANL A,@Rn            */
        cpu->a &= cpu->ram[indirect_addr(cpu, op & 1)];
        break;
    }

    case 0x43: {                                /* ORL A,#data          */
        cpu->a |= fetch(cpu);
        cycles = 2;
        break;
    }

    case 0x48: case 0x49: case 0x4A: case 0x4B:
    case 0x4C: case 0x4D: case 0x4E: case 0x4F: {
        cpu->a |= get_reg(cpu, op & 7);                  /* ORL A,Rn   */
        break;
    }

    case 0x40: case 0x41: {                     /* ORL A,@Rn            */
        cpu->a |= cpu->ram[indirect_addr(cpu, op & 1)];
        break;
    }

    case 0xD3: {                                /* XRL A,#data          */
        cpu->a ^= fetch(cpu);
        cycles = 2;
        break;
    }

    case 0xD8: case 0xD9: case 0xDA: case 0xDB:
    case 0xDC: case 0xDD: case 0xDE: case 0xDF: {
        cpu->a ^= get_reg(cpu, op & 7);                  /* XRL A,Rn   */
        break;
    }

    case 0xD0: case 0xD1: {                     /* XRL A,@Rn            */
        cpu->a ^= cpu->ram[indirect_addr(cpu, op & 1)];
        break;
    }

    /* ── Rotates ────────────────────────────────────────────────────── */
    case 0xE7:                                  /* RL A — no carry      */
        cpu->a = (uint8_t)((cpu->a << 1) | (cpu->a >> 7));
        break;

    case 0x77:                                  /* RR A — no carry      */
        cpu->a = (uint8_t)((cpu->a >> 1) | (cpu->a << 7));
        break;

    case 0xF7: {                                /* RLC A — through CY   */
        uint8_t old_cy = (cpu->psw & PSW_CY) ? 1 : 0;
        psw_assign(cpu, PSW_CY, (cpu->a & 0x80) != 0);
        cpu->a = (uint8_t)((cpu->a << 1) | old_cy);
        break;
    }

    case 0x67: {                                /* RRC A — through CY   */
        uint8_t old_cy = (cpu->psw & PSW_CY) ? 0x80 : 0;
        psw_assign(cpu, PSW_CY, (cpu->a & 0x01) != 0);
        cpu->a = (uint8_t)((cpu->a >> 1) | old_cy);
        break;
    }

    /* ── Flag manipulation: CY, F0, F1 ──────────────────────────────── */
    case 0x97:                                  /* CLR C                */
        psw_assign(cpu, PSW_CY, false);
        break;

    case 0xA7:                                  /* CPL C                */
        cpu->psw ^= PSW_CY;
        break;

    case 0x85:                                  /* CLR F0               */
        psw_assign(cpu, PSW_F0, false);
        break;

    case 0x95:                                  /* CPL F0               */
        cpu->psw ^= PSW_F0;
        break;

    case 0xA5:                                  /* CLR F1               */
        cpu->f1 = false;
        break;

    case 0xB5:                                  /* CPL F1               */
        cpu->f1 = !cpu->f1;
        break;

    case 0xB6: {                                /* JF0 addr             */
        uint8_t lo = fetch(cpu);
        if (cpu->psw & PSW_F0)
            cpu->pc = (uint16_t)((cpu->pc & 0xFF00) | lo);
        cycles = 2;
        break;
    }

    case 0x76: {                                /* JF1 addr             */
        uint8_t lo = fetch(cpu);
        if (cpu->f1)
            cpu->pc = (uint16_t)((cpu->pc & 0xFF00) | lo);
        cycles = 2;
        break;
    }

    /* ── Port IO: IN / OUTL / ANL / ORL on BUS, P1, P2 ──────────────── */
    case 0x08:                                  /* INS A,BUS            */
        cpu->a = port_read(cpu, I8035_PORT_BUS);
        cycles = 2;
        break;

    case 0x09:                                  /* IN A,P1              */
        cpu->a = port_read(cpu, I8035_PORT_P1);
        cycles = 2;
        break;

    case 0x0A:                                  /* IN A,P2              */
        cpu->a = port_read(cpu, I8035_PORT_P2);
        cycles = 2;
        break;

    case 0x02:                                  /* OUTL BUS,A           */
        port_write(cpu, I8035_PORT_BUS, cpu->a);
        cycles = 2;
        break;

    case 0x39:                                  /* OUTL P1,A            */
        port_write(cpu, I8035_PORT_P1, cpu->a);
        cycles = 2;
        break;

    case 0x3A:                                  /* OUTL P2,A            */
        port_write(cpu, I8035_PORT_P2, cpu->a);
        cycles = 2;
        break;

    case 0x98: {                                /* ANL BUS,#data        */
        uint8_t imm = fetch(cpu);
        port_write(cpu, I8035_PORT_BUS, (uint8_t)(0xFF & imm));
        cycles = 2;
        break;
    }

    case 0x99: {                                /* ANL P1,#data         */
        cpu->p1_out &= fetch(cpu);
        port_write(cpu, I8035_PORT_P1, cpu->p1_out);
        cycles = 2;
        break;
    }

    case 0x9A: {                                /* ANL P2,#data         */
        cpu->p2_out &= fetch(cpu);
        port_write(cpu, I8035_PORT_P2, cpu->p2_out);
        cycles = 2;
        break;
    }

    case 0x88: {                                /* ORL BUS,#data        */
        uint8_t imm = fetch(cpu);
        port_write(cpu, I8035_PORT_BUS, imm);
        cycles = 2;
        break;
    }

    case 0x89: {                                /* ORL P1,#data         */
        cpu->p1_out |= fetch(cpu);
        port_write(cpu, I8035_PORT_P1, cpu->p1_out);
        cycles = 2;
        break;
    }

    case 0x8A: {                                /* ORL P2,#data         */
        cpu->p2_out |= fetch(cpu);
        port_write(cpu, I8035_PORT_P2, cpu->p2_out);
        cycles = 2;
        break;
    }

    /* ── 8243 expander: MOVD / ANLD / ORLD ──────────────────────────── */
    case 0x0C: case 0x0D: case 0x0E: case 0x0F: {
        expander_op(cpu, 0x0, op & 3, true);    /* MOVD A,Pp (READ)     */
        cycles = 2;
        break;
    }

    case 0x3C: case 0x3D: case 0x3E: case 0x3F: {
        expander_op(cpu, 0x1, op & 3, false);   /* MOVD Pp,A (WRITE)    */
        cycles = 2;
        break;
    }

    case 0x8C: case 0x8D: case 0x8E: case 0x8F: {
        expander_op(cpu, 0x2, op & 3, false);   /* ORLD Pp,A            */
        cycles = 2;
        break;
    }

    case 0x9C: case 0x9D: case 0x9E: case 0x9F: {
        expander_op(cpu, 0x3, op & 3, false);   /* ANLD Pp,A            */
        cycles = 2;
        break;
    }

    /* ── Unimplemented in this commit — see file header comment. ────── */
    default:
        assert(!"i8035: unimplemented opcode");
        break;
    }

    return cycles;
}
