/*
 * floppy.c — WD1793 floppy disk controller
 *
 * Implements the KR1818VG93 (WD1793 clone) FDC used in the MS0515.
 *
 * The WD1793 has four command groups:
 *   Type I  — Restore, Seek, Step, Step-In, Step-Out
 *   Type II — Read Sector, Write Sector
 *   Type III — Read Address, Read Track, Write Track
 *   Type IV — Force Interrupt
 *
 * This implementation covers the subset used by the MS0515 BIOS and
 * standard DOS:  Restore, Seek, Step, Read Sector, Write Sector,
 * and Force Interrupt.
 *
 * Disk images are raw sector dumps in track/side/sector order
 * (no headers or gaps), matching the .img format commonly used
 * for MS0515 disk images.
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

/* ── Internal helpers ────────────────────────────────────────────────────── */

static fdc_drive_t *current_drive(ms0515_floppy_t *fdc)
{
    return &fdc->drives[fdc->selected];
}

/*
 * Compute the byte offset in the disk image for a given track/sector.
 *
 * Each image represents a single side (one logical FD unit), so the
 * side argument does not enter the calculation — side selection is
 * already baked into which logical unit is active.
 *
 * Layout: track 0 sectors 1..10, track 1 sectors 1..10, ...
 */
static long disk_offset(int track, int sector)
{
    return (long)track * FDC_TRACK_SIZE + (long)(sector - 1) * FDC_SECTOR_SIZE;
}

static bool drive_ready(const ms0515_floppy_t *fdc)
{
    const fdc_drive_t *drv = &fdc->drives[fdc->selected];
    return drv->image != NULL && drv->motor_on;
}

static void finish_command_now(ms0515_floppy_t *fdc)
{
    fdc->busy           = false;
    fdc->intrq          = true;
    fdc->status        &= ~FDC_ST_BUSY;
    fdc->pending_finish = false;
    fdc->busy_delay     = 0;
}

/*
 * Mark the current command as done, but keep BUSY asserted for a few
 * ticks so the CPU can observe the rising edge.  The BIOS at 174460
 * writes a command and then waits for BUSY to become 1 before waiting
 * for it to become 0 again — an instantaneous finish would trap it in
 * the first wait loop forever.  We force BUSY into the stored status
 * here so error paths that set status = NOT_READY / RNF still let the
 * CPU observe the rising edge, and then fdc_tick clears it.
 */
static void finish_command(ms0515_floppy_t *fdc)
{
    fdc->busy           = true;
    fdc->status        |= FDC_ST_BUSY;
    fdc->pending_finish = true;
    fdc->busy_delay     = 4;
}

static void update_type1_status(ms0515_floppy_t *fdc)
{
    fdc_drive_t *drv = current_drive(fdc);

    fdc->status = 0;

    if (fdc->busy)
        fdc->status |= FDC_ST_BUSY;

    if (drv->track == 0)
        fdc->status |= FDC_ST_TRACK0;

    if (!drive_ready(fdc))
        fdc->status |= FDC_ST_NOT_READY;

    if (drv->read_only)
        fdc->status |= FDC_ST_WRITE_PROT;

    /* Head is always loaded when motor is on */
    if (drv->motor_on)
        fdc->status |= FDC_ST_HEAD_LOADED;
}

/* ── Read a sector from the disk image into the buffer ───────────────────── */

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
    if (n != FDC_SECTOR_SIZE) {
        /* Partial read — zero-fill the rest (image may be truncated) */
        memset(fdc->buffer + n, 0, FDC_SECTOR_SIZE - n);
    }

    fdc->buf_pos = 0;
    fdc->buf_len = FDC_SECTOR_SIZE;
    return true;
}

/* ── Write the buffer to a sector on the disk image ──────────────────────── */

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

/* ── Command dispatch ────────────────────────────────────────────────────── */

static void cmd_restore(ms0515_floppy_t *fdc)
{
    fdc_drive_t *drv = current_drive(fdc);
    drv->track      = 0;
    fdc->track_reg  = 0;
    update_type1_status(fdc);
    finish_command(fdc);
}

static void cmd_seek(ms0515_floppy_t *fdc)
{
    fdc_drive_t *drv = current_drive(fdc);

    /* The data register holds the desired track number */
    int target = fdc->data_reg;
    if (target >= FDC_TRACKS)
        target = FDC_TRACKS - 1;

    drv->track     = target;
    fdc->track_reg = (uint8_t)target;

    update_type1_status(fdc);
    finish_command(fdc);
}

static void cmd_step(ms0515_floppy_t *fdc, int direction)
{
    fdc_drive_t *drv = current_drive(fdc);

    drv->track += direction;
    if (drv->track < 0)
        drv->track = 0;
    if (drv->track >= FDC_TRACKS)
        drv->track = FDC_TRACKS - 1;

    /* Update track register if T flag (bit 4) is set */
    if (fdc->command & 0x10)
        fdc->track_reg = (uint8_t)drv->track;

    update_type1_status(fdc);
    finish_command(fdc);
}

static void cmd_read(ms0515_floppy_t *fdc)
{
    if (!drive_ready(fdc)) {
        fdc->status = FDC_ST_NOT_READY;
        finish_command(fdc);
        return;
    }

    if (!read_sector(fdc)) {
        fdc->status = FDC_ST_SEEK_ERROR;
        finish_command(fdc);
        return;
    }

    /* First byte is ready — set DRQ */
    fdc->data_reg = fdc->buffer[fdc->buf_pos++];
    fdc->drq      = true;
    fdc->status   = FDC_ST_BUSY | FDC_ST_INDEX;  /* DRQ bit in Type II */
}

static void cmd_write(ms0515_floppy_t *fdc)
{
    fdc_drive_t *drv = current_drive(fdc);

    if (!drive_ready(fdc)) {
        fdc->status = FDC_ST_NOT_READY;
        finish_command(fdc);
        return;
    }

    if (drv->read_only) {
        fdc->status = FDC_ST_WRITE_PROT;
        finish_command(fdc);
        return;
    }

    /* Prepare buffer for incoming data */
    memset(fdc->buffer, 0, FDC_SECTOR_SIZE);
    fdc->buf_pos = 0;
    fdc->buf_len = FDC_SECTOR_SIZE;

    fdc->drq    = true;
    fdc->status = FDC_ST_BUSY | FDC_ST_INDEX;
}

static void cmd_force_interrupt(ms0515_floppy_t *fdc)
{
    /* Abort any running command immediately */
    fdc->busy = false;
    fdc->drq  = false;

    update_type1_status(fdc);

    /* Condition flags in bits 0-3 determine interrupt behavior.
     * If all zero, no interrupt is generated (just abort). */
    if (fdc->command & 0x0F)
        fdc->intrq = true;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void fdc_init(ms0515_floppy_t *fdc)
{
    memset(fdc, 0, sizeof(*fdc));
    fdc->selected = 0;
}

void fdc_reset(ms0515_floppy_t *fdc)
{
    /* Preserve attached disk images */
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
    fdc->busy           = false;
    fdc->buf_pos        = 0;
    fdc->buf_len        = 0;
    fdc->pending_finish = false;
    fdc->busy_delay     = 0;
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
 * Each side of a physical disk is a separate logical unit.  The OS
 * numbers them as DZ = side * 2 + drive:
 *   FD0 = DZ0 = drive 0 side 0,  FD1 = DZ1 = drive 1 side 0
 *   FD2 = DZ2 = drive 0 side 1,  FD3 = DZ3 = drive 1 side 1
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
    case 0:
        /* Command register.  If a previous command is still sitting in
         * its busy-delay window, collapse it immediately before the new
         * one overwrites its state. */
        if (fdc->pending_finish)
            finish_command_now(fdc);
        fdc->command = value;
        fdc->intrq   = false;

        /* Decode command from upper nibble */
        switch (value & 0xF0) {
        case CMD_RESTORE:
            fdc->busy = true;
            fdc->status = FDC_ST_BUSY;
            cmd_restore(fdc);
            break;

        case CMD_SEEK:
            fdc->busy = true;
            fdc->status = FDC_ST_BUSY;
            cmd_seek(fdc);
            break;

        case CMD_STEP:
        case 0x30:  /* Step with T flag */
            fdc->busy = true;
            fdc->status = FDC_ST_BUSY;
            /* Step in the last direction — default to +1 */
            cmd_step(fdc, 1);
            break;

        case CMD_STEP_IN:
        case 0x50:
            fdc->busy = true;
            fdc->status = FDC_ST_BUSY;
            cmd_step(fdc, 1);
            break;

        case CMD_STEP_OUT:
        case 0x70:
            fdc->busy = true;
            fdc->status = FDC_ST_BUSY;
            cmd_step(fdc, -1);
            break;

        case CMD_READ_SECTOR:
        case 0x90:
            fdc->busy = true;
            fdc->status = FDC_ST_BUSY;
            cmd_read(fdc);
            break;

        case CMD_WRITE_SECTOR:
        case 0xB0:
            fdc->busy = true;
            fdc->status = FDC_ST_BUSY;
            cmd_write(fdc);
            break;

        case CMD_READ_ADDRESS: {
            /*
             * Type III Read Address — finds the next ID field on the
             * current track and reports its header bytes.  Per the
             * WD1793 datasheet, the Track Address of the ID field is
             * also loaded into the Sector register as a side effect.
             *
             * The MS0515 BIOS at 174010 uses this as a head-position
             * self-test: it issues READ_ADDRESS, reads the sector reg,
             * seeks, steps, and verifies the reported track number
             * matches what it expects.  So we must write the current
             * drive track into sector_reg here — otherwise the BIOS
             * sees a stale value and aborts to "NGMD not ready".
             */
            fdc->busy   = true;
            fdc->status = FDC_ST_BUSY;
            if (drive_ready(fdc)) {
                fdc_drive_t *drv = current_drive(fdc);
                fdc->sector_reg = (uint8_t)drv->track;
            } else {
                fdc->status |= FDC_ST_SEEK_ERROR | FDC_ST_NOT_READY;
            }
            finish_command(fdc);
            break;
        }

        case CMD_FORCE_INT:
            cmd_force_interrupt(fdc);
            break;

        default:
            /* Unsupported command — still arm BUSY so the BIOS's poll
             * loop (wait for BUSY to rise, then fall) can make progress. */
            fdc->busy   = true;
            fdc->status = FDC_ST_BUSY;
            finish_command(fdc);
            break;
        }
        break;

    case 1:
        fdc->track_reg = value;
        break;

    case 2:
        fdc->sector_reg = value;
        break;

    case 3:
        fdc->data_reg = value;
        fdc->drq = false;

        /* If a write command is active, store the byte */
        if (fdc->busy && (fdc->command & 0xE0) == CMD_WRITE_SECTOR) {
            if (fdc->buf_pos < FDC_SECTOR_SIZE) {
                fdc->buffer[fdc->buf_pos++] = value;

                if (fdc->buf_pos >= FDC_SECTOR_SIZE) {
                    /* Sector complete — write to disk */
                    if (!write_sector(fdc))
                        fdc->status |= FDC_ST_SEEK_ERROR;

                    finish_command(fdc);
                } else {
                    fdc->drq = true;
                }
            }
        }
        break;
    }
}

uint8_t fdc_read(ms0515_floppy_t *fdc, int reg)
{
    switch (reg) {
    case 0: {
        /* Reading status clears INTRQ.  The DRQ bit (0x02 in Type II
         * commands — shared with INDEX in Type I) is kept in sync with
         * the fdc->drq flag, which is toggled byte-by-byte by the data
         * register read/write path.
         *
         * Bit 7 (NOT READY) must dynamically reflect the current drive
         * READY signal on every read, regardless of whether a command
         * is in progress — the BIOS poll loop at 163724 waits for it
         * to clear before issuing a Restore/Seek. */
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
        /* WD1793 semantics: reading the data register returns the byte
         * the controller has already latched, then prefetches the next
         * one.  cmd_read() primes data_reg with buffer[0] and sets
         * buf_pos=1, so we must *return* the current data_reg first and
         * *then* advance — otherwise the very first byte of every
         * sector is silently dropped. */
        uint8_t ret = fdc->data_reg;
        fdc->drq = false;
        if (fdc->busy && (fdc->command & 0xE0) == CMD_READ_SECTOR) {
            if (fdc->buf_pos < fdc->buf_len) {
                fdc->data_reg = fdc->buffer[fdc->buf_pos++];
                fdc->drq = true;
            } else {
                /* Last latched byte has just been consumed */
                finish_command(fdc);
            }
        }
        return ret;
    }

    default:
        return 0;
    }
}

void fdc_tick(ms0515_floppy_t *fdc)
{
    /*
     * Drive the deferred-finish countdown.  Type I commands complete
     * synchronously inside fdc_write() but the BIOS expects BUSY to be
     * observably set before it clears — so finish_command() arms a
     * short delay and the real clearing happens here.
     */
    if (fdc->pending_finish) {
        if (fdc->busy_delay > 0)
            fdc->busy_delay--;
        if (fdc->busy_delay == 0)
            finish_command_now(fdc);
    }
}
