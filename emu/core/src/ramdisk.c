/*
 * ramdisk.c — Expansion board RAM disk (EX0:)
 *
 * Emulates the optional MS0515 expansion board with 512 KB DRAM and
 * a KR580VV55A (8255 PPI) for address setup and control.
 *
 * Hardware data flow:
 *   CPU → PPI Port A → MA08-MA15 (page address)
 *   CPU → PPI Port B → MA16-MA18 (bank/high address) + control bits
 *   CPU → data port  → DRAM data bus (8 bits)
 *   Counter D14      → MA00-MA07 (auto-increment on each data access)
 *
 * The 19-bit DRAM address is composed as:
 *   {Port_B[2:0], Port_A[7:0], Counter[7:0]}
 *
 * Each read or write to the data port (0177550) accesses one byte at the
 * current address and increments the 8-bit counter.  The counter wraps
 * from 255 to 0, allowing sequential access within a 256-byte page.
 * To move to the next page, the driver writes a new value to Port A.
 *
 * The СБРОС bit (Port B bit 5) resets the counter to 0 when set.
 * The СТАРТ bit (Port B bit 7) enables data transfer (must be set
 * before reading/writing the data port).
 *
 * Sources:
 *   - Expansion board schematics (reference/docs/ext/)
 *   - EX.SYS driver disassembly
 *   - reference/docs/ext/l6_1.doc.txt (hardware description)
 */

#include <ms0515/core/ramdisk.h>
#include <stdlib.h>
#include <string.h>

/* ── Address computation ─────────────────────────────────────────────────── */

/*
 * Compute the 19-bit DRAM address from PPI state and internal counter.
 *
 * Bit layout:
 *   18  17  16  15  14  13  12  11  10  09  08  07  06  05  04  03  02  01  00
 *   [  Port B  ]  [         Port A (MA08-MA15)        ]  [  Counter (MA00-MA07) ]
 *   B2  B1  B0     A7  A6  A5  A4  A3  A2  A1  A0       C7  C6 ...         C0
 */
static uint32_t compute_address(const ms0515_ramdisk_t *rd)
{
    uint32_t hi  = (uint32_t)(rd->ppi_b & 0x07) << 16;  /* MA18:MA16 */
    uint32_t mid = (uint32_t)rd->ppi_a << 8;             /* MA15:MA08 */
    uint32_t lo  = (uint32_t)rd->counter;                /* MA07:MA00 */
    return (hi | mid | lo) & (RAMDISK_SIZE - 1);
}

/* ── PPI control word handling ───────────────────────────────────────────── */

/*
 * The KR580VV55A (8255 PPI) control register has two functions:
 *
 * When bit 7 = 1: Mode Set
 *   Bit 6-5: Group A mode (00=mode 0, 01=mode 1, 1x=mode 2)
 *   Bit 4:   Port A direction (1=input, 0=output)
 *   Bit 3:   Port C upper direction (1=input, 0=output)
 *   Bit 2:   Group B mode (0=mode 0, 1=mode 1)
 *   Bit 1:   Port B direction (1=input, 0=output)
 *   Bit 0:   Port C lower direction (1=input, 0=output)
 *
 * When bit 7 = 0: Bit Set/Reset on Port C
 *   Bits 3-1: Bit number (0-7)
 *   Bit 0:    1=set, 0=reset
 *
 * For the RAM disk, the typical setup is:
 *   Port A = output (MA08-MA15)
 *   Port B = output (control + MA16-MA18)
 *   Port C = mixed (serial interface signals)
 */
static void ppi_set_control(ms0515_ramdisk_t *rd, uint8_t value)
{
    if (value & 0x80) {
        /* Mode set command */
        rd->ppi_ctrl = value;
        rd->port_a_input = (value & 0x10) != 0;
        rd->port_b_input = (value & 0x02) != 0;
        /* Mode set resets all ports and the counter */
        rd->ppi_a = 0;
        rd->ppi_b = 0;
        rd->ppi_c = 0;
        rd->counter = 0;
    } else {
        /* Bit set/reset on Port C */
        uint8_t bit = (value >> 1) & 7;
        if (value & 1)
            rd->ppi_c |= (uint8_t)(1 << bit);
        else
            rd->ppi_c &= (uint8_t)~(1 << bit);
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void ramdisk_init(ms0515_ramdisk_t *rd)
{
    memset(rd, 0, sizeof(*rd));
    rd->enabled = false;
    rd->ram     = NULL;
}

void ramdisk_enable(ms0515_ramdisk_t *rd)
{
    if (rd->ram)
        return;  /* already allocated */

    rd->ram = (uint8_t *)calloc(1, RAMDISK_SIZE);
    if (!rd->ram)
        return;

    /*
     * Simulate uninitialized DRAM power-on state.
     *
     * Real DRAM (КР565РУ7Г) powers on with pseudo-random content.
     * The EX.SYS driver probes several pages to detect whether real
     * DRAM is present (non-uniform data) vs bus float (all 0xFF) vs
     * nothing (all 0x00).  We fill with LCG pseudo-random data.
     */
    uint32_t seed = 0xDEADBEEF;
    for (size_t i = 0; i < RAMDISK_SIZE; i++) {
        seed = seed * 1103515245u + 12345u;
        rd->ram[i] = (uint8_t)(seed >> 16);
    }

    rd->enabled = true;
}

void ramdisk_free(ms0515_ramdisk_t *rd)
{
    free(rd->ram);
    rd->ram = NULL;
    rd->enabled = false;
}

void ramdisk_reset(ms0515_ramdisk_t *rd)
{
    /* Reset PPI and counter to power-on state */
    rd->ppi_a = 0;
    rd->ppi_b = 0;
    rd->ppi_c = 0;
    rd->ppi_ctrl = 0;
    rd->port_a_input = false;
    rd->port_b_input = false;
    rd->counter = 0;
    /* DRAM contents are NOT cleared on reset */
}

bool ramdisk_handles(uint16_t offset)
{
    /* PPI read ports: 0x48-0x4E (0177510-0177516) */
    if (offset >= RAMDISK_IO_PPI_RD_A && offset <= RAMDISK_IO_PPI_RD_CTRL)
        return true;
    /* PPI write ports: 0x58-0x5E (0177530-0177536) */
    if (offset >= RAMDISK_IO_PPI_WR_A && offset <= RAMDISK_IO_PPI_WR_CTRL)
        return true;
    /* Data port: 0x68-0x69 (0177550-0177551) */
    if (offset == RAMDISK_IO_DATA || offset == (RAMDISK_IO_DATA + 1))
        return true;
    /* Data port alias: 0x78-0x79 (0177570-0177571).  The on-board address
     * decoder ignores address bit 4 for the data port, so 0177570 mirrors
     * 0177550.  EX.SYS uses this mirror for the first byte of each page
     * on the write path (see driver disassembly at 142152). */
    if (offset == RAMDISK_IO_DATA_ALIAS ||
        offset == (RAMDISK_IO_DATA_ALIAS + 1))
        return true;
    return false;
}

uint8_t ramdisk_read(ms0515_ramdisk_t *rd, uint16_t offset)
{
    /* PPI read ports */
    if (offset == RAMDISK_IO_PPI_RD_A)
        return rd->ppi_a;
    if (offset == RAMDISK_IO_PPI_RD_B)
        return rd->ppi_b;
    if (offset == RAMDISK_IO_PPI_RD_C)
        return rd->ppi_c;
    if (offset == RAMDISK_IO_PPI_RD_CTRL)
        return rd->ppi_ctrl;

    /* Data port — read byte from DRAM and auto-increment counter.
     * 0177550 and its mirror 0177570 behave identically. */
    if (offset == RAMDISK_IO_DATA       || offset == (RAMDISK_IO_DATA + 1) ||
        offset == RAMDISK_IO_DATA_ALIAS || offset == (RAMDISK_IO_DATA_ALIAS + 1)) {
        uint8_t val = 0xFF;  /* bus float if no RAM */
        if (rd->ram) {
            uint32_t addr = compute_address(rd);
            val = rd->ram[addr];
            rd->counter++;  /* uint8_t wraps at 256 naturally */
        }
        return val;
    }

    return 0;
}

void ramdisk_write(ms0515_ramdisk_t *rd, uint16_t offset, uint8_t value)
{
    /* PPI write ports */
    if (offset == RAMDISK_IO_PPI_WR_A) {
        rd->ppi_a = value;
        return;
    }
    if (offset == RAMDISK_IO_PPI_WR_B) {
        rd->ppi_b = value;
        /*
         * Bit 5 (СБРОС): reset the MA00-MA07 counter to 0.
         * On real hardware, the counter resets on the rising edge of
         * the СБРОС signal.  We reset whenever bit 5 is written as 1.
         * The EX.SYS driver writes 0xA0 (СТАРТ|СБРОС) then writes the
         * actual Port B value (with СТАРТ set but СБРОС clear).
         */
        if (value & RAMDISK_PB_RESET)
            rd->counter = 0;
        return;
    }
    if (offset == RAMDISK_IO_PPI_WR_C) {
        rd->ppi_c = value;
        return;
    }
    if (offset == RAMDISK_IO_PPI_WR_CTRL) {
        ppi_set_control(rd, value);
        return;
    }

    /* Data port — write byte to DRAM and auto-increment counter.
     * 0177550 and its mirror 0177570 behave identically.  EX.SYS writes
     * the first byte of each page to 0177570 and the rest to 0177550. */
    if (offset == RAMDISK_IO_DATA       || offset == (RAMDISK_IO_DATA + 1) ||
        offset == RAMDISK_IO_DATA_ALIAS || offset == (RAMDISK_IO_DATA_ALIAS + 1)) {
        if (rd->ram) {
            uint32_t addr = compute_address(rd);
            rd->ram[addr] = value;
            rd->counter++;  /* uint8_t wraps at 256 naturally */
        }
        return;
    }
}
