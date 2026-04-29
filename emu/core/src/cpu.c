/*
 * cpu.c — KR1807VM1 (PDP-11) CPU core: init, reset, step, interrupts
 *
 * Instruction dispatch uses a flat 64K-entry function pointer table,
 * indexed by the full 16-bit opcode.  This trades ~512 KB of memory
 * for O(1) dispatch with zero branching overhead.
 *
 * The table is built once by cpu_init_dispatch_table() on first use.
 *
 * Sources:
 *   - PDP-11 Architecture Handbook (DEC, EB-23657-18)
 *   - T-11 User's Guide (EK-DCT11-UG)
 *   - NS4 technical description, section 4.2
 */

#include <ms0515/cpu.h>
#include <ms0515/board.h>
#include <string.h>
#include <assert.h>

/* ── Internal instruction handler type ────────────────────────────────────── */

typedef void (*cpu_op_handler_t)(ms0515_cpu_t *cpu);

/* Defined in cpu_ops.c */
extern void cpu_ops_init_dispatch_table(cpu_op_handler_t *table);

/* ── Static dispatch table ────────────────────────────────────────────────── */

static cpu_op_handler_t dispatch_table[65536];
static bool dispatch_table_initialized = false;

/* ── Internal helpers ─────────────────────────────────────────────────────── */

/*
 * Push a word onto the stack: SP -= 2, then write word at SP.
 */
static void cpu_push(ms0515_cpu_t *cpu, uint16_t value)
{
    cpu->r[CPU_REG_SP] -= 2;
    board_write_word(cpu->board, cpu->r[CPU_REG_SP], value);
}

/*
 * Service an interrupt: push PSW and PC, load new PC and PSW from vector.
 */
static void cpu_service_interrupt(ms0515_cpu_t *cpu, uint16_t vector)
{
    uint16_t new_pc = board_read_word(cpu->board, vector);
    {
        uint8_t vec8 = (uint8_t)vector;
        BOARD_EVT(cpu->board, MS0515_EVT_TRAP, &vec8, 1);
    }
    cpu_push(cpu, cpu->psw);
    cpu_push(cpu, cpu->r[CPU_REG_PC]);
    cpu->r[CPU_REG_PC] = new_pc;
    cpu->psw            = board_read_word(cpu->board, vector + 2);
    cpu->waiting = false;
    cpu->halted  = false;
}

/*
 * Check and service pending interrupts.
 * Returns true if an interrupt was serviced.
 */
static bool cpu_check_interrupts(ms0515_cpu_t *cpu)
{
    int priority = cpu_get_priority(cpu);

    /* Non-maskable: HALT signal (highest priority) */
    if (cpu->irq_halt) {
        cpu->irq_halt = false;
        /*
         * HALT pushes PC and PSW to stack, loads restart address (0172004)
         * and PSW = 0340.  Per NS4 tech desc, section 4.5.2.
         */
        cpu_push(cpu, cpu->psw);
        cpu_push(cpu, cpu->r[CPU_REG_PC]);
        cpu->r[CPU_REG_PC] = 0172004;
        cpu->psw = 0340;
        cpu->waiting = false;
        return true;
    }

    /* Bus error (double fault) — priority 7 */
    if (cpu->irq_bus_error) {
        cpu->irq_bus_error = false;
        cpu_service_interrupt(cpu, CPU_VEC_BUS_ERROR);
        return true;
    }

    /* Internal traps — always serviced regardless of priority level */
    if (cpu->irq_reserved) {
        cpu->irq_reserved = false;
        cpu_service_interrupt(cpu, CPU_VEC_RESERVED);
        return true;
    }
    if (cpu->irq_bpt) {
        cpu->irq_bpt = false;
        cpu_service_interrupt(cpu, CPU_VEC_BPT);
        return true;
    }
    if (cpu->irq_iot) {
        cpu->irq_iot = false;
        cpu_service_interrupt(cpu, CPU_VEC_IOT);
        return true;
    }
    if (cpu->irq_emt) {
        cpu->irq_emt = false;
        cpu_service_interrupt(cpu, CPU_VEC_EMT);
        return true;
    }
    if (cpu->irq_trap) {
        cpu->irq_trap = false;
        cpu_service_interrupt(cpu, CPU_VEC_TRAP);
        return true;
    }

    /* T-bit trap — checked after each instruction if T was set */
    if (cpu->irq_tbit) {
        cpu->irq_tbit = false;
        cpu_service_interrupt(cpu, CPU_VEC_BPT);
        return true;
    }

    /* External vectored interrupts — check priority */
    for (int i = 0; i < 16; i++) {
        if (cpu->irq_virq[i]) {
            /*
             * Interrupt priority is encoded in the vector's PSW.
             * For simplicity, we compare the IRQ's inherent priority
             * against the CPU's current priority.
             *
             * MS0515 priority mapping (from NS4 Table 4):
             *   IRQ 11 (timer)     → priority 6
             *   IRQ 9  (serial RX) → priority 6
             *   IRQ 8  (serial TX) → priority 6
             *   IRQ 5  (keyboard)  → priority 5
             *   IRQ 2  (VBlank)    → priority 4
             *
             * We derive the priority from the PSW stored at vector+2.
             */
            uint16_t vec = cpu->irq_virq_vec[i];
            uint16_t new_psw = board_read_word(cpu->board, vec + 2);
            int irq_prio = (new_psw >> 5) & 7;

            if (irq_prio > priority) {
                cpu->irq_virq[i] = false;
                cpu_service_interrupt(cpu, vec);
                return true;
            }
        }
    }

    return false;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void cpu_init(ms0515_cpu_t *cpu, ms0515_board_t *board)
{
    memset(cpu, 0, sizeof(*cpu));
    cpu->board = board;

    if (!dispatch_table_initialized) {
        cpu_ops_init_dispatch_table(dispatch_table);
        dispatch_table_initialized = true;
    }
}

void cpu_reset(ms0515_cpu_t *cpu)
{
    /*
     * T-11 reset sequence (NS4 tech desc section 4.2.5):
     * On reset the CPU latches the external mode register.  On the
     * MS0515 the mode register bits <15:13> = 111, which selects
     * start address 0172000 and restart address 0172004.  The CPU
     * then begins FETCHING instructions at the start address — it
     * does NOT read an initial PC/PSW pair from that location.
     *
     * So after reset:
     *   PC  = 0172000  (start address, fetched from ROM on next step)
     *   PSW = 0        (priority level 0, no flags set)
     */
    for (int i = 0; i < 8; i++)
        cpu->r[i] = 0;

    cpu->r[CPU_REG_PC] = 0172000;
    cpu->psw           = 0;

    cpu->halted  = false;
    cpu->waiting = false;
    cpu->cycles  = 0;

    /* Clear all pending interrupts */
    cpu->irq_halt      = false;
    cpu->irq_bus_error = false;
    cpu->irq_reserved  = false;
    cpu->irq_bpt       = false;
    cpu->irq_iot       = false;
    cpu->irq_emt       = false;
    cpu->irq_trap      = false;
    cpu->irq_tbit      = false;
    memset(cpu->irq_virq, 0, sizeof(cpu->irq_virq));
}

int cpu_step(ms0515_cpu_t *cpu)
{
    if (cpu->halted)
        return 0;

    int prev_prio = cpu_get_priority(cpu);

    /* If waiting (WAIT instruction), only check interrupts */
    if (cpu->waiting) {
        if (cpu_check_interrupts(cpu)) {
            int now_prio = cpu_get_priority(cpu);
            if (now_prio != prev_prio) {
                uint8_t pl[2] = { (uint8_t)now_prio, (uint8_t)prev_prio };
                BOARD_EVT(cpu->board, MS0515_EVT_PSW, pl, 2);
            }
            return 1;
        }
        return 0;
    }

    /* Check interrupts before fetching next instruction */
    cpu_check_interrupts(cpu);

    /* Save T-bit state before execution (checked after instruction) */
    bool tbit_was_set = (cpu->psw & CPU_PSW_T) != 0;

    /*
     * Spontaneous-reset detection.  A fetch at the cold-start vector
     * after the initial cold boot means the machine effectively rebooted
     * itself (game JMPed to ROM, watchdog, etc.) — the frontend uses
     * this to take an automatic post-mortem snapshot.
     */
    if (cpu->r[CPU_REG_PC] == 0172000) {
        if (cpu->board->reset_first_seen)
            cpu->board->unexpected_reset = true;
        cpu->board->reset_first_seen = true;
    }

    /* Fetch instruction */
    cpu->instruction_pc = cpu->r[CPU_REG_PC];
    cpu->instruction    = board_read_word(cpu->board, cpu->r[CPU_REG_PC]);
    cpu->r[CPU_REG_PC] += 2;
    cpu->cycles = 9;   /* Instruction fetch + decode (per K1801VM1 / MAME T11) */

    /* Dispatch */
    dispatch_table[cpu->instruction](cpu);

    /* If T-bit was set before this instruction, queue a trap */
    if (tbit_was_set)
        cpu->irq_tbit = true;

    int now_prio = cpu_get_priority(cpu);
    if (now_prio != prev_prio) {
        uint8_t pl[2] = { (uint8_t)now_prio, (uint8_t)prev_prio };
        BOARD_EVT(cpu->board, MS0515_EVT_PSW, pl, 2);
    }

    return cpu->cycles;
}

int cpu_execute(ms0515_cpu_t *cpu, int max_cycles)
{
    int total = 0;
    while (total < max_cycles) {
        int c = cpu_step(cpu);
        if (c == 0)
            break;      /* Halted or waiting with no interrupt */
        total += c;
    }
    return total;
}

void cpu_interrupt(ms0515_cpu_t *cpu, int irq, uint16_t vector)
{
    assert(irq >= 0 && irq < 16);
    cpu->irq_virq[irq]     = true;
    cpu->irq_virq_vec[irq] = vector;
}

void cpu_clear_interrupt(ms0515_cpu_t *cpu, int irq)
{
    assert(irq >= 0 && irq < 16);
    cpu->irq_virq[irq] = false;
}
