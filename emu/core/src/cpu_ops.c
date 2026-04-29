/*
 * cpu_ops.c — PDP-11 instruction implementations for KR1807VM1
 *
 * This file contains:
 *   1. Addressing mode helpers (get effective address, read/write operands)
 *   2. All 66 instruction handlers
 *   3. Dispatch table initialization
 *
 * Instruction encoding (PDP-11):
 *
 *   Double-operand:  [opcode:4][src_mode:3][src_reg:3][dst_mode:3][dst_reg:3]
 *   Single-operand:  [opcode:10][mode:3][reg:3]
 *   Branch:          [opcode:8][offset:8]
 *   Register:        [opcode:10][reg:3] (RTS, etc.)
 *   JSR/XOR:         [opcode:7][reg:3][mode:3][reg:3]
 *   SOB:             [opcode:7][reg:3][offset:6]
 *
 * Byte instructions have bit 15 set and append 'B' to the mnemonic.
 * ADD and SUB exist only in word form.
 * MOVB to a register sign-extends the result to 16 bits.
 *
 * Sources:
 *   - PDP-11 Architecture Handbook (DEC, EB-23657-18)
 *   - PDP-11 instruction reference (University of Toronto CSC 258)
 *   - T-11 User's Guide (EK-DCT11-UG)
 */

#include <ms0515/cpu.h>
#include <ms0515/board.h>
#include <string.h>

/* ── Handler type (same as in cpu.c) ──────────────────────────────────────── */

typedef void (*cpu_op_handler_t)(ms0515_cpu_t *cpu);

/* ── PSW flag helpers ─────────────────────────────────────────────────────── */

static inline void set_nz_word(ms0515_cpu_t *cpu, uint16_t val)
{
    if (val == 0)
        cpu->psw |= CPU_PSW_Z;
    else
        cpu->psw &= ~CPU_PSW_Z;

    if (val & 0x8000)
        cpu->psw |= CPU_PSW_N;
    else
        cpu->psw &= ~CPU_PSW_N;
}

static inline void set_nz_byte(ms0515_cpu_t *cpu, uint8_t val)
{
    if (val == 0)
        cpu->psw |= CPU_PSW_Z;
    else
        cpu->psw &= ~CPU_PSW_Z;

    if (val & 0x80)
        cpu->psw |= CPU_PSW_N;
    else
        cpu->psw &= ~CPU_PSW_N;
}

static inline void set_c(ms0515_cpu_t *cpu, bool c)
{
    if (c) cpu->psw |= CPU_PSW_C; else cpu->psw &= ~CPU_PSW_C;
}

static inline void set_v(ms0515_cpu_t *cpu, bool v)
{
    if (v) cpu->psw |= CPU_PSW_V; else cpu->psw &= ~CPU_PSW_V;
}

static inline bool get_c(const ms0515_cpu_t *cpu)
{
    return (cpu->psw & CPU_PSW_C) != 0;
}

/* ── Addressing mode helpers ──────────────────────────────────────────────── */

/*
 * Compute the effective address for a word-sized operand.
 *
 * `mode` = 3-bit addressing mode (0–7)
 * `reg`  = 3-bit register number (0–7)
 *
 * Returns the memory address where the operand resides.
 * For mode 0 (register direct), returns a sentinel — caller must
 * handle register access separately.
 */
#define ADDR_REGISTER  0xFFFF   /* Sentinel: operand is in a register */

/*
 * Cycle accounting calibrated against MAME's T11 / K1801VM1 core
 * (src/devices/cpu/t11/t11ops.hxx in mamedev/mame).  The K1801VM1
 * splits each instruction into:
 *
 *   fetch+decode  +  per-mode source cost  +  per-mode dest cost
 *
 * which we model by accumulating cycles across get_word_addr,
 * read_word_op, write_word_op, and the discard read added for
 * pure-write opcodes.  The four constants below combine to give
 * exact agreement with MAME for every (src_mode, dst_mode) pair on
 * MOV (and approximate agreement for the rest):
 *
 *   - BUS_CYCLE       6 cycles per memory access (read or write)
 *   - REG_WRITE_CYCLE 3 cycles for storing into a register (mode 0
 *                       destination — matches MAME's kDstAdd[0]=3)
 *   - AUTODEC_CYCLE   3 cycles for the pre-decrement step in
 *                       modes 4 (autodec) and 5 (autodec deferred)
 *   - INDEX_CYCLE     3 cycles for the index-word arithmetic in
 *                       modes 6 (indexed) and 7 (indexed deferred)
 *
 * Initial instruction fetch is 9 cycles (set in cpu.c).
 *
 * Verified against MAME: MOV R,R = 12; MOV @R,@R = 27; MOV X(R),X(R) =
 * 45 — same as MAME's t11_device::mov_*_*() expressions.  Earlier
 * model (fetch=4, BUS=4, no register-write or autodec/index extras)
 * ran roughly 2× faster than the K1801VM1 spec.
 */
#define BUS_CYCLE        6
#define REG_WRITE_CYCLE  3
#define AUTODEC_CYCLE    3
#define INDEX_CYCLE      3

static uint16_t get_word_addr(ms0515_cpu_t *cpu, int mode, int reg)
{
    uint16_t addr;

    switch (mode) {
    case 0:  /* Register */
        return ADDR_REGISTER;

    case 1:  /* Register deferred: (Rn) */
        return cpu->r[reg];

    case 2:  /* Autoincrement: (Rn)+ */
        addr = cpu->r[reg];
        cpu->r[reg] += 2;
        return addr;

    case 3:  /* Autoincrement deferred: @(Rn)+ */
        addr = cpu->r[reg];
        cpu->r[reg] += 2;
        cpu->cycles += BUS_CYCLE;
        return board_read_word(cpu->board, addr);

    case 4:  /* Autodecrement: -(Rn) */
        cpu->cycles += AUTODEC_CYCLE;
        cpu->r[reg] -= 2;
        return cpu->r[reg];

    case 5:  /* Autodecrement deferred: @-(Rn) */
        cpu->cycles += AUTODEC_CYCLE + BUS_CYCLE;
        cpu->r[reg] -= 2;
        return board_read_word(cpu->board, cpu->r[reg]);

    case 6:  /* Index: X(Rn) */
        cpu->cycles += INDEX_CYCLE + BUS_CYCLE;
        addr = board_read_word(cpu->board, cpu->r[CPU_REG_PC]);
        cpu->r[CPU_REG_PC] += 2;
        return (uint16_t)(addr + cpu->r[reg]);

    case 7:  /* Index deferred: @X(Rn) */
        cpu->cycles += INDEX_CYCLE + 2 * BUS_CYCLE;
        addr = board_read_word(cpu->board, cpu->r[CPU_REG_PC]);
        cpu->r[CPU_REG_PC] += 2;
        return board_read_word(cpu->board, (uint16_t)(addr + cpu->r[reg]));

    default:
        return 0;
    }
}

/*
 * Compute the effective address for a byte-sized operand.
 *
 * Same as word, except autoincrement/decrement steps by 1 for R0–R5
 * and by 2 for R6 (SP) and R7 (PC) to maintain word alignment.
 */
static uint16_t get_byte_addr(ms0515_cpu_t *cpu, int mode, int reg)
{
    uint16_t addr;
    uint16_t step = (reg >= 6) ? 2 : 1;

    switch (mode) {
    case 0:
        return ADDR_REGISTER;

    case 1:
        return cpu->r[reg];

    case 2:
        addr = cpu->r[reg];
        cpu->r[reg] += step;
        return addr;

    case 3:
        addr = cpu->r[reg];
        cpu->r[reg] += 2;
        cpu->cycles += BUS_CYCLE;
        return board_read_word(cpu->board, addr);

    case 4:
        cpu->cycles += AUTODEC_CYCLE;
        cpu->r[reg] -= step;
        return cpu->r[reg];

    case 5:
        cpu->cycles += AUTODEC_CYCLE + BUS_CYCLE;
        cpu->r[reg] -= 2;
        return board_read_word(cpu->board, cpu->r[reg]);

    case 6:
        cpu->cycles += INDEX_CYCLE + BUS_CYCLE;
        addr = board_read_word(cpu->board, cpu->r[CPU_REG_PC]);
        cpu->r[CPU_REG_PC] += 2;
        return (uint16_t)(addr + cpu->r[reg]);

    case 7:
        cpu->cycles += INDEX_CYCLE + 2 * BUS_CYCLE;
        addr = board_read_word(cpu->board, cpu->r[CPU_REG_PC]);
        cpu->r[CPU_REG_PC] += 2;
        return board_read_word(cpu->board, (uint16_t)(addr + cpu->r[reg]));

    default:
        return 0;
    }
}

/* Read/write word operand — each memory access costs one bus cycle */
static uint16_t read_word_op(ms0515_cpu_t *cpu, int mode, int reg, uint16_t addr)
{
    if (mode == 0)
        return cpu->r[reg];
    cpu->cycles += BUS_CYCLE;
    return board_read_word(cpu->board, addr);
}

static void write_word_op(ms0515_cpu_t *cpu, int mode, int reg,
                           uint16_t addr, uint16_t val)
{
    if (mode == 0) {
        cpu->cycles += REG_WRITE_CYCLE;
        cpu->r[reg] = val;
    }
    else {
        cpu->cycles += BUS_CYCLE;
        board_write_word(cpu->board, addr, val);
    }
}

/* Read/write byte operand */
static uint8_t read_byte_op(ms0515_cpu_t *cpu, int mode, int reg, uint16_t addr)
{
    if (mode == 0)
        return (uint8_t)(cpu->r[reg] & 0xFF);
    cpu->cycles += BUS_CYCLE;
    return board_read_byte(cpu->board, addr);
}

static void write_byte_op(ms0515_cpu_t *cpu, int mode, int reg,
                           uint16_t addr, uint8_t val)
{
    if (mode == 0) {
        cpu->cycles += REG_WRITE_CYCLE;
        cpu->r[reg] = (cpu->r[reg] & 0xFF00) | val;
    } else {
        cpu->cycles += BUS_CYCLE;
        board_write_byte(cpu->board, addr, val);
    }
}

/*
 * Pure-write instructions (CLR, MOV, SXT, MFPS-to-memory) on the
 * K1801VM1 still go through a read-then-write bus cycle — the chip
 * has no stand-alone write pin.  The fetched value is discarded but
 * the read side-effect is real, which matters for memory-mapped I/O
 * registers that auto-clear or pop a FIFO on read.  These helpers
 * issue the implicit discard read; register-direct writes (mode 0)
 * stay clear of the bus and are skipped.
 *
 * The only writes that genuinely don't read first are stack pushes
 * (interrupt entry, JSR, MARK, PSW save).  Those go through `push`,
 * not write_word_op, so they remain pure writes — correct.
 */
static void discard_read_word(ms0515_cpu_t *cpu, int mode, uint16_t addr)
{
    if (mode == 0) return;
    cpu->cycles += BUS_CYCLE;
    (void)board_read_word(cpu->board, addr);
}

static void discard_read_byte(ms0515_cpu_t *cpu, int mode, uint16_t addr)
{
    if (mode == 0) return;
    cpu->cycles += BUS_CYCLE;
    (void)board_read_byte(cpu->board, addr);
}

/* ── Opcode field extraction macros ───────────────────────────────────────── */

#define DST_MODE(op)   (((op) >> 3) & 7)
#define DST_REG(op)    ((op) & 7)
#define SRC_MODE(op)   (((op) >> 9) & 7)
#define SRC_REG(op)    (((op) >> 6) & 7)
#define BRANCH_OFF(op) ((int8_t)((op) & 0xFF))

/* ── Stack helpers (accessible from ops) ──────────────────────────────────── */

static void push(ms0515_cpu_t *cpu, uint16_t val)
{
    cpu->r[CPU_REG_SP] -= 2;
    cpu->cycles += BUS_CYCLE;
    board_write_word(cpu->board, cpu->r[CPU_REG_SP], val);
}

static uint16_t pop(ms0515_cpu_t *cpu)
{
    cpu->cycles += BUS_CYCLE;
    uint16_t val = board_read_word(cpu->board, cpu->r[CPU_REG_SP]);
    cpu->r[CPU_REG_SP] += 2;
    return val;
}

/* ══════════════════════════════════════════════════════════════════════════ */
/*  INSTRUCTION IMPLEMENTATIONS                                              */
/* ══════════════════════════════════════════════════════════════════════════ */

/* ── Unknown / illegal instruction ────────────────────────────────────────── */

static void op_unknown(ms0515_cpu_t *cpu)
{
    cpu->irq_reserved = true;
}

/* ── No operation ─────────────────────────────────────────────────────────── */

static void op_nop(ms0515_cpu_t *cpu)
{
    (void)cpu;
}

/* ── HALT ─────────────────────────────────────────────────────────────────── */

/*
 * On the K1801VM1 (and the related 1807VM1 / DEC T-11) the HALT
 * instruction is not a "stop the CPU" — it traps to the restart
 * vector at 0172004 with PSW=0340, exactly like the external HALT
 * signal handled by cpu_check_interrupts.  Code that uses HALT to
 * drop into the monitor (or to re-enter the boot loader) relies on
 * this; halting the CPU outright wedged any program that did so.
 */
static void op_halt(ms0515_cpu_t *cpu)
{
    BOARD_EVT(cpu->board, MS0515_EVT_HALT, NULL, 0);
    push(cpu, cpu->psw);
    push(cpu, cpu->r[CPU_REG_PC]);
    cpu->r[CPU_REG_PC] = 0172004;
    cpu->psw = 0340;
    cpu->waiting = false;
}

/* ── WAIT ─────────────────────────────────────────────────────────────────── */

static void op_wait(ms0515_cpu_t *cpu)
{
    cpu->waiting = true;
}

/* ── RESET ────────────────────────────────────────────────────────────────── */

static void op_reset(ms0515_cpu_t *cpu)
{
    /* RESET re-initializes all peripheral devices but does NOT affect
     * the CPU registers or PSW.  Per PDP-11 Architecture Handbook. */
    /* board_reset_devices() would be called here through the board pointer.
     * For now, this is a placeholder — actual device reset is handled
     * at the board level when it detects a RESET instruction. */
    (void)cpu;
}

/* ── RTI — Return from Interrupt ──────────────────────────────────────────── */

/*
 * On the KR1807VM1 (DEC T-11 / PDP-11/03 family), RTI and RTT are
 * identical: both pop PC and PSW from the stack and inhibit the T-bit
 * trap for one instruction.  This differs from later PDP-11 models
 * (PDP-11/34, /40, /44, /70) where RTI checks the T-bit immediately.
 *
 * The MS0515 uses the KR1807VM1, so RTI must inhibit the T-trap.
 * This matters when code accidentally executes RTI (e.g. JSR to an
 * address where the word 000002 is data, not an instruction) — the
 * restored PSW may have T=1, and without inhibition, the emulator
 * would fire an unexpected BPT trap that crashes the guest.
 */
static void op_rti(ms0515_cpu_t *cpu)
{
    cpu->r[CPU_REG_PC] = pop(cpu);
    cpu->psw            = pop(cpu);
    /* KR1807VM1: RTI inhibits T-bit trap, same as RTT */
    cpu->irq_tbit = false;
}

/* ── RTT — Return from Trap ──────────────────────────────────────────────── */

/*
 * On the KR1807VM1, RTT is identical to RTI — both inhibit the T-bit
 * trap for one instruction after restoring PSW.
 */
static void op_rtt(ms0515_cpu_t *cpu)
{
    cpu->r[CPU_REG_PC] = pop(cpu);
    cpu->psw            = pop(cpu);
    /* RTT inhibits the T-bit trap for the next instruction */
    cpu->irq_tbit = false;
}

/* ── BPT — Breakpoint Trap ────────────────────────────────────────────────── */

static void op_bpt(ms0515_cpu_t *cpu)
{
    cpu->irq_bpt = true;
}

/* ── IOT — I/O Trap ───────────────────────────────────────────────────────── */

static void op_iot(ms0515_cpu_t *cpu)
{
    cpu->irq_iot = true;
}

/* ── EMT — Emulator Trap ──────────────────────────────────────────────────── */

static void op_emt(ms0515_cpu_t *cpu)
{
    cpu->irq_emt = true;
}

/* ── TRAP ─────────────────────────────────────────────────────────────────── */

static void op_trap(ms0515_cpu_t *cpu)
{
    cpu->irq_trap = true;
}

/* ── MFPT — Move From Processor Type ─────────────────────────────────────── */
/*
 * Move From Processor Type: writes a one-byte CPU identifier into R0.
 * Different PDP-11 family chips return different values; the K1801VM1
 * (and the related 1807VM1 and DEC T-11) returns 4.
 *
 * An earlier comment in this slot claimed Omega's boot loader needed a
 * reserved-instruction trap on MFPT — that turned out to be wrong: the
 * unpatched ROM-A + Omega config (the pink-screen known-bad in
 * test_boot.cpp) hung specifically because we trapped instead of
 * returning the spec value.
 */
static void op_mfpt(ms0515_cpu_t *cpu)
{
    cpu->r[0] = 4;
}

/* ── CLR / CLRB ───────────────────────────────────────────────────────────── */

static void op_clr(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_word_addr(cpu, mode, reg);
    discard_read_word(cpu, mode, addr);     /* K1801VM1: read-then-write */
    write_word_op(cpu, mode, reg, addr, 0);
    cpu->psw = (cpu->psw & ~(CPU_PSW_N | CPU_PSW_V | CPU_PSW_C)) | CPU_PSW_Z;
}

static void op_clrb(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_byte_addr(cpu, mode, reg);
    discard_read_byte(cpu, mode, addr);     /* K1801VM1: read-then-write */
    write_byte_op(cpu, mode, reg, addr, 0);
    cpu->psw = (cpu->psw & ~(CPU_PSW_N | CPU_PSW_V | CPU_PSW_C)) | CPU_PSW_Z;
}

/* ── COM / COMB — Complement ──────────────────────────────────────────────── */

static void op_com(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_word_addr(cpu, mode, reg);
    uint16_t val  = ~read_word_op(cpu, mode, reg, addr);
    write_word_op(cpu, mode, reg, addr, val);
    set_nz_word(cpu, val);
    set_v(cpu, false);
    set_c(cpu, true);
}

static void op_comb(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_byte_addr(cpu, mode, reg);
    uint8_t val   = ~read_byte_op(cpu, mode, reg, addr);
    write_byte_op(cpu, mode, reg, addr, val);
    set_nz_byte(cpu, val);
    set_v(cpu, false);
    set_c(cpu, true);
}

/* ── INC / INCB ───────────────────────────────────────────────────────────── */

static void op_inc(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_word_addr(cpu, mode, reg);
    uint16_t val  = read_word_op(cpu, mode, reg, addr);
    set_v(cpu, val == 077777);  /* 0x7FFF → overflow */
    val++;
    write_word_op(cpu, mode, reg, addr, val);
    set_nz_word(cpu, val);
    /* C not affected */
}

static void op_incb(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_byte_addr(cpu, mode, reg);
    uint8_t val   = read_byte_op(cpu, mode, reg, addr);
    set_v(cpu, val == 0177);  /* 0x7F → overflow */
    val++;
    write_byte_op(cpu, mode, reg, addr, val);
    set_nz_byte(cpu, val);
}

/* ── DEC / DECB ───────────────────────────────────────────────────────────── */

static void op_dec(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_word_addr(cpu, mode, reg);
    uint16_t val  = read_word_op(cpu, mode, reg, addr);
    set_v(cpu, val == 0100000);  /* 0x8000 → overflow */
    val--;
    write_word_op(cpu, mode, reg, addr, val);
    set_nz_word(cpu, val);
}

static void op_decb(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_byte_addr(cpu, mode, reg);
    uint8_t val   = read_byte_op(cpu, mode, reg, addr);
    set_v(cpu, val == 0200);  /* 0x80 → overflow */
    val--;
    write_byte_op(cpu, mode, reg, addr, val);
    set_nz_byte(cpu, val);
}

/* ── NEG / NEGB ───────────────────────────────────────────────────────────── */

static void op_neg(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_word_addr(cpu, mode, reg);
    uint16_t val  = read_word_op(cpu, mode, reg, addr);
    uint16_t result = (uint16_t)(-(int16_t)val);
    write_word_op(cpu, mode, reg, addr, result);
    set_nz_word(cpu, result);
    set_v(cpu, result == 0100000);
    set_c(cpu, result != 0);
}

static void op_negb(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_byte_addr(cpu, mode, reg);
    uint8_t val   = read_byte_op(cpu, mode, reg, addr);
    uint8_t result = (uint8_t)(-(int8_t)val);
    write_byte_op(cpu, mode, reg, addr, result);
    set_nz_byte(cpu, result);
    set_v(cpu, result == 0200);
    set_c(cpu, result != 0);
}

/* ── TST / TSTB ───────────────────────────────────────────────────────────── */

static void op_tst(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_word_addr(cpu, mode, reg);
    uint16_t val  = read_word_op(cpu, mode, reg, addr);
    set_nz_word(cpu, val);
    set_v(cpu, false);
    set_c(cpu, false);
}

static void op_tstb(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_byte_addr(cpu, mode, reg);
    uint8_t val   = read_byte_op(cpu, mode, reg, addr);
    set_nz_byte(cpu, val);
    set_v(cpu, false);
    set_c(cpu, false);
}

/* ── ASR / ASRB — Arithmetic Shift Right ──────────────────────────────────── */

static void op_asr(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_word_addr(cpu, mode, reg);
    uint16_t val  = read_word_op(cpu, mode, reg, addr);
    set_c(cpu, val & 1);
    val = (uint16_t)((int16_t)val >> 1);
    write_word_op(cpu, mode, reg, addr, val);
    set_nz_word(cpu, val);
    set_v(cpu, ((cpu->psw & CPU_PSW_N) != 0) ^ ((cpu->psw & CPU_PSW_C) != 0));
}

static void op_asrb(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_byte_addr(cpu, mode, reg);
    uint8_t val   = read_byte_op(cpu, mode, reg, addr);
    set_c(cpu, val & 1);
    val = (uint8_t)((int8_t)val >> 1);
    write_byte_op(cpu, mode, reg, addr, val);
    set_nz_byte(cpu, val);
    set_v(cpu, ((cpu->psw & CPU_PSW_N) != 0) ^ ((cpu->psw & CPU_PSW_C) != 0));
}

/* ── ASL / ASLB — Arithmetic Shift Left ───────────────────────────────────── */

static void op_asl(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_word_addr(cpu, mode, reg);
    uint16_t val  = read_word_op(cpu, mode, reg, addr);
    set_c(cpu, (val & 0x8000) != 0);
    val <<= 1;
    write_word_op(cpu, mode, reg, addr, val);
    set_nz_word(cpu, val);
    set_v(cpu, ((cpu->psw & CPU_PSW_N) != 0) ^ ((cpu->psw & CPU_PSW_C) != 0));
}

static void op_aslb(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_byte_addr(cpu, mode, reg);
    uint8_t val   = read_byte_op(cpu, mode, reg, addr);
    set_c(cpu, (val & 0x80) != 0);
    val <<= 1;
    write_byte_op(cpu, mode, reg, addr, val);
    set_nz_byte(cpu, val);
    set_v(cpu, ((cpu->psw & CPU_PSW_N) != 0) ^ ((cpu->psw & CPU_PSW_C) != 0));
}

/* ── ROR / RORB — Rotate Right ────────────────────────────────────────────── */

static void op_ror(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_word_addr(cpu, mode, reg);
    uint16_t val  = read_word_op(cpu, mode, reg, addr);
    bool old_c = get_c(cpu);
    set_c(cpu, val & 1);
    val = (val >> 1) | (old_c ? 0x8000 : 0);
    write_word_op(cpu, mode, reg, addr, val);
    set_nz_word(cpu, val);
    set_v(cpu, ((cpu->psw & CPU_PSW_N) != 0) ^ ((cpu->psw & CPU_PSW_C) != 0));
}

static void op_rorb(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_byte_addr(cpu, mode, reg);
    uint8_t val   = read_byte_op(cpu, mode, reg, addr);
    bool old_c = get_c(cpu);
    set_c(cpu, val & 1);
    val = (val >> 1) | (old_c ? 0x80 : 0);
    write_byte_op(cpu, mode, reg, addr, val);
    set_nz_byte(cpu, val);
    set_v(cpu, ((cpu->psw & CPU_PSW_N) != 0) ^ ((cpu->psw & CPU_PSW_C) != 0));
}

/* ── ROL / ROLB — Rotate Left ─────────────────────────────────────────────── */

static void op_rol(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_word_addr(cpu, mode, reg);
    uint16_t val  = read_word_op(cpu, mode, reg, addr);
    bool old_c = get_c(cpu);
    set_c(cpu, (val & 0x8000) != 0);
    val = (val << 1) | (old_c ? 1 : 0);
    write_word_op(cpu, mode, reg, addr, val);
    set_nz_word(cpu, val);
    set_v(cpu, ((cpu->psw & CPU_PSW_N) != 0) ^ ((cpu->psw & CPU_PSW_C) != 0));
}

static void op_rolb(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_byte_addr(cpu, mode, reg);
    uint8_t val   = read_byte_op(cpu, mode, reg, addr);
    bool old_c = get_c(cpu);
    set_c(cpu, (val & 0x80) != 0);
    val = (val << 1) | (old_c ? 1 : 0);
    write_byte_op(cpu, mode, reg, addr, val);
    set_nz_byte(cpu, val);
    set_v(cpu, ((cpu->psw & CPU_PSW_N) != 0) ^ ((cpu->psw & CPU_PSW_C) != 0));
}

/* ── ADC / ADCB — Add Carry ───────────────────────────────────────────────── */

static void op_adc(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_word_addr(cpu, mode, reg);
    uint16_t val  = read_word_op(cpu, mode, reg, addr);
    bool c = get_c(cpu);
    set_v(cpu, c && val == 077777);
    set_c(cpu, c && val == 0177777);
    if (c) val++;
    write_word_op(cpu, mode, reg, addr, val);
    set_nz_word(cpu, val);
}

static void op_adcb(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_byte_addr(cpu, mode, reg);
    uint8_t val   = read_byte_op(cpu, mode, reg, addr);
    bool c = get_c(cpu);
    set_v(cpu, c && val == 0177);
    set_c(cpu, c && val == 0377);
    if (c) val++;
    write_byte_op(cpu, mode, reg, addr, val);
    set_nz_byte(cpu, val);
}

/* ── SBC / SBCB — Subtract Carry ──────────────────────────────────────────── */

static void op_sbc(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_word_addr(cpu, mode, reg);
    uint16_t val  = read_word_op(cpu, mode, reg, addr);
    bool c = get_c(cpu);
    set_v(cpu, val == 0100000);
    set_c(cpu, c && val == 0);
    if (c) val--;
    write_word_op(cpu, mode, reg, addr, val);
    set_nz_word(cpu, val);
}

static void op_sbcb(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_byte_addr(cpu, mode, reg);
    uint8_t val   = read_byte_op(cpu, mode, reg, addr);
    bool c = get_c(cpu);
    set_v(cpu, val == 0200);
    set_c(cpu, c && val == 0);
    if (c) val--;
    write_byte_op(cpu, mode, reg, addr, val);
    set_nz_byte(cpu, val);
}

/* ── SXT — Sign Extend ────────────────────────────────────────────────────── */

static void op_sxt(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_word_addr(cpu, mode, reg);
    uint16_t val  = (cpu->psw & CPU_PSW_N) ? 0xFFFF : 0;
    discard_read_word(cpu, mode, addr);     /* K1801VM1: read-then-write */
    write_word_op(cpu, mode, reg, addr, val);
    set_nz_word(cpu, val);
    set_v(cpu, false);
}

/* ── SWAB — Swap Bytes ────────────────────────────────────────────────────── */

static void op_swab(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_word_addr(cpu, mode, reg);
    uint16_t val  = read_word_op(cpu, mode, reg, addr);
    val = (val >> 8) | (val << 8);
    write_word_op(cpu, mode, reg, addr, val);
    set_nz_byte(cpu, (uint8_t)(val & 0xFF));  /* Flags set on low byte */
    set_v(cpu, false);
    set_c(cpu, false);
}

/* ── MTPS — Move To PSW (byte) ────────────────────────────────────────────── */

static void op_mtps(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint16_t addr = get_byte_addr(cpu, mode, reg);
    uint8_t val   = read_byte_op(cpu, mode, reg, addr);
    /* On T-11, MTPS can only change the low byte of PSW */
    cpu->psw = (cpu->psw & 0xFF00) | val;
}

/* ── MFPS — Move From PSW (byte) ──────────────────────────────────────────── */

static void op_mfps(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);
    uint8_t val = (uint8_t)(cpu->psw & 0xFF);

    if (mode == 0) {
        /* To register: sign-extend */
        cpu->r[reg] = (val & 0x80) ? (0xFF00 | val) : val;
    } else {
        uint16_t addr = get_byte_addr(cpu, mode, reg);
        discard_read_byte(cpu, mode, addr); /* K1801VM1: read-then-write */
        write_byte_op(cpu, mode, reg, addr, val);
    }
    set_nz_byte(cpu, val);
    set_v(cpu, false);
}

/* ── MOV / MOVB ───────────────────────────────────────────────────────────── */

static void op_mov(ms0515_cpu_t *cpu)
{
    int sm = SRC_MODE(cpu->instruction), sr = SRC_REG(cpu->instruction);
    int dm = DST_MODE(cpu->instruction), dr = DST_REG(cpu->instruction);

    uint16_t saddr = get_word_addr(cpu, sm, sr);
    uint16_t val   = read_word_op(cpu, sm, sr, saddr);

    uint16_t daddr = get_word_addr(cpu, dm, dr);
    discard_read_word(cpu, dm, daddr);  /* K1801VM1: read-then-write */
    write_word_op(cpu, dm, dr, daddr, val);

    set_nz_word(cpu, val);
    set_v(cpu, false);
}

static void op_movb(ms0515_cpu_t *cpu)
{
    int sm = SRC_MODE(cpu->instruction), sr = SRC_REG(cpu->instruction);
    int dm = DST_MODE(cpu->instruction), dr = DST_REG(cpu->instruction);

    uint16_t saddr = get_byte_addr(cpu, sm, sr);
    uint8_t val    = read_byte_op(cpu, sm, sr, saddr);

    if (dm == 0) {
        /* MOVB to register: sign-extend to 16 bits */
        cpu->r[dr] = (val & 0x80) ? (0xFF00 | val) : val;
    } else {
        uint16_t daddr = get_byte_addr(cpu, dm, dr);
        discard_read_byte(cpu, dm, daddr);  /* K1801VM1: read-then-write */
        write_byte_op(cpu, dm, dr, daddr, val);
    }

    set_nz_byte(cpu, val);
    set_v(cpu, false);
}

/* ── CMP / CMPB ───────────────────────────────────────────────────────────── */

static void op_cmp(ms0515_cpu_t *cpu)
{
    int sm = SRC_MODE(cpu->instruction), sr = SRC_REG(cpu->instruction);
    int dm = DST_MODE(cpu->instruction), dr = DST_REG(cpu->instruction);

    uint16_t saddr = get_word_addr(cpu, sm, sr);
    uint16_t s     = read_word_op(cpu, sm, sr, saddr);
    uint16_t daddr = get_word_addr(cpu, dm, dr);
    uint16_t d     = read_word_op(cpu, dm, dr, daddr);

    uint16_t result = s - d;
    set_nz_word(cpu, result);
    /* V: overflow if signs differ and result sign matches dst */
    set_v(cpu, ((s ^ d) & (s ^ result) & 0x8000) != 0);
    set_c(cpu, s < d);
}

static void op_cmpb(ms0515_cpu_t *cpu)
{
    int sm = SRC_MODE(cpu->instruction), sr = SRC_REG(cpu->instruction);
    int dm = DST_MODE(cpu->instruction), dr = DST_REG(cpu->instruction);

    uint16_t saddr = get_byte_addr(cpu, sm, sr);
    uint8_t s      = read_byte_op(cpu, sm, sr, saddr);
    uint16_t daddr = get_byte_addr(cpu, dm, dr);
    uint8_t d      = read_byte_op(cpu, dm, dr, daddr);

    uint8_t result = s - d;
    set_nz_byte(cpu, result);
    set_v(cpu, ((s ^ d) & (s ^ result) & 0x80) != 0);
    set_c(cpu, s < d);
}

/* ── ADD ──────────────────────────────────────────────────────────────────── */

static void op_add(ms0515_cpu_t *cpu)
{
    int sm = SRC_MODE(cpu->instruction), sr = SRC_REG(cpu->instruction);
    int dm = DST_MODE(cpu->instruction), dr = DST_REG(cpu->instruction);

    uint16_t saddr = get_word_addr(cpu, sm, sr);
    uint16_t s     = read_word_op(cpu, sm, sr, saddr);
    uint16_t daddr = get_word_addr(cpu, dm, dr);
    uint16_t d     = read_word_op(cpu, dm, dr, daddr);

    uint32_t result = (uint32_t)s + (uint32_t)d;
    write_word_op(cpu, dm, dr, daddr, (uint16_t)result);

    set_nz_word(cpu, (uint16_t)result);
    set_v(cpu, ((~(s ^ d)) & (s ^ (uint16_t)result) & 0x8000) != 0);
    set_c(cpu, result > 0xFFFF);
}

/* ── SUB ──────────────────────────────────────────────────────────────────── */

static void op_sub(ms0515_cpu_t *cpu)
{
    int sm = SRC_MODE(cpu->instruction), sr = SRC_REG(cpu->instruction);
    int dm = DST_MODE(cpu->instruction), dr = DST_REG(cpu->instruction);

    uint16_t saddr = get_word_addr(cpu, sm, sr);
    uint16_t s     = read_word_op(cpu, sm, sr, saddr);
    uint16_t daddr = get_word_addr(cpu, dm, dr);
    uint16_t d     = read_word_op(cpu, dm, dr, daddr);

    uint16_t result = d - s;
    write_word_op(cpu, dm, dr, daddr, result);

    set_nz_word(cpu, result);
    set_v(cpu, ((d ^ s) & (~s ^ result) & 0x8000) != 0);
    set_c(cpu, d < s);
}

/* ── BIT / BITB — Bit Test ────────────────────────────────────────────────── */

static void op_bit(ms0515_cpu_t *cpu)
{
    int sm = SRC_MODE(cpu->instruction), sr = SRC_REG(cpu->instruction);
    int dm = DST_MODE(cpu->instruction), dr = DST_REG(cpu->instruction);

    uint16_t saddr = get_word_addr(cpu, sm, sr);
    uint16_t s     = read_word_op(cpu, sm, sr, saddr);
    uint16_t daddr = get_word_addr(cpu, dm, dr);
    uint16_t d     = read_word_op(cpu, dm, dr, daddr);

    uint16_t result = s & d;
    set_nz_word(cpu, result);
    set_v(cpu, false);
}

static void op_bitb(ms0515_cpu_t *cpu)
{
    int sm = SRC_MODE(cpu->instruction), sr = SRC_REG(cpu->instruction);
    int dm = DST_MODE(cpu->instruction), dr = DST_REG(cpu->instruction);

    uint16_t saddr = get_byte_addr(cpu, sm, sr);
    uint8_t s      = read_byte_op(cpu, sm, sr, saddr);
    uint16_t daddr = get_byte_addr(cpu, dm, dr);
    uint8_t d      = read_byte_op(cpu, dm, dr, daddr);

    uint8_t result = s & d;
    set_nz_byte(cpu, result);
    set_v(cpu, false);
}

/* ── BIC / BICB — Bit Clear ───────────────────────────────────────────────── */

static void op_bic(ms0515_cpu_t *cpu)
{
    int sm = SRC_MODE(cpu->instruction), sr = SRC_REG(cpu->instruction);
    int dm = DST_MODE(cpu->instruction), dr = DST_REG(cpu->instruction);

    uint16_t saddr = get_word_addr(cpu, sm, sr);
    uint16_t s     = read_word_op(cpu, sm, sr, saddr);
    uint16_t daddr = get_word_addr(cpu, dm, dr);
    uint16_t d     = read_word_op(cpu, dm, dr, daddr);

    uint16_t result = d & ~s;
    write_word_op(cpu, dm, dr, daddr, result);
    set_nz_word(cpu, result);
    set_v(cpu, false);
}

static void op_bicb(ms0515_cpu_t *cpu)
{
    int sm = SRC_MODE(cpu->instruction), sr = SRC_REG(cpu->instruction);
    int dm = DST_MODE(cpu->instruction), dr = DST_REG(cpu->instruction);

    uint16_t saddr = get_byte_addr(cpu, sm, sr);
    uint8_t s      = read_byte_op(cpu, sm, sr, saddr);
    uint16_t daddr = get_byte_addr(cpu, dm, dr);
    uint8_t d      = read_byte_op(cpu, dm, dr, daddr);

    uint8_t result = d & ~s;
    write_byte_op(cpu, dm, dr, daddr, result);
    set_nz_byte(cpu, result);
    set_v(cpu, false);
}

/* ── BIS / BISB — Bit Set ─────────────────────────────────────────────────── */

static void op_bis(ms0515_cpu_t *cpu)
{
    int sm = SRC_MODE(cpu->instruction), sr = SRC_REG(cpu->instruction);
    int dm = DST_MODE(cpu->instruction), dr = DST_REG(cpu->instruction);

    uint16_t saddr = get_word_addr(cpu, sm, sr);
    uint16_t s     = read_word_op(cpu, sm, sr, saddr);
    uint16_t daddr = get_word_addr(cpu, dm, dr);
    uint16_t d     = read_word_op(cpu, dm, dr, daddr);

    uint16_t result = d | s;
    write_word_op(cpu, dm, dr, daddr, result);
    set_nz_word(cpu, result);
    set_v(cpu, false);
}

static void op_bisb(ms0515_cpu_t *cpu)
{
    int sm = SRC_MODE(cpu->instruction), sr = SRC_REG(cpu->instruction);
    int dm = DST_MODE(cpu->instruction), dr = DST_REG(cpu->instruction);

    uint16_t saddr = get_byte_addr(cpu, sm, sr);
    uint8_t s      = read_byte_op(cpu, sm, sr, saddr);
    uint16_t daddr = get_byte_addr(cpu, dm, dr);
    uint8_t d      = read_byte_op(cpu, dm, dr, daddr);

    uint8_t result = d | s;
    write_byte_op(cpu, dm, dr, daddr, result);
    set_nz_byte(cpu, result);
    set_v(cpu, false);
}

/* ── XOR ──────────────────────────────────────────────────────────────────── */

static void op_xor(ms0515_cpu_t *cpu)
{
    int sr = (cpu->instruction >> 6) & 7;
    int dm = DST_MODE(cpu->instruction), dr = DST_REG(cpu->instruction);

    uint16_t s = cpu->r[sr];
    uint16_t daddr = get_word_addr(cpu, dm, dr);
    uint16_t d     = read_word_op(cpu, dm, dr, daddr);

    uint16_t result = s ^ d;
    write_word_op(cpu, dm, dr, daddr, result);
    set_nz_word(cpu, result);
    set_v(cpu, false);
}

/* ── Branch instructions ──────────────────────────────────────────────────── */

static void op_br(ms0515_cpu_t *cpu)
{
    cpu->r[CPU_REG_PC] += (int16_t)(BRANCH_OFF(cpu->instruction) * 2);
}

static void op_bne(ms0515_cpu_t *cpu)
{
    if (!(cpu->psw & CPU_PSW_Z))
        cpu->r[CPU_REG_PC] += (int16_t)(BRANCH_OFF(cpu->instruction) * 2);
}

static void op_beq(ms0515_cpu_t *cpu)
{
    if (cpu->psw & CPU_PSW_Z)
        cpu->r[CPU_REG_PC] += (int16_t)(BRANCH_OFF(cpu->instruction) * 2);
}

static void op_bpl(ms0515_cpu_t *cpu)
{
    if (!(cpu->psw & CPU_PSW_N))
        cpu->r[CPU_REG_PC] += (int16_t)(BRANCH_OFF(cpu->instruction) * 2);
}

static void op_bmi(ms0515_cpu_t *cpu)
{
    if (cpu->psw & CPU_PSW_N)
        cpu->r[CPU_REG_PC] += (int16_t)(BRANCH_OFF(cpu->instruction) * 2);
}

static void op_bvc(ms0515_cpu_t *cpu)
{
    if (!(cpu->psw & CPU_PSW_V))
        cpu->r[CPU_REG_PC] += (int16_t)(BRANCH_OFF(cpu->instruction) * 2);
}

static void op_bvs(ms0515_cpu_t *cpu)
{
    if (cpu->psw & CPU_PSW_V)
        cpu->r[CPU_REG_PC] += (int16_t)(BRANCH_OFF(cpu->instruction) * 2);
}

static void op_bge(ms0515_cpu_t *cpu)
{
    bool n = (cpu->psw & CPU_PSW_N) != 0;
    bool v = (cpu->psw & CPU_PSW_V) != 0;
    if (!(n ^ v))
        cpu->r[CPU_REG_PC] += (int16_t)(BRANCH_OFF(cpu->instruction) * 2);
}

static void op_blt(ms0515_cpu_t *cpu)
{
    bool n = (cpu->psw & CPU_PSW_N) != 0;
    bool v = (cpu->psw & CPU_PSW_V) != 0;
    if (n ^ v)
        cpu->r[CPU_REG_PC] += (int16_t)(BRANCH_OFF(cpu->instruction) * 2);
}

static void op_bgt(ms0515_cpu_t *cpu)
{
    bool n = (cpu->psw & CPU_PSW_N) != 0;
    bool v = (cpu->psw & CPU_PSW_V) != 0;
    bool z = (cpu->psw & CPU_PSW_Z) != 0;
    if (!(z || (n ^ v)))
        cpu->r[CPU_REG_PC] += (int16_t)(BRANCH_OFF(cpu->instruction) * 2);
}

static void op_ble(ms0515_cpu_t *cpu)
{
    bool n = (cpu->psw & CPU_PSW_N) != 0;
    bool v = (cpu->psw & CPU_PSW_V) != 0;
    bool z = (cpu->psw & CPU_PSW_Z) != 0;
    if (z || (n ^ v))
        cpu->r[CPU_REG_PC] += (int16_t)(BRANCH_OFF(cpu->instruction) * 2);
}

static void op_bhi(ms0515_cpu_t *cpu)
{
    bool c = (cpu->psw & CPU_PSW_C) != 0;
    bool z = (cpu->psw & CPU_PSW_Z) != 0;
    if (!(c || z))
        cpu->r[CPU_REG_PC] += (int16_t)(BRANCH_OFF(cpu->instruction) * 2);
}

static void op_blos(ms0515_cpu_t *cpu)
{
    bool c = (cpu->psw & CPU_PSW_C) != 0;
    bool z = (cpu->psw & CPU_PSW_Z) != 0;
    if (c || z)
        cpu->r[CPU_REG_PC] += (int16_t)(BRANCH_OFF(cpu->instruction) * 2);
}

static void op_bhis(ms0515_cpu_t *cpu)  /* BCC */
{
    if (!(cpu->psw & CPU_PSW_C))
        cpu->r[CPU_REG_PC] += (int16_t)(BRANCH_OFF(cpu->instruction) * 2);
}

static void op_blo(ms0515_cpu_t *cpu)   /* BCS */
{
    if (cpu->psw & CPU_PSW_C)
        cpu->r[CPU_REG_PC] += (int16_t)(BRANCH_OFF(cpu->instruction) * 2);
}

/* ── JMP ──────────────────────────────────────────────────────────────────── */

static void op_jmp(ms0515_cpu_t *cpu)
{
    int mode = DST_MODE(cpu->instruction);
    int reg  = DST_REG(cpu->instruction);

    if (mode == 0) {
        /* JMP Rn is illegal */
        cpu->irq_reserved = true;
        return;
    }

    uint16_t addr = get_word_addr(cpu, mode, reg);
    cpu->r[CPU_REG_PC] = addr;
}

/* ── JSR — Jump to Subroutine ─────────────────────────────────────────────── */

static void op_jsr(ms0515_cpu_t *cpu)
{
    int linkr = (cpu->instruction >> 6) & 7;
    int mode  = DST_MODE(cpu->instruction);
    int reg   = DST_REG(cpu->instruction);

    if (mode == 0) {
        /* JSR Rn, Rn is illegal */
        cpu->irq_reserved = true;
        return;
    }

    uint16_t addr = get_word_addr(cpu, mode, reg);
    push(cpu, cpu->r[linkr]);
    cpu->r[linkr] = cpu->r[CPU_REG_PC];
    cpu->r[CPU_REG_PC] = addr;
}

/* ── RTS — Return from Subroutine ─────────────────────────────────────────── */

static void op_rts(ms0515_cpu_t *cpu)
{
    int reg = cpu->instruction & 7;
    cpu->r[CPU_REG_PC] = cpu->r[reg];
    cpu->r[reg] = pop(cpu);
}

/* ── SOB — Subtract One and Branch ────────────────────────────────────────── */

static void op_sob(ms0515_cpu_t *cpu)
{
    int reg = (cpu->instruction >> 6) & 7;
    cpu->r[reg]--;
    if (cpu->r[reg] != 0) {
        int offset = cpu->instruction & 077;
        cpu->r[CPU_REG_PC] -= (uint16_t)(offset * 2);
    }
}

/* ── Condition code operations ────────────────────────────────────────────── */
/*
 * CCC (Clear Condition Codes): bits 3-0 of the instruction select
 * which flags to clear.  Opcodes 000240–000257.
 *
 * SCC (Set Condition Codes): same but sets flags.
 * Opcodes 000260–000277.
 */

static void op_ccc(ms0515_cpu_t *cpu)
{
    /* Clear selected condition code bits */
    uint16_t mask = cpu->instruction & 0x0F;
    cpu->psw &= ~mask;
}

static void op_scc(ms0515_cpu_t *cpu)
{
    /* Set selected condition code bits */
    uint16_t mask = cpu->instruction & 0x0F;
    cpu->psw |= mask;
}

/* ══════════════════════════════════════════════════════════════════════════ */
/*  DISPATCH TABLE INITIALIZATION                                            */
/* ══════════════════════════════════════════════════════════════════════════ */

/*
 * Register a handler for a range of opcodes.
 *
 * `start` and `end` are inclusive opcode values.
 * For instructions with operand fields, the range covers all
 * valid operand combinations.
 */
static void reg_range(cpu_op_handler_t *table,
                       uint16_t start, uint16_t end,
                       cpu_op_handler_t handler)
{
    for (uint32_t i = start; i <= end; i++)
        table[i] = handler;
}

void cpu_ops_init_dispatch_table(cpu_op_handler_t *table)
{
    /* Fill everything with "unknown instruction" first */
    for (int i = 0; i < 65536; i++)
        table[i] = op_unknown;

    /* ── Zero-operand instructions ──────────────────────────────────────── */
    table[0000000] = op_halt;
    table[0000001] = op_wait;
    table[0000002] = op_rti;
    table[0000003] = op_bpt;
    table[0000004] = op_iot;
    table[0000005] = op_reset;
    table[0000006] = op_rtt;
    table[0000007] = op_mfpt;

    /* ── NOP and condition code ops (000240–000277) ─────────────────────── */
    table[0000240] = op_nop;
    reg_range(table, 0000241, 0000257, op_ccc);  /* CLC..CCC */
    table[0000260] = op_nop;                     /* NOP variant */
    reg_range(table, 0000261, 0000277, op_scc);  /* SEC..SCC */

    /* ── RTS (000200–000207) ────────────────────────────────────────────── */
    reg_range(table, 0000200, 0000207, op_rts);

    /* ── Single-operand instructions ────────────────────────────────────── */
    /* JMP 0001DD */
    reg_range(table, 0000100, 0000177, op_jmp);
    /* SWAB 0003DD */
    reg_range(table, 0000300, 0000377, op_swab);

    /* CLR 0050DD,  CLRB 1050DD */
    reg_range(table, 0005000, 0005077, op_clr);
    reg_range(table, 0105000, 0105077, op_clrb);
    /* COM 0051DD, COMB 1051DD */
    reg_range(table, 0005100, 0005177, op_com);
    reg_range(table, 0105100, 0105177, op_comb);
    /* INC 0052DD, INCB 1052DD */
    reg_range(table, 0005200, 0005277, op_inc);
    reg_range(table, 0105200, 0105277, op_incb);
    /* DEC 0053DD, DECB 1053DD */
    reg_range(table, 0005300, 0005377, op_dec);
    reg_range(table, 0105300, 0105377, op_decb);
    /* NEG 0054DD, NEGB 1054DD */
    reg_range(table, 0005400, 0005477, op_neg);
    reg_range(table, 0105400, 0105477, op_negb);
    /* ADC 0055DD, ADCB 1055DD */
    reg_range(table, 0005500, 0005577, op_adc);
    reg_range(table, 0105500, 0105577, op_adcb);
    /* SBC 0056DD, SBCB 1056DD */
    reg_range(table, 0005600, 0005677, op_sbc);
    reg_range(table, 0105600, 0105677, op_sbcb);
    /* TST 0057DD, TSTB 1057DD */
    reg_range(table, 0005700, 0005777, op_tst);
    reg_range(table, 0105700, 0105777, op_tstb);
    /* ROR 0060DD, RORB 1060DD */
    reg_range(table, 0006000, 0006077, op_ror);
    reg_range(table, 0106000, 0106077, op_rorb);
    /* ROL 0061DD, ROLB 1061DD */
    reg_range(table, 0006100, 0006177, op_rol);
    reg_range(table, 0106100, 0106177, op_rolb);
    /* ASR 0062DD, ASRB 1062DD */
    reg_range(table, 0006200, 0006277, op_asr);
    reg_range(table, 0106200, 0106277, op_asrb);
    /* ASL 0063DD, ASLB 1063DD */
    reg_range(table, 0006300, 0006377, op_asl);
    reg_range(table, 0106300, 0106377, op_aslb);
    /* SXT 0067DD */
    reg_range(table, 0006700, 0006777, op_sxt);
    /* MTPS 1064DD */
    reg_range(table, 0106400, 0106477, op_mtps);
    /* MFPS 1067DD */
    reg_range(table, 0106700, 0106777, op_mfps);

    /* ── Double-operand instructions ────────────────────────────────────── */
    /* MOV 01SSDD, MOVB 11SSDD */
    reg_range(table, 0010000, 0017777, op_mov);
    reg_range(table, 0110000, 0117777, op_movb);
    /* CMP 02SSDD, CMPB 12SSDD */
    reg_range(table, 0020000, 0027777, op_cmp);
    reg_range(table, 0120000, 0127777, op_cmpb);
    /* BIT 03SSDD, BITB 13SSDD */
    reg_range(table, 0030000, 0037777, op_bit);
    reg_range(table, 0130000, 0137777, op_bitb);
    /* BIC 04SSDD, BICB 14SSDD */
    reg_range(table, 0040000, 0047777, op_bic);
    reg_range(table, 0140000, 0147777, op_bicb);
    /* BIS 05SSDD, BISB 15SSDD */
    reg_range(table, 0050000, 0057777, op_bis);
    reg_range(table, 0150000, 0157777, op_bisb);
    /* ADD 06SSDD */
    reg_range(table, 0060000, 0067777, op_add);
    /* SUB 16SSDD */
    reg_range(table, 0160000, 0167777, op_sub);

    /* ── Register + operand instructions ────────────────────────────────── */
    /* XOR 074RDD */
    reg_range(table, 0074000, 0074777, op_xor);
    /* JSR 004RDD */
    reg_range(table, 0004000, 0004777, op_jsr);
    /* SOB 077RNN */
    reg_range(table, 0077000, 0077777, op_sob);

    /* ── Branch instructions ────────────────────────────────────────────── */
    reg_range(table, 0000400, 0000777, op_br);
    reg_range(table, 0001000, 0001377, op_bne);
    reg_range(table, 0001400, 0001777, op_beq);
    reg_range(table, 0002000, 0002377, op_bge);
    reg_range(table, 0002400, 0002777, op_blt);
    reg_range(table, 0003000, 0003377, op_bgt);
    reg_range(table, 0003400, 0003777, op_ble);
    reg_range(table, 0100000, 0100377, op_bpl);
    reg_range(table, 0100400, 0100777, op_bmi);
    reg_range(table, 0101000, 0101377, op_bhi);
    reg_range(table, 0101400, 0101777, op_blos);
    reg_range(table, 0102000, 0102377, op_bvc);
    reg_range(table, 0102400, 0102777, op_bvs);
    reg_range(table, 0103000, 0103377, op_bhis);
    reg_range(table, 0103400, 0103777, op_blo);

    /* ── Trap instructions ──────────────────────────────────────────────── */
    reg_range(table, 0104000, 0104377, op_emt);
    reg_range(table, 0104400, 0104777, op_trap);
}
