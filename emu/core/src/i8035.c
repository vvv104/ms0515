/*
 * i8035.c — Intel MCS-48 (8035 / 8048 family) CPU core implementation.
 *
 * See i8035.h for the architectural model.  This file implements the
 * instruction set directly from the Intel MCS-48 Programmer's Manual
 * (1976), one opcode (or one tightly-related group of opcodes) per
 * static helper, dispatched through a single 256-entry switch.
 *
 * Coverage so far (commit 1 of phase 1):
 *   - control flow:  JMP page0..7, CALL page0..7, RET, conditional
 *                    branches on Acc / carry (JZ / JNZ / JC / JNC)
 *   - data movement: MOV A,#imm; MOV Rn,#imm; MOV @Rn,#imm;
 *                    MOV A,Rn; MOV Rn,A; MOV A,@Rn; MOV @Rn,A
 *   - simple ops:    NOP, CLR A, CPL A, SWAP A,
 *                    INC A, DEC A, INC Rn, DEC Rn, INC @Rn
 *   - bank / PSW:    SEL RB0, SEL RB1, MOV A,PSW, MOV PSW,A
 *
 * Coverage to follow in subsequent commits:
 *   - arithmetic (ADD/ADDC/DA), logic (AND/OR/XOR/rotates),
 *   - port IO (IN/OUTL/ANL/ORL Pn, MOVD/ANLD/ORLD to expander),
 *   - timer / counter (STRT/STOP/MOV T,A, JTF),
 *   - interrupts (EN/DIS I, EN/DIS TCNTI, RETR, vector dispatch),
 *   - external memory (MOVX), MOVP / MOVP3, JMPP, DJNZ.
 *
 * Unimplemented opcodes trap via assert() in debug, and behave as NOP
 * in release — this surfaces firmware paths that exercise something
 * we don't model yet, without crashing the emulator outright.
 */

#include <ms0515/i8035.h>

#include <assert.h>
#include <string.h>

/* ── Internal helpers ─────────────────────────────────────────────────── */

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

    /* ── Unimplemented in this commit — see file header comment. ────── */
    default:
        assert(!"i8035: unimplemented opcode");
        break;
    }

    return cycles;
}
