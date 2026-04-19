/*
 * floppy.h — Floppy disk controller (KR1818VG93 / WD1793)
 *
 * The MS0515 uses a KR1818VG93, which is a Soviet clone of the Western
 * Digital WD1793 floppy disk controller (FDC).  It supports MFM encoding
 * and interfaces with 5.25" floppy drives (QD format, 80 tracks,
 * double-sided, 10 sectors/track, 512 bytes/sector).
 *
 * The BIOS treats each side of a physical floppy as an independent
 * logical unit (NS4 section 4.11).  The full 2-bit unit index (0..3) is
 * written into bits 1:0 of System Register A and selects one of four
 * logical floppy slots, referred to here as FD0..FD3.  (The RT-11/OSA
 * driver naming — DZ, MZ, MY, MD — belongs to the OS layer, not the
 * hardware, so we avoid it here.)
 *
 * Each disk image represents one side only — 80 × 10 × 512 = 409600
 * bytes — and we keep four independent mount slots.
 *
 * Register addresses on the MS0515 bus:
 *   0177640  Status (read) / Command (write)
 *   0177642  Track register
 *   0177644  Sector register
 *   0177646  Data register
 *
 * Drive selection and motor control are handled by System Register A
 * (port 0177600, bits 0–3), per NS4 section 4.8 (Рис.15):
 *   Bits 1-0  Physical drive select (0..3)
 *   Bit  2    Motor on (0 = on, active low)
 *   Bit  3    Side select (0 = lower, 1 = upper)
 *
 * Logical unit = side * 2 + drive (matches OS DZ numbering):
 *   FD0 = DZ0 = drive 0, side 0    FD1 = DZ1 = drive 1, side 0
 *   FD2 = DZ2 = drive 0, side 1    FD3 = DZ3 = drive 1, side 1
 *
 * FDC status is readable through System Register B (port 0177602):
 *   Bit  0    INTRQ (0 = FDC ready for command)
 *   Bit  1    DRQ   (1 = data byte ready)
 *   Bit  2    Ready signal (0 = drive ready)
 *
 * The FDC does not generate CPU interrupts via the interrupt controller;
 * instead, the CPU polls the DRQ/INTRQ bits in System Register B.
 *
 * Sources:
 *   - WD1793 datasheet
 *   - NS4 technical description, sections 4.8 (system registers), 4.10
 *   - MAME driver: src/mame/ussr/ms0515.cpp
 */

#ifndef MS0515_FLOPPY_H
#define MS0515_FLOPPY_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── WD1793 Status register bits (Type I commands) ────────────────────────── */

#define FDC_ST_BUSY         0x01   /* Command in progress                   */
#define FDC_ST_INDEX        0x02   /* Index pulse (Type I) / DRQ (Type II)  */
#define FDC_ST_TRACK0       0x04   /* Head at track 0 (Type I)              */
#define FDC_ST_CRC_ERROR    0x08   /* CRC error                             */
#define FDC_ST_SEEK_ERROR   0x10   /* Seek error (track not found)          */
#define FDC_ST_HEAD_LOADED  0x20   /* Head loaded (Type I)                  */
#define FDC_ST_WRITE_PROT   0x40   /* Write protected                       */
#define FDC_ST_NOT_READY    0x80   /* Drive not ready                       */

/* ── Disk geometry ────────────────────────────────────────────────────────── */

#define FDC_TRACKS          80
#define FDC_SIDES           2
#define FDC_SECTORS         10
#define FDC_SECTOR_SIZE     512
#define FDC_TRACK_SIZE      (FDC_SECTORS * FDC_SECTOR_SIZE)
/* One image = one logical device = one side */
#define FDC_DISK_SIZE       (FDC_TRACKS * FDC_TRACK_SIZE)
#define FDC_LOGICAL_UNITS   4

/* ── Drive state ──────────────────────────────────────────────────────────── */

typedef struct {
    FILE    *image;             /* Disk image file handle (NULL = empty)     */
    bool     read_only;         /* Write protection flag                    */
    bool     motor_on;          /* Motor is spinning                        */
    int      track;             /* Current track position (0–79)            */
} fdc_drive_t;

/* ── FDC state ────────────────────────────────────────────────────────────── */

typedef struct ms0515_floppy {
    fdc_drive_t drives[FDC_LOGICAL_UNITS];  /* FD0..FD3, one per side       */
    int         selected;       /* Currently selected logical unit (0..3)   */

    /* WD1793 registers */
    uint8_t  status;            /* Status register (read-only)              */
    uint8_t  command;           /* Last command written                     */
    uint8_t  track_reg;         /* Track register                           */
    uint8_t  sector_reg;        /* Sector register                          */
    uint8_t  data_reg;          /* Data register                            */

    /* DMA / data transfer state */
    bool     drq;               /* Data Request — byte ready for transfer   */
    bool     intrq;             /* Interrupt Request — command complete     */
    bool     busy;              /* Command in progress                      */

    /* Sector buffer for read/write operations */
    uint8_t  buffer[FDC_SECTOR_SIZE];
    int      buf_pos;           /* Current position within buffer           */
    int      buf_len;           /* Number of valid bytes in buffer          */

    /* Deferred-finish state — real hardware keeps BUSY set for at least
     * one sampling period after a command is issued.  The BIOS polls the
     * status register waiting for BUSY to go high, then low.  Type I
     * commands complete synchronously in fdc_write(); we delay the
     * finish by a few fdc_tick() calls so the CPU observes the rising
     * edge of BUSY. */
    bool     pending_finish;
    int      busy_delay;
} ms0515_floppy_t;

/* ── Public API ───────────────────────────────────────────────────────────── */

void    fdc_init(ms0515_floppy_t *fdc);
void    fdc_reset(ms0515_floppy_t *fdc);

/*
 * fdc_attach — Attach a disk image file to a logical unit (FD0..FD3).
 *
 * `unit` selects FD0..FD3 directly.  Each image represents one side of
 * a physical disk and must be FDC_DISK_SIZE bytes.  Returns true on
 * success.  The file is kept open until detach.
 */
bool    fdc_attach(ms0515_floppy_t *fdc, int unit, const char *path,
                   bool read_only);

/*
 * fdc_detach — Remove the disk image from a logical unit (FD0..FD3).
 */
void    fdc_detach(ms0515_floppy_t *fdc, int unit);

/*
 * fdc_select — Set the active logical floppy unit.
 *
 * Called when System Register A changes.  `drive` is the physical drive
 * number from bits 1:0, `side` is the side from bit 3.  The logical
 * unit is computed as drive * 2 + side.
 */
void    fdc_select(ms0515_floppy_t *fdc, int drive, int side, bool motor);

/*
 * fdc_write — CPU writes to an FDC register.
 *   `reg` = 0: command, 1: track, 2: sector, 3: data
 */
void    fdc_write(ms0515_floppy_t *fdc, int reg, uint8_t value);

/*
 * fdc_read — CPU reads from an FDC register.
 *   `reg` = 0: status, 1: track, 2: sector, 3: data
 */
uint8_t fdc_read(ms0515_floppy_t *fdc, int reg);

/*
 * fdc_tick — Advance FDC state by one step.
 *
 * Called periodically to process ongoing commands (seek, read, write).
 */
void    fdc_tick(ms0515_floppy_t *fdc);

#ifdef __cplusplus
}
#endif

#endif /* MS0515_FLOPPY_H */
