/*
 * cpu.h — KR1807VM1 (PDP-11 compatible) CPU emulation
 *
 * The KR1807VM1 is a Soviet clone of the DEC T-11 (DC310/DCT11)
 * microprocessor.  It implements a subset of the PDP-11 instruction set:
 *   - No floating-point unit
 *   - No memory management unit (MMU)
 *   - MARK instruction is NOT supported (treated as illegal)
 *   - 66 base instructions
 *
 * Register set:
 *   R0–R5  General-purpose registers
 *   R6     Stack Pointer (SP)
 *   R7     Program Counter (PC)
 *
 * Processor Status Word (PSW) layout — 16 bits, upper byte reads as zero:
 *   Bit  0   C  — Carry
 *   Bit  1   V  — Overflow
 *   Bit  2   Z  — Zero
 *   Bit  3   N  — Negative
 *   Bit  4   T  — Trap (single-step debug)
 *   Bits 7-5 Priority level (0–7)
 *   Bits 15-8  Read as zero
 *
 * Addressing modes (3-bit encoding, combined with 3-bit register):
 *   0  Register           Rn
 *   1  Register deferred  (Rn)
 *   2  Autoincrement      (Rn)+
 *   3  Autoincrement def. @(Rn)+
 *   4  Autodecrement      -(Rn)
 *   5  Autodecrement def. @-(Rn)
 *   6  Index              X(Rn)
 *   7  Index deferred     @X(Rn)
 *
 * When R7 (PC) is used as the register, modes 2/3/6/7 produce the
 * familiar immediate / absolute / relative / relative-deferred modes.
 *
 * Sources:
 *   - PDP-11 Architecture Handbook (DEC, EB-23657-18)
 *   - PDP-11/04/34A/44/60/70 Processor Handbook
 *   - T-11 User's Guide (EK-DCT11-UG)
 *   - NS4 technical description (3.858.420 TO)
 *   - MAME driver: src/mame/ussr/ms0515.cpp
 */

#ifndef MS0515_CPU_H
#define MS0515_CPU_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Forward declarations ─────────────────────────────────────────────────── */

typedef struct ms0515_board ms0515_board_t;

/* ── PSW bit masks ────────────────────────────────────────────────────────── */

#define CPU_PSW_C    0x0001   /* Carry                                       */
#define CPU_PSW_V    0x0002   /* Overflow                                    */
#define CPU_PSW_Z    0x0004   /* Zero                                        */
#define CPU_PSW_N    0x0008   /* Negative                                    */
#define CPU_PSW_T    0x0010   /* Trap / single-step                          */
#define CPU_PSW_PRIO 0x00E0   /* Priority mask (bits 7-5)                    */
#define CPU_PSW_HALT 0x0100   /* HALT mode flag (internal, T-11 specific)    */

/* ── Register indices ─────────────────────────────────────────────────────── */

#define CPU_REG_SP   6
#define CPU_REG_PC   7

/* ── Interrupt vectors (octal, as per PDP-11 convention) ──────────────────── */
/*
 * Vector addresses from the NS4 technical description (Table 4):
 *   CP3  CP2  CP1  CP0   Priority  Vector   Device
 *    L    H    L    L      6       0100     Timer (PIT8253 ch.2)
 *    L    H    H    L      6       0110     Serial RX (i8251)
 *    L    H    H    H      6       0114     Serial TX (i8251)
 *    H    L    H    L      5       0130     Keyboard MS7004 (i8251)
 *    H    H    L    L      4       0060     Keyboard MS7007 (PPI)
 *    H    H    L    H      4       0064     VBlank / monitor
 *
 * Internal CPU vectors:
 *   0004  Bus error / illegal address
 *   0010  Reserved instruction
 *   0014  BPT / T-bit trap
 *   0020  IOT trap
 *   0024  Power fail (unused in MS0515)
 *   0030  EMT trap
 *   0034  TRAP instruction
 */
#define CPU_VEC_BUS_ERROR   0004
#define CPU_VEC_RESERVED    0010
#define CPU_VEC_BPT         0014
#define CPU_VEC_IOT         0020
#define CPU_VEC_POWER_FAIL  0024
#define CPU_VEC_EMT         0030
#define CPU_VEC_TRAP        0034
#define CPU_VEC_TIMER       0100
#define CPU_VEC_SERIAL_RX   0110
#define CPU_VEC_SERIAL_TX   0114
#define CPU_VEC_KBD7007     0060
#define CPU_VEC_VBLANK      0064
#define CPU_VEC_KEYBOARD    0130

/* ── CPU state structure ──────────────────────────────────────────────────── */

typedef struct ms0515_cpu {
    /* Registers */
    uint16_t r[8];              /* R0–R7 (R6=SP, R7=PC)                      */
    uint16_t psw;               /* Processor Status Word                     */

    /* Current instruction context (set during execution) */
    uint16_t instruction;       /* Opcode of the current instruction         */
    uint16_t instruction_pc;    /* PC at the start of the current instruction */

    /* CPU state flags */
    bool     halted;            /* true if HALT instruction was executed      */
    bool     waiting;           /* true if WAIT instruction is active         */

    /* Pending interrupt requests — one flag per source.
     * Checked between instructions; highest-priority wins. */
    bool     irq_halt;          /* HALT signal (non-maskable)                */
    bool     irq_bus_error;     /* Bus / address error                       */
    bool     irq_reserved;      /* Reserved (illegal) instruction            */
    bool     irq_bpt;           /* BPT instruction                           */
    bool     irq_iot;           /* IOT instruction                           */
    bool     irq_emt;           /* EMT instruction                           */
    bool     irq_trap;          /* TRAP instruction                          */
    bool     irq_tbit;          /* T-bit (single-step) trap                  */
    bool     irq_virq[16];     /* Vectored external IRQ lines               */
    uint16_t irq_virq_vec[16]; /* Corresponding vector addresses            */

    /* Cycle counter — incremented per bus access, used for timing */
    int      cycles;

    /* Back-pointer to the motherboard (set once at init, never changes) */
    ms0515_board_t *board;
} ms0515_cpu_t;

/* ── Public API ───────────────────────────────────────────────────────────── */

/*
 * cpu_init — Initialize CPU state.
 *
 * Must be called once before any other cpu_* function.
 * Sets all registers to zero, clears all pending interrupts.
 * The `board` pointer is stored for memory access callbacks.
 */
void cpu_init(ms0515_cpu_t *cpu, ms0515_board_t *board);

/*
 * cpu_reset — Perform a hardware reset sequence.
 *
 * Per the T-11 User's Guide, on reset the CPU:
 *   1. Reads the mode register (start address = 0172000 for MS0515)
 *   2. Fetches new PC from address 0172000
 *   3. Fetches new PSW from address 0172002
 *   4. Begins execution
 */
void cpu_reset(ms0515_cpu_t *cpu);

/*
 * cpu_step — Execute a single instruction.
 *
 * Returns the number of bus cycles consumed.
 * If the CPU is halted or waiting, returns 0 without executing.
 */
int cpu_step(ms0515_cpu_t *cpu);

/*
 * cpu_execute — Execute instructions for the given number of cycles.
 *
 * Returns the actual number of cycles consumed (may slightly exceed
 * `max_cycles` since the last instruction runs to completion).
 */
int cpu_execute(ms0515_cpu_t *cpu, int max_cycles);

/*
 * cpu_interrupt — Request an external vectored interrupt.
 *
 * `irq`    — IRQ line index (0–15)
 * `vector` — Interrupt vector address (octal)
 */
void cpu_interrupt(ms0515_cpu_t *cpu, int irq, uint16_t vector);

/*
 * cpu_clear_interrupt — Clear a pending external interrupt.
 */
void cpu_clear_interrupt(ms0515_cpu_t *cpu, int irq);

/* ── Inline accessors ─────────────────────────────────────────────────────── */

static inline uint16_t cpu_get_pc(const ms0515_cpu_t *cpu)
{
    return cpu->r[CPU_REG_PC];
}

static inline uint16_t cpu_get_sp(const ms0515_cpu_t *cpu)
{
    return cpu->r[CPU_REG_SP];
}

static inline uint16_t cpu_get_psw(const ms0515_cpu_t *cpu)
{
    return cpu->psw;
}

static inline int cpu_get_priority(const ms0515_cpu_t *cpu)
{
    return (cpu->psw >> 5) & 7;
}

#ifdef __cplusplus
}
#endif

#endif /* MS0515_CPU_H */
