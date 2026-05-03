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
    long     image_offset;      /* Byte offset of this side's track 0 sec 1 */
    long     track_stride;      /* Bytes between sector 1 of consecutive tracks
                                 * (FDC_TRACK_SIZE for SS, 2×FDC_TRACK_SIZE
                                 * for track-interleaved DS where each track
                                 * occupies a contiguous slot covering both
                                 * sides) */
} fdc_drive_t;

/* ── Asynchronous command state machine ──────────────────────────────────── */

/*
 * The FDC runs commands asynchronously: fdc_write() only latches the
 * command and arms the state machine; fdc_tick() advances it by the
 * supplied cycle count and performs disk I/O when the appropriate
 * phase is reached.  This keeps BUSY asserted for realistic durations
 * (head-stepping, sector search, per-byte data transfer) so the CPU
 * runs in parallel with disk operations the way real hardware allows.
 */
typedef enum {
    FDC_STATE_IDLE = 0,
    FDC_STATE_TYPE1_STEP,    /* Head stepping; cycles_remaining = until next pulse  */
    FDC_STATE_TYPE2_SEARCH,  /* Type II command-to-first-DRQ delay (sector search) */
    FDC_STATE_TYPE2_DATA,    /* Type II per-byte transfer; one byte per BYTE_CYCLES */
    FDC_STATE_FINISH,        /* BUSY held briefly before INTRQ asserts             */
} fdc_state_t;

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

    /* State machine bookkeeping */
    fdc_state_t state;             /* current phase                          */
    int         cycles_remaining;  /* until next state transition            */
    int         step_pulses_left;  /* Type I: remaining step pulses          */
    int         step_direction;    /* -1 or +1 (last commanded direction)    */
    int         step_rate_cycles;  /* armed at command latch (cmd bits 1:0)  */
    int         settle_cycles;     /* head settle delay (h flag in Type I)   */
    uint8_t     next_status;       /* status to apply at FINISH expiry       */
} ms0515_floppy_t;

/* ── Public API ───────────────────────────────────────────────────────────── */

void    fdc_init(ms0515_floppy_t *fdc);
void    fdc_reset(ms0515_floppy_t *fdc);

/*
 * fdc_attach — Attach a disk image file to a logical unit (FD0..FD3).
 *
 * `unit` selects FD0..FD3 directly.  The image layout is picked from
 * the file size:
 *   - FDC_DISK_SIZE (409600 bytes) — single-side image.
 *     image_offset = 0, track_stride = FDC_TRACK_SIZE.
 *   - 2*FDC_DISK_SIZE (819200 bytes) — double-side image in
 *     track-interleaved layout: each track occupies a contiguous
 *     2*FDC_TRACK_SIZE slot, side 0 first then side 1
 *     (T0S0, T0S1, T1S0, T1S1, ...).  Mount the same file to both
 *     side units of a drive: side-0 units (FD0/FD1) get
 *     image_offset = 0, side-1 units (FD2/FD3) get image_offset =
 *     FDC_TRACK_SIZE.  Both share track_stride = 2*FDC_TRACK_SIZE.
 *
 * Track-interleaved is the only DS layout the emulator understands —
 * it matches what raw MS0515 hardware dumps look like.  Other layouts
 * (e.g. side-major concatenations) are not supported; convert with
 * tools/split_double_sided.py first.
 *
 * Returns true on success.  The file is kept open until detach.
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
 * fdc_tick — Advance the FDC state machine by `cycles` CPU cycles.
 *
 * Called by the board after every CPU instruction.  Drives head stepping,
 * sector search latency, per-byte data transfer pacing, and command-end
 * INTRQ assertion.
 */
void    fdc_tick(ms0515_floppy_t *fdc, int cycles);

#ifdef __cplusplus
}
#endif

#endif /* MS0515_FLOPPY_H */
