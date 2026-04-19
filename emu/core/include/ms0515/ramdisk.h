/*
 * ramdisk.h — Expansion board RAM disk (EX0:)
 *
 * Optional expansion board for the Elektronika MS 0515 adding:
 *   - 512 KB RAM disk (1024 RT-11 blocks)
 *   - RS-232C serial interface (two channels)
 *
 * Hardware components (from expansion board schematic):
 *   D2  = КР580ВВ55А (Intel 8255 PPI — Programmable Peripheral Interface)
 *   D14 = К555ИЕ19 (8-bit binary counter for MA00-MA07)
 *   D19-D34 = 16× КР565РУ7Г (256Kx1 DRAM, organized as 2 banks × 8 bits)
 *   D4,D5 = КР1102АП15 (RS-232 line drivers)
 *   D3 = КР580ВИ53 (timer for baud rate generation)
 *
 * Memory organization:
 *   19-bit address space = 512 KB (2 banks × 256 KB)
 *   Address composition:
 *     MA18      = PPI Port B bit 2 (БАНК — bank select)
 *     MA17:MA16 = PPI Port B bits 1:0 (high address)
 *     MA15:MA08 = PPI Port A (middle address byte, "page number")
 *     MA07:MA00 = Internal 8-bit counter D14 (auto-increments on data access)
 *
 * I/O address map:
 *   0177510  PPI Port A read           (offset 0x48 from IO base)
 *   0177512  PPI Port B read           (offset 0x4A)
 *   0177514  PPI Port C read           (offset 0x4C)
 *   0177516  PPI control read          (offset 0x4E)
 *   0177530  PPI Port A write          (offset 0x58)
 *   0177532  PPI Port B write          (offset 0x5A)
 *   0177534  PPI Port C write          (offset 0x5C)
 *   0177536  PPI control write         (offset 0x5E)
 *   0177550  RAM data port read/write  (offset 0x68)
 *   0177570  Data port alias — mirror of 0177550 (address bit 4 ignored by
 *            the on-board decoder).  EX.SYS write routine writes the first
 *            byte of each 256-byte page to 0177570, then the remaining 255
 *            bytes to 0177550.  Both addresses perform identical read/write
 *            with auto-increment of the MA00-MA07 counter.
 *
 * PPI Port B bit definitions (output):
 *   Bits 1:0  MA16-MA17 (high address bits)
 *   Bit  2    БАНК (bank select = MA18)
 *   Bit  3    ДОП ОЗУ (additional RAM flag — active = board present)
 *   Bit  5    СБРОС (reset: sets MA00-MA07 counter to 0)
 *   Bit  7    СТАРТ (start: enables data transfer)
 *
 * Data transfer protocol (from EX.SYS driver analysis):
 *   1. Write MA08-MA15 to PPI Port A
 *   2. Write 0xA0 (СТАРТ|СБРОС) to PPI Port B → resets counter to 0
 *   3. Write address_high | СТАРТ to PPI Port B → sets MA16-MA18
 *   4. Read/write bytes at 0177550 — each access auto-increments counter
 *   5. After 256 bytes (one page), increment Port A and repeat
 *
 * Timing constraint (from hardware docs):
 *   Each bus access must complete within 100 μs (DRAM refresh timing).
 *   The driver disables interrupts (MTPS #340) during page transfers.
 *
 * Sources:
 *   - Expansion board schematics (reference/docs/ext/l1-l6)
 *   - EX.SYS driver disassembly
 */

#ifndef MS0515_RAMDISK_H
#define MS0515_RAMDISK_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ───────────────────────────────────────────────────────────── */

#define RAMDISK_SIZE        (512 * 1024)   /* 512 KB total capacity */
#define RAMDISK_PAGE_SIZE   256            /* Counter wraps at 256 bytes */
#define RAMDISK_BLOCKS      1024           /* RT-11 blocks (512 bytes each) */

/* PPI Port B control bits */
#define RAMDISK_PB_MA16     0x01    /* Address bit MA16 */
#define RAMDISK_PB_MA17     0x02    /* Address bit MA17 */
#define RAMDISK_PB_BANK     0x04    /* Bank select (MA18) */
#define RAMDISK_PB_DOP_RAM  0x08    /* ДОП ОЗУ flag (board present) */
#define RAMDISK_PB_RESET    0x20    /* СБРОС — reset counter to 0 */
#define RAMDISK_PB_START    0x80    /* СТАРТ — enable data transfer */

/* ── I/O port offsets (relative to 0177400 base) ────────────────────────── */

#define RAMDISK_IO_PPI_RD_A     0x48    /* 0177510 */
#define RAMDISK_IO_PPI_RD_B     0x4A    /* 0177512 */
#define RAMDISK_IO_PPI_RD_C     0x4C    /* 0177514 */
#define RAMDISK_IO_PPI_RD_CTRL  0x4E    /* 0177516 */
#define RAMDISK_IO_PPI_WR_A     0x58    /* 0177530 */
#define RAMDISK_IO_PPI_WR_B     0x5A    /* 0177532 */
#define RAMDISK_IO_PPI_WR_C     0x5C    /* 0177534 */
#define RAMDISK_IO_PPI_WR_CTRL  0x5E    /* 0177536 */
#define RAMDISK_IO_DATA         0x68    /* 0177550 */
#define RAMDISK_IO_DATA_ALIAS   0x78    /* 0177570 — mirror used by EX.SYS  */

/* ── RAM disk state ──────────────────────────────────────────────────────── */

typedef struct ms0515_ramdisk {
    bool     enabled;           /* Expansion board present and initialized  */

    /* KR580VV55A (8255 PPI) state */
    uint8_t  ppi_a;             /* Port A latch — MA08-MA15 (page address)  */
    uint8_t  ppi_b;             /* Port B latch — control + MA16-MA18       */
    uint8_t  ppi_c;             /* Port C latch — serial interface signals  */
    uint8_t  ppi_ctrl;          /* PPI control word (mode configuration)    */
    bool     port_a_input;      /* true if Port A configured as input       */
    bool     port_b_input;      /* true if Port B configured as input       */

    /* K555IE19 8-bit binary counter */
    uint8_t  counter;           /* MA00-MA07, auto-increments on data port  */

    /* DRAM backing store */
    uint8_t *ram;               /* 512 KB (malloc'd, NULL if not allocated) */

    /* Optional trace log (shared with board) */
    FILE    *trace;
} ms0515_ramdisk_t;

/* ── Public API ──────────────────────────────────────────────────────────── */

/*
 * ramdisk_init — Initialize the RAM disk state (does not allocate memory).
 */
void ramdisk_init(ms0515_ramdisk_t *rd);

/*
 * ramdisk_enable — Allocate 512 KB and enable the RAM disk.
 *
 * Fills the DRAM with pseudo-random data to simulate real DRAM power-on
 * state (the EX.SYS driver probes for this to distinguish real hardware
 * from bus float).
 */
void ramdisk_enable(ms0515_ramdisk_t *rd);

/*
 * ramdisk_free — Release the backing memory and disable the RAM disk.
 */
void ramdisk_free(ms0515_ramdisk_t *rd);

/*
 * ramdisk_reset — Reset PPI and counter to power-on state.
 *
 * Does NOT clear the DRAM contents (real DRAM retains data until refresh
 * is lost, which doesn't happen on a soft reset).
 */
void ramdisk_reset(ms0515_ramdisk_t *rd);

/*
 * ramdisk_handles — Check if an I/O offset belongs to the RAM disk.
 *
 * Returns true if `offset` (relative to 0177400) is in the RAM disk
 * address range (0177510-0177550).  Use this to route I/O from board.c.
 */
bool ramdisk_handles(uint16_t offset);

/*
 * ramdisk_read — Handle a byte read from a RAM disk I/O port.
 */
uint8_t ramdisk_read(ms0515_ramdisk_t *rd, uint16_t offset);

/*
 * ramdisk_write — Handle a byte write to a RAM disk I/O port.
 */
void ramdisk_write(ms0515_ramdisk_t *rd, uint16_t offset, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif /* MS0515_RAMDISK_H */
