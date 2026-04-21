/*
 * board.c — MS0515 system board integration
 *
 * Connects all hardware components through the I/O bus and manages
 * the timing relationships between the CPU (7.5 MHz), timer (2 MHz),
 * and video (50/60/72 Hz).
 *
 * The I/O dispatch maps CPU addresses 0177400–0177776 to the
 * appropriate peripheral registers (see board.h for the full map).
 *
 * Sources:
 *   - NS4 technical description, sections 4.1–4.10, Appendix 1
 *   - MAME driver: src/mame/ussr/ms0515.cpp
 */

#include <ms0515/board.h>
#include <stdlib.h>
#include <string.h>

/* ── Timing constants ────────────────────────────────────────────────────── */

/*
 * CPU clock: 7.5 MHz
 * Timer clock: 2 MHz (one timer tick every 3.75 CPU cycles, rounded to 4)
 * VBlank at 50 Hz: 150000 CPU cycles per frame
 */
#define CPU_CLOCK_HZ        7500000
#define TIMER_DIVIDER       4       /* CPU cycles per timer tick (7.5/2 ≈ 4) */
#define FRAME_CYCLES_50HZ   150000  /* 7500000 / 50 */
#define FRAME_CYCLES_60HZ   125000  /* 7500000 / 60 */
#define FRAME_CYCLES_72HZ   104167  /* 7500000 / 72 (approximate) */

/* Keyboard UART clock divider — 4800 baud from 2 MHz needs ~417 timer ticks.
 * We call kbd_tick() every 512 timer ticks for simplicity. */
#define KBD_TICK_DIVIDER    512

/* ── I/O port offsets (relative to 0177400 base) ─────────────────────────── */

/*
 * I/O port offsets — relative to the 0177400 base address.
 * Computed as (octal_address - 0177400), then expressed in decimal/hex.
 *
 * IMPORTANT: PDP-11 addresses are OCTAL.  The offset from 0177400 to
 * 0177500 is 0100 octal = 64 decimal = 0x40, NOT 0x100.
 */
#define IO_MEM_DISPATCHER   0x00    /* 0177400: Memory dispatcher register   */
#define IO_KBD_DATA_R       0x20    /* 0177440: Keyboard RX data (read)      */
#define IO_KBD_STATUS       0x22    /* 0177442: Keyboard status/command      */
#define IO_KBD_DATA_W       0x30    /* 0177460: Keyboard TX data (write)     */
#define IO_KBD_CMD          0x32    /* 0177462: Keyboard command (write)     */
#define IO_TIMER_R_BASE     0x40    /* 0177500: Timer read base              */
#define IO_TIMER_W_BASE     0x50    /* 0177520: Timer write base             */
#define IO_PPI2_BASE        0x60    /* 0177540: MS7007 PPI (parallel kbd)    */
#define IO_REG_A            0x80    /* 0177600: System Register A            */
#define IO_REG_B            0x82    /* 0177602: System Register B            */
#define IO_REG_C            0x84    /* 0177604: System Register C            */
#define IO_PPI_CTRL         0x86    /* 0177606: PPI control word             */
#define IO_FDC_BASE         0xA0    /* 0177640: FDC register base            */
#define IO_SERIAL_DATA_R    0xC0    /* 0177700: Serial RX data (read)        */
#define IO_SERIAL_STATUS    0xC2    /* 0177702: Serial status/command        */
#define IO_SERIAL_DATA_W    0xD0    /* 0177720: Serial TX data (write)       */
#define IO_SERIAL_CMD       0xD2    /* 0177722: Serial command (write)       */
#define IO_HALT_TIMER       0xF8    /* 0177770: Halt/timer service address   */

/* RAM disk I/O is handled by ramdisk.c — offsets defined in ramdisk.h */

/* ── Internal helpers ────────────────────────────────────────────────────── */

static int get_frame_cycles(const ms0515_board_t *board)
{
    switch (board->dip_refresh) {
    case 1:  return FRAME_CYCLES_72HZ;
    case 2:  return FRAME_CYCLES_60HZ;
    default: return FRAME_CYCLES_50HZ;
    }
}

static void update_sound(ms0515_board_t *board)
{
    /*
     * Speaker output path (NS4 tech desc, section 4.8, Fig.17):
     *
     *   Reg C bit 7 (СИНТ): GATE input for timer channel 2.
     *     Managed via apply_reg_c → timer_set_gate().
     *
     *   Reg C bit 6 (РАЗР ГР): sound enable.
     *     When 0, speaker is silent.
     *     When 1, speaker follows timer channel 2 OUT.
     *
     *   Reg C bit 5 (УПР ГР): direct speaker drive.
     *     Software can toggle this bit to produce clicks/tones
     *     without using the timer (like IBM PC port 61 bit 1).
     *
     * The POST melody uses timer-driven mode: programs ch2 in mode 3,
     * sets bits 7+6 to start the tone, clears them for silence.
     * Bit 5 is not used by POST but may be used by games for effects.
     */
    int new_value;

    if (!(board->reg_c & 0x40)) {
        /* Sound disabled — speaker off */
        new_value = 0;
    } else {
        /* Sound enabled — speaker follows timer channel 2 output */
        new_value = timer_get_out(&board->timer, 2) ? 1 : 0;
    }

    if (new_value != board->sound_value) {
        board->sound_value = new_value;
        if (board->sound_cb)
            board->sound_cb(board->cb_userdata, new_value);
    }
}

static void apply_reg_a(ms0515_board_t *board)
{
    /* Floppy drive selection and motor control */
    int drive = board->reg_a & 0x03;
    bool motor = !(board->reg_a & 0x04);   /* Active low */
    int side = (board->reg_a & 0x08) ? 0 : 1;  /* Active low */
    fdc_select(&board->fdc, drive, side, motor);

    /* Extended ROM visibility */
    board->mem.rom_extended = (board->reg_a & 0x80) != 0;
}

static void apply_reg_c(ms0515_board_t *board)
{
    /* Border color */
    board->border_color = board->reg_c & 0x07;

    /* Video resolution */
    board->hires_mode = (board->reg_c & 0x08) != 0;

    /* Timer channel 2 gate — bit 7 */
    timer_set_gate(&board->timer, 2, (board->reg_c & 0x80) != 0);

    update_sound(board);
}

static uint8_t read_reg_b(ms0515_board_t *board)
{
    uint8_t val = 0;

    /* Bit 0: FDC INTRQ (active low: 0 = interrupt pending) */
    if (!board->fdc.intrq)
        val |= 0x01;

    /* Bit 1: FDC DRQ */
    if (board->fdc.drq)
        val |= 0x02;

    /* Bit 2: Drive ready (active low: 0 = ready) */
    if (board->fdc.drives[board->fdc.selected].image &&
        board->fdc.drives[board->fdc.selected].motor_on)
        val &= ~0x04;
    else
        val |= 0x04;

    /* Bits 4-3: DIP switches for refresh rate */
    val |= (board->dip_refresh & 0x03) << 3;

    /* Bit 7: Tape data input.
     * On real hardware with no cassette recorder connected, this pin
     * reads a constant level (no edge transitions).  The ROM's tape
     * routine uses R3 as a countdown timer and exits after ~65535
     * iterations when no transitions are detected — this is the
     * hardware's built-in timeout for absent tape drives. */

    return val;
}

/* ── I/O port dispatch ───────────────────────────────────────────────────── */

static uint8_t io_read_byte(ms0515_board_t *board, uint16_t offset)
{
    /* Memory dispatcher — return low byte of dispatcher register */
    if (offset <= 0x1F)
        return (uint8_t)(board->mem.dispatcher & 0xFF);

    /* Keyboard */
    if (offset == IO_KBD_DATA_R)
        return kbd_read(&board->kbd, 0);
    if (offset == IO_KBD_STATUS)
        return kbd_read(&board->kbd, 1);

    /* Timer read registers (0177500–0177506) */
    if (offset >= IO_TIMER_R_BASE && offset <= (IO_TIMER_R_BASE + 6)) {
        int reg = (offset - IO_TIMER_R_BASE) >> 1;
        return timer_read(&board->timer, reg);
    }

    /* System registers */
    if (offset == IO_REG_A)
        return board->reg_a;
    if (offset == IO_REG_B)
        return read_reg_b(board);
    if (offset == IO_REG_C)
        return board->reg_c;

    /* FDC registers */
    if (offset >= IO_FDC_BASE && offset <= IO_FDC_BASE + 0x06) {
        int reg = (offset - IO_FDC_BASE) >> 1;
        return fdc_read(&board->fdc, reg);
    }

    /* Expansion RAM disk (EX0:) */
    if (board->ramdisk.enabled && ramdisk_handles(offset))
        return ramdisk_read(&board->ramdisk, offset);

    /* Unhandled — return 0 (no bus error emulation yet) */
    return 0;
}

static void io_write_byte(ms0515_board_t *board, uint16_t offset, uint8_t value)
{
    /* Memory dispatcher — write to the full 16-bit register on word writes.
     * For byte writes, update the low byte only. */
    if (offset <= 0x1F) {
        uint16_t old = board->mem.dispatcher;
        board->mem.dispatcher = (board->mem.dispatcher & 0xFF00) | value;
        /* Bit 8: monitor interrupt — edge-triggered on any transition.
         * Per NS4 tech desc: writing 1 initiates interrupt request. */
        if ((board->mem.dispatcher ^ old) & MEM_DISP_MON_IRQ)
            cpu_interrupt(&board->cpu, 2, 064);
        return;
    }

    /* Keyboard */
    if (offset == IO_KBD_DATA_W || offset == IO_KBD_DATA_R) {
        kbd_write(&board->kbd, 0, value);
        return;
    }
    if (offset == IO_KBD_CMD || offset == IO_KBD_STATUS) {
        kbd_write(&board->kbd, 1, value);
        return;
    }

    /* Timer write registers (0177520–0177526) */
    if (offset >= IO_TIMER_W_BASE && offset <= (IO_TIMER_W_BASE + 6)) {
        int reg = (offset - IO_TIMER_W_BASE) >> 1;
        timer_write(&board->timer, reg, value);
        return;
    }

    /* System registers */
    if (offset == IO_REG_A) {
        board->reg_a = value;
        apply_reg_a(board);
        return;
    }
    if (offset == IO_REG_C) {
        board->reg_c = value;
        apply_reg_c(board);
        return;
    }
    if (offset == IO_PPI_CTRL) {
        board->ppi_control = value;
        /* PPI bit-set/reset mode: bit 7 = 0 means set/reset single bit */
        if (!(value & 0x80)) {
            int bit = (value >> 1) & 0x07;
            if (value & 0x01)
                board->reg_c |= (1 << bit);
            else
                board->reg_c &= ~(1 << bit);
            apply_reg_c(board);
        }
        return;
    }

    /* FDC registers */
    if (offset >= IO_FDC_BASE && offset <= IO_FDC_BASE + 0x06) {
        int reg = (offset - IO_FDC_BASE) >> 1;
        fdc_write(&board->fdc, reg, value);
        return;
    }

    /* Serial port — stub: accept and discard */
    if (offset == IO_SERIAL_DATA_W) {
        if (board->serial_out_cb)
            board->serial_out_cb(board->cb_userdata, value);
        return;
    }
    if (offset == IO_SERIAL_CMD || offset == IO_SERIAL_STATUS) {
        /* Serial command — ignore for now */
        return;
    }

    /* Expansion RAM disk (EX0:) */
    if (board->ramdisk.enabled && ramdisk_handles(offset)) {
        ramdisk_write(&board->ramdisk, offset, value);
        return;
    }

    /* Unhandled write — silently ignore */
}

static uint16_t io_read_word(ms0515_board_t *board, uint16_t offset)
{
    /* Memory dispatcher returns full 16-bit value */
    if (offset <= 0x1F)
        return board->mem.dispatcher;

    /* For other registers, read as two bytes */
    uint8_t lo = io_read_byte(board, offset);
    uint8_t hi = io_read_byte(board, offset + 1);
    return (uint16_t)lo | ((uint16_t)hi << 8);
}

static void io_write_word(ms0515_board_t *board, uint16_t offset, uint16_t value)
{
    /* Memory dispatcher — write full 16-bit value */
    if (offset <= 0x1F) {
        uint16_t old = board->mem.dispatcher;
        board->mem.dispatcher = value;
        /* Bit 8: monitor interrupt — edge-triggered on any transition. */
        if ((value ^ old) & MEM_DISP_MON_IRQ)
            cpu_interrupt(&board->cpu, 2, 064);
        return;
    }

    /* For other registers, write low byte only (most peripherals are 8-bit) */
    io_write_byte(board, offset, (uint8_t)(value & 0xFF));
}

/* ── Memory bus interface ────────────────────────────────────────────────── */

uint16_t board_read_word(ms0515_board_t *board, uint16_t address)
{
    mem_translation_t tr = mem_translate(&board->mem, address);

    if (tr.type == ADDR_TYPE_IO)
        return io_read_word(board, (uint16_t)tr.offset);

    return mem_read_word(&board->mem, tr);
}

void board_write_word(ms0515_board_t *board, uint16_t address, uint16_t value)
{
    mem_translation_t tr = mem_translate(&board->mem, address);

    if (tr.type == ADDR_TYPE_IO) {
        io_write_word(board, (uint16_t)tr.offset, value);
        return;
    }

    mem_write_word(&board->mem, tr, value);
}

uint8_t board_read_byte(ms0515_board_t *board, uint16_t address)
{
    mem_translation_t tr = mem_translate(&board->mem, address);

    if (tr.type == ADDR_TYPE_IO)
        return io_read_byte(board, (uint16_t)tr.offset);

    return mem_read_byte(&board->mem, tr);
}

void board_write_byte(ms0515_board_t *board, uint16_t address, uint8_t value)
{
    mem_translation_t tr = mem_translate(&board->mem, address);

    if (tr.type == ADDR_TYPE_IO) {
        io_write_byte(board, (uint16_t)tr.offset, value);
        return;
    }

    mem_write_byte(&board->mem, tr, value);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void board_init(ms0515_board_t *board)
{
    memset(board, 0, sizeof(*board));

    mem_init(&board->mem);
    timer_init(&board->timer);
    kbd_init(&board->kbd);
    fdc_init(&board->fdc);
    cpu_init(&board->cpu, board);

    board->dip_refresh   = 0;  /* 50 Hz default */
    board->timer_counter = TIMER_DIVIDER;
    board->frame_counter = get_frame_cycles(board);

    ramdisk_init(&board->ramdisk);
}

void board_ramdisk_enable(ms0515_board_t *board)
{
    ramdisk_enable(&board->ramdisk);
}

void board_ramdisk_free(ms0515_board_t *board)
{
    ramdisk_free(&board->ramdisk);
}

void board_reset(ms0515_board_t *board)
{
    timer_reset(&board->timer);
    kbd_reset(&board->kbd);
    fdc_reset(&board->fdc);
    if (board->ramdisk.enabled)
        ramdisk_reset(&board->ramdisk);

    board->reg_a = 0;
    board->reg_b = 0;
    board->reg_c = 0;
    board->ppi_control = 0;

    /* Reset memory dispatcher to power-on state: all primary banks,
     * VRAM window disabled.  Without this, the ROM boot code operates
     * on stale bank mapping left by the OS (e.g. VRAM window overlaid
     * on low addresses), causing writes to hit VRAM instead of RAM. */
    board->mem.dispatcher   = 0x007F;
    board->mem.rom_extended = false;

    board->hires_mode   = false;
    board->border_color  = 0;
    board->sound_on      = false;
    board->sound_value   = 0;

    board->timer_counter = TIMER_DIVIDER;
    board->frame_counter = get_frame_cycles(board);

    /* Propagate initial reg_a so the FDC knows which drive/motor is
     * selected.  Without this, fdc_select is never called and the
     * FDC reports NOT_READY forever, jamming the BIOS poll loop at
     * 163724 after the D command. */
    apply_reg_a(board);

    /* Reset CPU last — it reads the boot vector from memory */
    cpu_reset(&board->cpu);
}

void board_load_rom(ms0515_board_t *board, const uint8_t *data, uint32_t size)
{
    mem_load_rom(&board->mem, data, size);
}

bool board_step_frame(ms0515_board_t *board)
{
    int total_cycles = get_frame_cycles(board);
    int cycles_left = total_cycles;
    static int kbd_counter = 0;

    board->frame_cycle_pos = 0;

    while (cycles_left > 0) {
        int c = cpu_step(&board->cpu);
        if (c == 0) {
            /* CPU halted or waiting — still tick peripherals for 1 cycle */
            c = 1;
        }
        cycles_left -= c;
        board->frame_cycle_pos = total_cycles - cycles_left;

        /* Tick timer at ~2 MHz (every TIMER_DIVIDER CPU cycles) */
        board->timer_counter -= c;
        while (board->timer_counter <= 0) {
            board->timer_counter += TIMER_DIVIDER;
            timer_tick(&board->timer);
            update_sound(board);

            /*
             * Timer interrupt — vector 0100, priority 6.
             * Gated by dispatcher bit 9 (MEM_DISP_TIMER_IRQ).
             * The timer IRQ is strobed by the VBlank signal, so we
             * check the timer channel 0 output here and fire the
             * interrupt when enabled.
             *
             * Per NS4 tech desc, Table 4 and section 4.9.
             */
        }

        /* Tick keyboard UART periodically */
        kbd_counter += c;
        if (kbd_counter >= KBD_TICK_DIVIDER * TIMER_DIVIDER) {
            kbd_counter = 0;
            kbd_tick(&board->kbd);

            /* Check keyboard interrupt — vector 0130, priority 5.
             * The real i8251 drives an active-high level signal, not
             * an edge.  We must assert when IRQ is active and clear
             * when it deasserts, so a latched-but-unserviced interrupt
             * is cancelled once the CPU reads the data register. */
            if (board->kbd.irq)
                cpu_interrupt(&board->cpu, 5, 0130);
            else
                cpu_clear_interrupt(&board->cpu, 5);
        }

        /* FDC tick (for future rotational timing) */
        fdc_tick(&board->fdc);
    }

    /*
     * End of frame: generate VBlank-related interrupts.
     *
     * Per NS4 tech desc:
     *   - Monitor → vector 064, priority 4
     *     Bit 8 of dispatcher is a SOFTWARE interrupt trigger (edge-
     *     triggered on write), not tied to VBlank.  Handled in the
     *     dispatcher write path above.
     *   - Timer → vector 0100, priority 6
     *     Gated by dispatcher bit 9 (MEM_DISP_TIMER_IRQ).
     *     The timer interrupt is strobed by VBlank, firing once per frame.
     */
    if (board->mem.dispatcher & MEM_DISP_TIMER_IRQ)
        cpu_interrupt(&board->cpu, 11, 0100);

    return !board->cpu.halted;
}

void board_step_cpu(ms0515_board_t *board)
{
    cpu_step(&board->cpu);
}

void board_key_event(ms0515_board_t *board, uint8_t scancode)
{
    kbd_push_scancode(&board->kbd, scancode);
}

/* ── Callback registration ───────────────────────────────────────────────── */

void board_set_sound_callback(ms0515_board_t *board,
                              board_sound_cb_t cb, void *userdata)
{
    board->sound_cb   = cb;
    board->cb_userdata = userdata;
}

void board_set_trace_callback(ms0515_board_t *board,
                              ms0515_trace_cb_t cb, void *userdata)
{
    board->trace.cb       = cb;
    board->trace.userdata = userdata;
}

void board_set_serial_callbacks(ms0515_board_t *board,
                                board_serial_in_cb_t in_cb,
                                board_serial_out_cb_t out_cb,
                                void *userdata)
{
    board->serial_in_cb  = in_cb;
    board->serial_out_cb = out_cb;
    board->cb_userdata   = userdata;
}

/* ── Video buffer access ─────────────────────────────────────────────────── */

const uint8_t *board_get_vram(const ms0515_board_t *board)
{
    return mem_get_vram(&board->mem);
}

bool board_is_hires(const ms0515_board_t *board)
{
    return board->hires_mode;
}

uint8_t board_get_border_color(const ms0515_board_t *board)
{
    return board->border_color;
}
