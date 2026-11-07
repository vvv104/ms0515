/*
 * board.c — MS0515 system board integration
 *
 * Connects all hardware components through the I/O bus and manages
 * the timing relationships between the CPU (7.5 MHz), timer (2 MHz),
 * and video (50 Hz as the machines ran; 60 and 72 Hz by the jumpers
 * E3/E4).
 *
 * The I/O dispatch maps CPU addresses 0177400–0177776 to the
 * appropriate peripheral registers (see board.h for the full map).
 *
 * Sources:
 *   - NS4 technical description, sections 4.1–4.10, Appendix 1
 *   - MAME driver: src/mame/ussr/ms0515.cpp
 */

#include <ms0515/core/board.h>
#include <stdlib.h>
#include <string.h>

/* ── Timing constants ────────────────────────────────────────────────────── */

/*
 * CPU clock: 7.5 MHz
 * Timer clock: 2 MHz (one timer tick every 3.75 CPU cycles, rounded to 4)
 * VBlank at 50 Hz: 150000 CPU cycles per frame
 */
#define CPU_CLOCK_HZ        7500000
#define TIMER_PERIOD_Q      15      /* The exact timer period in quarter CPU cycles:
                                     * 7.5 MHz / 2 MHz = 3.75 cycles = 15 quarters      */
#define CPU_CYCLE_Q         4       /* Quarter cycles per CPU cycle                     */
#define FRAME_CYCLES_50HZ   150000  /* 7500000 / 50 */
#define FRAME_CYCLES_60HZ   125000  /* 7500000 / 60 */
#define FRAME_CYCLES_72HZ   104167  /* 7500000 / 72 (approximate) */

/* ── I/O port offsets (relative to 0177400 base) ─────────────────────────── */

/*
 * I/O port offsets — relative to the 0177400 base address.
 * Computed as (octal_address - 0177400), then expressed in decimal/hex.
 *
 * IMPORTANT: PDP-11 addresses are OCTAL.  The offset from 0177400 to
 * 0177500 is 0100 octal = 64 decimal = 0x40, NOT 0x100.
 *
 * The decoding (3.858.420 Э3 sheet 2; docs/hardware/board.md, "The
 * register decoding"): the decoder D80 (К555ИД7) picks a peripheral's
 * 040-byte block by address bits 5-7 and nothing else, so every register
 * of the board is at its address and at that address with bit 4 set.
 * The chips take the bus strobes on their RD and WR pins: WR (ДЗП) at
 * any address, so a write at 177506 is a write at 177526 - the ROM
 * programs the baud-clock counters through 177500-506; the RD of the
 * ВВ51s and the ВИ53 is ЧТ51, ДЧТ gated by АД04 (D79.4), so those three
 * chips are read with bit 4 clear only, and the T-11's read before every
 * write - a MOV to 177526 reads 177526 first - finds no chip at the
 * "+020" write addresses the technical description lists.  The ВВ55s
 * and the ВГ93 take ДЧТ itself: read and written at both halves.
 *
 * The owner's machine carried a factory wire from АД03 into D80's G2B
 * (the FPGA rebuild's findings.md, "The green wire"): an address with
 * bit 3 set selects none of the board's chips - that half of each block
 * is the memory-expansion module's, whose RAM disk (ramdisk.c) answers
 * at 177510-516, 177530-536 and 177550/570.  Without the wire the
 * ВИ53's counter 0 would take EX.SYS's writes to the module's PPI at
 * 177530-536 and the keyboard's baud clock would stop, which is what
 * the rebuilt board without the wire does.  The emulator is that
 * machine: bit 4 ignored, bit 3 set is not the board.
 *
 * The offsets below are the registers' canonical addresses, bit 4
 * clear; io_decode() reduces an address to them.
 */
#define IO_MEM_DISPATCHER   0x00    /* 0177400: Memory dispatcher register   */
#define IO_KBD_DATA         0x20    /* 0177440 / 0177460: Keyboard data      */
#define IO_KBD_CTRL         0x22    /* 0177442 / 0177462: status / command   */
#define IO_TIMER_BASE       0x40    /* 0177500-506 / 0177520-526: the timer  */
#define IO_PPI2_A           0x60    /* 0177540: MS7007 PPI port A (rows out) */
#define IO_PPI2_B           0x62    /* 0177542: MS7007 PPI port B (in): the joystick */
#define IO_PPI2_C           0x64    /* 0177544: MS7007 PPI port C (in)       */
#define IO_PPI2_CTRL        0x66    /* 0177546: MS7007 PPI control word      */
#define IO_REG_A            0x80    /* 0177600: System Register A            */
#define IO_REG_B            0x82    /* 0177602: System Register B            */
#define IO_REG_C            0x84    /* 0177604: System Register C            */
#define IO_PPI_CTRL         0x86    /* 0177606: PPI control word             */
#define IO_FDC_BASE         0xA0    /* 0177640: FDC register base            */
#define IO_SERIAL_DATA      0xC0    /* 0177700 / 0177720: Serial data        */
#define IO_SERIAL_CTRL      0xC2    /* 0177702 / 0177722: status / command   */
#define IO_HALT_TIMER       0xF8    /* 0177770: Halt/timer service address   */

#define IO_MODULE_BIT       0x08    /* АД03: set = the module's, not the board's */
#define IO_HALF_BIT         0x10    /* АД04: not decoded by D80              */

/* RAM disk I/O is handled by ramdisk.c — offsets defined in ramdisk.h */

/* The board's chip an offset selects, as D80 with the wire decodes it:
 * the canonical offset (bit 4 clear), or -1 for an address with bit 3
 * set, which is the module's.  `strobe_51` says whether the USARTs' and
 * the timer's gated RD would fire at this address. */
typedef struct {
    int  reg;
    bool strobe_51;
} io_decode_t;

static io_decode_t io_decode(uint16_t offset)
{
    io_decode_t d;
    d.reg       = (offset & IO_MODULE_BIT) ? -1 : (int)(offset & ~IO_HALF_BIT);
    d.strobe_51 = (offset & IO_HALF_BIT) == 0;
    return d;
}

/* ── Internal helpers ────────────────────────────────────────────────────── */

int board_frame_cycles(const ms0515_board_t *board)
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
     *   Reg C bit 5 (УПР ГР): "changing this bit changes the speaker's
     *     tone" (tech desc 4.8) - NOT modelled.  The schematic is not at
     *     hand, and every program known to make sound is explained without
     *     it: the POST melody gates a mode-3 tone with bits 7+6, the
     *     Spectrum ports (BIRDS, SABOT2, FIST) bit-bang the speaker by
     *     toggling bit 6 with the gate closed (OUT held high), bit 5
     *     going with it (0x60 / 0x80).  A guessed model (XOR with OUT) once silenced those
     *     ports; the pattern is pinned by test_sound.cpp.
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

    /* Bit 6: cassette output line (CASS).  Forwarded to the cassette
     * module so it can record (currently a no-op). */
    cassette_set_output(&board->cassette, (board->reg_a & 0x40) != 0);
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

    /* Bit 7: Cassette input (CSIN) — comparator output from the
     * cassette module.  No tape inserted → reads as 0. */
    if (cassette_get_input(&board->cassette))
        val |= 0x80;

    return val;
}

/* ── I/O port dispatch ───────────────────────────────────────────────────── */

static uint8_t io_read_byte(ms0515_board_t *board, uint16_t offset)
{
    /* Memory dispatcher — return low byte of dispatcher register */
    if (offset <= 0x1F)
        return (uint8_t)(board->mem.dispatcher & 0xFF);

    const io_decode_t d = io_decode(offset);

    /* Keyboard USART and the timer: read with bit 4 clear only */
    if (d.strobe_51) {
        if (d.reg == IO_KBD_DATA)
            return kbd_read(&board->kbd, 0);
        if (d.reg == IO_KBD_CTRL)
            return kbd_read(&board->kbd, 1);
        if (d.reg >= IO_TIMER_BASE && d.reg <= IO_TIMER_BASE + 6)
            return timer_read(&board->timer, (d.reg - IO_TIMER_BASE) >> 1);
    }

    /* System registers */
    if (d.reg == IO_REG_A)
        return board->reg_a;
    if (d.reg == IO_REG_B)
        return read_reg_b(board);
    if (d.reg == IO_REG_C)
        return board->reg_c;

    /* FDC registers */
    if (d.reg >= IO_FDC_BASE && d.reg <= IO_FDC_BASE + 0x06)
        return fdc_read(&board->fdc, (d.reg - IO_FDC_BASE) >> 1);

    /* Paravirtual hard disk (HD:) — shadows the serial addresses while an
     * image is mounted.  The serial port has no read handler, so HD is the
     * only consumer of these offsets on the read path. */
    if (board->hd.enabled && hd_handles(offset))
        return hd_read_byte(&board->hd, offset);

    /* Expansion RAM disk (EX0:) */
    if (board->ramdisk.enabled && ramdisk_handles(offset)) {
        /* T-11 DATIO read-phase bypass for the data port.  The
         * K555IE19 counter on the EX board is clocked once per
         * atomic bus cycle, not once per sub-strobe; letting the
         * BDIN sub-strobe of a DATIO tick the counter would make
         * EX.SYS's MOVB-based format land every other byte at the
         * wrong DRAM offset.  PPI register reads have no counter
         * side-effect and stay routed unconditionally. */
        if (board->_datio_active &&
            (offset == RAMDISK_IO_DATA       ||
             offset == (RAMDISK_IO_DATA + 1) ||
             offset == RAMDISK_IO_DATA_ALIAS ||
             offset == (RAMDISK_IO_DATA_ALIAS + 1)))
            return 0xFF;
        return ramdisk_read(&board->ramdisk, offset);
    }

    /* The MS7007 PPI: port A reads its latch, port B the joystick lines
     * (closed switches low, open lines high), port C open lines. */
    if (d.reg == IO_PPI2_A)
        return board->ppi2_a;
    if (d.reg == IO_PPI2_B)
        return (uint8_t)(~board->joystick & 0xFF);
    if (d.reg == IO_PPI2_C || d.reg == IO_PPI2_CTRL)
        return 0xFF;

    /* No chip answers (a USART or the timer at its +020 half, the
     * module's half of a block with no module) — 0, no bus error */
    return 0;
}

/* Dispatcher bit 8 is the monitor's interrupt request itself (NS4 4.3:
 * "1 sets the request from the monitor, 0 resets it"): a write of 1 makes
 * vector 064 pending, a write of 0 takes a pending request back - and
 * raises none.  RT-11's terminal service clears the bit as it enters,
 * lowers the priority to print the character, sets the bit for the next
 * one and returns.  Taking the clear as an edge (any transition) raised a
 * request there, which broke into the service the moment its priority
 * dropped, before its RTI; one nesting per character ran the stack down
 * into the vector page and `?MON-F-Stack overflow` ended every long TYPE
 * on OSA and Omega alike.  (nzeemin's ms0515btl fires on any transition
 * too and has the same failure.) */
static void board_monitor_request(ms0515_board_t *board, uint16_t old)
{
    const uint16_t now = board->mem.dispatcher;
    if (now & MEM_DISP_MON_IRQ) {
        if (!(old & MEM_DISP_MON_IRQ)) cpu_interrupt(&board->cpu, 2, 064);
    } else if (old & MEM_DISP_MON_IRQ) {
        cpu_clear_interrupt(&board->cpu, 2);
    }
}

static void io_write_byte(ms0515_board_t *board, uint16_t offset, uint8_t value)
{
    /* Memory dispatcher — write to the full 16-bit register on word writes.
     * For byte writes, update the low byte only. */
    if (offset <= 0x1F) {
        uint16_t old = board->mem.dispatcher;
        board->mem.dispatcher = (board->mem.dispatcher & 0xFF00) | value;
        {
            uint8_t payload[2] = {
                (uint8_t)(board->mem.dispatcher & 0xFF),
                (uint8_t)(board->mem.dispatcher >> 8),
            };
            BOARD_EVT(board, MS0515_EVT_DISP, payload, sizeof payload);
        }
        board_monitor_request(board, old);
        return;
    }

    const io_decode_t d = io_decode(offset);

    /* Keyboard USART: written at either half of its block */
    if (d.reg == IO_KBD_DATA) {
        kbd_write(&board->kbd, 0, value);
        return;
    }
    if (d.reg == IO_KBD_CTRL) {
        kbd_write(&board->kbd, 1, value);
        return;
    }

    /* Timer: 0177500-506 and 0177520-526 alike */
    if (d.reg >= IO_TIMER_BASE && d.reg <= IO_TIMER_BASE + 6) {
        timer_write(&board->timer, (d.reg - IO_TIMER_BASE) >> 1, value);
        return;
    }

    /* System registers */
    if (d.reg == IO_REG_A) {
        board->reg_a = value;
        BOARD_EVT(board, MS0515_EVT_REG_A, &value, 1);
        apply_reg_a(board);
        return;
    }
    if (d.reg == IO_REG_C) {
        board->reg_c = value;
        apply_reg_c(board);
        return;
    }
    if (d.reg == IO_PPI_CTRL) {
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

    /* The MS7007 PPI: the port A latch and the control word are kept
     * (the ROM sets A to 0377 at boot, the monitor writes 0 after a key);
     * ports B and C are inputs. */
    if (d.reg == IO_PPI2_A) { board->ppi2_a = value; return; }
    if (d.reg == IO_PPI2_CTRL) { board->ppi2_control = value; return; }
    if (d.reg == IO_PPI2_B || d.reg == IO_PPI2_C) return;

    /* FDC registers */
    if (d.reg >= IO_FDC_BASE && d.reg <= IO_FDC_BASE + 0x06) {
        int reg = (d.reg - IO_FDC_BASE) >> 1;
        {
            uint8_t payload[2] = { (uint8_t)(reg & 3), value };
            BOARD_EVT(board, MS0515_EVT_FDC, payload, sizeof payload);
        }
        fdc_write(&board->fdc, reg, value);
        return;
    }

    /* Paravirtual hard disk (HD:) — shadows the serial TX addresses while
     * an image is mounted.  Checked before the serial stub so HD wins. */
    if (board->hd.enabled && hd_handles(offset)) {
        hd_write_byte(&board->hd, &board->mem, offset, value);
        return;
    }

    /* Serial port — stub: accept and discard; written at either half */
    if (d.reg == IO_SERIAL_DATA) {
        if (board->serial_out_cb)
            board->serial_out_cb(board->cb_userdata, value);
        return;
    }
    if (d.reg == IO_SERIAL_CTRL) {
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

    /* Paravirtual hard disk — 16-bit registers, read whole. */
    if (board->hd.enabled && hd_handles(offset))
        return hd_read_word(&board->hd, offset);

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
        board_monitor_request(board, old);
        return;
    }

    /* Paravirtual hard disk — 16-bit registers; a command write to HDCSR
     * must arrive whole, not split into two byte writes. */
    if (board->hd.enabled && hd_handles(offset)) {
        hd_write_word(&board->hd, &board->mem, offset, value);
        return;
    }

    /* For other registers, write low byte only (most peripherals are 8-bit) */
    io_write_byte(board, offset, (uint8_t)(value & 0xFF));
}

/* ── T-11 DATIO bracket ──────────────────────────────────────────────────── */

void board_datio_begin(ms0515_board_t *board)
{
    board->_datio_active = true;
}

void board_datio_end(ms0515_board_t *board)
{
    board->_datio_active = false;
}

/* ── Memory bus interface ────────────────────────────────────────────────── */

uint16_t board_read_word(ms0515_board_t *board, uint16_t address)
{
    /* K1807VM1 ignores the LSB on word access — align down silently. */
    address &= 0xFFFEu;
    mem_translation_t tr = mem_translate(&board->mem, address);
    uint16_t value = (tr.type == ADDR_TYPE_IO)
        ? io_read_word(board, (uint16_t)tr.offset)
        : mem_read_word(&board->mem, tr);

    if (board->read_watch_len &&
        address >= board->read_watch_addr &&
        (uint32_t)address <
            (uint32_t)board->read_watch_addr + board->read_watch_len) {
        uint8_t payload[4] = {
            (uint8_t)(address & 0xFF), (uint8_t)(address >> 8),
            (uint8_t)(value & 0xFF),   (uint8_t)(value >> 8),
        };
        BOARD_EVT(board, MS0515_EVT_MEMR, payload, 4);
    }
    return value;
}

void board_write_word(ms0515_board_t *board, uint16_t address, uint16_t value)
{
    /* K1807VM1 ignores the LSB on word access — align down silently. */
    address &= 0xFFFEu;
    mem_translation_t tr = mem_translate(&board->mem, address);

    if (tr.type == ADDR_TYPE_IO) {
        io_write_word(board, (uint16_t)tr.offset, value);
        return;
    }

    if (board->watch_len &&
        address >= board->watch_addr &&
        (uint32_t)address < (uint32_t)board->watch_addr + board->watch_len) {
        uint8_t payload[4] = {
            (uint8_t)(address & 0xFF), (uint8_t)(address >> 8),
            (uint8_t)(value & 0xFF),   (uint8_t)(value >> 8),
        };
        BOARD_EVT(board, MS0515_EVT_MEMW, payload, 4);
    }

    mem_write_word(&board->mem, tr, value);
}

uint8_t board_read_byte(ms0515_board_t *board, uint16_t address)
{
    mem_translation_t tr = mem_translate(&board->mem, address);
    uint8_t value = (tr.type == ADDR_TYPE_IO)
        ? io_read_byte(board, (uint16_t)tr.offset)
        : mem_read_byte(&board->mem, tr);

    if (board->read_watch_len &&
        address >= board->read_watch_addr &&
        (uint32_t)address <
            (uint32_t)board->read_watch_addr + board->read_watch_len) {
        uint8_t payload[3] = {
            (uint8_t)(address & 0xFF), (uint8_t)(address >> 8),
            value,
        };
        BOARD_EVT(board, MS0515_EVT_MEMR, payload, 3);
    }
    return value;
}

void board_write_byte(ms0515_board_t *board, uint16_t address, uint8_t value)
{
    mem_translation_t tr = mem_translate(&board->mem, address);

    if (tr.type == ADDR_TYPE_IO) {
        io_write_byte(board, (uint16_t)tr.offset, value);
        return;
    }

    if (board->watch_len &&
        address >= board->watch_addr &&
        (uint32_t)address < (uint32_t)board->watch_addr + board->watch_len) {
        uint8_t payload[3] = {
            (uint8_t)(address & 0xFF), (uint8_t)(address >> 8),
            value,
        };
        BOARD_EVT(board, MS0515_EVT_MEMW, payload, 3);
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
    cassette_init(&board->cassette);
    cpu_init(&board->cpu, board);

    /* The frame rate is the jumpers E3/E4 (NS4 TO Table 6), read back as
     * Register B bits 4-3 (Table 10).  The timer interrupt is the frame
     * sync itself (D60.1, clocked by КСИ), so an RT-11 kit's clock runs
     * at the frame rate: the kits of two bureaus say CLOCK = 50., and the
     * machines kept time in banks - at 60 Hz they would have gained 4.8
     * hours a day.  So the machines ran at 50 Hz, both jumpers fitted,
     * whatever the assembly drawing's standard list says (ms0515-fpga,
     * netlist/variants.md); 312 lines, 20 ms a frame. */
    board->dip_refresh   = 0;  /* 50 Hz */
    board->timer_counter = TIMER_PERIOD_Q;
    board->frame_counter = board_frame_cycles(board);

    ramdisk_init(&board->ramdisk);
    hd_init(&board->hd);
}

void board_ramdisk_enable(ms0515_board_t *board)
{
    ramdisk_enable(&board->ramdisk);
}

void board_ramdisk_free(ms0515_board_t *board)
{
    ramdisk_free(&board->ramdisk);
}

void board_hd_set_enabled(ms0515_board_t *board, bool enabled)
{
    hd_set_enabled(&board->hd, enabled);
}

bool board_hd_mount(ms0515_board_t *board, const uint8_t *data, uint32_t size)
{
    return hd_mount(&board->hd, data, size);
}

void board_hd_unmount(ms0515_board_t *board)
{
    hd_unmount(&board->hd);
}

void board_hd_set_write_through(ms0515_board_t *board,
                                ms0515_hd_write_through_fn cb, void *userdata)
{
    hd_set_write_through(&board->hd, cb, userdata);
}

void board_hd_set_backend(ms0515_board_t *board,
                          ms0515_hd_read_blocks_fn read_fn,
                          ms0515_hd_write_blocks_fn write_fn,
                          uint32_t blocks, void *userdata)
{
    hd_set_backend(&board->hd, read_fn, write_fn, blocks, userdata);
}

void board_reset(ms0515_board_t *board)
{
    timer_reset(&board->timer);
    kbd_reset(&board->kbd);
    fdc_reset(&board->fdc);
    cassette_init(&board->cassette);
    if (board->ramdisk.enabled)
        ramdisk_reset(&board->ramdisk);
    if (board->hd.enabled)
        hd_reset(&board->hd);

    board->reg_a = 0;
    board->reg_b = 0;
    board->reg_c = 0;
    board->ppi_control = 0;
    board->ppi2_a = 0;
    board->ppi2_control = 0;

    /* PPI reset clears all output latches → dispatcher = 0.
     * The ROM boot code is responsible for setting up bank mapping
     * before accessing RAM. */
    board->mem.dispatcher   = 0x0000;
    board->mem.rom_extended = false;

    board->hires_mode   = false;
    board->border_color  = 0;
    board->sound_on      = false;
    board->sound_value   = 0;

    board->timer_counter = TIMER_PERIOD_Q;
    board->frame_counter = board_frame_cycles(board);

    /* Reset CPU last — it reads the boot vector from memory */
    cpu_reset(&board->cpu);
}

void board_load_rom(ms0515_board_t *board, const uint8_t *data, uint32_t size)
{
    mem_load_rom(&board->mem, data, size);
}

bool board_step_frame(ms0515_board_t *board)
{
    int frame_cycles = board_frame_cycles(board);
    int cycles_left  = frame_cycles;

    board->frame_cycle_pos = 0;

    while (cycles_left > 0) {
        int c = cpu_step(&board->cpu);
        if (c == 0) {
            /* CPU halted or waiting — still tick peripherals for 1 cycle */
            c = 1;
        }
        board->total_cycles   += (uint64_t)c;
        cycles_left           -= c;
        board->frame_cycle_pos = frame_cycles - cycles_left;

        /* Tick timer at exactly 2 MHz: 4 ticks every 15 CPU cycles, counted
         * in quarter cycles (the NS4 tech desc gives the PIT clock as 2 MHz;
         * a game that paces itself on a channel's count relies on it). */
        board->timer_counter -= c * CPU_CYCLE_Q;
        while (board->timer_counter <= 0) {
            board->timer_counter += TIMER_PERIOD_Q;
            timer_tick(&board->timer);
            update_sound(board);
            /* Counter 0's output is the keyboard USART's clock (D9 OUT0
             * = СИНХР1 to D22's TxC and RxC): a rising edge clocks it. */
            const bool out0 = timer_get_out(&board->timer, 0);
            if (out0 && !board->kbd_clock_was)
                kbd_clock_edge(&board->kbd);
            board->kbd_clock_was = out0;

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

        /* A millisecond of the machine's time, for whatever runs on a
         * clock of its own (the keyboard's typematic). */
        board->ms_counter += c;
        while (board->ms_counter >= CPU_CLOCK_HZ / 1000) {
            board->ms_counter -= CPU_CLOCK_HZ / 1000;
            ++board->ms_clock;
            if (board->ms_cb)
                board->ms_cb(board->ms_userdata, board->ms_clock, board->frame_cycle_pos);
        }

        /* The keyboard's line moves on by the instruction's cycles; its
         * clock, the timer's OUT0, was given above. */
        kbd_tick(&board->kbd, c);

        /* Check keyboard interrupt — vector 0130, priority 5.
         * The real i8251 drives an active-high level signal, not
         * an edge.  We must assert when IRQ is active and clear
         * when it deasserts, so a latched-but-unserviced interrupt
         * is cancelled once the CPU reads the data register. */
        if (board->kbd.irq)
            cpu_interrupt(&board->cpu, 5, 0130);
        else
            cpu_clear_interrupt(&board->cpu, 5);

        /* Advance the FDC state machine by the cycles of this instruction. */
        fdc_tick(&board->fdc, c);

        /* Decay the HD activity LED (HD transfers are synchronous, so this
         * only fades the lamp after a completed read/write). */
        if (board->hd.enabled)
            hd_tick(&board->hd, c);
    }

    /*
     * End of frame: generate VBlank-related interrupts.
     *
     * Per NS4 tech desc:
     *   - Monitor → vector 064, level 4
     *     Bit 8 of the dispatcher is the request itself, not tied to
     *     VBlank: board_monitor_request() in the dispatcher write path.
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
    int c = cpu_step(&board->cpu);
    board->total_cycles += (uint64_t)(c > 0 ? c : 1);
}

void board_set_joystick(ms0515_board_t *board, uint8_t bits)
{
    board->joystick = bits & 0x1F;
}

void board_key_event(ms0515_board_t *board, uint8_t scancode)
{
    kbd_push_scancode(&board->kbd, scancode);
}

/* ── Callback registration ───────────────────────────────────────────────── */

void board_set_ms_callback(ms0515_board_t *board,
                           board_ms_cb_t cb, void *userdata)
{
    board->ms_cb       = cb;
    board->ms_userdata = userdata;
}

void board_set_sound_callback(ms0515_board_t *board,
                              board_sound_cb_t cb, void *userdata)
{
    board->sound_cb   = cb;
    board->cb_userdata = userdata;
}

void board_enable_history(ms0515_board_t *board, size_t n_events)
{
    ms0515_event_ring_resize(&board->history, n_events);
}

void board_set_memory_watch(ms0515_board_t *board,
                            uint16_t addr, uint16_t len)
{
    board->watch_addr = addr;
    board->watch_len  = len;
}

void board_set_read_watch(ms0515_board_t *board,
                          uint16_t addr, uint16_t len)
{
    board->read_watch_addr = addr;
    board->read_watch_len  = len;
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
