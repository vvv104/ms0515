/*
 * floppy.c — WD1793 floppy disk controller (asynchronous state machine).
 *
 * Implements the KR1818VG93 (WD1793 clone) FDC used in the MS0515.
 *
 * Commands run asynchronously via the state machine in fdc_tick:
 *
 *   IDLE  ──── fdc_write(cmd) ────►  TYPE1_STEP / TYPE2_SEARCH / FINISH
 *   TYPE1_STEP  ──(N pulses, step_rate_cycles each)──►  FINISH
 *   TYPE2_SEARCH  ──(SEARCH_CYCLES)──►  TYPE2_DATA
 *   TYPE2_DATA  ──(BYTE_CYCLES per byte * 512)──►  FINISH
 *   FINISH  ──(MIN_CMD_CYCLES)──►  IDLE (clears BUSY, asserts INTRQ)
 *
 * BUSY is held naturally throughout the active states so the CPU's
 * "wait for BUSY=1, then BUSY=0" poll loops observe both edges.
 *
 * Step rate honours the WD1793 datasheet command bits 1:0 at the FDC's
 * 1 MHz clock (6/12/20/30 ms), translated to CPU cycles at 7.5 MHz.
 *
 * Sources:
 *   - WD1793 datasheet (Western Digital, 1983)
 *   - NS4 technical description, sections 4.8, 4.10
 */

#include <ms0515/floppy.h>
#include <string.h>

/* ── WD1793 command codes (upper nibble) ─────────────────────────────────── */

#define CMD_RESTORE       0x00
#define CMD_SEEK          0x10
#define CMD_STEP          0x20
#define CMD_STEP_IN       0x40
#define CMD_STEP_OUT      0x60
#define CMD_READ_SECTOR   0x80
#define CMD_WRITE_SECTOR  0xA0
#define CMD_READ_ADDRESS  0xC0
#define CMD_FORCE_INT     0xD0

/* ── Timing constants (CPU cycles at 7.5 MHz) ────────────────────────────── */

/* WD1793 step rate from cmd bits 1:0 (datasheet, FDC clock = 1 MHz):
 *   00 = 6 ms,  01 = 12 ms,  10 = 20 ms,  11 = 30 ms.
 * At 7.5 MHz CPU clock these become 45000 / 90000 / 150000 / 225000. */
static const int step_rate_table[4] = { 45000, 90000, 150000, 225000 };

/* Head settle when the h flag (bit 2) is set on a Type I command: 15 ms. */
#define SETTLE_CYCLES        112500

/* Type II command-to-first-DRQ delay.  A real 5.25" drive at 300 RPM has
 * ~100 ms average rotational latency before the requested ID arrives.
 * We use a smaller value to keep boot times tolerable while still letting
 * the CPU run in parallel with the search. */
#define TYPE2_SEARCH_CYCLES  37500   /* ~5 ms */

/* Time per Type II data byte at 250 kbit/s MFM = 32 µs = 240 cycles. */
#define BYTE_CYCLES          240

/* Post-data delay before INTRQ asserts (CRC + post-amble window). */
#define TYPE2_FINISH_CYCLES  240

/* Minimum BUSY-observable phase for fast-finishing commands.  BIOS
 * polls "BUSY high then low"; if BUSY clears before the CPU sees the
 * rising edge, the loop hangs.  At 7.5 MHz, 240 cycles ≈ 32 µs. */
#define MIN_CMD_CYCLES       240

/* ── Internal helpers ────────────────────────────────────────────────────── */

static fdc_drive_t *current_drive(ms0515_floppy_t *fdc)
{
    return &fdc->drives[fdc->selected];
}

/* Compute byte offset in the disk image for a given track/sector.
 * Each image is one side, so layout is: track 0 sectors 1..10, track 1
 * sectors 1..10, ... */
static long disk_offset(int track, int sector)
{
    return (long)track * FDC_TRACK_SIZE + (long)(sector - 1) * FDC_SECTOR_SIZE;
}

static bool drive_ready(const ms0515_floppy_t *fdc)
{
    const fdc_drive_t *drv = &fdc->drives[fdc->selected];
    return drv->image != NULL && drv->motor_on;
}

/* Compose a Type I status byte.  Bit 1 (FDC_ST_INDEX) is omitted here —
 * fdc_read() derives it from fdc->drq for Type II commands and we don't
 * model the index pulse for Type I. */
static uint8_t type1_status(const ms0515_floppy_t *fdc, bool busy)
{
    const fdc_drive_t *drv = &fdc->drives[fdc->selected];
    uint8_t s = 0;
    if (busy)             s |= FDC_ST_BUSY;
    if (drv->track == 0)  s |= FDC_ST_TRACK0;
    if (!drive_ready(fdc)) s |= FDC_ST_NOT_READY;
    if (drv->read_only)   s |= FDC_ST_WRITE_PROT;
    if (drv->motor_on)    s |= FDC_ST_HEAD_LOADED;
    return s;
}

/* Read a sector from the disk image into the buffer. */
static bool read_sector(ms0515_floppy_t *fdc)
{
    fdc_drive_t *drv = current_drive(fdc);
    if (!drv->image)
        return false;
    if (fdc->sector_reg < 1 || fdc->sector_reg > FDC_SECTORS)
        return false;

    long offset = disk_offset(drv->track, fdc->sector_reg);
    if (fseek(drv->image, offset, SEEK_SET) != 0)
        return false;

    size_t n = fread(fdc->buffer, 1, FDC_SECTOR_SIZE, drv->image);
    if (n != FDC_SECTOR_SIZE)
        memset(fdc->buffer + n, 0, FDC_SECTOR_SIZE - n);

    fdc->buf_pos = 0;
    fdc->buf_len = FDC_SECTOR_SIZE;
    return true;
}

/* Flush the buffer to a sector on the disk image. */
static bool write_sector(ms0515_floppy_t *fdc)
{
    fdc_drive_t *drv = current_drive(fdc);
    if (!drv->image || drv->read_only)
        return false;
    if (fdc->sector_reg < 1 || fdc->sector_reg > FDC_SECTORS)
        return false;

    long offset = disk_offset(drv->track, fdc->sector_reg);
    if (fseek(drv->image, offset, SEEK_SET) != 0)
        return false;

    size_t n = fwrite(fdc->buffer, 1, FDC_SECTOR_SIZE, drv->image);
    fflush(drv->image);
    return n == FDC_SECTOR_SIZE;
}

/* Schedule the FINISH phase: BUSY stays asserted for `cycles` more CPU
 * cycles, then `final_status` (with BUSY masked off) is applied and
 * INTRQ is raised. */
static void schedule_finish(ms0515_floppy_t *fdc, uint8_t final_status,
                            int cycles)
{
    if (cycles < MIN_CMD_CYCLES)
        cycles = MIN_CMD_CYCLES;
    fdc->state            = FDC_STATE_FINISH;
    fdc->cycles_remaining = cycles;
    fdc->next_status      = final_status & ~FDC_ST_BUSY;
    fdc->status           = FDC_ST_BUSY | (final_status & ~FDC_ST_BUSY);
    fdc->drq              = false;
}

/* ── Command latch ───────────────────────────────────────────────────────── */

/* Begin a Type I (head positioning) command.  Computes the number of
 * step pulses required and arms the state machine — actual head motion
 * is performed pulse-by-pulse inside fdc_tick. */
static void start_type1(ms0515_floppy_t *fdc, int target_track)
{
    fdc_drive_t *drv = current_drive(fdc);
    if (target_track < 0)              target_track = 0;
    if (target_track >= FDC_TRACKS)    target_track = FDC_TRACKS - 1;

    int delta  = target_track - drv->track;
    int pulses = delta < 0 ? -delta : delta;

    if (delta != 0)
        fdc->step_direction = (delta < 0) ? -1 : 1;
    /* delta == 0: keep previous step_direction (matters for plain STEP) */

    fdc->step_pulses_left = pulses;
    fdc->step_rate_cycles = step_rate_table[fdc->command & 0x03];
    fdc->settle_cycles    = (fdc->command & 0x04) ? SETTLE_CYCLES : 0;

    if (pulses == 0) {
        /* Already on target — go straight to settle/finish. */
        schedule_finish(fdc, type1_status(fdc, false), fdc->settle_cycles);
    } else {
        fdc->state            = FDC_STATE_TYPE1_STEP;
        fdc->cycles_remaining = fdc->step_rate_cycles;
        fdc->status           = type1_status(fdc, true);
    }
}

/* Begin a Type II (read/write sector) command. */
static void start_type2(ms0515_floppy_t *fdc, bool writing)
{
    if (!drive_ready(fdc)) {
        schedule_finish(fdc, FDC_ST_NOT_READY, MIN_CMD_CYCLES);
        return;
    }
    if (writing && current_drive(fdc)->read_only) {
        schedule_finish(fdc, FDC_ST_WRITE_PROT, MIN_CMD_CYCLES);
        return;
    }

    if (writing)
        memset(fdc->buffer, 0, FDC_SECTOR_SIZE);
    fdc->buf_pos = 0;
    fdc->buf_len = FDC_SECTOR_SIZE;

    fdc->state            = FDC_STATE_TYPE2_SEARCH;
    fdc->cycles_remaining = TYPE2_SEARCH_CYCLES;
    fdc->status           = FDC_ST_BUSY;
}

/* Force Interrupt — abort any running command immediately. */
static void cmd_force_interrupt(ms0515_floppy_t *fdc)
{
    fdc->state            = FDC_STATE_IDLE;
    fdc->cycles_remaining = 0;
    fdc->busy             = false;
    fdc->drq              = false;
    fdc->status           = type1_status(fdc, false);
    /* Condition flags in bits 0-3: nonzero = generate INTRQ now. */
    if (fdc->command & 0x0F)
        fdc->intrq = true;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void fdc_init(ms0515_floppy_t *fdc)
{
    memset(fdc, 0, sizeof(*fdc));
    fdc->selected         = 0;
    fdc->step_direction   = 1;
    fdc->step_rate_cycles = step_rate_table[0];
}

void fdc_reset(ms0515_floppy_t *fdc)
{
    /* Preserve attached disk images. */
    for (int i = 0; i < FDC_LOGICAL_UNITS; i++) {
        fdc->drives[i].track    = 0;
        fdc->drives[i].motor_on = false;
    }

    fdc->status     = 0;
    fdc->command    = 0;
    fdc->track_reg  = 0;
    fdc->sector_reg = 1;
    fdc->data_reg   = 0;
    fdc->drq        = false;
    fdc->intrq      = false;
    fdc->busy       = false;
    fdc->buf_pos    = 0;
    fdc->buf_len    = 0;

    fdc->state            = FDC_STATE_IDLE;
    fdc->cycles_remaining = 0;
    fdc->step_pulses_left = 0;
    fdc->step_direction   = 1;
    fdc->step_rate_cycles = step_rate_table[0];
    fdc->settle_cycles    = 0;
    fdc->next_status      = 0;
}

bool fdc_attach(ms0515_floppy_t *fdc, int unit, const char *path,
                bool read_only)
{
    if (unit < 0 || unit >= FDC_LOGICAL_UNITS)
        return false;

    fdc_detach(fdc, unit);

    const char *mode = read_only ? "rb" : "r+b";
    FILE *f = fopen(path, mode);
    if (!f)
        return false;

    fdc->drives[unit].image     = f;
    fdc->drives[unit].read_only = read_only;
    fdc->drives[unit].track     = 0;
    return true;
}

void fdc_detach(ms0515_floppy_t *fdc, int unit)
{
    if (unit < 0 || unit >= FDC_LOGICAL_UNITS)
        return;

    if (fdc->drives[unit].image) {
        fclose(fdc->drives[unit].image);
        fdc->drives[unit].image = NULL;
    }
    fdc->drives[unit].read_only = false;
}

/*
 * Select the active logical floppy unit.
 *
 * Per NS4 section 4.8 (Рис.15), System Register A encodes:
 *   bits 1:0  — physical drive number (0..3)
 *   bit  3    — side select (0 = lower, 1 = upper)
 *
 * Each side of a physical disk is a separate logical unit, numbered
 * by the OS as side*2 + drive:
 *   FD0 = drive 0 side 0,  FD1 = drive 1 side 0
 *   FD2 = drive 0 side 1,  FD3 = drive 1 side 1
 */
void fdc_select(ms0515_floppy_t *fdc, int drive, int side, bool motor)
{
    int unit = side * 2 + drive;
    if (unit < 0 || unit >= FDC_LOGICAL_UNITS)
        return;

    fdc->selected = unit;
    fdc->drives[unit].motor_on = motor;
}

void fdc_write(ms0515_floppy_t *fdc, int reg, uint8_t value)
{
    switch (reg) {
    case 0: {
        uint8_t group = value & 0xF0;

        /* Force Interrupt is honoured even mid-command. */
        if (group == CMD_FORCE_INT) {
            fdc->command = value;
            cmd_force_interrupt(fdc);
            return;
        }

        /* If a previous command is still running, drop it on the floor —
         * issuing a new command without first issuing Force Interrupt is
         * a programming error on real hardware too. */
        if (fdc->state != FDC_STATE_IDLE) {
            fdc->state            = FDC_STATE_IDLE;
            fdc->cycles_remaining = 0;
            fdc->drq              = false;
        }

        fdc->command = value;
        fdc->intrq   = false;
        fdc->drq     = false;
        fdc->busy    = true;
        fdc->status  = FDC_ST_BUSY;

        switch (group) {
        case CMD_RESTORE:
            start_type1(fdc, 0);
            break;

        case CMD_SEEK:
            start_type1(fdc, fdc->data_reg);
            break;

        case CMD_STEP:
        case 0x30: {
            fdc_drive_t *drv = current_drive(fdc);
            start_type1(fdc, drv->track + fdc->step_direction);
            break;
        }

        case CMD_STEP_IN:
        case 0x50: {
            fdc_drive_t *drv = current_drive(fdc);
            fdc->step_direction = 1;
            start_type1(fdc, drv->track + 1);
            break;
        }

        case CMD_STEP_OUT:
        case 0x70: {
            fdc_drive_t *drv = current_drive(fdc);
            fdc->step_direction = -1;
            start_type1(fdc, drv->track - 1);
            break;
        }

        case CMD_READ_SECTOR:
        case 0x90:
            start_type2(fdc, /*writing=*/false);
            break;

        case CMD_WRITE_SECTOR:
        case 0xB0:
            start_type2(fdc, /*writing=*/true);
            break;

        case CMD_READ_ADDRESS: {
            /*
             * Type III Read Address — the BIOS at 174010 uses this as a
             * head-position self-test: it checks that the sector
             * register, after the command, equals the current track
             * number (the WD1793 deposits the ID's track byte into the
             * sector register as a side effect).  We replicate just
             * that side effect; the rest of the ID bytes go unread.
             */
            if (drive_ready(fdc)) {
                fdc_drive_t *drv = current_drive(fdc);
                fdc->sector_reg = (uint8_t)drv->track;
                schedule_finish(fdc, 0, MIN_CMD_CYCLES);
            } else {
                schedule_finish(fdc, FDC_ST_SEEK_ERROR | FDC_ST_NOT_READY,
                                MIN_CMD_CYCLES);
            }
            break;
        }

        default:
            /* Unsupported command — finish quickly so the BIOS poll
             * (BUSY high, then low) can make progress. */
            schedule_finish(fdc, 0, MIN_CMD_CYCLES);
            break;
        }
        break;
    }

    case 1:
        fdc->track_reg = value;
        break;

    case 2:
        fdc->sector_reg = value;
        break;

    case 3:
        fdc->data_reg = value;
        fdc->drq      = false;

        /* For a write in progress, latch the byte at the current buffer
         * position.  The state machine advances buf_pos when the byte
         * timer expires. */
        if (fdc->state == FDC_STATE_TYPE2_DATA &&
            (fdc->command & 0xE0) == CMD_WRITE_SECTOR &&
            fdc->buf_pos < FDC_SECTOR_SIZE) {
            fdc->buffer[fdc->buf_pos] = value;
        }
        break;
    }
}

uint8_t fdc_read(ms0515_floppy_t *fdc, int reg)
{
    switch (reg) {
    case 0: {
        /*
         * Reading status clears INTRQ.  Bit 1 (FDC_ST_INDEX) is shared
         * between Type I (Index pulse) and Type II (DRQ); we drive it
         * from fdc->drq for both kinds.  Bit 7 (NOT READY) must
         * dynamically reflect the drive READY signal even between
         * commands — the BIOS poll loop at 163724 waits for it to clear.
         */
        fdc->intrq = false;
        uint8_t s = fdc->status & ~FDC_ST_INDEX;
        if (fdc->drq)
            s |= FDC_ST_INDEX;
        if (drive_ready(fdc))
            s &= ~FDC_ST_NOT_READY;
        else
            s |= FDC_ST_NOT_READY;
        return s;
    }

    case 1:
        return fdc->track_reg;

    case 2:
        return fdc->sector_reg;

    case 3: {
        /*
         * Latch the current data byte and clear DRQ.  The next byte
         * will be supplied by the state machine when its byte timer
         * elapses (BYTE_CYCLES later).  This is the key difference
         * from the previous synchronous model: the CPU now actually
         * waits between bytes, freeing it for parallel work between
         * polls if the OS has any.
         */
        uint8_t ret = fdc->data_reg;
        fdc->drq    = false;
        return ret;
    }

    default:
        return 0;
    }
}

/* ── State machine ───────────────────────────────────────────────────────── */

void fdc_tick(ms0515_floppy_t *fdc, int cycles)
{
    if (fdc->state == FDC_STATE_IDLE)
        return;

    fdc->cycles_remaining -= cycles;

    while (fdc->cycles_remaining <= 0 && fdc->state != FDC_STATE_IDLE) {
        switch (fdc->state) {

        case FDC_STATE_TYPE1_STEP: {
            fdc_drive_t *drv      = current_drive(fdc);
            uint8_t      cmd_grp  = fdc->command & 0xF0;

            /* Issue one step pulse. */
            drv->track += fdc->step_direction;
            if (drv->track < 0)              drv->track = 0;
            if (drv->track >= FDC_TRACKS)    drv->track = FDC_TRACKS - 1;
            fdc->step_pulses_left--;

            /*
             * Track register update rules per WD1793 datasheet:
             *   Restore : forced to 0 each pulse
             *   Seek    : always tracks the head position
             *   Step*   : updated only if the T flag (bit 4) is set
             */
            if (cmd_grp == CMD_RESTORE)
                fdc->track_reg = 0;
            else if (cmd_grp == CMD_SEEK)
                fdc->track_reg = (uint8_t)drv->track;
            else if (fdc->command & 0x10)
                fdc->track_reg = (uint8_t)drv->track;

            if (fdc->step_pulses_left > 0) {
                fdc->cycles_remaining += fdc->step_rate_cycles;
                fdc->status            = type1_status(fdc, true);
            } else {
                /* Last pulse — settle, then INTRQ. */
                schedule_finish(fdc, type1_status(fdc, false),
                                fdc->settle_cycles);
            }
            break;
        }

        case FDC_STATE_TYPE2_SEARCH: {
            uint8_t cmd_grp = fdc->command & 0xE0;
            if (cmd_grp == CMD_READ_SECTOR) {
                if (!read_sector(fdc)) {
                    schedule_finish(fdc, FDC_ST_SEEK_ERROR, MIN_CMD_CYCLES);
                    break;
                }
                fdc->data_reg = fdc->buffer[fdc->buf_pos++];
                fdc->drq      = true;
                fdc->status   = FDC_ST_BUSY;
                fdc->state    = FDC_STATE_TYPE2_DATA;
                fdc->cycles_remaining += BYTE_CYCLES;
            } else { /* CMD_WRITE_SECTOR */
                fdc->buf_pos          = 0;
                fdc->drq              = true;
                fdc->status           = FDC_ST_BUSY;
                fdc->state            = FDC_STATE_TYPE2_DATA;
                fdc->cycles_remaining += BYTE_CYCLES;
            }
            break;
        }

        case FDC_STATE_TYPE2_DATA: {
            uint8_t cmd_grp = fdc->command & 0xE0;

            /*
             * If DRQ is still asserted, the CPU hasn't read (or written)
             * the previous byte yet.  Real WD1793 hardware would overwrite
             * the data register and set Lost Data — but that exposes the
             * emulator to a level of cycle-accuracy we don't claim to have:
             * a single OS interrupt longer than 32 µs would clobber the
             * transfer.
             *
             * Instead we defer the byte advance until the CPU keeps up.
             * BYTE_CYCLES becomes the FLOOR of the transfer rate (matching
             * the 250 kbit/s MFM data rate) rather than a hard cadence.
             * Total transfer time = max(real-time-floor, CPU-driven-time),
             * which preserves disk-speed realism for the audio plans
             * without losing data on long ISRs.
             */
            if (fdc->drq) {
                fdc->cycles_remaining += BYTE_CYCLES;
                break;
            }

            if (cmd_grp == CMD_READ_SECTOR) {
                if (fdc->buf_pos < fdc->buf_len) {
                    fdc->data_reg = fdc->buffer[fdc->buf_pos++];
                    fdc->drq      = true;
                    fdc->cycles_remaining += BYTE_CYCLES;
                } else if (fdc->command & 0x10) {
                    /* Multi-sector — advance and continue. */
                    fdc->sector_reg++;
                    if (fdc->sector_reg > FDC_SECTORS) {
                        schedule_finish(fdc, fdc->status, TYPE2_FINISH_CYCLES);
                    } else if (!read_sector(fdc)) {
                        schedule_finish(fdc, fdc->status | FDC_ST_SEEK_ERROR,
                                        MIN_CMD_CYCLES);
                    } else {
                        fdc->data_reg = fdc->buffer[fdc->buf_pos++];
                        fdc->drq      = true;
                        fdc->cycles_remaining += BYTE_CYCLES;
                    }
                } else {
                    schedule_finish(fdc, fdc->status, TYPE2_FINISH_CYCLES);
                }
            } else { /* CMD_WRITE_SECTOR */
                fdc->buf_pos++;
                if (fdc->buf_pos < fdc->buf_len) {
                    fdc->drq               = true;
                    fdc->cycles_remaining += BYTE_CYCLES;
                } else {
                    /* Sector buffer full — flush. */
                    if (!write_sector(fdc)) {
                        schedule_finish(fdc, fdc->status | FDC_ST_SEEK_ERROR,
                                        MIN_CMD_CYCLES);
                        break;
                    }
                    if ((fdc->command & 0x10) && fdc->sector_reg < FDC_SECTORS) {
                        fdc->sector_reg++;
                        memset(fdc->buffer, 0, FDC_SECTOR_SIZE);
                        fdc->buf_pos           = 0;
                        fdc->drq               = true;
                        fdc->cycles_remaining += BYTE_CYCLES;
                    } else {
                        schedule_finish(fdc, fdc->status, TYPE2_FINISH_CYCLES);
                    }
                }
            }
            break;
        }

        case FDC_STATE_FINISH:
            fdc->status           = fdc->next_status;
            fdc->busy             = false;
            fdc->drq              = false;
            fdc->intrq            = true;
            fdc->state            = FDC_STATE_IDLE;
            fdc->cycles_remaining = 0;
            break;

        case FDC_STATE_IDLE:
            /* Unreachable — loop guard checks for IDLE. */
            break;
        }
    }
}
